#include "agentloop/budget.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "agentloop/question.h"
#include "harness/roles.h"
#include "logger/operational.h"
#include "logger/session.h"

namespace apogee::agentloop {
namespace {

constexpr std::size_t kCharactersPerToken = 4;

/// Why a finished turn's result is a stub, and why an overflowing turn's is
/// -- with nothing to call it again by in a request that offers no tools
/// (57q): an objection's, a final pass's.
constexpr std::string_view kFinishedWhy = "not kept after its turn -- call it again if you need it";
constexpr std::string_view kOverflowWhy =
    "not sent: the context window was full -- call it again, narrower, if you need it";
constexpr std::string_view kOverflowNoToolsWhy = "not sent: the context window was full";

/// What a trim calls an injected source (57q).
[[nodiscard]] std::string_view source_name(InjectedSource source) noexcept {
    switch (source) {
        case InjectedSource::Retrieval:
            return "the retrieved context";
        case InjectedSource::Recall:
            return "the recalled sessions";
        case InjectedSource::Plan:
            return "the plan-mode note";
        case InjectedSource::Review:
            return "the review note";
        case InjectedSource::Standing:
            return "the standing context";
        case InjectedSource::Delegate:
            return "the delegate's notes";
    }
    return "the injected context";
}

/// `bytes` as "60 KB" (rounded up) or "N bytes" below one.
[[nodiscard]] std::string size_of(std::size_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " bytes";
    }
    return std::to_string((bytes + 1023) / 1024) + " KB";
}

/// `call` as the model wrote it, its arguments compact and clipped -- the
/// one-line summary a saved turn lists the call by too (M27).
[[nodiscard]] std::string render_call(const harness::ToolCall& call) {
    return call.name + "(" + logger::tool_call_summary(call) + ")";
}

/// Every call in `messages`, by id, so a result can name the call it answers.
[[nodiscard]] std::map<std::string, harness::ToolCall, std::less<>> calls_by_id(
    const std::vector<harness::ChatMessage>& messages) {
    std::map<std::string, harness::ToolCall, std::less<>> out;
    for (const harness::ChatMessage& message : messages) {
        for (const harness::ToolCall& call : message.tool_calls) {
            out.emplace(call.id, call);
        }
    }
    return out;
}

/// The call a tool result answers, rendered; its name alone when the call is
/// not in `calls`.
[[nodiscard]] std::string call_for(
    const harness::ChatMessage& result,
    const std::map<std::string, harness::ToolCall, std::less<>>& calls) {
    if (const auto it = calls.find(result.tool_call_id); it != calls.end()) {
        return render_call(it->second);
    }
    return (result.name.empty() ? std::string{"a tool"} : result.name) + "(...)";
}

/// Whether `result` is an answer the user gave through `ask_user`.
[[nodiscard]] bool is_answer(const harness::ChatMessage& result,
                             const std::map<std::string, harness::ToolCall, std::less<>>& calls) {
    if (result.name == kQuestionToolName) {
        return true;
    }
    const auto it = calls.find(result.tool_call_id);
    return it != calls.end() && it->second.name == kQuestionToolName;
}

[[nodiscard]] std::int64_t estimate_characters(std::size_t characters) {
    return static_cast<std::int64_t>((characters + kCharactersPerToken - 1) / kCharactersPerToken);
}

/// Tokens a message's framing can add at most -- its role and turn markers
/// in the chat template.
constexpr std::int64_t kFramingTokens = 8;

/// A count `request` cannot exceed: a token covers at least a byte, and each
/// message's framing at most `kFramingTokens`. Under the budget by this, a
/// request fits whatever the tokenizer -- no exact count needed.
[[nodiscard]] std::int64_t token_ceiling(const harness::ChatRequest& request) {
    std::size_t bytes = static_cast<std::size_t>(media_tokens(request));
    for (const harness::ChatMessage& message : request.messages) {
        bytes += message.content.plain_text().size();
        for (const harness::ToolCall& call : message.tool_calls) {
            bytes += call.name.size() + call.arguments.size();
        }
    }
    for (const harness::Tool& tool : request.tools) {
        bytes += tool.name.size() + tool.description.size() + tool.parameters_schema.size();
    }
    return static_cast<std::int64_t>(bytes) +
           (kFramingTokens * static_cast<std::int64_t>(request.messages.size() + 1));
}

/// `content` with an attachment in front of it: its native `parts` first --
/// the picture before the words about it -- then its `prefix` text.
[[nodiscard]] harness::MessageContent with_prefix(const harness::MessageContent& content,
                                                  std::string_view prefix,
                                                  const std::vector<harness::ContentPart>& media) {
    if (content.parts().empty() && media.empty()) {
        return harness::MessageContent{std::string{prefix} + "\n" + content.plain_text()};
    }
    std::vector<harness::ContentPart> parts{media};
    if (!prefix.empty()) {
        parts.push_back(harness::ContentPart::from_text(std::string{prefix}));
    }
    if (content.parts().empty()) {
        parts.push_back(harness::ContentPart::from_text(content.plain_text()));
    } else {
        parts.insert(parts.end(), content.parts().begin(), content.parts().end());
    }
    return harness::MessageContent::from_parts(std::move(parts));
}

/// "N thing(s)", for a trim's phrase.
[[nodiscard]] std::string counted(int count, std::string_view one, std::string_view many) {
    return std::to_string(count) + " " + std::string{count == 1 ? one : many};
}

/// One request assembled against a budget: the stubs, then -- only on
/// overflow -- each source trimmed in reverse priority.
class Assembler {
public:
    Assembler(const TurnBudget& budget, harness::ChatRequest shape,
              const std::vector<harness::ChatMessage>& history,
              const std::vector<harness::ChatMessage>& pinned,
              const std::vector<Injected>& injected, std::size_t at, std::size_t turn_start,
              const std::vector<InlineAttachment>& inlined)
        : budget_{budget},
          request_{std::move(shape)},
          pinned_{pinned},
          extra_{injected},
          available_{budget.budget.available()} {
        // Every call sent with its results (57q): a history an older
        // compaction left holding a call without them is sent without the
        // call, and every index into it moves with what was left out.
        Paired paired = pair_tool_calls(history);
        const auto moved = [&paired](std::size_t index) {
            return paired.position[std::min(index, paired.position.size() - 1)];
        };
        Stubbed stubbed = stub_tool_results(paired.messages, moved(turn_start));
        out_.stubs = stubbed.stubs;
        out_.stubbed_bytes = stubbed.bytes;
        sent_ = std::move(stubbed.messages);
        turn_ = std::min(moved(turn_start), sent_.size());
        position_ = std::min(moved(at), sent_.size());
        for (const InlineAttachment& attachment : inlined) {
            const std::size_t at_message =
                attachment.message < history.size() ? moved(attachment.message) : sent_.size();
            if (at_message >= sent_.size() || sent_[at_message].role != harness::Role::User) {
                out_.inline_dropped.push_back(attachment.name);
                continue;  // its message is gone: compacted, or never sent
            }
            harness::ChatMessage& message = sent_[at_message];
            // Every attachment on one message remembers the message as it
            // was before the first of them, so taking one off rebuilds it
            // from the rest (26q: a folder's map rides beside its text).
            const auto earlier = std::ranges::find(anchors_, at_message, &Anchor::position);
            anchors_.push_back(
                Anchor{.position = at_message,
                       .name = attachment.name,
                       .base = earlier != anchors_.end() ? earlier->base : message.content,
                       .text = attachment.text,
                       .parts = attachment.parts});
            message.content = with_prefix(message.content, attachment.text, attachment.parts);
        }
    }

