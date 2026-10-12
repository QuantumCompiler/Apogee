#include "agentloop/reasoning_summary.h"

#include <exception>
#include <utility>

#include "contracts/errors.h"

namespace apogee::agentloop {
namespace {

constexpr std::string_view kPrompt =
    "Below is the latest part of an AI model's private reasoning while it works on a request. "
    "In one short present-tense phrase of under ten words, say what it is doing right now. "
    "Reply with the phrase alone.";

[[nodiscard]] bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// Whether `c` begins a UTF-8 character rather than continuing one.
[[nodiscard]] bool starts_character(char c) {
    return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U;
}

/// `text` without the characters in `strip` at either end.
[[nodiscard]] std::string_view trimmed(std::string_view text, std::string_view strip) {
    while (!text.empty() && strip.find(text.front()) != std::string_view::npos) {
        text.remove_prefix(1);
    }
    while (!text.empty() && strip.find(text.back()) != std::string_view::npos) {
        text.remove_suffix(1);
    }
    return text;
}

/// Whether `text` starts with `prefix`, letters compared without case.
[[nodiscard]] bool starts_with_folded(std::string_view text, std::string_view prefix) {
    if (prefix.size() > text.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const auto fold = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        };
        if (fold(text[i]) != fold(prefix[i])) {
            return false;
        }
    }
    return true;
}

/// `text` without the curly quotes, in their UTF-8 bytes, at either end.
[[nodiscard]] std::string_view unquoted(std::string_view text) {
    for (const std::string_view quote : {"“", "”", "‘", "’"}) {
        if (text.starts_with(quote)) {
            text.remove_prefix(quote.size());
        }
        if (text.ends_with(quote)) {
            text.remove_suffix(quote.size());
        }
    }
    return text;
}

}  // namespace

harness::ChatRequest reasoning_summary_request(const std::string& backend,
                                               std::string_view reasoning) {
    harness::ChatRequest request;
    request.model = backend;
    request.messages.push_back(harness::ChatMessage::user(
        std::string{kPrompt} + "\n\nReasoning:\n" + std::string{reasoning}));
    request.max_tokens = kSummaryTokens;
    request.temperature = 0.0;
    // Not a turn of the conversation: a local utility model runs it on its
    // own context, and thinks nothing first.
    request.transient.side_request = true;
    request.thinking.mode = harness::ThinkingMode::Off;
    return request;
}

std::string summary_line(std::string_view reply) {
    // The first line with words in it.
    std::string_view line;
    while (!reply.empty()) {
        const std::size_t end = reply.find('\n');
        line = trimmed(reply.substr(0, end), " \t\r");
        if (!line.empty()) {
            break;
        }
        reply = end == std::string_view::npos ? std::string_view{} : reply.substr(end + 1);
    }
    // A list mark, quotes, and a closing full stop: none of them the gist.
    for (const std::string_view mark : {"- ", "* ", "• "}) {
        if (line.starts_with(mark)) {
            line.remove_prefix(mark.size());
        }
    }
    line = unquoted(trimmed(line, " \t\"'`"));
    line = trimmed(line, " \t\"'`.");
    // A subject the line need not carry: "It is parsing the problem" says
    // "parsing the problem" (a small utility model's habit, found on real
    // weights).
    for (const std::string_view subject :
         {"It is ", "It's ", "The model is ", "The AI is ", "The assistant is ", "I am ", "I'm ",
          "He is ", "She is ", "They are "}) {
        if (line.size() > subject.size() && starts_with_folded(line, subject)) {
            line.remove_prefix(subject.size());
            break;
        }
    }
    std::string out;
    for (const char c : line) {
        if (is_space(c)) {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
        } else {
            out += c;
        }
    }
    // A line opens with a capital, as a status does.
    if (!out.empty() && out.front() >= 'a' && out.front() <= 'z') {
        out.front() = static_cast<char>(out.front() - 'a' + 'A');
    }
    // Cut at a character, never inside one.
    std::size_t characters = 0;
    for (std::size_t at = 0; at < out.size(); ++at) {
        if (!starts_character(out[at])) {
            continue;
        }
        if (characters == kSummaryLine) {
            return std::string{trimmed(std::string_view{out}.substr(0, at), " ")} + "…";
        }
        ++characters;
    }
    return out;
}

SummaryRoute route_summary(const harness::Harness& harness, std::string_view chat_model,
                           std::string_view summary_model) {
    SummaryRoute route;
    if (harness.model_behavior_for(chat_model).reasoning_summarized) {
        route.vendor = true;
    } else if (summary_model.empty()) {
        route.why_not = "no utility model is set to summarise it";
    } else if (summary_model == chat_model) {
        route.why_not =
            "the utility model is the chat model, which cannot summarise while it thinks";
    } else if (harness.generation_is_metered(summary_model)) {
        route.why_not = "the utility model, " + std::string{summary_model} +
                        ", is billed per call, and a summary is not worth a charge";
    } else {
        route.model = std::string{summary_model};
    }
    return route;
}

ReasoningSummarizer::ReasoningSummarizer(const harness::Harness& harness, std::string backend,
                                         harness::CancellationToken cancellation, Clock clock)
    : harness_{harness},
      backend_{std::move(backend)},
      cancellation_{std::move(cancellation)},
      clock_{clock ? std::move(clock) : Clock{[] { return std::chrono::steady_clock::now(); }}} {}

std::optional<std::string> ReasoningSummarizer::feed(std::string_view chunk) {
    tail_ += chunk;
    total_ += chunk.size();
    if (tail_.size() > kSummaryInput) {
        std::size_t cut = tail_.size() - kSummaryInput;
        while (cut < tail_.size() && !starts_character(tail_[cut])) {
            ++cut;
        }
        tail_.erase(0, cut);
    }
    if (failed_ || stopped_ || chunk.empty()) {
        return std::nullopt;
    }
    const auto now = clock_();
    if (!began_.has_value()) {
        began_ = now;
    }
    const bool due =
        last_.has_value() ? now - *last_ >= kSummaryEvery : now - *began_ >= kSummaryFirst;
    if (!due || total_ - summarized_ < kSummaryNewReasoning) {
        return std::nullopt;
    }
    std::string reply;
    try {
        reply = harness_.chat(reasoning_summary_request(backend_, tail_), cancellation_)
                    .message.content.plain_text();
    } catch (const harness::CancelledError&) {
        // The turn is ending: the chat model's own stream says so. Thrown
        // from here it would cross a backend's callback.
        stopped_ = true;
        return std::nullopt;
    } catch (const std::exception&) {
        failed_ = true;
        return std::nullopt;
    }
    summarized_ = total_;
    last_ = clock_();
    std::string line = summary_line(reply);
    if (line.empty() || line == line_) {
        return std::nullopt;
    }
    line_ = line;
    return line;
}

}  // namespace apogee::agentloop
