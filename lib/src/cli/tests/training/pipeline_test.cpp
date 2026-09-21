#include "training/pipeline.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "support/env_guard.h"
#include "training/manifest.h"
#include "training/mock_trainer.h"

/// The pipeline on the mock trainer: stage 0's base the snapshot and stage
/// 1's its fused checkpoint, only the intermediate stage fused, the stage
/// runs carrying their lineage; a stage regressing on an earlier suite
/// failing the cumulative gate with the run aborted, the stage failed and
/// the later ones pending; resume running only the unpassed stages,
/// refusing a complete run and a drifted spec; soft and --continue-on-fail
/// completing with mixed statuses; rehearsal mixing deterministic; the
/// manifest rewritten on every transition; cancellation leaving `aborted`;
/// judge items through the closure.
namespace {

using apogee::training::EvalItem;
using apogee::training::MockTrainer;
using apogee::training::PipelineOutcome;
using apogee::training::PipelineRequest;
using apogee::training::PipelineRunManifest;
using apogee::training::ResolvedStage;

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

constexpr std::string_view kPlain =
    "{\"messages\": [{\"role\": \"user\", \"content\": \"hi\"}, {\"role\": \"assistant\", "
    "\"content\": \"hello\"}]}\n";

struct Fixture {
    apogee::testing::TempDir root{"pipeline-" + std::to_string(std::random_device{}())};
    std::filesystem::path training = root.path() / "training";
    std::filesystem::path snap = root.path() / "snap";
    MockTrainer trainer;
    apogee::harness::PipelineSpec spec;
    std::vector<ResolvedStage> stages;

    Fixture() {
        write(snap / "config.json", R"({"model_type": "llama"})");
        write(snap / "model.safetensors", "w");
        spec.name = "skills";
        spec.student = "snap";
    }

    void add_stage(const std::string& name, std::string_view dataset_text,
                   std::vector<EvalItem> suite, double rehearsal = 0.0) {
        apogee::harness::PipelineStageSpec stage;
        stage.name = name;
        stage.dataset = (root.path() / "data" / (name + ".jsonl")).string();
        stage.eval_suite = name;
        stage.iters = 2;
        stage.rehearsal_fraction = rehearsal;
        write(stage.dataset, dataset_text);
        spec.stages.push_back(stage);
        stages.push_back(ResolvedStage{
            .dataset = stage.dataset, .suite = std::move(suite), .suite_label = "suite:" + name});
    }

    [[nodiscard]] PipelineRequest request(const std::string& id) {
        PipelineRequest request;
        request.spec = &spec;
        request.stages = stages;
        request.pipeline_run_id = id;
        request.base_model = snap;
        request.training_dir = training;
        request.trainer = &trainer;
        return request;
    }

    [[nodiscard]] apogee::training::RunManifest stage_run(const std::string& id, int index) const {
        std::string error;
        const auto m = apogee::training::read_manifest(
            training / "runs" / apogee::training::stage_run_id(id, index), error);
        REQUIRE(m.has_value());
        return *m;
    }

    [[nodiscard]] PipelineRunManifest on_disk(const std::string& id) const {
        std::string error;
        const auto m = apogee::training::read_pipeline_manifest(training / "pipelines" / id, error);
        REQUIRE(m.has_value());
        return *m;
    }
};

}  // namespace

