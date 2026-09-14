#include "httpserver/admin_knowledge.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "backends/mock.h"
#include "commands/registry.h"
#include "commands/root.h"
#include "events/bus.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "httpserver/admin.h"
#include "httpserver/handler.h"
#include "httpserver/http_types.h"
#include "httpserver/jobs.h"
#include "httpserver/mux.h"
#include "knowledge/record.h"
#include "knowledge/store.h"
#include "support/env_guard.h"

/// The knowledge slice of the control plane: the capture twin on the
/// inference plane's own harness, the finished-record store, the honest
/// refusals, and the parity proof -- a record captured over HTTP is the
/// record the CLI captures from the same conversation.
namespace {

using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::Handler;
using apogee::httpserver::HandlerOptions;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;
using apogee::knowledge::Record;
using apogee::knowledge::Store;

constexpr std::string_view kRecord =
    R"({"intent": "We dropped the cancel button because testers kept mistaking it for back.", "decision": "Remove the cancel button.", "status": "shipped", "discipline": "ux", "downstream_link": "", "provenance": {"source": "meeting", "attribution": "Ada Lovelace"}})";

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

struct Fixture {
    apogee::testing::TempDir home{"admin-knowledge-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    apogee::harness::Config config;
    std::unique_ptr<apogee::harness::Harness> harness;
    std::shared_ptr<MockProvider> provider;
    std::unique_ptr<Handler> handler;

    explicit Fixture(std::vector<MockTurn> turns = {MockTurn{std::string{kRecord}}},
                     bool serve = true) {
        std::filesystem::create_directories(config_path.parent_path());
        // The CLI half of the parity proof builds ITS providers from this
        // file, so the mock entry carries the same script the in-memory
        // provider answers from.
        const std::filesystem::path script = home.path() / "clerk.json";
        std::ofstream{script, std::ios::binary}
            << nlohmann::json{{"turns", nlohmann::json::array({{{"text", std::string{kRecord}}}})}}
                   .dump();
        std::ofstream{config_path, std::ios::binary}
            << "models:\n  default: mock\nbackends:\n  mock:\n    type: mock\n    model_path: "
            << script.string() << "\n  vendor:\n    type: claude-cli\n";
        config = apogee::harness::load_config(config_path);
        harness = std::make_unique<apogee::harness::Harness>(config);
        MockProvider::Options options;
        options.backend_name = "mock";
        options.turns = std::move(turns);
        provider = std::make_shared<MockProvider>(std::move(options));
        harness->register_provider("mock", provider);
        harness->use_default_router();
        HandlerOptions served;
        if (serve) {
            served.served = {"mock"};
            served.default_backend = "mock";
        }
        handler = std::make_unique<Handler>(*harness, served, nullptr);
    }

    [[nodiscard]] AdminConfigContext context() const {
        return AdminConfigContext{.config_path = config_path, .startup = &config};
    }

    [[nodiscard]] static HttpRequest post(const nlohmann::json& body) {
        HttpRequest request;
        request.method = "POST";
        request.body = body.dump();
        request.remote_address = "127.0.0.1";
        return request;
    }

    [[nodiscard]] HttpResponse capture(const nlohmann::json& body) const {
        return apogee::httpserver::admin_capture_knowledge(context(), *handler, post(body));
    }

    [[nodiscard]] HttpResponse create(const nlohmann::json& body) const {
        return apogee::httpserver::admin_create_knowledge(context(), *handler, post(body));
    }

    [[nodiscard]] Store store(std::string_view db = "knowledge") const {
        return Store{home.path() / "embeddings" / (std::string{db} + ".db"),
                     home.path() / "knowledge" / "raw"};
    }

    void cli(const std::vector<std::string>& args) const {
        std::ostringstream sink;
        std::streambuf* old_out = std::cout.rdbuf(sink.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(sink.rdbuf());
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
        INFO(sink.str());
        REQUIRE(code == 0);
    }
};

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

}  // namespace

TEST_CASE(
    "POST /v1/admin/knowledge/capture runs the clerk on the served backend and stores the "
    "record: chunk, archive, registration",
    "[httpserver][admin][knowledge][capture]") {
    const Fixture fixture;
    const HttpResponse response = fixture.capture(
        {{"raw", "Ada: drop it? Bob: yes"}, {"link", "PROJ-1"}, {"status", "live"}});
    REQUIRE(response.status == 201);
    const nlohmann::json body = parsed(response);
    CHECK(body["db"] == "knowledge");
    CHECK(body["retriever"] == "lexical");
    CHECK(body["registered"] == true);
    const std::string id = body["record"]["id"].get<std::string>();
    CHECK(id.starts_with("kr-"));
    CHECK(body["record"]["status"] == "shipped");
    CHECK(body["record"]["downstream_link"] == "PROJ-1");
    CHECK(body["record"]["provenance"]["attribution"] == "Ada Lovelace");
    CHECK(body["record"]["raw_ref"].get<std::string>().ends_with(id + ".md"));

    const Store store = fixture.store();
    const std::optional<Record> stored = store.get(id);
    REQUIRE(stored.has_value());
    CHECK(bytes(stored->raw_ref) == "Ada: drop it? Bob: yes");
    CHECK(store.chunks().search("cancel button", 5).size() == 1);
    CHECK(apogee::harness::load_config(fixture.config_path).find_embedding("knowledge") != nullptr);
    // The clerk ran once, on the served backend, as a side request.
    REQUIRE(fixture.provider->requests().size() == 1);
    CHECK(fixture.provider->requests().front().transient.side_request);
    CHECK(fixture.provider->requests().front().model == "mock");

    // A second capture: no re-registration, and an explicit model resolves.
    const HttpResponse again = fixture.capture({{"raw", "more"}, {"model", "mock"}});
    REQUIRE(again.status == 201);
    CHECK(parsed(again)["registered"] == false);
    CHECK(store.list().size() == 2);
}

TEST_CASE("the capture twin refuses what the CLI refuses, with the status a client acts on",
          "[httpserver][admin][knowledge][refusals]") {
    const Fixture fixture;
    HttpRequest not_json;
    not_json.method = "POST";
    not_json.body = "[";
    CHECK(apogee::httpserver::admin_capture_knowledge(fixture.context(), *fixture.handler, not_json)
              .status == 400);
    CHECK(fixture.capture({{"link", "x"}}).status == 400);
    CHECK(parsed(fixture.capture({{"link", "x"}}))["error"]["message"].get<std::string>().find(
              "raw is required") != std::string::npos);
    CHECK(fixture.capture({{"raw", 7}}).status == 400);
    CHECK(fixture.capture({{"raw", "x"}, {"status", "maybe"}}).status == 400);
    CHECK(fixture.capture({{"raw", "x"}, {"retriever", "sideways"}}).status == 400);
    CHECK(fixture.capture({{"raw", "x"}, {"db", "../x"}}).status == 400);
    CHECK(fixture.capture({{"raw", "x"}, {"model", "nope"}}).status == 400);
    const HttpResponse vendor = fixture.capture({{"raw", "x"}, {"model", "vendor"}});
    CHECK(vendor.status == 400);
    CHECK(parsed(vendor)["error"]["message"].get<std::string>().find("vendor CLI") !=
          std::string::npos);
    // An explicit vector ask with no embedder: the resolver's refusal.
    const HttpResponse vector = fixture.capture({{"raw", "x"}, {"retriever", "vector"}});
    CHECK(vector.status == 400);
    CHECK(parsed(vector)["error"]["message"].get<std::string>().find("vector ingest cannot run") !=
          std::string::npos);
    // Nothing was stored by any of those, and the clerk was never asked.
    CHECK_FALSE(std::filesystem::exists(fixture.home.path() / "embeddings" / "knowledge.db"));
    CHECK(fixture.provider->requests().empty());

    // No generation backend served: 501, honestly, before any clerk call.
    const Fixture unserved{{MockTurn{std::string{kRecord}}}, /*serve=*/false};
    const HttpResponse none = unserved.capture({{"raw", "x"}});
    CHECK(none.status == 501);
    CHECK(parsed(none)["error"]["type"] == "backend_unavailable");
    CHECK(unserved.provider->requests().empty());

    // A clerk that never conforms: 502, and nothing stored.
    const Fixture stubborn{{MockTurn{"no"}, MockTurn{"still no"}}};
    const HttpResponse failed = stubborn.capture({{"raw", "x"}});
    CHECK(failed.status == 502);
    CHECK(parsed(failed)["error"]["type"] == "backend_error");
    CHECK(parsed(failed)["error"]["message"].get<std::string>().find("after 2 attempts") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(stubborn.home.path() / "embeddings" / "knowledge.db"));
}

TEST_CASE(
    "POST /v1/admin/knowledge stores a finished record without the clerk, minting what is "
    "missing and keeping what is given",
    "[httpserver][admin][knowledge][create]") {
    const Fixture fixture;
    const HttpResponse created =
        fixture.create({{"intent", "because the old one leaked"},
                        {"decision", "rotate the token"},
                        {"status", "Implemented"},
                        {"discipline", "eng"},
                        {"downstream_link", "SEC-9"},
                        {"provenance", {{"source", "review"}, {"attribution", "Bob"}}},
                        {"raw", "the review thread"}});
    REQUIRE(created.status == 201);
    const nlohmann::json body = parsed(created);
    const std::string id = body["record"]["id"].get<std::string>();
    CHECK(id.starts_with("kr-"));
    CHECK(body["record"]["status"] == "shipped");
    CHECK(body["record"]["timestamp"].get<std::string>().ends_with("Z"));
    CHECK(body["registered"] == true);
    CHECK(fixture.provider->requests().empty());
    const Store store = fixture.store();
    REQUIRE(store.get(id).has_value());
    CHECK(bytes(store.get(id)->raw_ref) == "the review thread");
    CHECK(store.get(id)->provenance.attribution == "Bob");

    // A given id is kept; a supersedes target is flipped; no raw, no archive.
    const HttpResponse second = fixture.create({{"id", "kr-20260913T120000Z-fixed1"},
                                                {"intent", "newer thinking"},
                                                {"status", "shipped"},
                                                {"supersedes", id}});
    REQUIRE(second.status == 201);
    CHECK(parsed(second)["record"]["id"] == "kr-20260913T120000Z-fixed1");
    CHECK_FALSE(parsed(second)["record"].contains("raw_ref"));
    CHECK(store.get(id)->status == "superseded");
    const std::optional<Record> fixed = store.get("kr-20260913T120000Z-fixed1");
    REQUIRE(fixed.has_value());
    CHECK(fixed->provenance.source == "manual");
    CHECK(fixed->timestamp.ends_with("Z"));

    // Validation is the record's: no intent, a bad status.
    CHECK(fixture.create({{"decision", "x"}, {"status", "shipped"}}).status == 400);
    CHECK(fixture.create({{"intent", "x"}, {"status", "perhaps"}}).status == 400);
    CHECK(fixture.create({{"intent", "x"}, {"retriever", "vector"}}).status == 400);
    CHECK(store.list().size() == 2);
}

TEST_CASE("a record captured over HTTP is the record the CLI captures from the same conversation",
          "[httpserver][admin][knowledge][parity]") {
    const Fixture fixture;
    const std::string raw = "Ada: drop the cancel button? Bob: yes, testers mistake it for back";
    const HttpResponse response = fixture.capture({{"raw", raw}, {"link", "PROJ-42"}});
    REQUIRE(response.status == 201);
    fixture.cli({"knowledge", "capture", "--link", "PROJ-42", raw});

    const Store store = fixture.store();
    const std::vector<Record> records = store.list();
    REQUIRE(records.size() == 2);
    const auto strip = [](Record record) {
        record.id.clear();
        record.timestamp.clear();
        record.raw_ref.clear();
        return nlohmann::json(record);
    };
    CHECK(strip(records[0]) == strip(records[1]));
    CHECK(bytes(records[0].raw_ref) == bytes(records[1].raw_ref));
    CHECK(store.chunks().chunk_by_id(*store.chunk_id(records[0].id))->text ==
          store.chunks().chunk_by_id(*store.chunk_id(records[1].id))->text);
}

TEST_CASE("the knowledge routes sit behind the gate and dispatch through the mux",
          "[httpserver][admin][knowledge][mux]") {
    const Fixture fixture;
    apogee::events::Bus bus;
    apogee::httpserver::JobRegistry jobs{bus};
    apogee::httpserver::AdminOptions admin_options;
    admin_options.config_path = fixture.config_path;
    admin_options.startup = fixture.config;
    apogee::httpserver::AdminHandler admin{admin_options, jobs, bus};
    const apogee::httpserver::Mux mux{*fixture.handler, admin, "token"};

    HttpRequest request = Fixture::post({{"raw", "x"}});
    request.path = "/v1/admin/knowledge/capture";
    CHECK(mux.dispatch(request).status == 401);
    request.headers["authorization"] = "Bearer token";
    CHECK(mux.dispatch(request).status == 201);
    request.path = "/v1/admin/knowledge";
    request.body = nlohmann::json{{"intent", "why"}, {"status", "shipped"}}.dump();
    CHECK(mux.dispatch(request).status == 201);
    request.method = "GET";
    CHECK(mux.dispatch(request).status == 405);
}
