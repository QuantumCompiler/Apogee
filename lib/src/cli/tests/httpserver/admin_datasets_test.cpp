#include "httpserver/admin_datasets.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "backends/mock.h"
#include "commands/registry.h"
#include "commands/root.h"
#include "events/bus.h"
#include "harness/assets.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "harness/layout.h"
#include "httpserver/handler.h"
#include "httpserver/jobs.h"
#include "httpserver/mux.h"
#include "support/env_guard.h"
#include "training/datasets.h"

/// The datasets slice of the control plane: the parity proof (a dataset
/// created over HTTP and one created by the CLI are the same bytes), every
/// refusal with the status a client acts on, get/delete by name, the kits
/// listing, synth as a job on the plane's harness with the CLI's teacher
/// rules, and the literal paths never read as dataset names.
namespace {

using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::Handler;
using apogee::httpserver::HandlerOptions;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;
using apogee::httpserver::JobRegistry;
using apogee::httpserver::JobStatus;
using apogee::httpserver::JobWorkers;
using apogee::training::chat_line;

constexpr std::string_view kBatch =
    R"([{"prompt": "What is 15% of 240?", "completion": "36"},
        {"prompt": "If 3x = 21, what is x?", "completion": "7"}])";

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

struct Fixture {
    apogee::testing::TempDir home{"admin-datasets-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    apogee::harness::Config config;
    std::unique_ptr<apogee::harness::Harness> harness;
    std::unique_ptr<Handler> handler;
    apogee::events::Bus bus;
    JobRegistry jobs{bus};
    JobWorkers workers;

    explicit Fixture(bool serve = true) {
        std::filesystem::create_directories(config_path.parent_path());
        // The CLI half builds its provider from the config, so the same
        // batch is scripted there; the HTTP half registers it directly.
        std::ofstream{home.path() / "teacher.json", std::ios::binary} << nlohmann::json{
            {"turns",
             nlohmann::json::array({{{"text", std::string{kBatch}}}})}}.dump();
        std::ofstream{config_path, std::ios::binary}
            << "models:\n  default: mock\nbackends:\n  mock:\n    type: mock\n    model_path: "
            << (home.path() / "teacher.json").string()
            << "\n  vendor:\n    type: claude-cli\n  prose:\n    type: mock\n";
        config = apogee::harness::load_config(config_path);
        harness = std::make_unique<apogee::harness::Harness>(config);
        MockProvider::Options options;
        options.backend_name = "mock";
        options.turns = {MockTurn{std::string{kBatch}}};
        harness->register_provider("mock", std::make_shared<MockProvider>(std::move(options)));
        MockProvider::Options prose;
        prose.backend_name = "prose";
        prose.turns = {MockTurn{"only prose"}};
        harness->register_provider("prose", std::make_shared<MockProvider>(std::move(prose)));
        harness->use_default_router();
        HandlerOptions served;
        if (serve) {
            served.served = {"mock", "prose"};
            served.default_backend = "mock";
        }
        handler = std::make_unique<Handler>(*harness, served, nullptr);
        REQUIRE(apogee::harness::seed_data_directory(home.path()).ok());
    }

    [[nodiscard]] AdminConfigContext context() const {
        return AdminConfigContext{.config_path = config_path, .startup = &config};
    }

    [[nodiscard]] static HttpRequest request(std::string method, const nlohmann::json& body = {}) {
        HttpRequest out;
        out.method = std::move(method);
        if (!body.is_null()) {
            out.body = body.dump();
        }
        out.remote_address = "127.0.0.1";
        return out;
    }

    [[nodiscard]] HttpResponse create(const nlohmann::json& body) const {
        return apogee::httpserver::admin_create_dataset(context(), request("POST", body));
    }

    [[nodiscard]] HttpResponse synth(const nlohmann::json& body) {
        return apogee::httpserver::admin_synth_dataset(context(), *handler, jobs, workers,
                                                       request("POST", body));
    }

    [[nodiscard]] std::filesystem::path dataset(std::string_view name) const {
        return home.path() / "training" / "datasets" / (std::string{name} + ".jsonl");
    }

    int cli(const std::vector<std::string>& args) const {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        apogee::commands::RootCommand root{apogee::commands::default_registry()};
        std::vector<std::string> full{"--config", config_path.string()};
        full.insert(full.end(), args.begin(), args.end());
        std::vector<const char*> argv{"apogee"};
        for (const std::string& arg : full) {
            argv.push_back(arg.c_str());
        }
        const int code = root.run(static_cast<int>(argv.size()), argv.data());
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        return code;
    }

    [[nodiscard]] apogee::httpserver::JobRecord wait(const std::string& id) const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
        while (std::chrono::steady_clock::now() < deadline) {
            const std::optional<apogee::httpserver::JobRecord> record = jobs.get(id);
            REQUIRE(record.has_value());
            if (record->status != JobStatus::Running) {
                return *record;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        FAIL("the job never finished");
        return {};
    }
};

}  // namespace

TEST_CASE("a dataset created over HTTP is byte-identical to one created by the CLI",
          "[httpserver][admin][datasets][parity]") {
    Fixture fixture;
    REQUIRE(fixture.cli({"datasets", "create", "by-cli"}) == 0);
    HttpResponse response = fixture.create({{"name", "by-http"}});
    REQUIRE(response.status == 201);
    CHECK(parsed(response)["examples"] == 2);
    CHECK(bytes(fixture.dataset("by-http")) == bytes(fixture.dataset("by-cli")));

    // The listing shows both; get names one; a duplicate is a conflict.
    const nlohmann::json listed =
        parsed(apogee::httpserver::admin_list_datasets(fixture.context()));
    CHECK(listed["object"] == "list");
    CHECK(listed["data"].size() == 2);
    const nlohmann::json one =
        parsed(apogee::httpserver::admin_get_dataset(fixture.context(), "by-http"));
    CHECK(one["lines"] == 2);
    CHECK(one["shape"] == "chat");
    CHECK(fixture.create({{"name", "by-http"}}).status == 409);
    CHECK(fixture.create({{"name", "by-http"}, {"force", true}, {"from", "empty"}}).status == 201);
    CHECK(bytes(fixture.dataset("by-http")).empty());
}

TEST_CASE("create's refusals and its explicit lines", "[httpserver][admin][datasets][create]") {
    Fixture fixture;
    CHECK(fixture.create({}).status == 400);
    CHECK(fixture.create({{"name", "../x"}}).status == 400);
    CHECK(fixture.create({{"name", "x"}, {"from", "chat-logs"}}).status == 400);
    CHECK(fixture.create({{"name", "x"}, {"lines", "notalist"}}).status == 400);
    CHECK(fixture.create({{"name", "x"}, {"from", "sessions"}, {"since", "nope"}}).status == 400);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("x")));

    const nlohmann::json line = nlohmann::json::parse(chat_line("q", "a"));
    HttpResponse response = fixture.create({{"name", "explicit"}, {"lines", {line}}});
    REQUIRE(response.status == 201);
    CHECK(parsed(response)["examples"] == 1);
    CHECK(bytes(fixture.dataset("explicit")) == line.dump() + "\n");

    HttpRequest bad = Fixture::request("POST");
    bad.body = "not json";
    CHECK(apogee::httpserver::admin_create_dataset(fixture.context(), bad).status == 400);
}

