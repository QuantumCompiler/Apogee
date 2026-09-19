#include "httpserver/admin_graph.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "backends/mock.h"
#include "embedstore/store.h"
#include "events/bus.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/harness.h"
#include "httpserver/handler.h"
#include "httpserver/jobs.h"
#include "httpserver/mux.h"
#include "support/env_guard.h"

/// The graph slice of the control plane: the build as an async job on the
/// plane's harness with the CLI's resolution and refusals, its progress and
/// counts, cancellation; stats never an error; entity lookup exact then
/// fuzzy; delete; the `enabled` twin through the one editor; and the routes
/// behind the gate through the mux.
namespace {

using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::embedstore::Store;
using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::Handler;
using apogee::httpserver::HandlerOptions;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;
using apogee::httpserver::JobRegistry;
using apogee::httpserver::JobStatus;
using apogee::httpserver::JobWorkers;

constexpr std::string_view kExtraction =
    R"({"entities": [{"name": "Atlas", "type": "system", "description": "collects readings"},
                     {"name": "Vault", "type": "system", "description": "the warehouse"}],
        "relations": [{"source": "Atlas", "target": "Vault", "relation": "stores readings in",
                       "description": ""}]})";

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
    apogee::testing::TempDir home{"admin-graph-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    apogee::harness::Config config;
    std::unique_ptr<apogee::harness::Harness> harness;
    std::shared_ptr<MockProvider> provider;
    std::unique_ptr<Handler> handler;
    apogee::events::Bus bus;
    JobRegistry jobs{bus};
    // Declared after the handler: destroyed first, so every worker is
    // cancelled and joined before the plane it works on goes.
    JobWorkers workers;

    /// `metered_default` makes the default a paid backend (Anthropic with a
    /// key) that the cost policy refuses by fall-through; `extra_config` is
    /// appended verbatim.
    explicit Fixture(std::vector<MockTurn> turns = {MockTurn{std::string{kExtraction}}},
                     bool serve = true, bool metered_default = false,
                     std::string_view extra_config = {}) {
        std::filesystem::create_directories(config_path.parent_path());
        std::ofstream{config_path, std::ios::binary}
            << "models:\n  default: " << (metered_default ? "paid" : "mock") << "\n"
            << "backends:\n  mock:\n    type: mock\n  vendor:\n    type: claude-cli\n"
            << "  paid:\n    type: anthropic\n    api_key: sk-ant-test\n"
            << extra_config;
        config = apogee::harness::load_config(config_path);
        harness = std::make_unique<apogee::harness::Harness>(config);
        MockProvider::Options options;
        options.backend_name = "mock";
        options.turns = std::move(turns);
        provider = std::make_shared<MockProvider>(std::move(options));
        harness->register_provider("mock", provider);
        // A second mock that claims to be metered: the cost policy is
        // testable against a paid backend with no network.
        MockProvider::Options paid_options;
        paid_options.backend_name = "paid";
        paid_options.turns = {MockTurn{std::string{kExtraction}}};
        paid_options.metered = true;
        harness->register_provider("paid", std::make_shared<MockProvider>(std::move(paid_options)));
        harness->use_default_router();
        HandlerOptions served;
        if (serve) {
            served.served = {"mock", "paid"};
            served.default_backend = metered_default ? "paid" : "mock";
        }
        handler = std::make_unique<Handler>(*harness, served, nullptr);
    }

    void ingest() const {
        Store store{home.path() / "embeddings" / "notes.db"};
        store.replace_source("a.md", {"Atlas collects readings from the field probes."});
        store.replace_source("b.md", {"The warehouse keeps every record for seven years."});
    }

    [[nodiscard]] Store store() const {
        return Store{home.path() / "embeddings" / "notes.db"};
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

    [[nodiscard]] HttpResponse build(std::string_view collection, const nlohmann::json& body = {}) {
        return apogee::httpserver::admin_build_graph(context(), *handler, jobs, workers, collection,
                                                     request("POST", body));
    }

    [[nodiscard]] HttpResponse stats(std::string_view collection) const {
        return apogee::httpserver::admin_graph_stats(context(), collection, request("GET"));
    }

    [[nodiscard]] HttpResponse entity(std::string_view collection, std::string name) const {
        HttpRequest get = request("GET");
        get.query["name"] = std::move(name);
        return apogee::httpserver::admin_graph_entity(context(), collection, get);
    }

    [[nodiscard]] HttpResponse remove(std::string_view collection) const {
        return apogee::httpserver::admin_delete_graph(context(), collection, request("DELETE"));
    }

    [[nodiscard]] HttpResponse enable(std::string_view collection,
                                      const nlohmann::json& body) const {
        return apogee::httpserver::admin_set_graph_enabled(context(), collection,
                                                           request("PUT", body));
    }

    /// Waits for the job to leave `running`; a test never asserts on a
    /// thread's timing, only on the record it leaves.
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

TEST_CASE(
    "POST /graph/{name}/build runs the build as a job on the plane's harness and records "
    "the counts, then the enabled write lands through the one editor",
    "[httpserver][admin][graph][build]") {
    Fixture fixture;
    fixture.ingest();
    const std::string before = bytes(fixture.config_path);
    HttpResponse response = fixture.build("notes");
    REQUIRE(response.status == 202);
    const std::string id = parsed(response)["job_id"].get<std::string>();
    const apogee::httpserver::JobRecord done = fixture.wait(id);
    INFO(done.error);
    CHECK(done.status == JobStatus::Succeeded);
    CHECK(done.kind == "graph-build");
    CHECK(done.result["collection"] == "notes");
    CHECK(done.result["files_planned"] == 2);
    CHECK(done.result["files_extracted"] == 2);
    CHECK(done.result["chunks_extracted"] == 2);
    CHECK(done.result["nodes_upserted"] == 2);
    CHECK(done.result["edges_upserted"] == 2);  // one edge, corroborated by the second chunk
    CHECK(done.result["mentions_added"] == 4);
    CHECK(done.result["record_nodes"] == 0);
    CHECK(done.result["enabled"] == true);
    CHECK(fixture.provider->requests().size() == 2);
    CHECK(fixture.provider->requests().front().transient.side_request);
    CHECK(fixture.store().graph_stats().nodes == 2);
    // The config gained the entry and its graph block, exactly as the CLI
    // would have written them.
    const std::string after = bytes(fixture.config_path);
    CHECK(after.starts_with(before));
    CHECK(after.ends_with("embeddings:\n  notes:\n    graph:\n      enabled: true\n"));
    CHECK(apogee::harness::load_config(fixture.config_path).find_embedding("notes")->graph.enabled);

    // A second build plans nothing and leaves the config alone.
    response = fixture.build("notes", nlohmann::json::object());
    REQUIRE(response.status == 202);
    const apogee::httpserver::JobRecord again =
        fixture.wait(parsed(response)["job_id"].get<std::string>());
    CHECK(again.status == JobStatus::Succeeded);
    CHECK(again.result["files_planned"] == 0);
    CHECK_FALSE(again.result.contains("enabled"));
    CHECK(bytes(fixture.config_path) == after);
    // A forced build re-extracts, with the limit honoured.
    response = fixture.build("notes", nlohmann::json{{"force", true}, {"limit", 1}});
    REQUIRE(response.status == 202);
    const apogee::httpserver::JobRecord forced =
        fixture.wait(parsed(response)["job_id"].get<std::string>());
    CHECK(forced.result["chunks_extracted"] == 1);
    CHECK(forced.result["limit_hit"] == true);
}

TEST_CASE("the build route refuses what the CLI refuses, with the status a client acts on",
          "[httpserver][admin][graph][refusals]") {
    Fixture fixture;
    CHECK(fixture.build("notes").status == 404);  // no collection yet
    fixture.ingest();
    CHECK(fixture.build("../x").status == 400);
    CHECK(fixture.build("notes", nlohmann::json{{"model", 7}}).status == 400);
    CHECK(fixture.build("notes", nlohmann::json{{"limit", -1}}).status == 400);
    CHECK(fixture.build("notes", nlohmann::json{{"model", "vendor"}}).status == 400);
    CHECK(fixture.build("notes", nlohmann::json{{"model", "ghost"}}).status == 400);
    CHECK(fixture.store().graph_stats().nodes == 0);
    CHECK(fixture.jobs.list().empty());

    // A metered default is refused by fall-through; named, or reached
    // through the collection's block or the role, it is allowed.
    Fixture metered{{MockTurn{std::string{kExtraction}}}, true, true};
    metered.ingest();
    HttpResponse refused = metered.build("notes");
    CHECK(refused.status == 400);
    CHECK(parsed(refused)["error"]["message"].get<std::string>().find("metered") !=
          std::string::npos);
    CHECK(metered.jobs.list().empty());
    HttpResponse named = metered.build("notes", nlohmann::json{{"model", "mock"}});
    CHECK(named.status == 202);
    CHECK(metered.wait(parsed(named)["job_id"].get<std::string>()).status == JobStatus::Succeeded);
    // The metered backend itself, named explicitly: allowed -- it is the
    // fall-through that is refused, not the backend.
    HttpResponse paid = metered.build("notes", nlohmann::json{{"model", "paid"}, {"force", true}});
    CHECK(paid.status == 202);
    CHECK(metered.wait(parsed(paid)["job_id"].get<std::string>()).status == JobStatus::Succeeded);

    Fixture pinned{{MockTurn{std::string{kExtraction}}},
                   true,
                   true,
                   "embeddings:\n  notes:\n    graph:\n      extract_backend: mock\n"};
    pinned.ingest();
    HttpResponse through_pin = pinned.build("notes");
    CHECK(through_pin.status == 202);
    CHECK(pinned.wait(parsed(through_pin)["job_id"].get<std::string>()).status ==
          JobStatus::Succeeded);

    // No generation backend at all: 501.
    Fixture unserved{{MockTurn{std::string{kExtraction}}}, /*serve=*/false};
    unserved.ingest();
    CHECK(unserved.build("notes").status == 501);
}

TEST_CASE("a build job is cancellable between chunks, and a failing extractor counts",
          "[httpserver][admin][graph][cancel]") {
    Fixture fixture{{MockTurn{"I cannot produce JSON."}, MockTurn{"still prose"}}};
    fixture.ingest();
    HttpResponse response = fixture.build("notes");
    REQUIRE(response.status == 202);
    const apogee::httpserver::JobRecord done =
        fixture.wait(parsed(response)["job_id"].get<std::string>());
    CHECK(done.status == JobStatus::Succeeded);
    CHECK(done.result["chunks_failed"] == 2);
    CHECK(done.result["files_extracted"] == 0);
    CHECK(fixture.store().source_states().empty());

    // Cancel wins: a cancelled job stays cancelled whatever the worker does.
    Fixture slow;
    slow.ingest();
    HttpResponse started = slow.build("notes");
    REQUIRE(started.status == 202);
    const std::string id = parsed(started)["job_id"].get<std::string>();
    REQUIRE(slow.jobs.cancel(id).has_value());
    const apogee::httpserver::JobRecord cancelled = slow.wait(id);
    CHECK(cancelled.status == JobStatus::Cancelled);
}

TEST_CASE(
    "GET stats is never an error, entity lookup is exact then fuzzy, and DELETE clears the "
    "rows",
    "[httpserver][admin][graph][reads]") {
    Fixture fixture;
    // Nothing on disk: zeros, not a 404.
    HttpResponse response = fixture.stats("notes");
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["nodes"] == 0);
    CHECK(parsed(response)["nodes_by_type"].is_object());
    CHECK(fixture.stats("../x").status == 400);
    CHECK(fixture.entity("notes", "Atlas").status == 404);
    CHECK(fixture.remove("notes").status == 404);

    fixture.ingest();
    CHECK(parsed(fixture.stats("notes"))["total_chunks"] == 2);
    CHECK(parsed(fixture.stats("notes"))["stale_files"] == 2);
    REQUIRE(fixture.build("notes").status == 202);
    (void)fixture.wait(fixture.jobs.list().front().id);
    response = fixture.stats("notes");
    CHECK(parsed(response)["nodes"] == 2);
    CHECK(parsed(response)["nodes_by_type"]["system"] == 2);
    CHECK(parsed(response)["chunks_with_mentions"] == 2);
    CHECK(parsed(response)["extract_model"] == "mock");
    CHECK(parsed(response)["stale_files"] == 0);

    CHECK(fixture.entity("notes", "").status == 400);
    response = fixture.entity("notes", "  ATLAS ");
    REQUIRE(response.status == 200);
    nlohmann::json body = parsed(response);
    CHECK(body["fuzzy"] == false);
    REQUIRE(body["data"].size() == 1);
    CHECK(body["data"][0]["name"] == "Atlas");
    CHECK(body["data"][0]["mentions"] == 2);
    REQUIRE(body["data"][0]["relations"].size() == 1);
    CHECK(body["data"][0]["relations"][0]["direction"] == "out");
    CHECK(body["data"][0]["relations"][0]["peer"] == "Vault");
    CHECK(body["data"][0]["chunks"].size() == 2);
    response = fixture.entity("notes", "warehouse");
    REQUIRE(response.status == 200);
    body = parsed(response);
    CHECK(body["fuzzy"] == true);
    CHECK(body["data"][0]["name"] == "Vault");
    CHECK(fixture.entity("notes", "nothing like this").status == 404);

    response = fixture.remove("notes");
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["deleted"]["nodes"] == 2);
    CHECK(parsed(response)["deleted"]["edges"] == 1);
    CHECK(fixture.store().graph_stats().nodes == 0);
    CHECK(fixture.store().chunk_count() == 2);
    CHECK(parsed(fixture.stats("notes"))["nodes"] == 0);
}

