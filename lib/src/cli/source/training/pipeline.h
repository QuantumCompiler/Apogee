#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
#include "harness/config.h"
#include "training/eval.h"
#include "training/manifest.h"
#include "training/trainer.h"

/// Multi-stage pipelines: several skills taught in sequence without
/// forgetting. Each stage is a fresh LoRA on top of the previous stage's
/// **fused** weights -- stage 0 from the snapshot, every passing intermediate
/// stage fused into a concrete SafeTensors checkpoint the next stage trains
/// from, never a stack of raw adapters -- and the final stage is left
/// unfused for `train promote`.
///
/// **The cumulative gate is the contract.** Stage N is gated on the union of
/// suites 0..N at 100%, so a stage that improves its own task but regresses
/// an earlier one fails. The manifest under `training/pipelines/<id>/` is
/// rewritten on every transition, and each stage is an ordinary run
/// `<id>-s<N>` under `training/runs/` carrying `parent_run` and
/// `pipeline_run_id`, so `train eval` and `train promote` can target one
/// directly. A run that stopped -- a failed gate, a crash, Ctrl-C -- resumes
/// from the first stage that has not passed.
///
/// Thin orchestration: this composes the trainer, `run_eval` and the
/// trainer's fuse; nothing here is a second copy of any of them, and nothing
/// here names a backend or a surface.
namespace apogee::training {

inline constexpr std::string_view kPipelineIdPrefix = "pipe-";
inline constexpr std::string_view kPipelinesDirName = "pipelines";

inline constexpr std::string_view kStagePending = "pending";
inline constexpr std::string_view kStageTraining = "training";
inline constexpr std::string_view kStageEvaluating = "evaluating";
inline constexpr std::string_view kStageFusing = "fusing";
inline constexpr std::string_view kStagePassed = "passed";
inline constexpr std::string_view kStageFailed = "failed";

inline constexpr std::string_view kPipelineRunning = "running";
inline constexpr std::string_view kPipelineComplete = "complete";
/// A gate failed under a hard gate, or the run was cancelled.
inline constexpr std::string_view kPipelineAborted = "aborted";
/// Something other than the gate broke: training, eval, fuse, the disk.
inline constexpr std::string_view kPipelineFailed = "failed";

/// One stage's execution record.
struct PipelineStageRecord {
    int index = 0;
    std::string name;
    /// `<pipeline id>-s<index>`.
    std::string run_id;
    /// The snapshot for stage 0; the previous stage's fused checkpoint after.
    std::string base_model;
    /// What actually trained -- the rehearsal mix when one was made.
    std::string dataset;
    std::string adapter_dir;
    /// Intermediate stages only; the last stage is fused by `promote`.
    std::string fused_dir;
    std::string status = std::string{kStagePending};
    std::optional<EvalResults> eval;
    double cumulative_score = 0.0;
    bool cumulative_passed = false;
};

/// `training/pipelines/<id>/manifest.json`.
struct PipelineRunManifest {
    std::string pipeline_run_id;
    std::string spec_name;
    /// The student as named.
    std::string student;
    /// The resolved snapshot.
    std::string base_model;
    std::vector<PipelineStageRecord> stages;
    std::string status = std::string{kPipelineRunning};
    std::string started_at;
    std::string completed_at;

    [[nodiscard]] bool complete() const noexcept {
        return status == kPipelineComplete;
    }

    /// The index of the last passed stage, or -1.
    [[nodiscard]] int last_passed() const noexcept;
    /// The index of the first stage that has not passed; `stages.size()`
    /// when every stage has.
    [[nodiscard]] std::size_t first_unpassed() const noexcept;
    /// The run id `promote` takes: the last passed stage's, or empty.
    [[nodiscard]] std::string promote_run_id() const;
};

[[nodiscard]] nlohmann::json pipeline_manifest_to_json(const PipelineRunManifest& manifest);
/// Throws std::runtime_error on a wrong shape or a missing id.
[[nodiscard]] PipelineRunManifest pipeline_manifest_from_json(const nlohmann::json& json);

[[nodiscard]] std::filesystem::path pipeline_manifest_path(const std::filesystem::path& dir);
/// Writes through a temp file and a rename, creating the directory. The
/// error text, or empty.
[[nodiscard]] std::string write_pipeline_manifest(const std::filesystem::path& dir,
                                                  const PipelineRunManifest& manifest);
[[nodiscard]] std::optional<PipelineRunManifest> read_pipeline_manifest(
    const std::filesystem::path& dir, std::string& error);

/// `<pipeline id>-s<index>`.
[[nodiscard]] std::string stage_run_id(std::string_view pipeline_run_id, int index);

/// A stage's inputs as the caller resolved them from the spec's names: the
/// dataset file and the suite's items. The core never resolves a name.
struct ResolvedStage {
    std::filesystem::path dataset;
    std::vector<EvalItem> suite;
    /// Where the suite came from, for the record.
    std::string suite_label;
};

/// One progress event: a stage's phase, a training tick, or a message.
struct PipelineProgress {
    int stage_index = 0;
    std::string stage_name;
    /// `training`, `evaluating`, `fusing`.
    std::string phase;
    /// Set for a training tick.
    const ProgressEvent* training = nullptr;
    std::string message;
};

struct PipelineRequest {
    const harness::PipelineSpec* spec = nullptr;
    /// One per spec stage, in order.
    std::vector<ResolvedStage> stages;
    std::string pipeline_run_id;
    /// The student snapshot.
    std::filesystem::path base_model;
    /// `runs/` and `pipelines/` are beneath it.
    std::filesystem::path training_dir;
    Trainer* trainer = nullptr;
    /// Recorded in every eval; empty means judge items skip.
    std::string judge_backend;
    JudgeFn judge;
    GateMode gate_mode = GateMode::Hard;
    /// Go on past a failed gate under a hard gate.
    bool continue_on_fail = false;
    std::function<void(const PipelineProgress&)> on_progress;
    harness::CancellationToken cancellation;
};

struct PipelineOutcome {
    /// The run ended `complete`.
    bool ok = false;
    bool cancelled = false;
    /// Why it did not, or empty.
    std::string error;
    PipelineRunManifest manifest;
};

/// Runs every stage from the snapshot. The manifest is written `running`
/// before the first stage and on every transition after.
[[nodiscard]] PipelineOutcome run_pipeline(const PipelineRequest& request);

/// Whether `existing` may be resumed against `spec`: not already complete,
/// and the spec's stage count unchanged (a drifted spec would train stage
/// N on a different plan than stages 0..N-1 passed under). Empty when it
/// may.
[[nodiscard]] std::string resume_check(const PipelineRunManifest& existing,
                                       const harness::PipelineSpec& spec);

/// Continues from the first stage that has not passed, the passed ones
/// untouched.
[[nodiscard]] PipelineOutcome resume_pipeline(const PipelineRequest& request,
                                              PipelineRunManifest existing);

/// The cumulative suite for stage `through`: the items of stages
/// 0..through concatenated, in order.
[[nodiscard]] std::vector<EvalItem> merge_suites(const std::vector<ResolvedStage>& stages,
                                                 std::size_t through);

/// Writes `primary`'s lines plus a deterministic sample of `fraction` of
/// each prior dataset's lines, shuffled, to `out`. Deterministic across
/// runs: the seeds come from the sizes alone. A prior that cannot be read
/// is skipped. The error text, or empty.
[[nodiscard]] std::string mix_datasets(const std::filesystem::path& primary,
                                       const std::vector<std::filesystem::path>& priors,
                                       double fraction, const std::filesystem::path& out);

}  // namespace apogee::training
