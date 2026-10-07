#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
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
/// Nothing here edits the config or knows a backend type: the conversion,
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

/// Why `ledger` cannot take a `target` version, or empty when it can: a
/// backend's versions are all of one kind, since a rollback repoints the
/// entry's `model_path` and never its type. Asked before anything is built.
[[nodiscard]] std::string target_conflict(const VersionLedger& ledger, PromoteTarget target);

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

    /// What the version is: the GGUF, or for an MLX target the fused
    /// directory.
    [[nodiscard]] const std::filesystem::path& artifact() const noexcept {
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
/// entry's artifacts are removed (`remove`; by default its GGUF file, or its
/// MLX directory) and the entry marked, never erased -- the ledger is
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
/// its file (or an MLX version's directory) is gone, or it is not the kind
/// the active version is. Deletes nothing.
[[nodiscard]] RollbackTarget rollback_target(const VersionLedger& ledger);

}  // namespace apogee::training
