#include "cli/train_stream.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "machine/json_reporter.h"
#include "training/eval.h"
#include "training/pipeline.h"
#include "training/trainer.h"

/// The training relay (55h): a pipeline's stages said as its manifest writes
/// them -- every stage on the first write, after that each stage a write
/// moved, in the manifest's own words -- and its progress as the console's
/// lines and the trainer's ticks, over scripted manifests and events: no
/// trainer runs here.
namespace {

using apogee::commands::JsonReporter;
using apogee::commands::PipelineRelay;
using apogee::training::PipelineProgress;
using apogee::training::PipelineRunManifest;
using apogee::training::PipelineStageRecord;
using apogee::training::ProgressEvent;

[[nodiscard]] std::vector<nlohmann::json> events_of(const std::string& text) {
    std::vector<nlohmann::json> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        out.push_back(nlohmann::json::parse(line));
    }
    return out;
}

/// A two-stage run as `run_pipeline` first writes it: both stages pending.
[[nodiscard]] PipelineRunManifest fresh() {
    PipelineRunManifest manifest;
    manifest.pipeline_run_id = "pipe-20261025-120000";
    manifest.spec_name = "two";
    for (int index = 0; index < 2; ++index) {
        PipelineStageRecord stage;
        stage.index = index;
        stage.name = index == 0 ? "a" : "b";
        manifest.stages.push_back(stage);
    }
    return manifest;
}

[[nodiscard]] apogee::training::EvalResults scored(double score) {
    apogee::training::EvalResults results;
    results.score = score;
    results.passed = score >= 1.0;
    return results;
}

/// `stage`'s identifying fields: where it is and what it says.
[[nodiscard]] std::string said(const nlohmann::json& event) {
    std::string out =
        event.at("type").get<std::string>() + " " + std::to_string(event.at("index").get<int>()) +
        "/" + std::to_string(event.at("count").get<int>()) + " " +
        event.at("label").get<std::string>() + " " + event.at("state").get<std::string>();
    return out;
}

}  // namespace

TEST_CASE("the pipeline relay says every stage on the first write, then each stage a write moved",
          "[commands][train][stream]") {
    std::ostringstream out;
    JsonReporter events{out};
    PipelineRelay relay{events};

    PipelineRunManifest manifest = fresh();
    relay.manifest(manifest);
    // The same write again moves nothing, so says nothing.
    relay.manifest(manifest);
    manifest.stages[0].run_id = "pipe-20261025-120000-s0";
    manifest.stages[0].status = "training";
    relay.manifest(manifest);
    manifest.stages[0].status = "evaluating";
    relay.manifest(manifest);
    // The gate's verdict is set in memory and only written with the fuse.
    manifest.stages[0].eval = scored(1.0);
    manifest.stages[0].cumulative_score = 1.0;
    manifest.stages[0].status = "fusing";
    relay.manifest(manifest);
    manifest.stages[0].status = "passed";
    manifest.stages[0].fused_dir = "/runs/pipe-20261025-120000-s0/fused";
    relay.manifest(manifest);
    manifest.stages[1].run_id = "pipe-20261025-120000-s1";
    manifest.stages[1].status = "training";
    relay.manifest(manifest);
    manifest.stages[1].eval = scored(0.5);
    manifest.stages[1].cumulative_score = 0.5;
    manifest.stages[1].status = "failed";
    manifest.status = "aborted";
    relay.manifest(manifest);

    const std::vector<nlohmann::json> written = events_of(out.str());
    std::vector<std::string> seen;
    for (const nlohmann::json& event : written) {
        seen.push_back(said(event));
    }
    CHECK(seen == std::vector<std::string>{"stage 1/2 a pending", "stage 2/2 b pending",
                                           "stage 1/2 a training", "stage 1/2 a evaluating",
                                           "stage 1/2 a fusing", "stage 1/2 a passed",
                                           "stage 2/2 b training", "stage 2/2 b failed"});
    // Every one names its pipeline run; a stage's own run once it has one;
    // its score only once the gate has scored it.
    for (const nlohmann::json& event : written) {
        CHECK(event.at("pipeline_run_id") == "pipe-20261025-120000");
    }
    CHECK_FALSE(written[0].contains("run_id"));
    CHECK(written[2].at("run_id") == "pipe-20261025-120000-s0");
    CHECK_FALSE(written[3].contains("cumulative_score"));
    CHECK(written[4].at("cumulative_score") == 1.0);
    CHECK(written[7].at("cumulative_score") == 0.5);
}

TEST_CASE("a relay joining at a resume says the stages that passed before it",
          "[commands][train][stream]") {
    std::ostringstream out;
    JsonReporter events{out};
    PipelineRelay relay{events};
    PipelineRunManifest manifest = fresh();
    manifest.stages[0].status = "passed";
    manifest.stages[0].run_id = "pipe-20261025-120000-s0";
    manifest.stages[0].eval = scored(1.0);
    manifest.stages[0].cumulative_score = 1.0;
    manifest.stages[1].status = "failed";
    relay.manifest(manifest);
    manifest.stages[1].status = "training";
    relay.manifest(manifest);
    const std::vector<nlohmann::json> written = events_of(out.str());
    REQUIRE(written.size() == 3);
    CHECK(said(written[0]) == "stage 1/2 a passed");
    CHECK(written[0].at("cumulative_score") == 1.0);
    CHECK(said(written[1]) == "stage 2/2 b failed");
    CHECK(said(written[2]) == "stage 2/2 b training");
}

TEST_CASE("the relay frames a trainer's ticks as iterations and every line in the console's words",
          "[commands][train][stream]") {
    std::ostringstream out;
    JsonReporter events{out};
    PipelineRelay relay{events};
    ProgressEvent tick;
    tick.kind = ProgressEvent::Kind::Iteration;
    tick.iteration = 10;
    tick.total_iters = 600;
    tick.loss = 1.5;
    tick.lr = 0.0001;
    tick.throughput = 2.5;
    relay.progress(PipelineProgress{.stage_index = 1, .stage_name = "b", .training = &tick});
    tick.total_iters = 0;
    relay.progress(PipelineProgress{.stage_index = 1, .stage_name = "b", .training = &tick});
    ProgressEvent message;
    message.text = "mock trainer: 2 iteration(s), method lora";
    relay.progress(PipelineProgress{.stage_index = 0, .training = &message});
    ProgressEvent error;
    error.kind = ProgressEvent::Kind::Error;
    error.text = "out of memory";
    relay.progress(PipelineProgress{.stage_index = 0, .training = &error});
    relay.progress(PipelineProgress{
        .stage_index = 0, .phase = "evaluating", .message = "[gate] stage 'a' PASSED"});
    // A progress with nothing to say says nothing.
    relay.progress(PipelineProgress{.stage_index = 0, .phase = "fusing"});

    const std::vector<nlohmann::json> written = events_of(out.str());
    REQUIRE(written.size() == 5);
    CHECK(written[0] == nlohmann::json{{"type", "iteration"},
                                       {"iteration", 10},
                                       {"iterations", 600},
                                       {"loss", 1.5},
                                       {"lr", 0.0001},
                                       {"throughput", 2.5},
                                       {"stage", 2}});
    CHECK_FALSE(written[1].contains("iterations"));
    CHECK(written[2] ==
          nlohmann::json{{"type", "notice"},
                         {"text", "[train] mock trainer: 2 iteration(s), method lora"}});
    CHECK(written[3].at("text") == "[train] error: out of memory");
    CHECK(written[4] == nlohmann::json{{"type", "notice"}, {"text", "[gate] stage 'a' PASSED"}});
}
