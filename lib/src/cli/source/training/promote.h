#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
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
/// Nothing here edits the config or knows a backend type: the conversion,
/// the header check and the quantizer arrive as closures, and the config
/// edit is the command's (`commands/train.cpp`, the composition root). What
/// this package owns is the arithmetic the reference got wrong -- version
/// numbers are `max + 1`, never the count, because a count regresses after
/// the first prune and the next promote overwrote a live file -- and
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
/// The GGUF's header parses. The error text, or empty.
using Verifier = std::function<std::string(const std::filesystem::path& gguf)>;
/// `input` → `output` at `type`. The error text, or empty.
using Quantizer =
    std::function<std::string(const std::filesystem::path& input,
                              const std::filesystem::path& output, std::string_view type)>;

/// `max(version) + 1`, or 1 for an empty ledger. Pruned entries count:
/// their numbers are taken and must never be reissued.
[[nodiscard]] int next_version(const VersionLedger& ledger);

struct PromotePlan {
    int version = 0;
    /// `runs/<id>/fused`.
    std::filesystem::path fused_dir;
    /// `versions/<backend>/v<N>.gguf` -- what the ledger records.
    std::filesystem::path gguf_path;
    /// Where the F16 conversion lands: `gguf_path` itself, or a sibling
    /// `v<N>.f16.gguf` that the quantizer reads and that is removed after.
    std::filesystem::path f16_path;
    /// Empty means F16 is the artifact.
    std::string quantize_type;
    bool keep_fused = false;
};

[[nodiscard]] PromotePlan plan_promotion(const VersionLedger& ledger,
                                         const std::filesystem::path& run_dir,
                                         const std::filesystem::path& versions_dir,
                                         std::string_view backend, std::string_view quantize_type,
                                         bool keep_fused);

struct ArtifactsResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::int64_t gguf_bytes = 0;
};

/// Fuse, convert, verify, quantize (when asked) and verify again, then drop
/// the fused checkpoint unless kept. A failure at any step removes what the
/// failed step left and reports it; the run directory is otherwise as it
/// was. The config is not touched here.
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

/// Appends `entry`, makes it active, and prunes per `retain`: each pruned
/// entry's file is removed and the entry marked, never erased -- the ledger
/// is history, and a rollback can name what is gone.
[[nodiscard]] PruneResult record_promotion(VersionLedger& ledger, VersionEntry entry, int retain,
                                           std::string pruned_at);

struct RollbackTarget {
    /// The entry to repoint at, or null with `error` set.
    const VersionEntry* entry = nullptr;
    std::string error;
};

/// The highest version below the active one -- not `active - 1`, since
/// numbers have gaps after a prune -- refused by name when it was pruned or
/// its file is gone. Deletes nothing.
[[nodiscard]] RollbackTarget rollback_target(const VersionLedger& ledger);

}  // namespace apogee::training