TEST_CASE(
    "stage 0 trains from the snapshot and stage 1 from stage 0's fused checkpoint; only the "
    "intermediate stage is fused; the stage runs carry their lineage and the cumulative suite",
    "[training][pipeline]") {
    Fixture fixture;
    fixture.add_stage("a", kPlain, {{"say hello", "hello"}});
    fixture.add_stage("b", kPlain, {{"say bye", "bye"}});
    const PipelineOutcome outcome = apogee::training::run_pipeline(fixture.request("pipe-1"));
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    const PipelineRunManifest& m = outcome.manifest;
    CHECK(m.complete());
    CHECK_FALSE(m.completed_at.empty());
    CHECK(m.spec_name == "skills");
    CHECK(m.base_model == fixture.snap.string());
    REQUIRE(m.stages.size() == 2);
    CHECK(m.stages[0].base_model == fixture.snap.string());
    const std::filesystem::path fused = fixture.training / "runs" / "pipe-1-s0" / "fused";
    CHECK(m.stages[0].fused_dir == fused.string());
    CHECK(std::filesystem::exists(fused / "config.json"));
    CHECK(m.stages[1].base_model == fused.string());
    CHECK(m.stages[1].fused_dir.empty());
    CHECK_FALSE(std::filesystem::exists(fixture.training / "runs" / "pipe-1-s1" / "fused"));
    CHECK(m.stages[0].status == "passed");
    CHECK(m.stages[1].status == "passed");
    CHECK(m.last_passed() == 1);
    CHECK(m.promote_run_id() == "pipe-1-s1");
    CHECK(m.first_unpassed() == 2);

    const apogee::training::RunManifest s0 = fixture.stage_run("pipe-1", 0);
    CHECK(s0.parent_run.empty());
    CHECK(s0.pipeline_run_id == "pipe-1");
    CHECK(s0.complete());
    CHECK(s0.trainer == "mock");
    CHECK(s0.iters == 2);
    REQUIRE(s0.eval.has_value());
    CHECK(s0.eval->total == 1);
    const apogee::training::RunManifest s1 = fixture.stage_run("pipe-1", 1);
    CHECK(s1.parent_run == "pipe-1-s0");
    CHECK(s1.pipeline_run_id == "pipe-1");
    CHECK(s1.base_model == fused.string());
    REQUIRE(s1.eval.has_value());
    CHECK(s1.eval->total == 2);
    CHECK(s1.eval->suite == "suite:a + suite:b");
    CHECK(m.stages[1].cumulative_score == 1.0);
    CHECK(m.stages[1].cumulative_passed);
    // The manifest on disk is the one returned.
    CHECK(apogee::training::pipeline_manifest_to_json(fixture.on_disk("pipe-1")) ==
          apogee::training::pipeline_manifest_to_json(m));
}

TEST_CASE(
    "a stage regressing on an earlier suite fails the cumulative gate -- the run aborted, the "
    "stage failed, later stages pending -- and resume runs only the unpassed stages",
    "[training][pipeline][gate][resume]") {
    Fixture fixture;
    fixture.add_stage("a", kPlain, {{"say hello", "hello"}});
    fixture.add_stage("b", "{\"mock\": {\"answer\": \"nope\"}}\n", {{"say nope", "nope"}});
    fixture.add_stage("c", kPlain, {{"say bye", "bye"}});
    const PipelineOutcome first = apogee::training::run_pipeline(fixture.request("pipe-2"));
    CHECK_FALSE(first.ok);
    CHECK_FALSE(first.cancelled);
    CHECK(first.error.find("cumulative eval gate") != std::string::npos);
    CHECK(first.error.find("pipeline resume pipe-2") != std::string::npos);
    CHECK(first.manifest.status == "aborted");
    REQUIRE(first.manifest.stages.size() == 3);
    CHECK(first.manifest.stages[0].status == "passed");
    CHECK(first.manifest.stages[1].status == "failed");
    CHECK(first.manifest.stages[2].status == "pending");
    CHECK(first.manifest.stages[2].run_id.empty());
    // Its own suite passed; the earlier one regressed.
    REQUIRE(first.manifest.stages[1].eval.has_value());
    CHECK(first.manifest.stages[1].eval->total == 2);
    CHECK(first.manifest.stages[1].eval->num_passed == 1);
    CHECK_FALSE(first.manifest.stages[1].cumulative_passed);
    CHECK(first.manifest.last_passed() == 0);
    CHECK(first.manifest.first_unpassed() == 1);
    CHECK(first.manifest.promote_run_id() == "pipe-2-s0");
    // The failed stage's own run is complete with its eval recorded.
    CHECK(fixture.stage_run("pipe-2", 1).complete());
    CHECK_FALSE(fixture.stage_run("pipe-2", 1).eval->passed);

    // Fix the data, resume: stage 0 is untouched, 1 and 2 run, complete.
    const std::string s0_started = fixture.stage_run("pipe-2", 0).started_at;
    write(fixture.spec.stages[1].dataset, kPlain);
    PipelineRequest again = fixture.request("pipe-2");
    const PipelineOutcome resumed =
        apogee::training::resume_pipeline(again, fixture.on_disk("pipe-2"));
    INFO(resumed.error);
    REQUIRE(resumed.ok);
    CHECK(resumed.manifest.complete());
    CHECK(resumed.manifest.stages[0].status == "passed");
    CHECK(resumed.manifest.stages[1].status == "passed");
    CHECK(resumed.manifest.stages[2].status == "passed");
    CHECK(fixture.stage_run("pipe-2", 0).started_at == s0_started);
    CHECK(resumed.manifest.stages[1].base_model ==
          (fixture.training / "runs" / "pipe-2-s0" / "fused").string());
    CHECK(resumed.manifest.stages[2].base_model ==
          (fixture.training / "runs" / "pipe-2-s1" / "fused").string());
    CHECK(resumed.manifest.stages[2].fused_dir.empty());
    CHECK(resumed.manifest.promote_run_id() == "pipe-2-s2");

    // A complete run and a drifted spec are refused.
    CHECK(apogee::training::resume_check(fixture.on_disk("pipe-2"), fixture.spec)
              .find("already complete") != std::string::npos);
    apogee::harness::PipelineSpec drifted = fixture.spec;
    drifted.stages.pop_back();
    PipelineRunManifest running = first.manifest;
    CHECK(apogee::training::resume_check(running, drifted).find("stage(s)") != std::string::npos);
    CHECK(apogee::training::resume_check(running, fixture.spec).empty());
    PipelineRequest drifted_request = fixture.request("pipe-2");
    drifted_request.spec = &drifted;
    CHECK_FALSE(apogee::training::resume_pipeline(drifted_request, running).ok);
}

