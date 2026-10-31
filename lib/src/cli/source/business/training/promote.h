#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "training/eval.h"
#include "training/kit.h"
#include "training/manifest.h"
#include "training/store.h"
#include "training/trainer.h"

/// Promotion: the only path from a run to inference, as a plan the command
/// executes step by step -- gate → fuse → convert → verify → version --
/// with the property that matters stated once: **a failing candidate never
/// reaches inference.** The gate runs first; the GGUF is written and its
/// header verified before the config is touched; a failure at any step
/// leaves the config and the ledger exactly as they were.
///
/// **Two targets, one plan** (27c): a GGUF for a llamacpp backend, or --
/// with MLX inference in the house -- the fused SafeTensors themselves,
/// verified as a whole model directory, for an mlx backend: gate → fuse →
/// verify → version, the conversion never run. Everything else -- the
/// gate, `max + 1`, retention, the rollback target -- is the same code.
///
/// **Two modes** (53b): merged, the default, as above; or, opt-in, the
/// adapter itself as the version -- gate → convert to a GGUF LoRA (or, for
/// MLX, copy the adapter directory as it is) → verify → version, the fuse
/// never run -- registered on the entry's `adapters:` over the base it
/// already runs. The gate is the same one, before either: the mode changes
/// what is written, never what is judged.
///
/// **Judged where it serves** (53c): an adapter bound for a quantized base --
/// weights its full-precision training never ran on -- is scored on that
/// base, through a closure the command builds from the entry's own runtime,
/// once it is built and pinned and before the config is touched. That eval
/// is its gate, and its only one; every other promotion is gated on its run's
/// recorded eval, as before.
///
/// Nothing here edits the config or knows a backend type: the conversions,
/// the header check and the quantizer arrive as closures, and the config
/// edit is the command's (`cli/train.cpp`, the composition root). What
/// this package owns is the arithmetic that is easy to get wrong -- version
/// numbers are `max + 1`, never the count, because a count regresses after
/// the first prune and the next promote would overwrite a live file -- and
/// retention that never prunes the active version.
namespace apogee::training {

struct GateVerdict {
    bool ok = false;
    /// Why promotion is refused (Hard), or empty.
    std::string error;
    /// Why the operator should think twice (Soft, or `--force` over a failed
    /// eval), or empty.
    std::string warning;
};

/// The eval gate over a run's manifest: no eval is a refusal under Hard and
/// a warning under Soft; a failed eval likewise; `force` turns a refusal
/// into a warning; a run that did not complete is refused whatever the mode.
[[nodiscard]] GateVerdict eval_gate(const RunManifest& manifest, GateMode mode, bool force);

/// The base a promoted adapter will serve on (53c), as the command reads it
/// from the target entry: plain facts -- the weights, the name a sentence
/// gives them, their precision -- so this package never names a runtime.
struct ServingBase {
    /// The entry the version registers on.
    std::string backend{};
    /// Its `model_path`: the weights the adapter applies over.
    std::filesystem::path weights{};
    /// The weights as a sentence names them: a GGUF's file name, a stored
    /// MLX model's `<model>/mlx/<id>`.
    std::string label{};
    /// How they are stored -- `Q4_K_M`, `F16`, `4-bit`, `bf16` -- or empty
    /// when nothing says.
    std::string precision{};
    /// Quantized: weights the full-precision training never ran on, so the
    /// gate scores the candidate here rather than where its trainer ran it.
    bool quantized = false;
};

/// The base as the gate's every word names it (53c): `<label> (<precision>)`,
/// or the label alone when nothing says its precision -- what a report says
/// it was "scored on", and what the version's ledger entry records.
[[nodiscard]] std::string describe(const ServingBase& base);

/// The candidate on the serving base (53c): the run's adapter over it, with
/// every adapter the entry will apply -- or, `tuned` false, the bare base,
/// the judge's baseline. The composition root builds it from the target
/// entry's own runtime, so this package never names one; null with `error`
/// set when it cannot be built.
using ServingRunnerFactory =
    std::function<std::unique_ptr<CandidateRunner>(bool tuned, std::string& error)>;

/// What the gate on the serving base scores with (53c).
struct ServingGateRequest {
    ServingBase base{};
    /// The suite's items, and where they came from; empty when nothing named
    /// one.
    std::vector<EvalItem> items{};
    std::string suite{};
    GateMode mode = GateMode::Hard;
    bool force = false;
};

/// The gate on the serving base before anything is built (53c), so its
/// refusals come before the adapter is converted: a run that is not complete
/// refused whatever the mode, as `eval_gate` refuses it; `--force` skipping
/// the scoring, said; no suite to score on refused under a hard gate, a
/// warning under a soft one. `score` says whether `gate_on_serving_base` will
/// run an eval at all.
struct ServingPlan {
    GateVerdict verdict{};
    bool score = false;
};

[[nodiscard]] ServingPlan plan_serving_gate(const RunManifest& manifest,
                                            const ServingGateRequest& request);

/// What the gate on the serving base found.
struct ServingGate {
    GateVerdict verdict{};
    bool cancelled = false;
    /// The one eval, when it ran -- what the version records.
    std::optional<EvalResults> results{};
};

/// The eval gate on the base the version will serve on (53c): one eval per
/// candidate, there and nowhere else -- the run's recorded eval, scored where
/// its trainer ran, is never read, and no full-precision score is computed
/// beside it. `plan_serving_gate` first; then `run_eval` over the factory's
/// candidate -- its baseline built only when `options` carries a judge --
/// under its never-fail contract, a baseline that cannot be built leaving
/// judged items to skip, counted; then the gate's own rule, 100%, a hard gate
/// refusing and a soft one warning, every word saying where it scored. A
/// candidate that cannot be built or asked has nothing to say for itself,
/// and is held to the gate as unscored.
[[nodiscard]] ServingGate gate_on_serving_base(const RunManifest& manifest,
                                               const ServingGateRequest& request,
                                               const ServingRunnerFactory& runners,
                                               EvalOptions options);

/// `entry` as the gate on its serving base let it through (53c): its eval
/// that gate's and where it scored, or none when it ran no eval (`--force`,
/// or a soft gate with no suite) -- the run's own eval, scored where its
/// trainer ran, is never this version's.
void record_serving_gate(VersionEntry& entry, const ServingGate& gate, const ServingBase& base);

/// The fused SafeTensors directory → a GGUF at the path given. The error
/// text, or empty.
using Converter = std::function<std::string(
    const std::filesystem::path& fused_dir, const std::filesystem::path& gguf,
    const MessageSink& on_message, const harness::CancellationToken& cancellation)>;
/// The artifact reads whole: a GGUF's header parses, or an MLX directory's
/// files are all there (27c). The error text, or empty.
using Verifier = std::function<std::string(const std::filesystem::path& artifact)>;
/// `input` → `output` at `type`. The error text, or empty.
using Quantizer =
    std::function<std::string(const std::filesystem::path& input,
                              const std::filesystem::path& output, std::string_view type)>;
/// A PEFT adapter directory → a GGUF LoRA at the path given, read against the
/// configuration of the base it was trained over (53b). The error text, or
/// empty.
using AdapterConverter = std::function<std::string(
    const std::filesystem::path& adapter_dir, const std::filesystem::path& base_dir,
    const std::filesystem::path& gguf, const MessageSink& on_message,
    const harness::CancellationToken& cancellation)>;

/// `max(version) + 1`, or 1 for an empty ledger. Pruned entries count:
/// their numbers are taken and must never be reissued.
[[nodiscard]] int next_version(const VersionLedger& ledger);

/// What a promotion makes runnable (27c). Named for the store's formats,
/// never a backend: which backend type runs which is the command's to say.
enum class PromoteTarget : std::uint8_t {
    /// A GGUF: fused, converted, verified, quantized when asked.
    Gguf,
    /// The fused SafeTensors themselves, verified as a whole model directory
    /// -- no conversion runs.
    Mlx,
};

[[nodiscard]] std::string_view to_string(PromoteTarget target) noexcept;
/// "gguf" or "mlx"; nullopt for anything else.
[[nodiscard]] std::optional<PromoteTarget> promote_target_from_string(
    std::string_view name) noexcept;
/// Every target's spelling, for a flag's completion.
[[nodiscard]] std::span<const std::string_view> promote_target_names() noexcept;
/// What a recorded version is.
[[nodiscard]] PromoteTarget target_of(const VersionEntry& entry) noexcept;

/// How a promotion makes its version (53b).
enum class PromoteMode : std::uint8_t {
    /// The adapter fused into the base and the fused model made the version:
    /// a full model per version. The default.
    Merge,
    /// The fuse skipped: the adapter itself is the version -- converted to a
    /// GGUF LoRA for a GGUF target, copied as it is for an MLX one -- and
    /// registered on the entry's `adapters:` over the base it runs, which it
    /// never touches. Opt-in (`train promote --adapter`).
    Adapter,
};

/// What a recorded version is made by.
[[nodiscard]] PromoteMode mode_of(const VersionEntry& entry) noexcept;

/// Why `ledger` cannot take a `target` version made by `mode`, or empty when
/// it can: a backend's versions are all of one kind, since a rollback
/// repoints the entry's `model_path` -- or, for adapter versions, its
/// `adapters:` -- and never its type or its base. Asked before anything is
/// built.
[[nodiscard]] std::string target_conflict(const VersionLedger& ledger, PromoteTarget target,
                                          PromoteMode mode = PromoteMode::Merge);

/// Why the run's adapter cannot be registered as it is on a `target` entry,
/// or empty when it can (53b). Formats never cross, and are said, never
/// guessed: a PEFT adapter (trainer `peft`) is converted to a GGUF LoRA for a
/// llamacpp entry, an mlx-lm adapter directory (trainer `mlx`) is registered
/// as it is on an mlx entry, and the mock's is either. And an adapter applies
/// only over the weights it was trained on: a pipeline stage after the first
/// trained over the stage before's fused checkpoint, which no entry runs.
[[nodiscard]] std::string adapter_promotion_conflict(const RunManifest& manifest,
                                                     PromoteTarget target);

/// Where an adapter version lives (53b): `<versions_dir>/<backend>/v<N>.lora.gguf`
/// for a GGUF target, the directory `<versions_dir>/<backend>/v<N>.lora` for
/// an MLX one -- beside the backend's ledger, kilobytes rather than a model.
[[nodiscard]] std::filesystem::path adapter_version_path(const std::filesystem::path& versions_dir,
                                                         std::string_view backend, int version,
                                                         PromoteTarget target);

struct PromotePlan {
    int version = 0;
    PromoteTarget target = PromoteTarget::Gguf;
    /// `runs/<id>/fused` -- or, for an MLX target, the staging directory the
    /// command names, which the fuse writes and which IS the version.
    std::filesystem::path fused_dir;
    /// `<output>/<backend>-v<N>.gguf`, built where the command says -- a
    /// staging directory in the model store, which the command commits.
    std::filesystem::path gguf_path;
    /// Where the F16 conversion lands: `gguf_path` itself, or a sibling
    /// `<backend>-v<N>.f16.gguf` that the quantizer reads and that is removed
    /// after.
    std::filesystem::path f16_path;
    /// Empty means F16 is the artifact.
    std::string quantize_type;
    bool keep_fused = false;
    /// Merge, the default; or the adapter itself as the version (53b).
    PromoteMode mode = PromoteMode::Merge;
    /// An adapter version's file or directory (`adapter_version_path`); empty
    /// for a merged one, which writes nothing here.
    std::filesystem::path adapter_path{};

