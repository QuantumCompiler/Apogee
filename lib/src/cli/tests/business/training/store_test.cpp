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
    // A GGUF version is written as it always was: no mlx_path.
    CHECK(json["versions"][1]["gguf_path"] == "/v/tuned/v2.gguf");
    CHECK_FALSE(json["versions"][1].contains("mlx_path"));

    // An MLX version (27c): its directory, and no gguf_path beside it.
    VersionLedger mlx;
    mlx.backend = "tuned-mlx";
    mlx.active_version = 1;
    VersionEntry m1;
    m1.version = 1;
    m1.run_id = "r1";
    m1.mlx_path = "/models/m/mlx/abcdef012345";
    m1.promoted_at = "2026-10-04T12:00:00Z";
    mlx.versions = {m1};
    const nlohmann::json mlx_json = apogee::training::ledger_to_json(mlx);
    CHECK(mlx_json["versions"][0]["mlx_path"] == "/models/m/mlx/abcdef012345");
    CHECK_FALSE(mlx_json["versions"][0].contains("gguf_path"));
    const VersionLedger mlx_back = apogee::training::ledger_from_json(mlx_json);
    REQUIRE(mlx_back.versions.size() == 1);
    CHECK(mlx_back.versions[0].mlx());
    CHECK(mlx_back.versions[0].weights() == "/models/m/mlx/abcdef012345");
    CHECK(mlx_back.versions[0].gguf_path.empty());
    CHECK_FALSE(back->versions[0].mlx());

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

TEST_CASE(
    "an adapter version round-trips its adapter and the base hash it was pinned to; a merged "
    "ledger is written byte for byte as before",
    "[training][store][ledger][adapter]") {
    // 53b: the ledger records `adapter_path` and the base's weight hash, and
    // a rollback restores both without reading the base again.
    const apogee::testing::TempDir root{"store-adapter-" + std::to_string(std::random_device{}())};
    const TrainingStore store{root.path()};
    const std::string pin(64, 'c');
    VersionLedger ledger;
    ledger.backend = "based";
    ledger.active_version = 2;
    VersionEntry a1;
    a1.version = 1;
    a1.run_id = "r1";
    a1.adapter_path = (store.versions_dir() / "based" / "v1.lora.gguf").string();
    a1.base_sha256 = pin;
    a1.promoted_at = "2026-10-21T12:00:00Z";
    a1.pruned_at = "2026-10-21T14:00:00Z";
    VersionEntry a2;
    a2.version = 2;
    a2.run_id = "r2";
    a2.adapter_path = (store.versions_dir() / "based" / "v2.lora").string();
    a2.base_sha256 = pin;
    a2.promoted_at = "2026-10-21T13:00:00Z";
    a2.eval_score = 1.0;
    a2.eval_passed = true;
    ledger.versions = {a1, a2};
    REQUIRE(apogee::training::save_ledger(store.versions_dir(), ledger).empty());
    const std::optional<VersionLedger> back = store.list_versions("based");
    REQUIRE(back.has_value());
    REQUIRE(back->versions.size() == 2);
    CHECK(back->versions[0].adapter());
    CHECK(back->versions[0].adapter_path == a1.adapter_path);
    CHECK(back->versions[0].base_sha256 == pin);
    CHECK(back->versions[0].pruned());
    CHECK_FALSE(back->versions[0].mlx());  // a GGUF LoRA
    CHECK(back->versions[1].mlx());        // an mlx-lm directory
    CHECK(back->versions[1].weights() == a2.adapter_path);
    CHECK(back->versions[1].gguf_path.empty());
    CHECK(back->versions[1].mlx_path.empty());
    const nlohmann::json json = apogee::training::ledger_to_json(*back);
    CHECK(json["versions"][1]["adapter_path"] == a2.adapter_path);
    CHECK(json["versions"][1]["base_sha256"] == pin);
    CHECK_FALSE(json["versions"][1].contains("gguf_path"));
    CHECK_FALSE(json["versions"][1].contains("mlx_path"));

    // A merged version is written exactly as it was before adapters: the
    // same keys, in the same order -- nothing of 53b's added.
    VersionLedger merged;
    merged.backend = "tuned";
    merged.active_version = 1;
    VersionEntry m1;
    m1.version = 1;
    m1.run_id = "r1";
    m1.gguf_path = "/models/m/gguf/abcdef012345/tuned-v1.gguf";
    m1.promoted_at = "2026-10-21T12:00:00Z";
    m1.eval_score = 1.0;
    m1.eval_passed = true;
    merged.versions = {m1};
    CHECK(apogee::training::ledger_to_json(merged).dump() ==
          R"({"active_version":1,"backend_name":"tuned","versions":[{"eval_passed":true,)"
          R"("eval_score":1.0,"gguf_path":"/models/m/gguf/abcdef012345/tuned-v1.gguf",)"
          R"("promoted_at":"2026-10-21T12:00:00Z","run_id":"r1","version":1}]})");
}

