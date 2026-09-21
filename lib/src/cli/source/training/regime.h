#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
#include "harness/config.h"
#include "training/kit.h"
#include "training/pipeline.h"
#include "training/synth.h"
#include "training/trainer.h"

/// The regime: distillation as one command. For each kit the teacher
/// synthesises a dataset through the synth core into
/// `training/regime/<id>/<kit>.jsonl` with the kit's inline eval
/// materialised beside it; the kits become one eval-gated pipeline (a stage
/// per kit, the kit's `train:` block its defaults, the cumulative gate
/// across skills); and the caller promotes the last passing stage. A thin
/// layer over synth → pipeline: nothing here is a second copy of either.
///
/// A regime is ad-hoc from flags, a `training.regimes:` entry, or a spec
/// file, and the flags always win over a loaded spec.
namespace apogee::training {

inline constexpr std::string_view kRegimeIdPrefix = "regime-";
inline constexpr std::string_view kRegimeDirName = "regime";
/// The pipeline a regime runs is `<regime id>-pipe`.
inline constexpr std::string_view kRegimePipelineSuffix = "-pipe";

/// The `train regime run` flags, each empty or zero when not given.
struct RegimeFlags {
    std::string teacher;
    std::string student;
    std::vector<std::string> kits;
    bool all_kits = false;
    int count = 0;
    std::string promote_as;
    int iters = 0;
    double temperature = 0.0;
};

/// `base` with the flags applied: every given flag replaces its field;
/// `--all-kits` fills `kits` with `installed` (alphabetical) only when no
/// `--kit` was given, so an explicit list curates the order; the name
/// defaults to `regime`.
[[nodiscard]] harness::RegimeSpec apply_regime_flags(harness::RegimeSpec base,
                                                     const RegimeFlags& flags,
                                                     const std::vector<std::string>& installed);

/// The kit checks a regime must pass: at least one kit, every one found
/// under `kits_dir`, loading and validating. (The teacher and the student
/// are the caller's to check -- they are backends and snapshots.) Empty
/// when valid.
[[nodiscard]] std::string validate_regime_kits(const harness::RegimeSpec& spec,
                                               const std::filesystem::path& kits_dir);

/// The pipeline stage one kit becomes: the kit's name, the two files, the
/// regime's `iters` when set else the kit's, the kit's batch size and
/// layer count.
[[nodiscard]] harness::PipelineStageSpec regime_stage(const harness::RegimeSpec& spec,
                                                      const Kit& kit,
                                                      const std::filesystem::path& dataset,
                                                      const std::filesystem::path& suite);

struct RegimeKitResult {
    std::string kit;
    std::filesystem::path dataset;
    std::filesystem::path suite;
    int examples = 0;
    int calls = 0;
};

struct RegimeRequest {
    harness::RegimeSpec spec;
    std::filesystem::path kits_dir;
    /// `training/regime/<id>`.
    std::filesystem::path work_dir;
    std::filesystem::path training_dir;
    std::string regime_run_id;
    /// The resolved student snapshot.
    std::filesystem::path base_model;
    GenerateFn generate;
    int max_tokens = 4096;
    int parallel = 1;
    Trainer* trainer = nullptr;
    std::string judge_backend;
    JudgeFn judge;
    GateMode gate_mode = GateMode::Hard;
    std::function<void(std::string_view)> on_message;
    std::function<void(const PipelineProgress&)> on_progress;
    harness::CancellationToken cancellation;
};

struct RegimeOutcome {
    /// Every kit synthesised and the pipeline complete.
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<RegimeKitResult> kits;
    std::optional<PipelineOutcome> pipeline;
    /// The last passing stage's run id, for `promote`; empty when none.
    std::string promote_run_id;
};

[[nodiscard]] RegimeOutcome run_regime(const RegimeRequest& request);

}  // namespace apogee::training