    [[nodiscard]] Assembly assemble() {
        build();
        if (!budget_.budget.known()) {
            return std::move(out_);
        }
        measure();
        if (!over()) {
            return std::move(out_);
        }
        drop_exchanges();
        stub_this_turn();
        drop_injected();
        strip_inlined();
        if (over()) {
            out_.trims.push_back("still " + std::to_string(out_.tokens.tokens - available_) +
                                 " tokens over the window after the reserve -- sent as it is");
        }
        return std::move(out_);
    }

private:
    [[nodiscard]] bool over() const noexcept {
        return out_.tokens.tokens > available_;
    }

    void build() {
        std::vector<harness::ChatMessage> transient = pinned_;
        for (const Injected& injected : extra_) {
            transient.push_back(injected.message);
        }
        out_.messages = splice_transient(sent_, transient, position_);
        out_.transient_start = transient.empty() ? 0 : position_;
        out_.transient_length = transient.size();
    }

    void measure() {
        request_.messages = out_.messages;
        // A request whose byte count fits needs no exact count -- a local
        // count renders and tokenizes the whole prompt -- and cannot be
        // mistaken: an estimate alone could be, several times over on text
        // that tokenizes densely.
        out_.tokens = token_ceiling(request_) <= available_ ? estimate_request_tokens(request_)
                                                            : budget_.tokens(request_);
    }

