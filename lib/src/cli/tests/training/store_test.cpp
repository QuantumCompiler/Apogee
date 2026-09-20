#include "training/store.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "support/env_guard.h"
#include "training/manifest.h"

/// The read side: runs listed newest first with an unreadable manifest
/// skipped and a running one shown as such, an unknown or path-shaped id
/// answered with nothing, the ledger round-tripping (pruned entries and the
/// optional eval fields included) and saved atomically, every ledger listed
/// by backend, the summary JSON.
namespace {

using apogee::training::RunManifest;
using apogee::training::TrainingStore;
using apogee::training::VersionEntry;
using apogee::training::VersionLedger;

RunManifest run(std::string id, std::string status = "complete") {
    RunManifest m;
    m.run_id = std::move(id);
    m.trainer = "mock";
    m.base_model = "/snap";
    m.dataset = "/d.jsonl";
    m.method = "lora";
    m.final_loss = 0.3;
    m.iterations = 5;
    m.status = std::move(status);
    m.started_at = "2026-09-19T12:00:00Z";
    return m;
}

}  // namespace

TEST_CASE(
    "runs list newest first, an unreadable manifest is skipped, and an unknown id is "
    "nothing",
    "[training][store][runs]") {
    const apogee::testing::TempDir root{"store-runs-" + std::to_string(std::random_device{}())};
    const TrainingStore store{root.path()};
    CHECK(store.list_runs().empty());  // no directory yet

    REQUIRE(
        apogee::training::write_manifest(store.run_dir("20260919-100000"), run("20260919-100000"))
            .empty());
    REQUIRE(apogee::training::write_manifest(store.run_dir("20260919-110000"),
                                             run("20260919-110000", "running"))
                .empty());
    RunManifest evaluated = run("20260918-090000");
    apogee::training::EvalResults eval;
    eval.passed = true;
    eval.score = 1.0;
    eval.total = 1;
    eval.num_passed = 1;
    evaluated.eval = eval;
    REQUIRE(apogee::training::write_manifest(store.run_dir("20260918-090000"), evaluated).empty());
    std::filesystem::create_directories(store.run_dir("broken"));
    std::ofstream{apogee::training::manifest_path(store.run_dir("broken")), std::ios::binary}
        << "nope";
    std::filesystem::create_directories(store.run_dir("empty"));

    const std::vector<apogee::training::RunSummary> runs = store.list_runs();
    REQUIRE(runs.size() == 3);
    CHECK(runs[0].id == "20260919-110000");
    CHECK(runs[0].status == "running");
    CHECK_FALSE(runs[0].eval_passed.has_value());
    CHECK(runs[1].id == "20260919-100000");
    CHECK(runs[2].id == "20260918-090000");
    CHECK(runs[2].eval_passed == true);
    CHECK(runs[2].eval_score == 1.0);

    const nlohmann::json json = apogee::training::run_summary_json(runs[2]);
    CHECK(json["kind"] == "run");
    CHECK(json["id"] == "20260918-090000");
    CHECK(json["eval_passed"] == true);
    CHECK(json["eval_score"] == 1.0);
    CHECK_FALSE(apogee::training::run_summary_json(runs[0]).contains("eval_passed"));

    REQUIRE(store.get_run("20260919-100000").has_value());
    CHECK(store.get_run("20260919-100000")->trainer == "mock");
    CHECK_FALSE(store.get_run("nope").has_value());
    CHECK_FALSE(store.get_run("broken").has_value());
    CHECK_FALSE(store.get_run("").has_value());
    // A path-shaped id must never reach outside runs/, even when the
    // target it names exists there.
    REQUIRE(apogee::training::write_manifest(root.path() / "escape", run("escape")).empty());
    CHECK_FALSE(store.get_run("../escape").has_value());
    CHECK_FALSE(store.get_run("../../escape").has_value());
}

TEST_CASE("a ledger round-trips, is saved atomically, and every ledger lists by backend",
          "[training][store][ledger]") {
    const apogee::testing::TempDir root{"store-ledger-" + std::to_string(std::random_device{}())};
    const TrainingStore store{root.path()};
    CHECK(store.all_versions().empty());
    CHECK_FALSE(store.list_versions("tuned").has_value());

    VersionLedger ledger;
    ledger.backend = "tuned";
    ledger.active_version = 3;
    VersionEntry v1;
    v1.version = 1;
    v1.run_id = "r1";
    v1.gguf_path = "/v/tuned/v1.gguf";
    v1.promoted_at = "2026-09-19T12:00:00Z";
    v1.pruned_at = "2026-09-19T14:00:00Z";
    VersionEntry v3;
    v3.version = 3;
    v3.run_id = "r3";
    v3.gguf_path = "/v/tuned/v3.gguf";
    v3.promoted_at = "2026-09-19T14:00:00Z";
    v3.eval_score = 0.9;
    v3.eval_passed = false;
    VersionEntry v2;
    v2.version = 2;
    v2.run_id = "r2";
    v2.gguf_path = "/v/tuned/v2.gguf";
    v2.promoted_at = "2026-09-19T13:00:00Z";
    ledger.versions = {v3, v1, v2};  // deliberately unsorted
    CHECK(apogee::training::save_ledger(store.versions_dir(), ledger).empty());
    CHECK(std::filesystem::exists(apogee::training::ledger_path(store.versions_dir(), "tuned")));

    const std::optional<VersionLedger> back = store.list_versions("tuned");
    REQUIRE(back.has_value());
    CHECK(back->backend == "tuned");
    CHECK(back->active_version == 3);
    REQUIRE(back->versions.size() == 3);
    CHECK(back->versions[0].version == 1);
    CHECK(back->versions[0].pruned());
    CHECK(back->versions[1].version == 2);
    CHECK_FALSE(back->versions[1].eval_score.has_value());
    CHECK(back->versions[2].eval_score == 0.9);
    CHECK(back->versions[2].eval_passed == false);
    CHECK(back->kept() == 2);
    CHECK(back->active()->run_id == "r3");
    CHECK(back->find(2)->run_id == "r2");
    CHECK(back->find(9) == nullptr);

    const nlohmann::json json = apogee::training::ledger_to_json(*back);
    CHECK(json["backend_name"] == "tuned");
    CHECK(json["versions"][0]["pruned_at"] == "2026-09-19T14:00:00Z");
    CHECK_FALSE(json["versions"][1].contains("pruned_at"));
    CHECK_FALSE(json["versions"][1].contains("eval_score"));
    CHECK(json["versions"][2]["eval_passed"] == false);

    VersionLedger other;
    other.backend = "alpha";
    other.active_version = 1;
    other.versions = {v1};
    CHECK(apogee::training::save_ledger(store.versions_dir(), other).empty());
    std::ofstream{store.versions_dir() / "broken.json", std::ios::binary} << "{";
    std::ofstream{store.versions_dir() / "notes.txt", std::ios::binary} << "x";
    const std::vector<VersionLedger> all = store.all_versions();
    REQUIRE(all.size() == 2);
    CHECK(all[0].backend == "alpha");
    CHECK(all[1].backend == "tuned");

    std::string error;
    CHECK_FALSE(apogee::training::load_ledger(store.versions_dir(), "broken", error).has_value());
    CHECK(error.find("not JSON") != std::string::npos);
    CHECK_THROWS_AS(apogee::training::ledger_from_json(nlohmann::json{{"versions", "x"}}),
                    std::runtime_error);
}
