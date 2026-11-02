#include "operations/training_reads.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "contracts/layout.h"
#include "support/env_guard.h"
#include "training/cycle.h"
#include "training/pipeline.h"
#include "training/store.h"

/// The training reads the command line and the control plane share, as 57h
/// made them truthful: `cycle` null and `cycle_error` saying why when the
/// history cannot be read, both null on a fresh home; a pipeline document's
/// `resume` at the point the runner takes -- after the last passed stage,
/// an earlier failed one named as staying -- null for a complete run and a
/// cycle pass, and naming the `--pipeline` value that finds the run's spec;
/// `spec_ref` beside it; and a manifest that is there and cannot be read
/// said with its file, never as no run.
namespace {

namespace fs = std::filesystem;
using apogee::training::PipelineRunManifest;
using apogee::training::PipelineStageRecord;

struct Home {
    apogee::testing::TempDir root{"training-reads-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", root.path().string()};
    apogee::training::TrainingStore store{root.path() / "training"};

    [[nodiscard]] fs::path history() const {
        return apogee::training::history_path(root.path() / "training" / "cycle");
    }

    static void write(const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << text;
    }

    /// A pipeline run whose stages carry `statuses`, in order.
    void pipeline(const std::string& id, const std::vector<std::string>& statuses,
                  const std::string& status = "aborted", const std::string& spec_name = "skills",
                  const std::string& spec_ref = {}) const {
        PipelineRunManifest m;
        m.pipeline_run_id = id;
        m.spec_name = spec_name;
        m.spec_ref = spec_ref;
        m.status = status;
        m.started_at = "2026-11-01T09:00:00Z";
        int index = 0;
        for (const std::string& stage_status : statuses) {
            PipelineStageRecord stage;
            stage.index = index;
            stage.name = std::string(1, static_cast<char>('a' + index));
            stage.status = stage_status;
            m.stages.push_back(stage);
            ++index;
        }
        REQUIRE(apogee::training::write_pipeline_manifest(store.pipeline_dir(id), m).empty());
    }
};

[[nodiscard]] apogee::harness::Config config_with(
    const std::vector<std::pair<std::string, std::string>>& entries) {
    apogee::harness::Config config;
    for (const auto& [key, name] : entries) {
        apogee::harness::PipelineSpec spec;
        spec.name = name;
        config.training.pipelines[key] = spec;
    }
    return config;
}

[[nodiscard]] nlohmann::json document(const std::string& id,
                                      const apogee::harness::Config& config = {}) {
    std::string error;
    const std::optional<nlohmann::json> out =
        apogee::operations::training_pipeline_document(id, config, error);
    INFO(error);
    REQUIRE(out.has_value());
    CHECK(error.empty());
    return *out;
}

}  // namespace

TEST_CASE(
    "the status document says an unreadable cycle history as cycle_error with cycle null, and "
    "a fresh home has both null",
    "[operations][training][status][57h]") {
    const Home home;
    nlohmann::json status = apogee::operations::training_status_document();
    CHECK(status["cycle"].is_null());
    REQUIRE(status.contains("cycle_error"));
    CHECK(status["cycle_error"].is_null());

    for (const std::string body : {R"({"backend":"x","runs":[)", R"({"backend":"x","runs":{}})",
                                   R"({"halted": "yes"})", R"({"backend":"x","total_runs":"3"})"}) {
        INFO(body);
        Home::write(home.history(), body);
        status = apogee::operations::training_status_document();
        // Never an object with an error inside: a client reading
        // `cycle.halted` would see a halted loop as not halted.
        CHECK(status["cycle"].is_null());
        REQUIRE(status["cycle_error"].is_string());
        const std::string error = status["cycle_error"].get<std::string>();
        CHECK(error.starts_with(home.history().string()));
        CHECK(error.find("history.json") != std::string::npos);
    }

    // A readable one is the headline object, and no error.
    apogee::training::CycleHistory history;
    history.backend = "nightly";
    history.halted = true;
    REQUIRE(
        apogee::training::save_history(home.root.path() / "training" / "cycle", history).empty());
    status = apogee::operations::training_status_document();
    CHECK(status["cycle"]["halted"] == true);
    CHECK(status["cycle_error"].is_null());
}

