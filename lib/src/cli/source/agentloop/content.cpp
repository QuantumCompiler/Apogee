#include "agentloop/content.h"

#include <algorithm>

#include "harness/errors.h"

namespace apogee::agentloop {
namespace {

constexpr std::size_t kCharactersPerToken = 4;

constexpr std::string_view kSummaryPrompt =
    "Please provide a concise but complete summary of our conversation so far. Capture all "
    "key facts, decisions, and context that would be needed to continue this conversation "
    "meaningfully. Be thorough but succinct.";

constexpr std::string_view kSummaryPreamble =
    "The following is a summary of the conversation so far. Earlier messages have been "
    "compacted to preserve context window space. Use this summary as the authoritative "
    "record of what has been discussed:\n\n";

}  // namespace

TokenCount estimate_tokens(std::string_view text) {
    return TokenCount{
        static_cast<std::int64_t>((text.size() + kCharactersPerToken - 1) / kCharactersPerToken),
        true};
}

TokenCount estimate_prompt_tokens(const std::vector<harness::ChatMessage>& messages) {
    std::size_t characters = 0;
    for (const harness::ChatMessage& message : messages) {
        characters += message.content.plain_text().size() + 1;
    }
    return TokenCount{
        static_cast<std::int64_t>((characters + kCharactersPerToken - 1) / kCharactersPerToken),
        true};
}

std::size_t count_turns(const std::vector<harness::ChatMessage>& history) {
    std::size_t conversational = 0;
    for (const harness::ChatMessage& message : history) {
        if (message.role != harness::Role::System) {
            ++conversational;
        }
    }
    return conversational / 2;  // one turn = one user + one assistant message
}

std::vector<harness::ChatMessage> splice_transient(
    const std::vector<harness::ChatMessage>& messages,
    const std::vector<harness::ChatMessage>& prefix, std::size_t at) {
    if (prefix.empty()) {
        return messages;
    }
    const std::size_t insert_at = std::min(at, messages.size());

    std::vector<harness::ChatMessage> out;
    out.reserve(messages.size() + prefix.size());
    out.insert(out.end(), messages.begin(),
               messages.begin() + static_cast<std::ptrdiff_t>(insert_at));
    out.insert(out.end(), prefix.begin(), prefix.end());
    out.insert(out.end(), messages.begin() + static_cast<std::ptrdiff_t>(insert_at),
               messages.end());
    return out;
}

std::vector<harness::ChatMessage> compact_history(const harness::Harness& harness,
                                                  const std::vector<harness::ChatMessage>& history,
                                                  const std::string& model,
                                                  const harness::CancellationToken& cancellation) {
    std::vector<harness::ChatMessage> system_messages;
    std::vector<harness::ChatMessage> conversation;
    for (const harness::ChatMessage& message : history) {
        if (message.role == harness::Role::System) {
            system_messages.push_back(message);
        } else {
            conversation.push_back(message);
        }
    }

    if (conversation.empty()) {
        return history;
    }

    harness::ChatRequest request;
    request.model = model;
    request.messages = conversation;
    request.messages.push_back(harness::ChatMessage::user(std::string{kSummaryPrompt}));

    std::string summary;
    try {
        summary = harness.chat(request, cancellation).message.content.plain_text();
    } catch (const harness::HarnessError&) {
        // A failed compaction degrades to a longer prompt, never to a lost
        // conversation. The caller may be told, but the history is intact.
        return history;
    }
    if (summary.empty()) {
        return history;
    }

    std::vector<harness::ChatMessage> compacted = std::move(system_messages);
    compacted.push_back(harness::ChatMessage::system(std::string{kSummaryPreamble} + summary));

    // Keep the most recent assistant message so the model has at least one
    // prior turn to reference, rather than a confusing "no prior context".
    for (auto it = conversation.rbegin(); it != conversation.rend(); ++it) {
        if (it->role == harness::Role::Assistant) {
            compacted.push_back(*it);
            break;
        }
    }
    return compacted;
}

}  // namespace apogee::agentloop