TEST_CASE("get and delete answer honest 404s, and delete removes the file",
          "[httpserver][admin][datasets]") {
    Fixture fixture;
    CHECK(apogee::httpserver::admin_get_dataset(fixture.context(), "none").status == 404);
    CHECK(apogee::httpserver::admin_delete_dataset(fixture.context(), "none").status == 404);
    CHECK(apogee::httpserver::admin_get_dataset(fixture.context(), "../x").status == 400);
    REQUIRE(fixture.create({{"name", "gone"}}).status == 201);
    const HttpResponse deleted =
        apogee::httpserver::admin_delete_dataset(fixture.context(), "gone");
    CHECK(deleted.status == 200);
    CHECK(parsed(deleted)["deleted"] == "gone");
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("gone")));
}

TEST_CASE("the kits route lists the seeded kits", "[httpserver][admin][datasets][kits]") {
    Fixture fixture;
    const nlohmann::json kits = parsed(apogee::httpserver::admin_list_kits(fixture.context()));
    REQUIRE(kits["data"].size() == 4);
    CHECK(kits["data"][0]["name"] == "instruction-following");
    CHECK(kits["data"][1]["name"] == "reasoning");
    CHECK(kits["data"][1]["eval_items"] == 8);
    CHECK_FALSE(kits["data"][1].contains("error"));
}