TEST_CASE("PUT /embeddings/{name}/graph is the twin of the auto-enable write",
          "[httpserver][admin][graph][enabled]") {
    Fixture fixture{{MockTurn{std::string{kExtraction}}},
                    true,
                    false,
                    "embeddings:\n  notes:\n    chunk_size: 512\n"};
    const std::string before = bytes(fixture.config_path);
    CHECK(fixture.enable("notes", nlohmann::json{{"enabled", "yes"}}).status == 400);
    CHECK(fixture.enable("notes", nlohmann::json::object()).status == 400);
    CHECK(fixture.enable("ghost", nlohmann::json{{"enabled", true}}).status == 404);
    CHECK(bytes(fixture.config_path) == before);

    HttpResponse response = fixture.enable("notes", nlohmann::json{{"enabled", true}});
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["enabled"] == true);
    // Byte-identical to the CLI's own edit of the same file.
    CHECK(bytes(fixture.config_path) ==
          apogee::harness::set_embedding_graph_enabled(before, "notes", true));
    CHECK(apogee::harness::load_config(fixture.config_path).find_embedding("notes")->graph.enabled);
    response = fixture.enable("notes", nlohmann::json{{"enabled", false}});
    REQUIRE(response.status == 200);
    CHECK_FALSE(
        apogee::harness::load_config(fixture.config_path).find_embedding("notes")->graph.enabled);
}