    void rebuild() {
        build();
        measure();
    }

    /// Earlier exchanges, oldest first, to none. The leading system prompt
    /// and summary stay; an exchange's calls leave with their results.
    void drop_exchanges() {
        int dropped = 0;
        while (over()) {
            std::size_t first = 0;
            while (first < turn_ && sent_[first].role != harness::Role::User) {
                ++first;
            }
            if (first >= turn_) {
                break;
            }
            std::size_t next = first + 1;
            while (next < turn_ && sent_[next].role != harness::Role::User) {
                ++next;
            }
            const std::size_t removed = next - first;
            sent_.erase(sent_.begin() + static_cast<std::ptrdiff_t>(first),
                        sent_.begin() + static_cast<std::ptrdiff_t>(next));
            turn_ -= removed;
            if (position_ > first) {
                position_ -= std::min(position_ - first, removed);
            }
            // An attachment inlined on a dropped message goes with it.
            std::erase_if(anchors_, [&](const Anchor& anchor) {
                if (anchor.position >= first && anchor.position < next) {
                    out_.inline_dropped.push_back(anchor.name);
                    return true;
                }
                return false;
            });
            for (Anchor& anchor : anchors_) {
                if (anchor.position >= next) {
                    anchor.position -= removed;
                }
            }
            ++dropped;
            rebuild();
        }
        if (dropped > 0) {
            out_.trims.push_back(counted(dropped, "earlier exchange", "earlier exchanges") +
                                 " not sent");
        }
    }

    /// This turn's tool results, oldest first: to their share, then to the
    /// newest alone -- what the model asked for last and has not read yet.
    /// An `ask_user` answer is the user's own words, and never one of them
    /// (57q), as it is never a finished turn's stub.
    void stub_this_turn() {
        const auto calls = calls_by_id(sent_);
        std::vector<std::size_t> results;
        for (std::size_t index = turn_; index < sent_.size(); ++index) {
            if (sent_[index].role == harness::Role::Tool &&
                sent_[index].content.plain_text().size() > kStubbedAbove &&
                !is_answer(sent_[index], calls)) {
                results.push_back(index);
            }
        }
        if (results.size() < 2 || !over()) {
            return;
        }
        // A result's size in the count's own units: its estimate, scaled by
        // how far the whole request's count sits from its estimate.
        request_.messages = out_.messages;
        const std::int64_t estimate = estimate_request_tokens(request_).tokens;
        const double ratio =
            estimate > 0 ? static_cast<double>(out_.tokens.tokens) / static_cast<double>(estimate)
                         : 1.0;
        const auto tokens_of = [&](std::size_t index) {
            return static_cast<std::int64_t>(std::ceil(
                static_cast<double>(estimate_characters(sent_[index].content.plain_text().size())) *
                ratio));
        };
        std::int64_t this_turn = 0;
        for (const std::size_t index : results) {
            this_turn += tokens_of(index);
        }
        // "Call it again" only where something can be called (57q).
        const std::string_view why = request_.tools.empty() ? kOverflowNoToolsWhy : kOverflowWhy;
        int stubbed = 0;
        std::size_t next = 0;
        const auto stub_next = [&] {
            this_turn -= tokens_of(results[next]);
            harness::ChatMessage& message = sent_[results[next++]];
            message.content = tool_result_stub(call_for(message, calls),
                                               message.content.plain_text().size(), why);
            ++stubbed;
            rebuild();
        };
        // To their share...
        const std::int64_t share = budget_.budget.share(BudgetSource::ToolResults);
        while (over() && next + 1 < results.size() && this_turn > share) {
            stub_next();
        }
        // ...and below it only when every source lower in priority is at its
        // own share. Retrieval already is -- it asks for its share -- and so
        // are the inlined attachments: one is inlined only while it fits
        // theirs (26d).
        while (over() && next + 1 < results.size()) {
            stub_next();
        }
        if (stubbed > 0) {
            out_.trims.push_back(
                counted(stubbed, "of this turn's tool results", "of this turn's tool results") +
                " sent as a stub" + (stubbed == 1 ? "" : "s"));
        }
    }