TEST_CASE(
    "a pipeline document's resume is the point the runner takes: after the last passed stage, "
    "an earlier failed stage named as staying, null for a complete run, a cycle pass or none left",
    "[operations][training][pipeline][resume][57h]") {
    const Home home;
    // A hard-gate abort at stage 2: the first stage that has not passed.
    home.pipeline("pipe-hard", {"passed", "failed", "pending"});
    nlohmann::json resume = document("pipe-hard")["resume"];
    CHECK(resume["stage"] == 2);
    CHECK(resume["index"] == 1);
    CHECK(resume["name"] == "b");
    CHECK(resume["note"].is_null());

    // The sweep's pE: a failed its gate, b passed, c's training errored.
    home.pipeline("pipe-e", {"failed", "passed", "failed"}, "failed");
    resume = document("pipe-e")["resume"];
    CHECK(resume["stage"] == 3);
    CHECK(resume["index"] == 2);
    CHECK(resume["name"] == "c");
    CHECK(resume["note"] == "stage 1, a, failed its gate and stays as it is");
    CHECK(apogee::operations::resume_stage_text(*home.store.get_pipeline("pipe-e")) ==
          "stage 3, c");

    // Two kept: both named.
    home.pipeline("pipe-two", {"failed", "failed", "passed", "pending"}, "aborted");
    CHECK(document("pipe-two")["resume"]["note"] ==
          "stage 1, a and stage 2, b, failed their gates and stay as they are");

    // None: complete, a cycle pass, nothing left.
    home.pipeline("pipe-done", {"passed", "passed"}, "complete");
    CHECK(document("pipe-done")["resume"].is_null());
    home.pipeline("cycle-20261101-030000", {"failed"}, "aborted", "nightly");
    CHECK(document("cycle-20261101-030000")["resume"].is_null());
    home.pipeline("pipe-left", {"passed", "passed"}, "running");
    CHECK(document("pipe-left")["resume"].is_null());
}

TEST_CASE(
    "a pipeline document names the --pipeline value that finds the run's spec now, and carries "
    "the run's spec_ref",
    "[operations][training][pipeline][spec_ref][57h]") {
    const Home home;
    // An older manifest: no spec_ref, the spec's `name:` not its key.
    home.pipeline("pipe-named", {"passed", "failed"}, "aborted", "Nightly SFT");
    const apogee::harness::Config config = config_with({{"nightly", "Nightly SFT"}});
    nlohmann::json out = document("pipe-named", config);
    CHECK(out["spec_ref"].is_null());
    CHECK(out["resume"]["pipeline"] == "nightly");
    // No config to find it in: null, never a guess.
    CHECK(document("pipe-named")["resume"]["pipeline"].is_null());
    // Two entries of that name: none chosen.
    CHECK(document("pipe-named", config_with({{"nightly", "Nightly SFT"},
                                              {"other", "Nightly SFT"}}))["resume"]["pipeline"]
              .is_null());

    // A spec-file run: its absolute path while the file is there.
    const fs::path file = home.root.path() / "two.yaml";
    Home::write(file, "stages: []\n");
    home.pipeline("pipe-file", {"passed", "failed"}, "aborted", "two", file.string());
    out = document("pipe-file");
    CHECK(out["spec_ref"] == file.string());
    CHECK(out["pipeline"]["spec_ref"] == file.string());
    CHECK(out["resume"]["pipeline"] == file.string());
    fs::remove(file);
    CHECK(document("pipe-file", config_with({{"two", "two"}}))["resume"]["pipeline"].is_null());
}

TEST_CASE(
    "a pipeline manifest that is there and cannot be read is said with its file, never as no "
    "run; the config a read names specs from is the file's, or none",
    "[operations][training][pipeline][57h]") {
    const Home home;
    Home::write(home.store.pipeline_dir("pipe-bad") / "manifest.json",
                R"({"pipeline_run_id": "pipe-bad", "stages": [{"index": "one"}]})");
    std::string error;
    CHECK_FALSE(apogee::operations::training_pipeline_document("pipe-bad", {}, error).has_value());
    CHECK(
        error.starts_with((home.store.pipeline_dir("pipe-bad") / "manifest.json").string() + ": "));
    CHECK_FALSE(apogee::operations::training_run_document("pipe-bad", {}, error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(apogee::operations::training_pipeline_document("pipe-none", {}, error).has_value());
    CHECK(error.empty());

    // The config: absent or unreadable is none; a readable one is read.
    CHECK(apogee::operations::config_for_reads(home.root.path() / "nope.yaml")
              .training.pipelines.empty());
    const fs::path broken = home.root.path() / "broken.yaml";
    Home::write(broken, "backends: [\n");
    CHECK(apogee::operations::config_for_reads(broken).training.pipelines.empty());
    const fs::path good = home.root.path() / "good.yaml";
    Home::write(good,
                "training:\n  pipelines:\n    nightly:\n      name: Nightly SFT\n      student: "
                "tiny\n      stages:\n        - name: a\n          dataset: d\n          "
                "eval_suite: s\n");
    CHECK(apogee::operations::config_for_reads(good).training.pipelines.contains("nightly"));
}