TEST_CASE(
    "soft gate mode and --continue-on-fail complete with mixed statuses, the failed stage "
    "still fused for the next; the regression persists through the fused checkpoint",
    "[training][pipeline][gate]") {
    for (const bool soft : {true, false}) {
        Fixture fixture;
        fixture.add_stage("a", kPlain, {{"say hello", "hello"}});
        fixture.add_stage("b", "{\"mock\": {\"answer\": \"nope\"}}\n", {{"say nope", "nope"}});
        fixture.add_stage("c", kPlain, {{"say bye", "bye"}});
        PipelineRequest request = fixture.request("pipe-3");
        if (soft) {
            request.gate_mode = apogee::training::GateMode::Soft;
        } else {
            request.continue_on_fail = true;
        }
        const PipelineOutcome outcome = apogee::training::run_pipeline(request);
        INFO(outcome.error);
        REQUIRE(outcome.ok);
        CHECK(outcome.manifest.complete());
        CHECK(outcome.manifest.stages[0].status == "passed");
        CHECK(outcome.manifest.stages[1].status == "failed");
        // Stage 2 trains from stage 1's fused checkpoint, which carries the
        // regression, so the cumulative suite fails again.
        CHECK(outcome.manifest.stages[1].fused_dir ==
              (fixture.training / "runs" / "pipe-3-s1" / "fused").string());
        CHECK(outcome.manifest.stages[2].base_model == outcome.manifest.stages[1].fused_dir);
        CHECK(outcome.manifest.stages[2].status == "failed");
        CHECK(outcome.manifest.promote_run_id() == "pipe-3-s0");
    }
}

