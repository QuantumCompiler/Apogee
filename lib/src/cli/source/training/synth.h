#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
#include "training/kit.h"

/// Teacher-driven synthetic data -- the distillation step. A strong
/// **teacher** model fabricates supervised `prompt → completion` examples
/// for a kit's skill; those become the dataset a weaker **student** is
/// fine-tuned on. This file is **model-free**: the caller supplies a
/// `GenerateFn` wrapping any generation backend, and the core only
/// orchestrates batching, parsing, de-duplication and the bound on calls --
/// the same split `graph/` and `knowledge/` keep, and the reason the CLI and
/// the admin job cannot synthesise differently.
///
/// Ommi's rules, kept exactly: the teacher is asked for `per_seed` examples
/// per call with the kit's seeds cycled for diversity; the reply is parsed
/// as a JSON array tolerating prose, fences and the common field aliases;
/// prompts are de-duplicated case-insensitively; and total calls are bounded
/// so a model that keeps returning junk cannot spin. Two upgrades: batches
/// may run in parallel (an API teacher, not a local one), and a failed batch
/// is retried with backoff before it is skipped -- the rate-limit handling
/// the placeholder promised over the reference's sequential `claude -p`.
namespace apogee::training {

struct SynthExample {
    std::string prompt;
    std::string completion;
};

/// One teacher call's result. `retryable` says whether trying again could
/// help (a rate limit, a transient failure) -- a refused key cannot.
struct GenerateOutcome {
    bool ok = false;
    std::string text;
    std::string error;
    bool retryable = true;
};

using GenerateFn = std::function<GenerateOutcome(std::string_view system, std::string_view user,
                                                 double temperature, int max_tokens,
                                                 const harness::CancellationToken& cancellation)>;

struct SynthOptions {
    /// Target examples; 0 means the kit's `synth.count`.
    int count = 0;
    /// Extra focus appended to every batch prompt.
    std::string topic;
    /// 0 means the kit's `synth.temperature`.
    double temperature = 0.0;
    int max_tokens = 4096;
    /// Batches in flight at once. 1 is sequential and deterministic.
    int parallel = 1;
    /// Retries per batch on a retryable failure, with exponential backoff
    /// from `base_delay` capped at `max_delay`.
    int max_retries = 5;
    std::chrono::milliseconds base_delay{1000};
    std::chrono::milliseconds max_delay{60000};
    /// Called after every batch with (produced, target).
    std::function<void(int produced, int target)> on_progress;
    harness::CancellationToken cancellation;
    /// How a retry waits. Injectable so a test never sleeps.
    std::function<void(std::chrono::milliseconds)> sleep;
};

struct SynthResult {
    std::vector<SynthExample> examples;
    /// Teacher calls made, retries included.
    int calls = 0;
    /// Batches that failed every attempt or returned nothing usable.
    int failed_batches = 0;
    int retries = 0;
    bool cancelled = false;
    /// Set when nothing usable came back at all.
    std::string error;
};

/// The kit's prompt plus the output contract every teacher gets.
[[nodiscard]] std::string synth_system_prompt(std::string_view kit_system);

/// The per-batch instruction.
[[nodiscard]] std::string synth_user_prompt(int n, std::string_view seed, std::string_view topic);

/// The substring from the first `[` to its matching `]` by bracket depth,
/// ignoring brackets inside strings. Empty when none.
[[nodiscard]] std::string extract_json_array(std::string_view text);

/// The examples in a teacher reply: the array extracted, each object read
/// through the aliases `prompt|input|user|instruction|question` and
/// `completion|output|assistant|response|answer` (case-insensitive), blank
/// halves dropped.
[[nodiscard]] std::vector<SynthExample> parse_synth_examples(std::string_view raw);

/// The bound on teacher calls: `(target / per_call + 1) * 3 + seeds`.
[[nodiscard]] int max_synth_calls(int target, int per_call, std::size_t seeds) noexcept;

[[nodiscard]] SynthResult synthesize(const Kit& kit, const GenerateFn& generate,
                                     const SynthOptions& options);

}  // namespace apogee::training