    /// The injected context, from its end, to none -- each source cut named
    /// in the order cut (57q): never "the retrieved context" for a standing
    /// context or a note, nor a count of messages that names nothing.
    void drop_injected() {
        std::vector<InjectedSource> dropped;
        while (over() && !extra_.empty()) {
            dropped.push_back(extra_.back().source);
            extra_.pop_back();
            rebuild();
        }
        std::vector<InjectedSource> named;
        for (const InjectedSource source : dropped) {
            if (std::ranges::find(named, source) == named.end()) {
                named.push_back(source);
            }
        }
        for (const InjectedSource source : named) {
            const auto cut = std::ranges::count(dropped, source);
            const auto kept = std::ranges::count(extra_, source, &Injected::source);
            std::string phrase{source_name(source)};
            if (kept > 0) {
                phrase = "part of " + phrase + " not sent (" + std::to_string(cut) + " of " +
                         std::to_string(cut + kept) + " messages)";
            } else {
                phrase += " not sent";
                if (cut > 1) {
                    phrase += " (" + std::to_string(cut) + " messages)";
                }
            }
            out_.trims.push_back(std::move(phrase));
        }
    }

    /// The inlined attachments still riding their messages: the lowest
    /// priority to trim, and so the last.
    void strip_inlined() {
        int stripped = 0;
        while (over() && !anchors_.empty()) {
            const Anchor anchor = anchors_.front();
            anchors_.erase(anchors_.begin());
            harness::MessageContent content = anchor.base;
            for (const Anchor& rest : anchors_) {
                if (rest.position == anchor.position) {
                    content = with_prefix(content, rest.text, rest.parts);
                }
            }
            sent_[anchor.position].content = std::move(content);
            out_.inline_dropped.push_back(anchor.name);
            ++stripped;
            rebuild();
        }
        if (stripped > 0) {
            out_.trims.push_back(counted(stripped, "inlined attachment", "inlined attachments") +
                                 " not sent");
        }
    }

    struct Anchor {
        std::size_t position = 0;
        std::string name;
        /// The message before any attachment rode it.
        harness::MessageContent base;
        std::string text;
        std::vector<harness::ContentPart> parts;
    };

    const TurnBudget& budget_;
    harness::ChatRequest request_;
    const std::vector<harness::ChatMessage>& pinned_;
    std::vector<Injected> extra_;
    std::int64_t available_;
    std::vector<harness::ChatMessage> sent_;
    std::size_t turn_ = 0;
    std::size_t position_ = 0;
    std::vector<Anchor> anchors_;
    Assembly out_;
};

}  // namespace

std::int64_t ContextBudget::available() const noexcept {
    if (!known()) {
        return 0;
    }
    // A reserve past half the window would leave the question no room: the
    // model stops at its window either way, so the budget holds back half.
    return window - std::min(std::max<std::int64_t>(reserve, 0), window / 2);
}

std::int64_t ContextBudget::share(BudgetSource source) const noexcept {
    double fraction = 0.0;
    switch (source) {
        case BudgetSource::Attachments:
            fraction = shares.attachments;
            break;
        case BudgetSource::Retrieval:
            fraction = shares.retrieval;
            break;
        case BudgetSource::ToolResults:
            fraction = shares.tool_results;
            break;
    }
    return static_cast<std::int64_t>(std::floor(static_cast<double>(available()) * fraction));
}

TokenCount TurnBudget::tokens(const harness::ChatRequest& request) const {
    return count ? count(request) : estimate_request_tokens(request);
}