    /// What the version is: the GGUF, or for an MLX target the fused
    /// directory -- or, for an adapter version, the adapter.
    [[nodiscard]] const std::filesystem::path& artifact() const noexcept {
        if (mode == PromoteMode::Adapter) {
            return adapter_path;
        }
        return target == PromoteTarget::Mlx ? fused_dir : gguf_path;
    }
};

/// The plan for the next version. A GGUF target builds `<backend>-v<N>.gguf`
/// under `output_dir`; an MLX target fuses straight into `output_dir` -- a
/// staging directory in the model store that the command commits -- with no
/// GGUF, no quantization and nothing to keep or drop.
[[nodiscard]] PromotePlan plan_promotion(const VersionLedger& ledger,
                                         const std::filesystem::path& run_dir,
                                         const std::filesystem::path& output_dir,
                                         std::string_view backend, std::string_view quantize_type,
                                         bool keep_fused,
                                         PromoteTarget target = PromoteTarget::Gguf);

struct ArtifactsResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    /// The artifact's size: the GGUF, or every file of the MLX directory.
    std::int64_t bytes = 0;
};

/// Fuse, convert, verify, quantize (when asked) and verify again, then drop
/// the fused checkpoint unless kept. A failure at any step removes what the
/// failed step left and reports it; the run directory is otherwise as it
/// was. The config is not touched here.
///
/// For an MLX target: fuse, then `verify` the fused directory -- the
/// command hands it a reader of whole model directories -- and nothing
/// else: `convert` and `quantize` are never called (27c).
[[nodiscard]] ArtifactsResult build_promotion_artifacts(
    const RunManifest& manifest, const PromotePlan& plan, Trainer& trainer,
    const Converter& convert, const Verifier& verify, const Quantizer& quantize,
    const MessageSink& on_message, const harness::CancellationToken& cancellation);