TEST_CASE("rehearsal mixing is deterministic across runs and the stage trains on the mix",
          "[training][pipeline][rehearsal]") {
    Fixture fixture;
    std::string four;
    for (int i = 0; i < 4; ++i) {
        four += "{\"messages\": [{\"role\": \"user\", \"content\": \"q" + std::to_string(i) +
                "\"}, {\"role\": \"assistant\", \"content\": \"a\"}]}\n";
    }
    fixture.add_stage("a", four, {{"say hello", "hello"}});
    fixture.add_stage("b", kPlain, {{"say bye", "bye"}}, 0.5);
    const std::filesystem::path once = fixture.root.path() / "mix1.jsonl";
    const std::filesystem::path twice = fixture.root.path() / "mix2.jsonl";
    const std::vector<std::filesystem::path> priors{fixture.spec.stages[0].dataset};
    REQUIRE(
        apogee::training::mix_datasets(fixture.spec.stages[1].dataset, priors, 0.5, once).empty());
    REQUIRE(
        apogee::training::mix_datasets(fixture.spec.stages[1].dataset, priors, 0.5, twice).empty());
    CHECK(read(once) == read(twice));
    int lines = 0;
    std::istringstream in{read(once)};
    for (std::string line; std::getline(in, line);) {
        ++lines;
    }
    CHECK(lines == 3);  // the stage's one line plus half of four
    // A prior that cannot be read is skipped; a primary that cannot is an error.
    CHECK(apogee::training::mix_datasets(fixture.spec.stages[1].dataset,
                                         {fixture.root.path() / "nope.jsonl"}, 0.5, once)
              .empty());
    CHECK(lines == 3);
    CHECK_FALSE(
        apogee::training::mix_datasets(fixture.root.path() / "nope.jsonl", priors, 0.5, once)
            .empty());

    const PipelineOutcome outcome = apogee::training::run_pipeline(fixture.request("pipe-4"));
    REQUIRE(outcome.ok);
    const std::filesystem::path mixed = fixture.training / "runs" / "pipe-4-s1" / "rehearsal.jsonl";
    CHECK(outcome.manifest.stages[1].dataset == mixed.string());
    CHECK(std::filesystem::exists(mixed));
    CHECK(fixture.stage_run("pipe-4", 1).dataset == mixed.string());
    CHECK(outcome.manifest.stages[0].dataset == fixture.spec.stages[0].dataset);
}

TEST_CASE(
    "the manifest is rewritten on every transition, and cancellation leaves the run aborted "
    "with the stage's run cancelled",
    "[training][pipeline][cancel]") {
    Fixture fixture;
    fixture.add_stage("a", kPlain, {{"say hello", "hello"}});
    fixture.add_stage("b", kPlain, {{"say bye", "bye"}});
    std::set<std::string> seen;
    PipelineRequest request = fixture.request("pipe-5");
    request.on_progress = [&](const apogee::training::PipelineProgress& progress) {
        if (progress.stage_index == 0) {
            seen.insert(fixture.on_disk("pipe-5").stages[0].status);
        }
    };
    REQUIRE(apogee::training::run_pipeline(request).ok);
    CHECK(seen.contains("training"));
    CHECK(seen.contains("evaluating"));
    CHECK(seen.contains("fusing"));

    Fixture cancelled;
    cancelled.add_stage("a", kPlain, {{"say hello", "hello"}});
    cancelled.add_stage("b", kPlain, {{"say bye", "bye"}});
    PipelineRequest stop = cancelled.request("pipe-6");
    stop.cancellation = apogee::harness::CancellationToken::create();
    stop.on_progress = [&stop](const apogee::training::PipelineProgress& progress) {
        if (progress.training != nullptr &&
            progress.training->kind == apogee::training::ProgressEvent::Kind::Iteration) {
            stop.cancellation.cancel();
        }
    };
    const PipelineOutcome outcome = apogee::training::run_pipeline(stop);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.cancelled);
    CHECK(outcome.manifest.status == "aborted");
    CHECK(cancelled.on_disk("pipe-6").status == "aborted");
    CHECK(cancelled.stage_run("pipe-6", 0).status == "cancelled");
    CHECK(outcome.manifest.stages[1].status == "pending");
    // Resumable: not complete, the spec unchanged.
    CHECK(apogee::training::resume_check(outcome.manifest, cancelled.spec).empty());
}

