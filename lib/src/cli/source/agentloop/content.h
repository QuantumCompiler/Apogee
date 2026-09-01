#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "harness/harness.h"
#include "harness/types.h"

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
/// **On any failure the original history is returned unchanged.** A failed
/// compaction must degrade to a longer prompt, never to a lost conversation.
[[nodiscard]] std::vector<harness::ChatMessage> compact_history(
    const harness::Harness& harness, const std::vector<harness::ChatMessage>& history,
    const std::string& model, const harness::CancellationToken& cancellation = {});

}  // namespace apogee::agentloop