ContextBudget budget_for(const harness::Harness& harness, const std::string& model,
                         std::optional<std::int64_t> max_tokens) {
    ContextBudget budget;
    budget.window = harness.context_window_for_model(model);
    if (max_tokens.has_value() && *max_tokens > 0) {
        budget.reserve = *max_tokens;
        return budget;
    }
    const std::string backend = harness::resolve_chat_backend(harness.config(), model);
    if (const harness::BackendConfig* entry = harness.config().find_backend(backend);
        entry != nullptr && entry->max_tokens.has_value() && *entry->max_tokens > 0) {
        budget.reserve = *entry->max_tokens;
    }
    return budget;
}

TokenCount estimate_request_tokens(const harness::ChatRequest& request) {
    std::size_t characters = 0;
    for (const harness::ChatMessage& message : request.messages) {
        characters += message.content.plain_text().size() + 1;
        for (const harness::ToolCall& call : message.tool_calls) {
            characters += call.name.size() + call.arguments.size();
        }
    }
    // The definitions ride every request with tools, and on a small window
    // they are not a rounding error.
    for (const harness::Tool& tool : request.tools) {
        characters += tool.name.size() + tool.description.size() + tool.parameters_schema.size();
    }
    return TokenCount{.tokens = estimate_characters(characters) + media_tokens(request),
                      .estimated = true};
}

std::int64_t media_tokens(const harness::ChatRequest& request) {
    constexpr std::int64_t kImageTokens = 1024;
    constexpr std::int64_t kFrameTokens = 256;
    // 16-bit mono at 16 kHz is 32,000 bytes a second; at 25 tokens a second
    // that is a token per 1,280 bytes -- generous for a higher rate.
    constexpr std::int64_t kAudioBytesPerToken = 1280;
    std::int64_t tokens = 0;
    for (const harness::ChatMessage& message : request.messages) {
        for (const harness::ContentPart& part : message.content.parts()) {
            if (part.kind == harness::ContentPart::Kind::ImageUrl) {
                tokens += part.video_frame ? kFrameTokens : kImageTokens;
            } else if (part.kind == harness::ContentPart::Kind::InputAudio) {
                const auto bytes = static_cast<std::int64_t>(part.audio_data.size() / 4 * 3);
                tokens += (bytes + kAudioBytesPerToken - 1) / kAudioBytesPerToken;
            }
        }
    }
    return tokens;
}

TokenCounter token_counter(const harness::Harness& harness, const std::string& model) {
    return [&harness, model](const harness::ChatRequest& request) {
        harness::ChatRequest probe = request;
        probe.model = model;
        if (const std::optional<std::int64_t> exact = harness.count_prompt_tokens(model, probe)) {
            // A tokenizer counts the words; the pictures and sounds are an
            // encoder's, so they keep their allowance (26e).
            return TokenCount{.tokens = *exact + media_tokens(request), .estimated = false};
        }
        return estimate_request_tokens(request);
    };
}

TurnBudget turn_budget(const harness::Harness& harness, const std::string& model,
                       std::optional<std::int64_t> max_tokens) {
    return TurnBudget{.budget = budget_for(harness, model, max_tokens),
                      .count = token_counter(harness, model)};
}

std::size_t current_turn_start(const std::vector<harness::ChatMessage>& messages) {
    for (std::size_t index = messages.size(); index > 0; --index) {
        if (messages[index - 1].role == harness::Role::User) {
            return index - 1;
        }
    }
    return messages.size();
}

std::vector<Injected> injected_as(const std::vector<harness::ChatMessage>& messages,
                                  InjectedSource source) {
    std::vector<Injected> out;
    out.reserve(messages.size());
    for (const harness::ChatMessage& message : messages) {
        out.push_back(Injected{.message = message, .source = source});
    }
    return out;
}

