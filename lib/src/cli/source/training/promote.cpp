#include "training/promote.h"

#include <algorithm>
#include <system_error>

namespace apogee::training {

GateVerdict eval_gate(const RunManifest& manifest, GateMode mode, bool force) {
    GateVerdict verdict;
    if (!manifest.complete()) {
        verdict.error = "run " + manifest.run_id + " is " +
                        (manifest.status.empty() ? std::string{"incomplete"} : manifest.status) +
                        (manifest.error.empty() ? std::string{} : " (" + manifest.error + ")") +
                        " -- only a complete run can be promoted";
        return verdict;
    }
    std::string problem;
    if (!manifest.eval.has_value()) {
        problem = "no eval results for run " + manifest.run_id + ". Run 'apogee train eval " +
                  manifest.run_id + " --suite <path>' first";
    } else if (!manifest.eval->passed) {
        const int percent = static_cast<int>(manifest.eval->score * 100.0 + 0.5);
        problem = "the eval gate failed for run " + manifest.run_id + " (" +
                  std::to_string(percent) + "%: " + std::to_string(manifest.eval->num_passed) +
                  "/" + std::to_string(manifest.eval->total) +
                  " passed). Fix the training or the suite, then re-run eval";
    }
    if (problem.empty()) {
        verdict.ok = true;
        return verdict;
    }
    if (force) {
        verdict.ok = true;
        verdict.warning = "promoting despite the gate (--force): " + problem;
        return verdict;
    }
    if (mode == GateMode::Soft) {
        verdict.ok = true;
        verdict.warning = "gate_mode is soft: " + problem;
        return verdict;
    }
    verdict.error = problem + ", or pass --force to skip the gate";
    return verdict;
}

int next_version(const VersionLedger& ledger) {
    int highest = 0;
    for (const VersionEntry& entry : ledger.versions) {
        highest = std::max(highest, entry.version);
    }
    return highest + 1;
}

PromotePlan plan_promotion(const VersionLedger& ledger, const std::filesystem::path& run_dir,
                           const std::filesystem::path& versions_dir, std::string_view backend,
                           std::string_view quantize_type, bool keep_fused) {
    PromotePlan plan;
    plan.version = next_version(ledger);
    plan.fused_dir = run_dir / kFusedDirName;
    const std::filesystem::path dir = versions_dir / std::string{backend};
    const std::string stem = "v" + std::to_string(plan.version);
    plan.gguf_path = dir / (stem + ".gguf");
    plan.quantize_type = std::string{quantize_type};
    plan.f16_path = quantize_type.empty() ? plan.gguf_path : dir / (stem + ".f16.gguf");
    plan.keep_fused = keep_fused;
    return plan;
}

ArtifactsResult build_promotion_artifacts(const RunManifest& manifest, const PromotePlan& plan,
                                          Trainer& trainer, const Converter& convert,
                                          const Verifier& verify, const Quantizer& quantize,
                                          const MessageSink& on_message,
                                          const harness::CancellationToken& cancellation) {
    ArtifactsResult result;
    std::error_code code;
    auto say = [&on_message](const std::string& text) {
        if (on_message) {
            on_message(text);
        }
    };
    auto discard = [&code](const std::filesystem::path& path) {
        std::filesystem::remove_all(path, code);
        code.clear();
    };
    // A fused tree is model-sized; a failure after it exists drops it too
    // (unless asked to keep), since the next attempt fuses again anyway.
    auto discard_fused = [&]() {
        if (!plan.keep_fused) {
            discard(plan.fused_dir);
        }
    };
    auto cancelled = [&]() {
        if (!cancellation.stop_requested()) {
            return false;
        }
        result.cancelled = true;
        result.error = "cancelled";
        return true;
    };

    if (std::filesystem::exists(plan.gguf_path, code)) {
        result.error = plan.gguf_path.string() + " already exists";
        return result;
    }
    if (cancelled()) {
        return result;
    }

    // A fused tree left by an earlier failed attempt is stale: fuse writes
    // the whole thing again.
    discard(plan.fused_dir);
    say("fusing the adapter into the base model");
    say("  base:    " + manifest.base_model);
    say("  adapter: " + manifest.adapter_dir);
    say("  output:  " + plan.fused_dir.string());
    if (const std::string error = trainer.fuse(manifest.base_model, manifest.adapter_dir,
                                               plan.fused_dir, on_message, cancellation);
        !error.empty()) {
        result.cancelled = cancellation.stop_requested();
        result.error = "fuse: " + error;
        discard(plan.fused_dir);
        return result;
    }
    if (cancelled()) {
        discard(plan.fused_dir);
        return result;
    }

    say("converting the fused model to GGUF (F16): " + plan.f16_path.string());
    std::filesystem::create_directories(plan.f16_path.parent_path(), code);
    if (const std::string error = convert(plan.fused_dir, plan.f16_path, on_message, cancellation);
        !error.empty()) {
        result.cancelled = cancellation.stop_requested();
        result.error = "convert: " + error;
        discard(plan.f16_path);
        discard_fused();
        return result;
    }
    if (const std::string error = verify(plan.f16_path); !error.empty()) {
        // The converter produced something the header reader cannot read:
        // it must not be registered, so it must not exist.
        result.error = "the converted GGUF does not parse: " + error;
        discard(plan.f16_path);
        discard_fused();
        return result;
    }

    if (!plan.quantize_type.empty()) {
        if (cancelled()) {
            discard(plan.f16_path);
            discard_fused();
            return result;
        }
        say("quantizing to " + plan.quantize_type + ": " + plan.gguf_path.string());
        if (const std::string error = quantize(plan.f16_path, plan.gguf_path, plan.quantize_type);
            !error.empty()) {
            result.error = "quantize: " + error;
            discard(plan.gguf_path);
            discard(plan.f16_path);
            discard_fused();
            return result;
        }
        if (const std::string error = verify(plan.gguf_path); !error.empty()) {
            result.error = "the quantized GGUF does not parse: " + error;
            discard(plan.gguf_path);
            discard(plan.f16_path);
            discard_fused();
            return result;
        }
        discard(plan.f16_path);
    }

    if (!plan.keep_fused) {
        discard(plan.fused_dir);
    } else {
        say("keeping the fused checkpoint at " + plan.fused_dir.string());
    }
    result.gguf_bytes = static_cast<std::int64_t>(std::filesystem::file_size(plan.gguf_path, code));
    result.ok = true;
    return result;
}

VersionEntry promotion_entry(const RunManifest& manifest, const PromotePlan& plan,
                             std::string promoted_at) {
    VersionEntry entry;
    entry.version = plan.version;
    entry.run_id = manifest.run_id;
    entry.gguf_path = plan.gguf_path.string();
    entry.promoted_at = std::move(promoted_at);
    if (manifest.eval.has_value()) {
        entry.eval_score = manifest.eval->score;
        entry.eval_passed = manifest.eval->passed;
    }
    return entry;
}

std::vector<int> prune_candidates(const VersionLedger& ledger, int retain) {
    std::vector<int> candidates;
    if (retain <= 0) {
        return candidates;
    }
    int kept = ledger.kept();
    // Ascending by version, so the oldest goes first; the active one is
    // skipped, never counted out.
    for (const VersionEntry& entry : ledger.versions) {
        if (kept <= retain) {
            break;
        }
        if (entry.pruned() || entry.version == ledger.active_version) {
            continue;
        }
        candidates.push_back(entry.version);
        --kept;
    }
    return candidates;
}

PruneResult record_promotion(VersionLedger& ledger, VersionEntry entry, int retain,
                             std::string pruned_at) {
    PruneResult result;
    ledger.active_version = entry.version;
    ledger.versions.push_back(std::move(entry));
    std::ranges::sort(ledger.versions, [](const VersionEntry& a, const VersionEntry& b) {
        return a.version < b.version;
    });
    for (const int version : prune_candidates(ledger, retain)) {
        for (VersionEntry& old : ledger.versions) {
            if (old.version != version) {
                continue;
            }
            std::error_code code;
            const bool existed = std::filesystem::exists(old.gguf_path, code);
            std::filesystem::remove(old.gguf_path, code);
            if (code && existed) {
                result.failed.push_back(old.gguf_path + ": " + code.message());
                continue;
            }
            old.pruned_at = pruned_at;
            result.removed.push_back(old.gguf_path);
        }
    }
    return result;
}

RollbackTarget rollback_target(const VersionLedger& ledger) {
    RollbackTarget target;
    if (ledger.versions.empty() || ledger.active_version == 0) {
        target.error = "no version history for backend '" + ledger.backend + "'";
        return target;
    }
    const VersionEntry* previous = nullptr;
    for (const VersionEntry& entry : ledger.versions) {
        if (entry.version < ledger.active_version &&
            (previous == nullptr || entry.version > previous->version)) {
            previous = &entry;
        }
    }
    if (previous == nullptr) {
        target.error =
            ledger.versions.size() == 1
                ? "backend '" + ledger.backend + "' has only one promoted version (v" +
                      std::to_string(ledger.active_version) + ") -- nothing to roll back to"
                : "v" + std::to_string(ledger.active_version) + " is the oldest version of '" +
                      ledger.backend + "' -- nothing below it to roll back to";
        return target;
    }
    if (previous->pruned()) {
        target.error = "v" + std::to_string(previous->version) + " of '" + ledger.backend +
                       "' was pruned by retain_versions on " + previous->pruned_at +
                       " -- its GGUF is gone, so rolling back to it is not possible";
        return target;
    }
    std::error_code code;
    if (!std::filesystem::is_regular_file(previous->gguf_path, code)) {
        target.error = "the GGUF for v" + std::to_string(previous->version) + " of '" +
                       ledger.backend + "' is not at " + previous->gguf_path +
                       " -- rolling back to it is not possible";
        return target;
    }
    target.entry = previous;
    return target;
}

}  // namespace apogee::training