TEST_CASE("the graph routes sit behind the gate and dispatch through the mux",
          "[httpserver][admin][graph][mux]") {
    Fixture fixture;
    apogee::httpserver::AdminOptions options;
    options.config_path = fixture.config_path;
    options.startup = fixture.config;
    apogee::httpserver::AdminHandler admin{options, fixture.jobs, fixture.bus};
    const apogee::httpserver::Mux mux{*fixture.handler, admin, "secret"};

    HttpRequest anonymous;
    anonymous.method = "GET";
    anonymous.path = "/v1/admin/graph/notes/stats";
    CHECK(mux.dispatch(anonymous).status == 401);

    HttpRequest stats = anonymous;
    stats.headers["authorization"] = "Bearer secret";
    HttpResponse response = mux.dispatch(stats);
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["nodes"] == 0);

    HttpRequest entity = stats;
    entity.path = "/v1/admin/graph/notes/entity";
    entity.query["name"] = "x";
    CHECK(mux.dispatch(entity).status == 404);  // no collection: the route ran

    HttpRequest build = stats;
    build.method = "POST";
    build.path = "/v1/admin/graph/notes/build";
    CHECK(mux.dispatch(build).status == 404);  // no collection: the route ran
    build.method = "GET";
    CHECK(mux.dispatch(build).status == 405);

    HttpRequest remove = stats;
    remove.method = "DELETE";
    remove.path = "/v1/admin/graph/notes";
    CHECK(mux.dispatch(remove).status == 404);
    remove.method = "GET";
    CHECK(mux.dispatch(remove).status == 405);

    HttpRequest enable = stats;
    enable.method = "PUT";
    enable.path = "/v1/admin/embeddings/notes/graph";
    enable.body = R"({"enabled": true})";
    CHECK(mux.dispatch(enable).status == 404);  // the route ran; no such entry
    apogee::harness::edit_config_file(fixture.config_path, [](std::string_view content) {
        return apogee::harness::append_embedding(content, "notes",
                                                 apogee::harness::EmbeddingConfig{}, false);
    });
    CHECK(mux.dispatch(enable).status == 200);
    CHECK(apogee::harness::load_config(fixture.config_path).find_embedding("notes")->graph.enabled);
}