TEST_CASE(
    "judge items go through the closure pairwise against the untuned base, and a lost "
    "verdict fails the gate; the request's refusals",
    "[training][pipeline][judge]") {
    for (const std::string verdict : {"A", "B"}) {
        Fixture fixture;
        fixture.add_stage("a", kPlain, {{"say hello", "hello"}, {"free", ""}});
        PipelineRequest request = fixture.request("pipe-7");
        request.judge_backend = "judge";
        std::string baseline_seen;
        request.judge = [&](std::string_view, std::string_view, std::string_view baseline,
                            const apogee::harness::CancellationToken&) {
            baseline_seen = std::string{baseline};
            return apogee::training::JudgeReply{.ok = true, .text = verdict};
        };
        const PipelineOutcome outcome = apogee::training::run_pipeline(request);
        CHECK(baseline_seen == "base: free");
        REQUIRE(outcome.manifest.stages[0].eval.has_value());
        CHECK(outcome.manifest.stages[0].eval->judge_backend == "judge");
        CHECK(outcome.manifest.stages[0].eval->items[1].check_type == "judge");
        CHECK(outcome.ok == (verdict == "A"));
    }

    Fixture fixture;
    fixture.add_stage("a", kPlain, {{"say hello", "hello"}});
    PipelineRequest request = fixture.request("pipe-8");
    apogee::harness::PipelineSpec empty;
    request.spec = &empty;
    CHECK(apogee::training::run_pipeline(request).error.find("no stages") != std::string::npos);
    request = fixture.request("pipe-8");
    request.trainer = nullptr;
    CHECK(apogee::training::run_pipeline(request).error == "no trainer");
    request = fixture.request("pipe-8");
    request.stages.clear();
    CHECK(apogee::training::run_pipeline(request).error.find("resolved") != std::string::npos);
    request = fixture.request("../pipe");
    CHECK(apogee::training::run_pipeline(request).error.find("not a pipeline run id") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "pipelines"));

    const std::vector<ResolvedStage> stages{ResolvedStage{.suite = {{"p1", "e1"}}},
                                            ResolvedStage{.suite = {{"p2", "e2"}}},
                                            ResolvedStage{.suite = {{"p3", "e3"}}}};
    const std::vector<EvalItem> merged = apogee::training::merge_suites(stages, 1);
    REQUIRE(merged.size() == 2);
    CHECK(merged[0].prompt == "p1");
    CHECK(merged[1].prompt == "p2");
    CHECK(apogee::training::merge_suites(stages, 9).size() == 3);
    CHECK(apogee::training::stage_run_id("pipe-x", 3) == "pipe-x-s3");

    // The JSON round trip keeps every field, and a missing id is refused.
    PipelineRunManifest m;
    m.pipeline_run_id = "pipe-9";
    m.spec_name = "s";
    m.student = "snap";
    m.base_model = "/snap";
    m.status = "aborted";
    m.started_at = "2026-09-19T12:00:00Z";
    apogee::training::PipelineStageRecord stage;
    stage.index = 0;
    stage.name = "a";
    stage.run_id = "pipe-9-s0";
    stage.status = "failed";
    stage.cumulative_score = 0.5;
    apogee::training::EvalResults eval;
    eval.total = 2;
    eval.num_passed = 1;
    eval.score = 0.5;
    stage.eval = eval;
    m.stages.push_back(stage);
    const PipelineRunManifest back = apogee::training::pipeline_manifest_from_json(
        apogee::training::pipeline_manifest_to_json(m));
    CHECK(back.pipeline_run_id == "pipe-9");
    CHECK(back.status == "aborted");
    CHECK(back.completed_at.empty());
    REQUIRE(back.stages.size() == 1);
    CHECK(back.stages[0].run_id == "pipe-9-s0");
    CHECK(back.stages[0].cumulative_score == 0.5);
    REQUIRE(back.stages[0].eval.has_value());
    CHECK(back.stages[0].eval->num_passed == 1);
    CHECK_THROWS_AS(apogee::training::pipeline_manifest_from_json(nlohmann::json{{"status", "x"}}),
                    std::runtime_error);
    std::string error;
    CHECK_FALSE(
        apogee::training::read_pipeline_manifest(fixture.root.path() / "nope", error).has_value());
    CHECK(error.find("no pipeline manifest") != std::string::npos);
}