/// The plan for the next version as an adapter (53b), beside the fused plan:
/// the same `max + 1`, the version at `adapter_version_path` under
/// `versions_dir`, and nothing fused, quantized or kept.
[[nodiscard]] PromotePlan plan_adapter_promotion(const VersionLedger& ledger,
                                                 const std::filesystem::path& versions_dir,
                                                 std::string_view backend, PromoteTarget target);

/// The adapter version's file (53b), the fuse skipped -- no trainer is asked
/// for one. A GGUF target: `convert` the run's PEFT adapter against its base,
/// once, then `verify` that it parses. An MLX target: the run's mlx-lm
/// adapter directory copied as it is -- every file but the trainer's numbered
/// checkpoints (`0000100_adapters.safetensors`), which mlx-lm never loads --
/// then `verify`'d; `convert` is never called. Either is built under a
/// staging name beside the plan's path (`.incoming-v<N>.lora[.gguf]`) and
/// renamed into it once verified; a path already taken is never overwritten.
/// A failure removes what it left; the run directory, the config and the
/// ledger are untouched here.
[[nodiscard]] ArtifactsResult build_adapter_artifacts(
    const RunManifest& manifest, const PromotePlan& plan, const AdapterConverter& convert,
    const Verifier& verify, const MessageSink& on_message,
    const harness::CancellationToken& cancellation);