TEST_CASE(
    "a version gated on its serving base round-trips where it was scored; one gated on its "
    "run's eval is written as before",
    "[training][store][ledger][serving]") {
    // 53c: the eval that let an adapter version through on a quantized base
    // says where it scored -- and only that one.
    const apogee::testing::TempDir root{"store-serving-" + std::to_string(std::random_device{}())};
    const TrainingStore store{root.path()};
    VersionLedger ledger;
    ledger.backend = "q4";
    ledger.active_version = 1;
    VersionEntry served;
    served.version = 1;
    served.run_id = "r1";
    served.adapter_path = (store.versions_dir() / "q4" / "v1.lora.gguf").string();
    served.base_sha256 = std::string(64, 'd');
    served.promoted_at = "2026-10-21T12:00:00Z";
    served.eval_score = 0.75;
    served.eval_passed = false;
    served.eval_scored_on = "tiny-Q4_K_M.gguf (Q4_K_M)";
    ledger.versions = {served};
    REQUIRE(apogee::training::save_ledger(store.versions_dir(), ledger).empty());
    const std::optional<VersionLedger> back = store.list_versions("q4");
    REQUIRE(back.has_value());
    REQUIRE(back->versions.size() == 1);
    CHECK(back->versions[0].eval_scored_on == "tiny-Q4_K_M.gguf (Q4_K_M)");
    CHECK(back->versions[0].eval_score == 0.75);
    CHECK(apogee::training::ledger_to_json(*back)["versions"][0]["eval_scored_on"] ==
          "tiny-Q4_K_M.gguf (Q4_K_M)");
    // Not scored there: no key at all, so every earlier ledger reads and
    // writes as it did.
    served.eval_scored_on.clear();
    ledger.versions = {served};
    CHECK_FALSE(apogee::training::ledger_to_json(ledger)["versions"][0].contains("eval_scored_on"));
    CHECK(apogee::training::ledger_from_json(apogee::training::ledger_to_json(ledger))
              .versions[0]
              .eval_scored_on.empty());
}

TEST_CASE(
    "pipelines list newest first with an unreadable manifest skipped, one by id or "
    "nothing, the running one active, and the summary JSON",
    "[training][store][pipeline]") {
    const apogee::testing::TempDir root{"store-pipelines-" +
                                        std::to_string(std::random_device{}())};
    const TrainingStore store{root.path()};
    CHECK(store.list_pipelines().empty());
    CHECK_FALSE(store.active_pipeline().has_value());
    CHECK_FALSE(store.get_pipeline("pipe-1").has_value());
    auto pipeline = [&](const std::string& id, const std::string& status,
                        const std::string& started) {
        apogee::training::PipelineRunManifest m;
        m.pipeline_run_id = id;
        m.spec_name = "skills";
        m.status = status;
        m.started_at = started;
        apogee::training::PipelineStageRecord a;
        a.index = 0;
        a.name = "a";
        a.status = "passed";
        apogee::training::PipelineStageRecord b;
        b.index = 1;
        b.name = "b";
        b.status = status == "complete" ? "passed" : "pending";
        m.stages = {a, b};
        REQUIRE(apogee::training::write_pipeline_manifest(store.pipeline_dir(id), m).empty());
    };
    pipeline("pipe-20260919-100000", "complete", "2026-09-19T10:00:00Z");
    pipeline("pipe-20260919-110000", "running", "2026-09-19T11:00:00Z");
    pipeline("pipe-20260919-090000", "aborted", "2026-09-19T09:00:00Z");
    std::filesystem::create_directories(store.pipeline_dir("broken"));
    std::ofstream{store.pipeline_dir("broken") / "manifest.json"} << "{";
    const std::vector<apogee::training::PipelineSummary> listed = store.list_pipelines();
    REQUIRE(listed.size() == 3);
    CHECK(listed[0].id == "pipe-20260919-110000");
    CHECK(listed[0].status == "running");
    CHECK(listed[0].stages == 2);
    CHECK(listed[0].passed == 1);
    CHECK(listed[1].id == "pipe-20260919-100000");
    CHECK(listed[1].passed == 2);
    CHECK(listed[2].id == "pipe-20260919-090000");
    REQUIRE(store.active_pipeline().has_value());
    CHECK(store.active_pipeline()->id == "pipe-20260919-110000");
    const auto one = store.get_pipeline("pipe-20260919-100000");
    REQUIRE(one.has_value());
    CHECK(one->spec_name == "skills");
    CHECK_FALSE(store.get_pipeline("../pipe-20260919-100000").has_value());
    CHECK_FALSE(store.get_pipeline("broken").has_value());
    const nlohmann::json json = apogee::training::pipeline_summary_json(listed[1]);
    CHECK(json["kind"] == "pipeline");
    CHECK(json["id"] == "pipe-20260919-100000");
    CHECK(json["spec_name"] == "skills");
    CHECK(json["stages"] == 2);
    CHECK(json["passed"] == 2);
    CHECK(json["started_at"] == "2026-09-19T10:00:00Z");
    CHECK_FALSE(json.contains("completed_at"));
}
