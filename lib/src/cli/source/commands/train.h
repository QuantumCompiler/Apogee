#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "harness/config.h"
#include "models/store.h"
#include "training/eval.h"
#include "training/promote.h"
#include "training/python_env.h"
#include "training/store.h"
#include "training/trainer.h"

/// `apogee train` -- fine-tuning local models, end to end on the CLI:
///
///   setup     the Python environment Apogee owns (never the system Python)
///   run       a LoRA/QLoRA fine-tune of a SafeTensors snapshot through the
///             selected driver, live progress, a manifest
///   eval      the gate: substring and pairwise-judge items at 100%
///   promote   fuse -> convert -> verify -> register through the one config
///             editor -> the version ledger, with retention
///   rollback  repoint a backend at its previous version; delete nothing
///   versions  a backend's ledger
///   status    the cycle, the active pipeline, the runs, the active versions
///   pipeline  run|resume|status -- ordered stages from fused checkpoints
///             under a cumulative 100% gate, the last stage left for promote
///   regime    run -- a teacher distils a dataset per kit, the kits become
///             one gated pipeline, the last passing stage is promoted
///   cycle     run|status|halt|resume -- one scheduler-invoked gated pass:
///             sources, the pipeline, the anchor dual gate, promote or discard
///
/// **Training control is CLI-only over HTTP, forever.** An expensive GPU
/// job with live progress is not a control surface a remote client should
/// be able to start; the reads (`status`, `runs`, `versions`) are served
/// under `/v1/admin/training/*`, and every control action is a documented
/// parity carve-out. This file is the composition root: it resolves the
/// trainer, the student, the teacher, the judge and the config edit, and
/// hands the promote body to the regime and the cycle as a closure;
/// `training/` holds the model-free cores it composes.
namespace apogee::commands {

class TrainCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

/// Which trainer stack `--trainer auto` picks on this host: `mlx` on
/// macOS/arm64, `peft` where `nvidia-smi` is on PATH, else empty with the
/// reason.
[[nodiscard]] std::string detect_trainer(std::string& reason);

/// The environment a command needs, created after a yes on a terminal when
/// absent. On a pipe, or on a no, throws the CLI's user error naming
/// `apogee train setup`. `purpose` names the command asking.
[[nodiscard]] training::PythonEnv require_python_env(const harness::Config& config,
                                                     std::string_view purpose);

/// What `train setup` does, shared with the prompt above.
struct SetupRequest {
    /// `auto`, `mlx`, `peft`, or empty for no trainer stack.
    std::string trainer;
    std::vector<training::RequirementSet> with;
};

/// Runs a setup, printing each step. Throws the CLI's user error on failure.
void run_train_setup(const harness::Config& config, const SetupRequest& request);

/// The snapshot `name` resolves to: a directory path as given, else a
/// snapshot of that name under `paths.hf_dir` or `models/`. A backend
/// entry's name, a GGUF, or anything that is not a snapshot is refused
/// naming `apogee models pull <owner>/<repo> --safetensors`. Empty `path`
/// with `error` set on a refusal.
/// The model a run's weights belong to in the store: the model directory its
/// base model came from -- for a pipeline stage, the pipeline's original
/// student, not the stage before it. Where the promoted GGUF and a kept
/// fine-tune land, beside the weights they were trained from.
[[nodiscard]] std::string base_model_name(const models::StoreRoots& roots,
                                          const training::TrainingStore& store,
                                          const training::RunManifest& manifest);

struct StudentResolution {
    std::filesystem::path path;
    std::string error;
};

[[nodiscard]] StudentResolution resolve_student(const harness::Config& config,
                                                const std::filesystem::path& models_dir,
                                                const std::filesystem::path& snapshot_root,
                                                std::string_view name);

/// The dataset `name_or_path` resolves to: a file path as given, else
/// `training/datasets/<name>.jsonl`. Empty with `error` set otherwise.
[[nodiscard]] std::filesystem::path resolve_dataset(const std::filesystem::path& datasets_dir,
                                                    std::string_view name_or_path,
                                                    std::string& error);

/// The eval suite `--suite` (or `training.eval_suite_path`) names: a file
/// path; else `training/suites/<name>.jsonl`; else `training/datasets/
/// <name>.eval.jsonl` (what `datasets prepare --as-eval` writes); else a
/// kit's inline eval. `label` records what was found.
struct SuiteResolution {
    std::vector<training::EvalItem> items;
    std::string label;
    std::string error;
};

[[nodiscard]] SuiteResolution resolve_suite(const std::filesystem::path& training_dir,
                                            std::string_view name_or_path);

/// One status-line rendering of an iteration: `iter n/N · loss L · lr R ·
/// T it/s`.
[[nodiscard]] std::string render_iteration(const training::ProgressEvent& event);

}  // namespace apogee::commands