Paired pair_tool_calls(const std::vector<harness::ChatMessage>& messages) {
    Paired out;
    out.position.assign(messages.size() + 1, 0);
    const auto repaired = [&out](const std::string& what) {
        logger::log(logger::Level::Warn, "agentloop", what + " -- left out of the request");
        ++out.repaired;
    };
    std::size_t index = 0;
    while (index < messages.size()) {
        out.position[index] = out.messages.size();
        const harness::ChatMessage& message = messages[index];
        if (message.role == harness::Role::Tool) {
            // Reached only past every call's run: a result nothing asked for.
            repaired("a tool result (" + message.tool_call_id + " " + message.name +
                     ") with no call before it");
            ++index;
            continue;
        }
        if (message.role != harness::Role::Assistant || message.tool_calls.empty()) {
            out.messages.push_back(message);
            ++index;
            continue;
        }
        // The call's results are the tool messages straight after it.
        std::size_t end = index + 1;
        std::set<std::string, std::less<>> answered;
        while (end < messages.size() && messages[end].role == harness::Role::Tool) {
            answered.insert(messages[end].tool_call_id);
            ++end;
        }
        std::set<std::string, std::less<>> asked;
        harness::ChatMessage sent = message;
        std::string unanswered;
        std::erase_if(sent.tool_calls, [&](const harness::ToolCall& call) {
            asked.insert(call.id);
            if (answered.contains(call.id)) {
                return false;
            }
            unanswered += (unanswered.empty() ? "" : ", ") + call.id + " " + call.name;
            return true;
        });
        const std::string text = sent.content.plain_text();
        const bool said_something =
            sent.content.is_rich() || text.find_first_not_of(" \t\r\n") != std::string::npos;
        if (!unanswered.empty()) {
            repaired("a tool call with no result (" + unanswered + ")" +
                     (sent.tool_calls.empty() && !said_something ? ", its empty message"
                                                                 : std::string{}));
        }
        if (!sent.tool_calls.empty() || said_something) {
            out.messages.push_back(std::move(sent));
        }
        for (std::size_t result = index + 1; result < end; ++result) {
            out.position[result] = out.messages.size();
            if (asked.contains(messages[result].tool_call_id)) {
                out.messages.push_back(messages[result]);
            } else {
                repaired("a tool result (" + messages[result].tool_call_id + " " +
                         messages[result].name + ") with no call before it");
            }
        }
        index = end;
    }
    out.position[messages.size()] = out.messages.size();
    return out;
}

std::string tool_result_stub(const std::string& call, std::size_t bytes, std::string_view why) {
    return "[" + call + " returned " + size_of(bytes) + "; " + std::string{why} + ".]";
}

Stubbed stub_tool_results(const std::vector<harness::ChatMessage>& messages, std::size_t before) {
    Stubbed out;
    out.messages = messages;
    const auto calls = calls_by_id(messages);
    const std::size_t end = std::min(before, out.messages.size());
    for (std::size_t index = 0; index < end; ++index) {
        harness::ChatMessage& message = out.messages[index];
        if (message.role != harness::Role::Tool || is_answer(message, calls)) {
            continue;
        }
        const std::size_t bytes = message.content.plain_text().size();
        if (bytes <= kStubbedAbove) {
            continue;
        }
        message.content = tool_result_stub(call_for(message, calls), bytes, kFinishedWhy);
        ++out.stubs;
        out.bytes += bytes;
    }
    return out;
}

Assembly assemble_request(const TurnBudget& budget, const harness::ChatRequest& shape,
                          const std::vector<harness::ChatMessage>& history,
                          const std::vector<harness::ChatMessage>& pinned,
                          const std::vector<Injected>& injected, std::size_t at,
                          std::size_t turn_start, const std::vector<InlineAttachment>& inlined) {
    Assembler assembler{budget, shape, history, pinned, injected, at, turn_start, inlined};
    return assembler.assemble();
}

std::size_t fitting_prefix(const TurnBudget& budget, std::int64_t share, std::size_t items,
                           const std::function<std::string(std::size_t)>& render) {
    if (!budget.budget.known() || items == 0) {
        return items;
    }
    const auto fits = [&](std::size_t count) {
        harness::ChatRequest request;
        request.messages.push_back(harness::ChatMessage::system(render(count)));
        return budget.tokens(request).tokens <= share;
    };
    if (fits(items)) {
        return items;
    }
    // The largest count that fits; 0 when not even one does.
    std::size_t low = 0;
    std::size_t high = items;  // does not fit
    while (high - low > 1) {
        const std::size_t middle = low + ((high - low) / 2);
        if (fits(middle)) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

}  // namespace apogee::agentloop
