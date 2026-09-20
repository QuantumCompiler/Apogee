#include "httpserver/admin_training.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "events/bus.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "harness/layout.h"
#include "httpserver/admin.h"
#include "httpserver/handler.h"
#include "httpserver/jobs.h"
#include "httpserver/mux.h"
#include "support/env_guard.h"
#include "training/manifest.h"
#include "training/store.h"

/// The training reads on the control plane: status with the running ids
/// and the ledgers, the runs list newest first with `data` never null and
/// the kind filter, one run's manifest with an honest 404 (and a 400 for a
/// path-shaped id), the versions -- one ledger or all -- and every route
/// behind the gate. The CLI writes; this only reads what it wrote.
namespace {

using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

struct Fixture {
    apogee::testing::TempDir home{"admin-training-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    apogee::harness::Config config;
    apogee::training::TrainingStore store{home.path() / "training"};

    Fixture() {
        std::filesystem::create_directories(config_path.parent_path());
        std::ofstream{config_path, std::ios::binary} << "backends:\n  mock:\n    type: mock\n";
        config = apogee::harness::load_config(config_path);
    }

    [[nodiscard]] AdminConfigContext context() const {
        return AdminConfigContext{.config_path = config_path, .startup = &config};
    }

    void run(const std::string& id, const std::string& status, bool with_eval = false) const {
        apogee::training::RunManifest m;
        m.run_id = id;
        m.trainer = "mock";
        m.base_model = "/snap";
        m.dataset = "/d.jsonl";
        m.method = "lora";
        m.status = status;
        m.final_loss = 0.25;
        m.started_at = "2026-09-19T12:00:00Z";
        if (with_eval) {
            apogee::training::EvalResults eval;
            eval.passed = true;
            eval.score = 1.0;
            eval.total = 2;
            eval.num_passed = 2;
            m.eval = eval;
        }
        REQUIRE(apogee::training::write_manifest(store.run_dir(id), m).empty());
    }

    void ledger(const std::string& backend, int active, int count) const {
        apogee::training::VersionLedger l;
        l.backend = backend;
        l.active_version = active;
        for (int v = 1; v <= count; ++v) {
            apogee::training::VersionEntry e;
            e.version = v;
            e.run_id = "r" + std::to_string(v);
            e.gguf_path = "/v/" + backend + "/v" + std::to_string(v) + ".gguf";
            e.promoted_at = "2026-09-19T12:00:00Z";
            l.versions.push_back(e);
        }
        REQUIRE(apogee::training::save_ledger(store.versions_dir(), l).empty());
    }

    [[nodiscard]] static HttpRequest get(const std::string& path) {
        HttpRequest out;
        out.method = "GET";
        out.path = path;
        return out;
    }
};

}  // namespace

TEST_CASE(
    "status reports the runs, the running ids, the ledgers, and the reserved pipeline "
    "fields",
    "[httpserver][admin][training][status]") {
    Fixture fixture;
    const nlohmann::json empty =
        parsed(apogee::httpserver::admin_training_status(fixture.context()));
    CHECK(empty["runs"] == 0);
    CHECK(empty["running"].is_array());
    CHECK(empty["running"].empty());
    CHECK(empty["versions"].is_array());
    CHECK(empty["active_pipeline"].is_null());
    CHECK(empty["cycle_active"] == false);

    fixture.run("20260919-100000", "complete", true);
    fixture.run("20260919-110000", "running");
    fixture.run("20260919-120000", "failed");
    fixture.ledger("tuned", 2, 2);
    const nlohmann::json status =
        parsed(apogee::httpserver::admin_training_status(fixture.context()));
    CHECK(status["runs"] == 3);
    CHECK(status["running"] == nlohmann::json::array({"20260919-110000"}));
    REQUIRE(status["versions"].size() == 1);
    CHECK(status["versions"][0]["backend"] == "tuned");
    CHECK(status["versions"][0]["active_version"] == 2);
    CHECK(status["versions"][0]["kept"] == 2);
    CHECK(status["versions"][0]["total"] == 2);
}

TEST_CASE(
    "the runs list is newest first with data never null, the kind filter applies, and "
    "one run is its manifest or an honest 404",
    "[httpserver][admin][training][runs]") {
    Fixture fixture;
    const HttpResponse none =
        apogee::httpserver::admin_list_training_runs(fixture.context(), Fixture::get("/x"));
    CHECK(none.status == 200);
    CHECK(parsed(none)["object"] == "list");
    CHECK(parsed(none)["data"].is_array());
    CHECK(parsed(none)["data"].empty());

    fixture.run("20260919-100000", "complete", true);
    fixture.run("20260919-110000", "running");
    const nlohmann::json listed =
        parsed(apogee::httpserver::admin_list_training_runs(fixture.context(), Fixture::get("/x")));
    REQUIRE(listed["data"].size() == 2);
    CHECK(listed["data"][0]["id"] == "20260919-110000");
    CHECK(listed["data"][0]["status"] == "running");
    CHECK(listed["data"][0]["kind"] == "run");
    CHECK_FALSE(listed["data"][0].contains("eval_passed"));
    CHECK(listed["data"][1]["eval_passed"] == true);
    CHECK(listed["data"][1]["eval_score"] == 1.0);

    HttpRequest runs_only = Fixture::get("/x");
    runs_only.query["kind"] = "run";
    CHECK(parsed(apogee::httpserver::admin_list_training_runs(fixture.context(), runs_only))["data"]
              .size() == 2);
    HttpRequest pipelines = Fixture::get("/x");
    pipelines.query["kind"] = "pipeline";
    CHECK(parsed(apogee::httpserver::admin_list_training_runs(fixture.context(), pipelines))["data"]
              .empty());
    HttpRequest bad = Fixture::get("/x");
    bad.query["kind"] = "job";
    CHECK(apogee::httpserver::admin_list_training_runs(fixture.context(), bad).status == 400);

    const HttpResponse one =
        apogee::httpserver::admin_get_training_run(fixture.context(), "20260919-100000");
    CHECK(one.status == 200);
    CHECK(parsed(one)["kind"] == "run");
    CHECK(parsed(one)["run"]["run_id"] == "20260919-100000");
    CHECK(parsed(one)["run"]["trainer"] == "mock");
    CHECK(parsed(one)["run"]["eval_results"]["passed"] == true);
    CHECK(apogee::httpserver::admin_get_training_run(fixture.context(), "nope").status == 404);
    CHECK(apogee::httpserver::admin_get_training_run(fixture.context(), "../x").status == 400);
}

TEST_CASE("versions: one ledger by backend or 404, else every ledger as a list",
          "[httpserver][admin][training][versions]") {
    Fixture fixture;
    const HttpResponse none =
        apogee::httpserver::admin_list_training_versions(fixture.context(), Fixture::get("/x"));
    CHECK(none.status == 200);
    CHECK(parsed(none)["data"].is_array());
    CHECK(parsed(none)["data"].empty());
    HttpRequest missing = Fixture::get("/x");
    missing.query["backend"] = "tuned";
    CHECK(apogee::httpserver::admin_list_training_versions(fixture.context(), missing).status ==
          404);

    fixture.ledger("tuned", 3, 3);
    fixture.ledger("alpha", 1, 1);
    const nlohmann::json one =
        parsed(apogee::httpserver::admin_list_training_versions(fixture.context(), missing));
    CHECK(one["backend_name"] == "tuned");
    CHECK(one["active_version"] == 3);
    CHECK(one["versions"].size() == 3);
    CHECK(one["versions"][2]["gguf_path"] == "/v/tuned/v3.gguf");
    const nlohmann::json all = parsed(
        apogee::httpserver::admin_list_training_versions(fixture.context(), Fixture::get("/x")));
    REQUIRE(all["data"].size() == 2);
    CHECK(all["data"][0]["backend_name"] == "alpha");
    CHECK(all["data"][1]["backend_name"] == "tuned");
}

TEST_CASE(
    "the training routes are gated, served through the mux, and nothing under them "
    "mutates",
    "[httpserver][admin][training][mux]") {
    Fixture fixture;
    fixture.run("20260919-100000", "complete");
    apogee::harness::Harness harness{fixture.config};
    apogee::httpserver::Handler handler{harness, {}, nullptr};
    apogee::events::Bus bus;
    apogee::httpserver::JobRegistry jobs{bus};
    apogee::httpserver::AdminOptions options;
    options.config_path = fixture.config_path;
    options.startup = fixture.config;
    apogee::httpserver::AdminHandler admin{options, jobs, bus};
    const apogee::httpserver::Mux mux{handler, admin, "secret"};

    for (const char* path :
         {"/v1/admin/training/status", "/v1/admin/training/runs",
          "/v1/admin/training/runs/20260919-100000", "/v1/admin/training/versions"}) {
        HttpRequest probe = Fixture::get(path);
        INFO(path);
        CHECK(mux.dispatch(probe).status == 401);
        probe.headers["authorization"] = "Bearer secret";
        CHECK(mux.dispatch(probe).status == 200);
    }
    HttpRequest by_query = Fixture::get("/v1/admin/training/runs");
    by_query.query["kind"] = "pipeline";
    by_query.headers["authorization"] = "Bearer secret";
    const HttpResponse filtered = mux.dispatch(by_query);
    CHECK(filtered.status == 200);
    CHECK(parsed(filtered)["data"].empty());

    HttpRequest post = Fixture::get("/v1/admin/training/runs");
    post.method = "POST";
    post.headers["authorization"] = "Bearer secret";
    CHECK(mux.dispatch(post).status != 200);
    HttpRequest promote = Fixture::get("/v1/admin/training/promote");
    promote.method = "POST";
    promote.headers["authorization"] = "Bearer secret";
    CHECK(mux.dispatch(promote).status == 404);
}
