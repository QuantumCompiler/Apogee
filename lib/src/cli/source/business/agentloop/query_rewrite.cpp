#include "agentloop/query_rewrite.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <string_view>

#include "contracts/errors.h"

namespace apogee::agentloop {
namespace {

/// Earlier messages shown to the rewriter: enough for what "it" and "the
/// second one" refer to, not the whole conversation.
constexpr std::size_t kContextMessages = 6;

/// Bytes of each shown message.
constexpr std::size_t kMessageBytes = 400;

/// The rewrite's budget: a query is a line.
constexpr std::int64_t kRewriteTokens = 64;

/// A query longer than this is an answer, not a query, and is not used.
constexpr std::size_t kLongestQuery = 400;

constexpr std::string_view kPrompt =
    "Rewrite the last question below as one standalone search query: fill in whatever it "
    "refers to from the conversation, so it can be understood with nothing before it. Reply "
    "with the query alone, on one line.";

/// `text` cut at `limit` bytes, on a character boundary, marked when cut.
[[nodiscard]] std::string clipped(std::string_view text, std::size_t limit) {
    if (text.size() <= limit) {
        return std::string{text};
    }
    std::size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return std::string{text.substr(0, cut)} + " ...";
}

/// The quotes, backticks and emphasis a model sometimes wraps a query in.
constexpr std::string_view kTrim = " \t\r\"'`*";

/// Whether `line` leads in to the query rather than being it -- `Sure! Here
/// is the standalone search query:`, a bare `Query:` -- by ending in a colon,
/// emphasis aside (57r).
[[nodiscard]] bool lead_in(std::string_view line) {
    const std::size_t last = line.find_last_not_of(" \t\r*");
    return last != std::string_view::npos && line[last] == ':';
}

/// `line` without the label a model puts before a query, `Query:` or
/// `Search query:` in any case, emphasis before it aside; `line` itself when
/// it has none.
[[nodiscard]] std::string_view without_label(std::string_view line) {
    const std::size_t first = line.find_first_not_of(kTrim);
    if (first == std::string_view::npos) {
        return line;
    }
    const std::string_view text = line.substr(first);
    for (const std::string_view label :
         {std::string_view{"search query:"}, std::string_view{"query:"}}) {
        if (text.size() >= label.size() &&
            std::ranges::equal(text.substr(0, label.size()), label, [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == b;
            })) {
            return text.substr(label.size());
        }
    }
    return line;
}

/// The reply's query: its first line with anything on it that is not a
/// lead-in (57r), without its label and wrapping. Later lines are ignored; a
/// reply of lead-ins alone has none.
[[nodiscard]] std::string query_of(std::string_view reply) {
    std::string_view line;
    while (!reply.empty()) {
        const std::size_t end = reply.find('\n');
        const std::string_view candidate = reply.substr(0, end);
        reply = end == std::string_view::npos ? std::string_view{} : reply.substr(end + 1);
        if (candidate.find_first_not_of(" \t\r") == std::string_view::npos || lead_in(candidate)) {
            continue;
        }
        line = without_label(candidate);
        break;
    }
    const std::size_t begin = line.find_first_not_of(kTrim);
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = line.find_last_not_of(kTrim);
    return std::string{line.substr(begin, end - begin + 1)};
}

}  // namespace

bool has_earlier_turn(const std::vector<harness::ChatMessage>& history) {
    return std::count_if(history.begin(), history.end(), [](const harness::ChatMessage& message) {
               return message.role == harness::Role::User;
           }) >= 2;
}

harness::ChatRequest rewrite_request(const std::string& backend,
                                     const std::vector<harness::ChatMessage>& history,
                                     const std::string& question) {
    // The conversation before the question, newest last: the question itself
    // is the last user message, and is given on its own line below.
    std::vector<const harness::ChatMessage*> shown;
    bool skipped_question = false;
    for (auto it = history.rbegin(); it != history.rend() && shown.size() < kContextMessages;
         ++it) {
        if (it->role != harness::Role::User && it->role != harness::Role::Assistant) {
            continue;  // system prompts and tool results say nothing a query needs
        }
        if (!skipped_question && it->role == harness::Role::User) {
            skipped_question = true;
            continue;
        }
        if (it->content.plain_text().empty()) {
            continue;  // an assistant turn that only called tools
        }
        shown.push_back(&*it);
    }
    std::string conversation;
    for (auto it = shown.rbegin(); it != shown.rend(); ++it) {
        conversation += (*it)->role == harness::Role::User ? "User: " : "Assistant: ";
        conversation += clipped((*it)->content.plain_text(), kMessageBytes);
        conversation += "\n";
    }

    harness::ChatRequest request;
    request.model = backend;
    request.messages.push_back(
        harness::ChatMessage::user(std::string{kPrompt} + "\n\nConversation:\n" + conversation +
                                   "\nLast question: " + clipped(question, kMessageBytes)));
    request.max_tokens = kRewriteTokens;
    request.temperature = 0.0;
    // Not a turn of the conversation: a local model runs it on its own
    // context, and the chat's cache is untouched.
    request.transient.side_request = true;
    // A reasoning model would think for hundreds of tokens before one line.
    request.thinking.mode = harness::ThinkingMode::Off;
    return request;
}

QueryRewrite rewrite_query(const harness::Harness& harness, const std::string& backend,
                           const std::vector<harness::ChatMessage>& history,
                           const std::string& question,
                           const harness::CancellationToken& cancellation) {
    QueryRewrite out;
    out.query = question;
    if (backend.empty() || !has_earlier_turn(history)) {
        return out;
    }
    std::string reply;
    try {
        reply = harness.chat(rewrite_request(backend, history, question), cancellation)
                    .message.content.plain_text();
    } catch (const harness::CancelledError&) {
        throw;
    } catch (const std::exception& e) {
        out.note = std::string{"the search query could not be rewritten ("} + e.what() +
                   ") -- searching with the question as asked";
        return out;
    }
    const std::string query = query_of(reply);
    if (query.empty() || query.size() > kLongestQuery) {
        out.note = "the rewrite was not a query -- searching with the question as asked";
        return out;
    }
    out.query = query;
    out.rewritten = true;
    return out;
}

}  // namespace apogee::agentloop