TEST_CASE(
    "POST /datasets/synth runs the teacher as a job on the plane's harness and writes "
    "the dataset",
    "[httpserver][admin][datasets][synth]") {
    Fixture fixture;
    HttpResponse response =
        fixture.synth({{"name", "maths"}, {"teacher", "mock"}, {"kit", "reasoning"}, {"count", 2}});
    REQUIRE(response.status == 202);
    const std::string id = parsed(response)["job_id"].get<std::string>();
    const apogee::httpserver::JobRecord done = fixture.wait(id);
    INFO(done.error);
    REQUIRE(done.status == JobStatus::Succeeded);
    CHECK(done.kind == "datasets-synth");
    CHECK(done.result["examples"] == 2);
    CHECK(done.result["name"] == "maths");
    CHECK(bytes(fixture.dataset("maths")) == chat_line("What is 15% of 240?", "36") + "\n" +
                                                 chat_line("If 3x = 21, what is x?", "7") + "\n");

    // The same dataset made by the CLI from the same teacher is the same bytes.
    REQUIRE(fixture.cli({"datasets", "synth", "maths-cli", "--teacher", "mock", "--kit",
                         "reasoning", "--count", "2"}) == 0);
    CHECK(bytes(fixture.dataset("maths-cli")) == bytes(fixture.dataset("maths")));

    // A teacher that never produces fails the job and writes nothing.
    response = fixture.synth(
        {{"name", "empty"}, {"teacher", "prose"}, {"kit", "reasoning"}, {"count", 1}});
    REQUIRE(response.status == 202);
    const apogee::httpserver::JobRecord failed =
        fixture.wait(parsed(response)["job_id"].get<std::string>());
    CHECK(failed.status == JobStatus::Failed);
    CHECK(failed.error.find("no usable examples") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("empty")));
}

TEST_CASE("synth's refusals: the CLI's rules as 400s, a conflict, and 501 with no backend",
          "[httpserver][admin][datasets][synth][policy]") {
    Fixture fixture;
    CHECK(fixture.synth({{"name", "a"}}).status == 400);
    CHECK(fixture.synth({{"name", "a"}, {"teacher", "nope"}, {"kit", "reasoning"}}).status == 400);
    CHECK(fixture.synth({{"name", "a"}, {"teacher", "vendor"}, {"kit", "reasoning"}}).status ==
          400);
    CHECK(fixture.synth({{"name", "a"}, {"teacher", "mock"}, {"kit", "nokit"}}).status == 400);
    CHECK(fixture.synth({{"name", "a"}, {"teacher", "mock"}, {"kit", "reasoning"}, {"count", -1}})
              .status == 400);
    CHECK(fixture.synth({{"name", "../a"}, {"teacher", "mock"}, {"kit", "reasoning"}}).status ==
          400);
    REQUIRE(fixture.create({{"name", "taken"}}).status == 201);
    CHECK(fixture.synth({{"name", "taken"}, {"teacher", "mock"}, {"kit", "reasoning"}}).status ==
          409);
    CHECK(fixture.jobs.list().empty());

    Fixture bare{false};
    CHECK(bare.synth({{"name", "a"}, {"teacher", "mock"}, {"kit", "reasoning"}}).status == 501);
}

TEST_CASE("the datasets routes are gated and the literal paths are never dataset names",
          "[httpserver][admin][datasets][mux]") {
    Fixture fixture;
    apogee::httpserver::AdminOptions options;
    options.config_path = fixture.config_path;
    options.startup = fixture.config;
    apogee::httpserver::AdminHandler admin{options, fixture.jobs, fixture.bus};
    const apogee::httpserver::Mux mux{*fixture.handler, admin, "secret"};

    for (const auto& [method, path] :
         std::vector<std::pair<std::string, std::string>>{{"GET", "/v1/admin/datasets"},
                                                          {"POST", "/v1/admin/datasets"},
                                                          {"POST", "/v1/admin/datasets/synth"},
                                                          {"GET", "/v1/admin/datasets/kits"},
                                                          {"GET", "/v1/admin/datasets/x"},
                                                          {"DELETE", "/v1/admin/datasets/x"}}) {
        HttpRequest probe;
        probe.method = method;
        probe.path = path;
        INFO(method << " " << path);
        CHECK(mux.dispatch(probe).status == 401);
    }

    HttpRequest kits;
    kits.method = "GET";
    kits.path = "/v1/admin/datasets/kits";
    kits.headers["authorization"] = "Bearer secret";
    const HttpResponse listed = mux.dispatch(kits);
    CHECK(listed.status == 200);
    CHECK(parsed(listed)["data"].size() == 4);

    HttpRequest synth = kits;
    synth.method = "POST";
    synth.path = "/v1/admin/datasets/synth";
    synth.body = "{}";
    CHECK(mux.dispatch(synth).status == 400);  // reached the route, refused its body
}
