#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "contracts/types.h"
#include "harness/harness.h"

/// History shaping: token estimation, compaction, and transient splicing.
namespace apogee::agentloop {

/// A token count and whether it was measured or guessed.
///
/// The two are NOT interchangeable, and conflating them is how a context
/// warning ends up firing at the wrong point. A provider's own count is exact;
/// ours is characters/4, which is wrong by a model-dependent margin. Any
/// surface displaying this must say which it has.
struct TokenCount {
    std::int64_t tokens = 0;
    bool estimated = true;
};

/// Rough token estimate for a string: ~4 characters per token.
///
/// Deliberately crude. A real tokenizer is per-model, and shipping one that is
/// right for Llama and wrong for Claude would be worse than an honest
/// approximation that always flags itself.
[[nodiscard]] TokenCount estimate_tokens(std::string_view text);

/// Rough token estimate for a message list.
[[nodiscard]] TokenCount estimate_prompt_tokens(const std::vector<harness::ChatMessage>& messages);

/// Number of user+assistant exchanges in `history`.
[[nodiscard]] std::size_t count_turns(const std::vector<harness::ChatMessage>& history);

/// How close a conversation is to filling its context window.
///
/// Lives with the other history-shaping helpers rather than in a command,
/// because every surface that owns a conversation asks the same question --
/// the chat REPL, a driven chat, and a served session all warn at 80% and
/// compact at 90%. A copy per surface is how the thresholds drift.
struct ContextUsage {
    std::int64_t used_tokens = 0;
    std::int64_t window = 0;
    /// True when the count came from the provider rather than an estimate.
    bool exact = false;

    /// 0.0 when the window is unknown -- and an unknown window must NOT be
    /// treated as full.
    [[nodiscard]] double fraction() const noexcept {
        return window > 0 ? static_cast<double>(used_tokens) / static_cast<double>(window) : 0.0;
    }

    [[nodiscard]] bool should_warn() const noexcept {
        return window > 0 && fraction() >= 0.80;
    }

    [[nodiscard]] bool should_compact() const noexcept {
        return window > 0 && fraction() >= 0.90;
    }
};

/// Measures `messages` against the window `model` resolves to -- as the
/// budget would send them, earlier turns' tool results as stubs (26c).
///
/// Falls back to an estimate when the provider offers no exact count, and says
/// which it used. **A context warning that fires at the wrong point is worse
/// than none**, so the distinction is carried rather than smoothed over.
[[nodiscard]] ContextUsage measure_context(const harness::Harness& harness,
                                           const std::vector<harness::ChatMessage>& messages,
                                           const std::string& model);

/// Returns `messages` with `prefix` inserted at `at`.
///
/// The seam RAG rides on: injected context goes into the OUTGOING REQUEST only
/// and is never appended to the persisted history. If it were, it would be
/// re-sent on every later turn, growing the prompt without bound and feeding
/// the model context it was told applied to one question.
///
/// `at` is clamped to the message count, so a caller cannot splice past the
/// end.
[[nodiscard]] std::vector<harness::ChatMessage> splice_transient(
    const std::vector<harness::ChatMessage>& messages,
    const std::vector<harness::ChatMessage>& prefix, std::size_t at);

/// What a compaction did (55b): the history to keep, and whether anything was
/// folded. Every door says and counts it as it is -- a compaction that
/// changed nothing is never reported, or counted, as one that did.
struct Compaction {
    enum class Outcome : std::uint8_t {
        /// The earlier exchanges folded into a summary.
        Compacted,
        /// No exchange to fold: system messages alone, or with them only the
        /// answer an earlier compaction kept.
        Nothing,
        /// The summariser failed or answered nothing.
        Failed,
    };
    Outcome outcome = Outcome::Nothing;
    /// The compacted history -- or, when nothing was folded, the one given.
    std::vector<harness::ChatMessage> history{};
    /// Why it failed, in a line: the call's error, or an empty summary.
    std::string failure{};

    [[nodiscard]] bool compacted() const noexcept {
        return outcome == Outcome::Compacted;
    }
};

/// Summarises `history` into a single system message to free context space.
///
/// Preserves any leading system messages, replaces the conversation with a
/// summary, and keeps the most recent assistant message so the model has one
/// prior turn to reference.
///
/// The summary is a **system** message, not an assistant one: system reads as
/// authoritative record, so the model can answer "what have we discussed?"
/// correctly rather than treating its own summary as something it once said.
///
/// **On any failure the original history is kept unchanged**, and the result
/// says it failed and why. A failed compaction must degrade to a longer
/// prompt, never to a lost conversation -- nor to a claim that it compacted.
/// A history with no exchange to fold asks no model and says so.
///
/// Tool results are shown to the summariser as their stubs (26c): tool
/// output is condensed before conversation.
///
/// `model` writes the summary: the utility model when one is set, else the
/// conversation's own backend (26b). It is asked as a side request, so a
/// local chat model's cache is not disturbed by it.
[[nodiscard]] Compaction compact_history(const harness::Harness& harness,
                                         const std::vector<harness::ChatMessage>& history,
                                         const std::string& model,
                                         const harness::CancellationToken& cancellation = {});

}  // namespace apogee::agentloop
