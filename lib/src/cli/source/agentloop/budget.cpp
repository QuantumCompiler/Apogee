#include "agentloop/budget.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "agentloop/question.h"
#include "harness/roles.h"

namespace apogee::agentloop {
namespace {

constexpr std::size_t kCharactersPerToken = 4;

/// The most of a call's arguments a stub repeats.
constexpr std::size_t kStubArgumentBytes = 120;

/// Why a finished turn's result is a stub, and why an overflowing turn's is.
constexpr std::string_view kFinishedWhy = "not kept after its turn -- call it again if you need it";
constexpr std::string_view kOverflowWhy =
    "not sent: the context window was full -- call it again, narrower, if you need it";

/// `bytes` as "60 KB" (rounded up) or "N bytes" below one.
[[nodiscard]] std::string size_of(std::size_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " bytes";
    }
    return std::to_string((bytes + 1023) / 1024) + " KB";
}

/// `call` as the model wrote it, its arguments compact and clipped.
[[nodiscard]] std::string render_call(const harness::ToolCall& call) {
    const nlohmann::json parsed = nlohmann::json::parse(call.arguments, nullptr, false);
    std::string arguments = parsed.is_discarded() ? call.arguments : parsed.dump();
    if (arguments.size() > kStubArgumentBytes) {
        std::size_t cut = kStubArgumentBytes;
        while (cut > 0 && (static_cast<unsigned char>(arguments[cut]) & 0xC0U) == 0x80U) {
            --cut;  // never inside a character
        }
        arguments = arguments.substr(0, cut) + "...";
    }
    return call.name + "(" + arguments + ")";
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
    std::size_t bytes = 0;
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
              const std::vector<harness::ChatMessage>& injected, std::size_t at,
              std::size_t turn_start)
        : budget_{budget},
          request_{std::move(shape)},
          pinned_{pinned},
          extra_{injected},
          available_{budget.budget.available()} {
        Stubbed stubbed = stub_tool_results(history, turn_start);
        out_.stubs = stubbed.stubs;
        out_.stubbed_bytes = stubbed.bytes;
        sent_ = std::move(stubbed.messages);
        turn_ = std::min(turn_start, sent_.size());
        position_ = std::min(at, sent_.size());
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
        transient.insert(transient.end(), extra_.begin(), extra_.end());
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
    void stub_this_turn() {
        std::vector<std::size_t> results;
        for (std::size_t index = turn_; index < sent_.size(); ++index) {
            if (sent_[index].role == harness::Role::Tool &&
                sent_[index].content.plain_text().size() > kStubbedAbove) {
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
        const auto calls = calls_by_id(sent_);
        int stubbed = 0;
        std::size_t next = 0;
        const auto stub_next = [&] {
            this_turn -= tokens_of(results[next]);
            harness::ChatMessage& message = sent_[results[next++]];
            message.content = tool_result_stub(call_for(message, calls),
                                               message.content.plain_text().size(), kOverflowWhy);
            ++stubbed;
            rebuild();
        };
        // To their share...
        const std::int64_t share = budget_.budget.share(BudgetSource::ToolResults);
        while (over() && next + 1 < results.size() && this_turn > share) {
            stub_next();
        }
        // ...and below it only when every source lower in priority is at its
        // own share. Retrieval already is -- it asks for its share -- and the
        // attachments (26d) will be trimmed to theirs here, between the two.
        while (over() && next + 1 < results.size()) {
            stub_next();
        }
        if (stubbed > 0) {
            out_.trims.push_back(
                counted(stubbed, "of this turn's tool results", "of this turn's tool results") +
                " sent as a stub" + (stubbed == 1 ? "" : "s"));
        }
    }

    /// The injected context, from its end, to none.
    void drop_injected() {
        int dropped = 0;
        while (over() && !extra_.empty()) {
            extra_.pop_back();
            ++dropped;
            rebuild();
        }
        if (dropped > 0) {
            out_.trims.push_back(extra_.empty()
                                     ? std::string{"the retrieved context not sent"}
                                     : counted(dropped, "injected message", "injected messages") +
                                           " not sent");
        }
    }

    const TurnBudget& budget_;
    harness::ChatRequest request_;
    const std::vector<harness::ChatMessage>& pinned_;
    std::vector<harness::ChatMessage> extra_;
    std::int64_t available_;
    std::vector<harness::ChatMessage> sent_;
    std::size_t turn_ = 0;
    std::size_t position_ = 0;
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
    return TokenCount{.tokens = estimate_characters(characters), .estimated = true};
}

TokenCounter token_counter(const harness::Harness& harness, const std::string& model) {
    return [&harness, model](const harness::ChatRequest& request) {
        harness::ChatRequest probe = request;
        probe.model = model;
        if (const std::optional<std::int64_t> exact = harness.count_prompt_tokens(model, probe)) {
            return TokenCount{.tokens = *exact, .estimated = false};
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
                          const std::vector<harness::ChatMessage>& injected, std::size_t at,
                          std::size_t turn_start) {
    Assembler assembler{budget, shape, history, pinned, injected, at, turn_start};
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
