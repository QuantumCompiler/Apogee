#include "training/promote.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>
#include <utility>

namespace apogee::training {

namespace {

/// Why a run cannot be promoted whatever the gate says, or empty: only a
/// complete run has an adapter to judge.
[[nodiscard]] std::string incomplete(const RunManifest& manifest) {
    if (manifest.complete()) {
        return {};
    }
    return "run " + manifest.run_id + " is " +
           (manifest.status.empty() ? std::string{"incomplete"} : manifest.status) +
           (manifest.error.empty() ? std::string{} : " (" + manifest.error + ")") +
           " -- only a complete run can be promoted";
}

/// The gate's one rule over a `problem` -- none passes; `force` and a soft
/// gate let it through, said; a hard one refuses, naming the way past it.
[[nodiscard]] GateVerdict judged(const std::string& problem, GateMode mode, bool force) {
    GateVerdict verdict;
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

/// A score as the gate's sentences give it: `50%: 1/2 passed`.
[[nodiscard]] std::string tally(const EvalResults& results) {
    const int percent = static_cast<int>(results.score * 100.0 + 0.5);
    return std::to_string(percent) + "%: " + std::to_string(results.num_passed) + "/" +
           std::to_string(results.total) + " passed";
}

/// Where the serving gate scores, for a sentence: `<label> (<precision>), the
/// base '<backend>' serves`.
[[nodiscard]] std::string serving_place(const ServingBase& base) {
    return describe(base) + ", the base '" + base.backend + "' serves";
}

}  // namespace

GateVerdict eval_gate(const RunManifest& manifest, GateMode mode, bool force) {
    if (std::string refused = incomplete(manifest); !refused.empty()) {
        GateVerdict verdict;
        verdict.error = std::move(refused);
        return verdict;
    }
    std::string problem;
    if (!manifest.eval.has_value()) {
        problem = "no eval results for run " + manifest.run_id + ". Run 'apogee train eval " +
                  manifest.run_id + " --suite <path>' first";
    } else if (!manifest.eval->passed) {
        problem = "the eval gate failed for run " + manifest.run_id + " (" + tally(*manifest.eval) +
                  "). Fix the training or the suite, then re-run eval";
    }
    return judged(problem, mode, force);
}

std::string describe(const ServingBase& base) {
    return base.precision.empty() ? base.label : base.label + " (" + base.precision + ")";
}

ServingPlan plan_serving_gate(const RunManifest& manifest, const ServingGateRequest& request) {
    ServingPlan plan;
    if (std::string refused = incomplete(manifest); !refused.empty()) {
        plan.verdict.error = std::move(refused);
        return plan;
    }
    if (request.force) {
        // Skipped, as the flag says: no eval is run -- and no judge is paid
        // -- for a verdict nobody will heed.
        plan.verdict =
            judged("run " + manifest.run_id + " was not scored on " + serving_place(request.base),
                   request.mode, true);
        return plan;
    }
    if (request.items.empty()) {
        plan.verdict = judged("no eval suite to score run " + manifest.run_id + " on " +
                                  serving_place(request.base) +
                                  " -- pass --suite <path|name>, or set "
                                  "training.eval_suite_path",
                              request.mode, false);
        return plan;
    }
    plan.verdict.ok = true;
    plan.score = true;
    return plan;
}

ServingGate gate_on_serving_base(const RunManifest& manifest, const ServingGateRequest& request,
                                 const ServingRunnerFactory& runners, EvalOptions options) {
    ServingGate gate;
    const ServingPlan plan = plan_serving_gate(manifest, request);
    if (!plan.score) {
        gate.verdict = plan.verdict;
        return gate;
    }
    const std::string place = serving_place(request.base);
    const auto unscored = [&](const std::string& why) {
        return judged("run " + manifest.run_id + " could not be scored on " + place + ": " + why,
                      request.mode, false);
    };
    std::string error;
    const std::unique_ptr<CandidateRunner> candidate =
        runners ? runners(true, error) : std::unique_ptr<CandidateRunner>{};
    if (candidate == nullptr) {
        gate.verdict = unscored(error.empty() ? std::string{"no runtime to run it on"} : error);
        return gate;
    }
    // The untuned base through the same runtime, for the judge alone. One
    // that cannot be built leaves the judged items to skip, counted --
    // never a reason to refuse what the substring items can still say.
    std::unique_ptr<CandidateRunner> baseline;
    if (options.judge) {
        std::string ignored;
        baseline = runners(false, ignored);
    }
    options.baseline = baseline.get();
    EvalRun run = run_eval(request.items, *candidate, options);
    if (run.cancelled) {
        gate.cancelled = true;
        return gate;
    }
    if (!run.ok) {
        gate.verdict = unscored(run.error);
        return gate;
    }
    run.results.suite = request.suite;
    gate.verdict =
        judged(run.results.passed ? std::string{}
                                  : "the eval gate failed for run " + manifest.run_id +
                                        ", scored on " + place + " (" + tally(run.results) +
                                        "). Fix the training or the suite, then promote again",
               request.mode, false);
    gate.results = std::move(run.results);
    return gate;
}

void record_serving_gate(VersionEntry& entry, const ServingGate& gate, const ServingBase& base) {
    entry.eval_score.reset();
    entry.eval_passed.reset();
    entry.eval_scored_on.clear();
    if (gate.results.has_value()) {
        entry.eval_score = gate.results->score;
        entry.eval_passed = gate.results->passed;
        entry.eval_scored_on = describe(base);
    }
}

namespace {

constexpr std::array<std::string_view, 2> kTargetNames{"gguf", "mlx"};

/// Every file under `dir`, in bytes.
[[nodiscard]] std::int64_t directory_bytes(const std::filesystem::path& dir) {
    std::error_code code;
    std::int64_t total = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        if (it->is_regular_file(code)) {
            total += static_cast<std::int64_t>(it->file_size(code));
        }
    }
    return total;
}

[[nodiscard]] std::string_view kind_of(PromoteTarget target) noexcept {
    return target == PromoteTarget::Mlx ? "MLX models" : "GGUFs";
}

/// What a version is, for a sentence about kinds that do not mix (53b).
[[nodiscard]] std::string kind_of(const VersionEntry& entry) {
    if (entry.adapter()) {
        return entry.mlx() ? "an mlx-lm adapter" : "a GGUF LoRA adapter";
    }
    return entry.mlx() ? "an MLX model" : "a GGUF";
}

/// A trainer's numbered checkpoint, `0000100_adapters.safetensors` --
/// mlx-lm's `--save-every`, which its load never reads (53b).
[[nodiscard]] bool numbered_checkpoint(const std::filesystem::path& file) {
    const std::string name = file.filename().string();
    constexpr std::string_view suffix = "_adapters.safetensors";
    if (!name.ends_with(suffix) || name.size() == suffix.size()) {
        return false;
    }
    return std::ranges::all_of(name.substr(0, name.size() - suffix.size()),
                               [](unsigned char c) { return std::isdigit(c) != 0; });
}

/// `from` copied to `to` (53b), every file but a numbered checkpoint. The
/// error text, or empty.
[[nodiscard]] std::string copy_adapter_directory(const std::filesystem::path& from,
                                                 const std::filesystem::path& to) {
    std::error_code code;
    std::filesystem::create_directories(to, code);
    if (code) {
        return "could not create " + to.string() + ": " + code.message();
    }
    for (auto it = std::filesystem::recursive_directory_iterator(from, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        const std::filesystem::path target = to / it->path().lexically_relative(from);
        if (it->is_directory(code)) {
            std::filesystem::create_directories(target, code);
        } else if (it->is_regular_file(code) && !numbered_checkpoint(it->path())) {
            std::filesystem::copy_file(it->path(), target,
                                       std::filesystem::copy_options::overwrite_existing, code);
        }
        if (code) {
            return "could not copy " + it->path().string() + ": " + code.message();
        }
    }
    return code ? "could not read " + from.string() + ": " + code.message() : std::string{};
}

}  // namespace

std::string_view to_string(PromoteTarget target) noexcept {
    return target == PromoteTarget::Mlx ? kTargetNames[1] : kTargetNames[0];
}

std::optional<PromoteTarget> promote_target_from_string(std::string_view name) noexcept {
    if (name == kTargetNames[0]) {
        return PromoteTarget::Gguf;
    }
    if (name == kTargetNames[1]) {
        return PromoteTarget::Mlx;
    }
    return std::nullopt;
}

std::span<const std::string_view> promote_target_names() noexcept {
    return kTargetNames;
}

PromoteTarget target_of(const VersionEntry& entry) noexcept {
    return entry.mlx() ? PromoteTarget::Mlx : PromoteTarget::Gguf;
}

PromoteMode mode_of(const VersionEntry& entry) noexcept {
    return entry.adapter() ? PromoteMode::Adapter : PromoteMode::Merge;
}

std::string target_conflict(const VersionLedger& ledger, PromoteTarget target, PromoteMode mode) {
    const VersionEntry* active = ledger.active();
    if (active == nullptr) {
        return {};
    }
    const std::string at = " (active v" + std::to_string(active->version) + ")";
    // Merged and adapter versions never mix in one ledger (53b): a merged
    // version replaces the base an adapter applies over, and an adapter over
    // a merged version would apply over a fine-tune it was not trained on.
    if (mode_of(*active) != mode) {
        if (mode == PromoteMode::Adapter) {
            return "'" + ledger.backend + "' holds merged versions" + at +
                   ", so its model_path is a fine-tune, not the base an adapter was trained "
                   "over -- promote into it without --adapter, or --adapter into an entry that "
                   "runs the base";
        }
        return "'" + ledger.backend + "' holds adapter versions" + at +
               " applied over its base, and a merged version would replace that base under "
               "them -- promote into it with --adapter, or merged under a new name";
    }
    if (target_of(*active) == target) {
        return {};
    }
    const std::string held = !active->adapter() ? std::string{kind_of(target_of(*active))}
                             : active->mlx()    ? "mlx-lm adapters"
                                                : "GGUF LoRA adapters";
    return "'" + ledger.backend + "' holds " + held + at +
           ", and a rollback never changes a backend's type -- promote --target " +
           std::string{to_string(target)} + " under a new name";
}

std::string adapter_promotion_conflict(const RunManifest& manifest, PromoteTarget target) {
    if (!manifest.parent_run.empty()) {
        return "run " + manifest.run_id + " is a pipeline stage trained over stage " +
               manifest.parent_run +
               "'s fused checkpoint, which no entry runs -- an adapter applies only over the "
               "weights it was trained on: promote it without --adapter";
    }
    // What wrote the adapter decides its format; the entry's runtime decides
    // the format it reads. Never converted across.
    const std::string& trainer = manifest.trainer;
    if (trainer == "mock") {
        return {};
    }
    const std::string ways =
        " -- adapter formats never cross: register it on " +
        std::string{target == PromoteTarget::Gguf ? "an mlx entry (--target mlx)"
                                                  : "a llamacpp entry (--target gguf)"} +
        ", or promote it without --adapter";
    if (target == PromoteTarget::Gguf && trainer == "mlx") {
        return "run " + manifest.run_id +
               "'s adapter is an mlx-lm adapter (trainer mlx), and a llamacpp entry applies a "
               "GGUF LoRA, converted from a PEFT adapter (trainer peft)" +
               ways;
    }
    if (target == PromoteTarget::Mlx && trainer == "peft") {
        return "run " + manifest.run_id +
               "'s adapter is a PEFT adapter (trainer peft), and an mlx entry applies an "
               "mlx-lm adapter directory (trainer mlx) as it is" +
               ways;
    }
    if (trainer != "mlx" && trainer != "peft") {
        return "run " + manifest.run_id + " records " +
               (trainer.empty() ? std::string{"no trainer"} : "the trainer '" + trainer + "'") +
               ", so its adapter's format is not known -- promote it without --adapter";
    }
    return {};
}

std::filesystem::path adapter_version_path(const std::filesystem::path& versions_dir,
                                           std::string_view backend, int version,
                                           PromoteTarget target) {
    const std::string stem = "v" + std::to_string(version) + ".lora";
    return versions_dir / std::string{backend} /
           (target == PromoteTarget::Mlx ? stem : stem + ".gguf");
}

int next_version(const VersionLedger& ledger) {
    int highest = 0;
    for (const VersionEntry& entry : ledger.versions) {
        highest = std::max(highest, entry.version);
    }
    return highest + 1;
}

PromotePlan plan_promotion(const VersionLedger& ledger, const std::filesystem::path& run_dir,
                           const std::filesystem::path& output_dir, std::string_view backend,
                           std::string_view quantize_type, bool keep_fused, PromoteTarget target) {
    PromotePlan plan;
    plan.version = next_version(ledger);
    plan.target = target;
    if (target == PromoteTarget::Mlx) {
        // The fused weights are the version: written where they are stored.
        plan.fused_dir = output_dir;
        plan.keep_fused = true;
        return plan;
    }
    plan.fused_dir = run_dir / kFusedDirName;
    const std::string stem = std::string{backend} + "-v" + std::to_string(plan.version);
    plan.gguf_path = output_dir / (stem + ".gguf");
    plan.quantize_type = std::string{quantize_type};
    plan.f16_path = quantize_type.empty() ? plan.gguf_path : output_dir / (stem + ".f16.gguf");
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

    if (std::filesystem::exists(plan.artifact(), code)) {
        result.error = plan.artifact().string() + " already exists";
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

    if (plan.target == PromoteTarget::Mlx) {
        // The fused weights run as they are: read whole, and nothing more.
        say("no GGUF conversion: the fused model is registered as it is, for the mlx backend");
        if (const std::string error = verify(plan.fused_dir); !error.empty()) {
            result.error = "the fused model is not a whole model directory: " + error;
            discard(plan.fused_dir);
            return result;
        }
        result.bytes = directory_bytes(plan.fused_dir);
        result.ok = true;
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
    result.bytes = static_cast<std::int64_t>(std::filesystem::file_size(plan.gguf_path, code));
    result.ok = true;
    return result;
}

PromotePlan plan_adapter_promotion(const VersionLedger& ledger,
                                   const std::filesystem::path& versions_dir,
                                   std::string_view backend, PromoteTarget target) {
    PromotePlan plan;
    plan.version = next_version(ledger);
    plan.target = target;
    plan.mode = PromoteMode::Adapter;
    plan.adapter_path = adapter_version_path(versions_dir, backend, plan.version, target);
    return plan;
}

ArtifactsResult build_adapter_artifacts(const RunManifest& manifest, const PromotePlan& plan,
                                        const AdapterConverter& convert, const Verifier& verify,
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
    if (std::filesystem::exists(plan.adapter_path, code)) {
        // Never overwritten: the ledger records no such version, but an
        // entry's adapters may still name the file.
        result.error = plan.adapter_path.string() +
                       " already exists, though the ledger records no v" +
                       std::to_string(plan.version) +
                       " -- if no entry's adapters name it, remove it and promote again";
        return result;
    }
    if (cancellation.stop_requested()) {
        result.cancelled = true;
        result.error = "cancelled";
        return result;
    }
    const std::filesystem::path adapter{manifest.adapter_dir};
    if (!std::filesystem::is_directory(adapter, code)) {
        result.error = "the run's adapter is not at " + adapter.string();
        return result;
    }
    // Built beside its place and renamed into it once it reads whole, so the
    // version's path only ever holds a whole adapter. What an interrupted
    // attempt left under the staging name was never registered: it goes.
    const std::filesystem::path staging =
        plan.adapter_path.parent_path() / (".incoming-" + plan.adapter_path.filename().string());
    discard(staging);
    std::filesystem::create_directories(plan.adapter_path.parent_path(), code);
    say("no fuse: the adapter is the version, applied over the entry's base when it loads");
    say("  trained on: " + manifest.base_model);
    say("  adapter:    " + manifest.adapter_dir);

    if (plan.target == PromoteTarget::Mlx) {
        // mlx-lm reads its own adapter directory: nothing to convert.
        say("no conversion: the mlx-lm adapter directory is registered as it is: " +
            plan.adapter_path.string());
        if (const std::string error = copy_adapter_directory(adapter, staging); !error.empty()) {
            result.error = error;
            discard(staging);
            return result;
        }
        if (const std::string error = verify(staging); !error.empty()) {
            result.error = "the copied adapter is not an mlx-lm adapter directory: " + error;
            discard(staging);
            return result;
        }
    } else {
        say("converting the adapter to a GGUF LoRA (F16): " + plan.adapter_path.string());
        if (const std::string error = convert(adapter, std::filesystem::path{manifest.base_model},
                                              staging, on_message, cancellation);
            !error.empty()) {
            result.cancelled = cancellation.stop_requested();
            result.error = "convert: " + error;
            discard(staging);
            return result;
        }
        if (const std::string error = verify(staging); !error.empty()) {
            // Unreadable, it must not be registered, so it must not exist.
            result.error = "the converted LoRA does not parse: " + error;
            discard(staging);
            return result;
        }
    }
    std::filesystem::rename(staging, plan.adapter_path, code);
    if (code) {
        result.error =
            "could not move the adapter into " + plan.adapter_path.string() + ": " + code.message();
        discard(staging);
        return result;
    }
    result.bytes =
        plan.target == PromoteTarget::Mlx
            ? directory_bytes(plan.adapter_path)
            : static_cast<std::int64_t>(std::filesystem::file_size(plan.adapter_path, code));
    result.ok = true;
    return result;
}

VersionEntry promotion_entry(const RunManifest& manifest, const PromotePlan& plan,
                             std::string promoted_at) {
    VersionEntry entry;
    entry.version = plan.version;
    entry.run_id = manifest.run_id;
    if (plan.mode == PromoteMode::Adapter) {
        entry.adapter_path = plan.adapter_path.string();
    } else if (plan.target == PromoteTarget::Mlx) {
        entry.mlx_path = plan.fused_dir.string();
    } else {
        entry.gguf_path = plan.gguf_path.string();
    }
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
                             std::string pruned_at, const ArtifactRemover& remove) {
    PruneResult result;
    ledger.active_version = entry.version;
    ledger.versions.push_back(std::move(entry));
    std::ranges::sort(ledger.versions, [](const VersionEntry& a, const VersionEntry& b) {
        return a.version < b.version;
    });
    const std::vector<int> candidates = prune_candidates(ledger, retain);
    for (const int version : candidates) {
        for (VersionEntry& old : ledger.versions) {
            if (old.version != version) {
                continue;
            }
            // Identical weights promoted twice share one stored file; it goes
            // only when no version still kept records it.
            const bool shared =
                std::ranges::any_of(ledger.versions, [&](const VersionEntry& other) {
                    return other.version != old.version && !other.pruned() &&
                           other.weights() == old.weights() &&
                           std::ranges::find(candidates, other.version) == candidates.end();
                });
            if (!shared) {
                std::string error;
                if (remove) {
                    error = remove(old);
                } else {
                    std::error_code code;
                    const bool existed = std::filesystem::exists(old.weights(), code);
                    if (old.mlx()) {
                        std::filesystem::remove_all(old.weights(), code);
                    } else {
                        std::filesystem::remove(old.weights(), code);
                    }
                    if (code && existed) {
                        error = code.message();
                    }
                }
                if (!error.empty()) {
                    result.failed.push_back(old.weights() + ": " + error);
                    continue;
                }
            }
            old.pruned_at = pruned_at;
            result.removed.push_back(old.weights());
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
    const std::string what = previous->adapter() ? "adapter"
                             : previous->mlx()   ? "MLX model"
                                                 : "GGUF";
    if (previous->pruned()) {
        target.error = "v" + std::to_string(previous->version) + " of '" + ledger.backend +
                       "' was pruned by retain_versions on " + previous->pruned_at + " -- its " +
                       what + " is gone, so rolling back to it is not possible";
        return target;
    }
    if (const VersionEntry* active = ledger.active();
        active != nullptr &&
        (target_of(*active) != target_of(*previous) || mode_of(*active) != mode_of(*previous))) {
        target.error = "v" + std::to_string(previous->version) + " of '" + ledger.backend +
                       "' is " + kind_of(*previous) + " and the active v" +
                       std::to_string(active->version) +
                       " is not -- a rollback never changes a backend's type";
        return target;
    }
    std::error_code code;
    const bool there = previous->mlx()
                           ? std::filesystem::is_directory(previous->weights(), code)
                           : std::filesystem::is_regular_file(previous->weights(), code);
    if (!there) {
        target.error = "the " + what + " for v" + std::to_string(previous->version) + " of '" +
                       ledger.backend + "' is not at " + previous->weights() +
                       " -- rolling back to it is not possible";
        return target;
    }
    target.entry = previous;
    return target;
}

}  // namespace apogee::training
