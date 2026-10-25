#include "agentloop/content.h"

#include <algorithm>
#include <optional>

#include "agentloop/budget.h"
#include "contracts/errors.h"
#include "logger/session.h"

namespace apogee::agentloop {
namespace {

constexpr std::size_t kCharactersPerToken = 4;

constexpr std::string_view kSummaryPrompt =
    "Please provide a concise but complete summary of our conversation so far. Capture all "
    "key facts, decisions, and context that would be needed to continue this conversation "
    "meaningfully. Be thorough but succinct.";

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

ContextUsage measure_context(const harness::Harness& harness,
                             const std::vector<harness::ChatMessage>& messages,
                             const std::string& model) {
    ContextUsage usage;
    usage.window = harness.context_window_for_model(model);

    // What would be SENT: earlier turns' tool results as the budget sends
    // them, as stubs (26c). Measuring the transcript instead would compact a
    // conversation whose request is nowhere near full.
    const std::vector<harness::ChatMessage> sent =
        stub_tool_results(messages, current_turn_start(messages)).messages;

    // Ask the provider first: a backend that owns a tokenizer (a local model
    // does) answers exactly, and the 80/90 thresholds are only as good as the
    // number they fire on. `std::nullopt` means "no tokenizer, or not cheaply
    // right now" -- never an error, and never a reason to skip the check.
    harness::ChatRequest probe;
    probe.messages = sent;
    probe.model = model;
    if (const std::optional<std::int64_t> exact = harness.count_prompt_tokens(model, probe)) {
        usage.used_tokens = *exact;
        usage.exact = true;
        return usage;
    }

    // The estimate path. `exact` is carried rather than assumed because a
    // warning that fires at the wrong point is worse than none, and the surface
    // says which number it has.
    const TokenCount count = estimate_prompt_tokens(sent);
    usage.used_tokens = count.tokens;
    usage.exact = !count.estimated;
    return usage;
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

Compaction compact_history(const harness::Harness& harness,
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

    // Nothing to fold without an exchange (55b): an empty conversation, or
    // only the answer the last compaction kept -- summarising that again would
    // stack a second summary and fold nothing.
    if (std::ranges::none_of(conversation, [](const harness::ChatMessage& message) {
            return message.role == harness::Role::User;
        })) {
        return Compaction{
            .outcome = Compaction::Outcome::Nothing, .history = history, .failure = {}};
    }
    const auto failed = [&history](std::string why) {
        return Compaction{
            .outcome = Compaction::Outcome::Failed, .history = history, .failure = std::move(why)};
    };

    harness::ChatRequest request;
    request.model = model;
    // Tool output is condensed before the conversation is (26c): the
    // summariser reads each result as its stub, since the answers that used
    // them carry what mattered -- and a whole file would be most of what it
    // read.
    request.messages = stub_tool_results(conversation, conversation.size()).messages;
    request.messages.push_back(harness::ChatMessage::user(std::string{kSummaryPrompt}));
    // Not a turn of the conversation: on the chat's own local model it runs
    // on a context of its own, and the conversation's cache is untouched
    // (26b). The history it replaces is re-read on the next turn either way.
    request.transient.side_request = true;

    std::string summary;
    try {
        summary = harness.chat(request, cancellation).message.content.plain_text();
    } catch (const harness::HarnessError& e) {
        // A failed compaction degrades to a longer prompt, never to a lost
        // conversation: the history is intact, and the caller says why.
        return failed(e.what());
    }
    if (summary.find_first_not_of(" \t\r\n") == std::string::npos) {
        return failed("the summary came back empty");
    }

    // Opened by the preamble a read of the saved history knows a compaction
    // by (M27): `chats show` puts the summary where it happened.
    std::vector<harness::ChatMessage> compacted = std::move(system_messages);
    compacted.push_back(
        harness::ChatMessage::system(std::string{logger::kCompactionPreamble} + summary));

    // Keep the most recent assistant message so the model has at least one
    // prior turn to reference, rather than a confusing "no prior context".
    for (auto it = conversation.rbegin(); it != conversation.rend(); ++it) {
        if (it->role == harness::Role::Assistant) {
            compacted.push_back(*it);
            break;
        }
    }
    return Compaction{
        .outcome = Compaction::Outcome::Compacted, .history = std::move(compacted), .failure = {}};
}

}  // namespace apogee::agentloop