/// The ledger entry a successful build records.
[[nodiscard]] VersionEntry promotion_entry(const RunManifest& manifest, const PromotePlan& plan,
                                           std::string promoted_at);

/// The versions retention removes when a new one is recorded: the oldest
/// kept entries beyond `retain`, never the active one, none when `retain`
/// is 0.
[[nodiscard]] std::vector<int> prune_candidates(const VersionLedger& ledger, int retain);

struct PruneResult {
    std::vector<std::string> removed;
    /// Files that could not be removed, with the reason.
    std::vector<std::string> failed;
};

/// Removes a pruned version's artifacts. The error text, or empty. A closure
/// because where they live is the model store's business, not this package's.
using ArtifactRemover = std::function<std::string(const VersionEntry& entry)>;

/// Appends `entry`, makes it active, and prunes per `retain`: each pruned
/// entry's artifacts are removed (`remove`; by default its GGUF file, its
/// MLX directory, or -- 53b -- its adapter's file or directory, kilobytes
/// under the same count) and the entry marked, never erased -- the ledger is
/// history, and a rollback can name what is gone. Weights another kept
/// version still records -- identical weights promoted twice share one
/// stored file -- are never removed.
[[nodiscard]] PruneResult record_promotion(VersionLedger& ledger, VersionEntry entry, int retain,
                                           std::string pruned_at,
                                           const ArtifactRemover& remove = {});

struct RollbackTarget {
    /// The entry to repoint at, or null with `error` set.
    const VersionEntry* entry = nullptr;
    std::string error;
};

/// The highest version below the active one -- not `active - 1`, since
/// numbers have gaps after a prune -- refused by name when it was pruned,
/// its file (or an MLX version's directory, or an adapter version's adapter)
/// is gone, or it is not the kind the active version is. Deletes nothing.
[[nodiscard]] RollbackTarget rollback_target(const VersionLedger& ledger);

}  // namespace apogee::training
