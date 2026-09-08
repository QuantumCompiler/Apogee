#include "backends/native_tool_calls.h"

#include <algorithm>
#include <cctype>

namespace apogee::backends {
namespace {

/// What separates the function name from the arguments hint.
constexpr std::string_view kConstrain = "<|constrain|>";
/// What introduces the argument object.
constexpr std::string_view kMessage = "<|message|>";
/// The namespace every declared function is rendered under.
constexpr std::string_view kNamespace = "functions.";

[[nodiscard]] bool is_space_byte(unsigned char byte) noexcept {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

/// Whether `name` could plausibly be a tool name.
///
/// Prose containing a stray marker must not dispatch, so a garbled span is
/// rejected here rather than sent to the registry to fail by name.
[[nodiscard]] bool plausible_tool_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128) {
        return false;
    }
    return std::ranges::all_of(name, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return std::isalnum(byte) != 0 || c == '_' || c == '-';
    });
}

[[nodiscard]] std::size_t skip_spaces(std::string_view text, std::size_t at) noexcept {
    while (at < text.size() && is_space_byte(static_cast<unsigned char>(text[at]))) {
        ++at;
    }
    return at;
}

/// The length of the balanced JSON object starting at `text[0]`, or 0.
///
/// String-aware, so a brace inside a value -- a shell command, a path, a
/// snippet of code -- does not end the object early.
[[nodiscard]] std::size_t json_object_length(std::string_view text) noexcept {
    if (text.empty() || text.front() != '{') {
        return 0;
    }
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char c = text[index];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return index + 1;
            }
        }
    }
    // Unbalanced: a truncated call. Reporting 0 sends it to the safety net,
    // which shows the text -- far better than dispatching half an argument.
    return 0;
}

/// Parses one call body starting just past an opener. Returns the bytes
/// consumed, or 0 when this span is not a well-formed call.
[[nodiscard]] std::size_t parse_one(std::string_view body, harness::ToolCall& out) {
    // The function name runs to the first whitespace or marker character.
    std::size_t end = 0;
    while (end < body.size() && !is_space_byte(static_cast<unsigned char>(body[end])) &&
           body[end] != '<') {
        ++end;
    }
    std::string_view name = body.substr(0, end);
    if (name.starts_with(kNamespace)) {
        name.remove_prefix(kNamespace.size());
    }
    if (!plausible_tool_name(name)) {
        return 0;
    }

    std::size_t at = skip_spaces(body, end);

    // `<|constrain|>FORMAT` is a hint about the argument encoding. Optional
    // because it is the model's choice to emit it, and a call without one is
    // still a call.
    if (body.substr(at).starts_with(kConstrain)) {
        at += kConstrain.size();
        while (at < body.size() && !is_space_byte(static_cast<unsigned char>(body[at])) &&
               body[at] != '<') {
            ++at;
        }
        at = skip_spaces(body, at);
    }

    if (!body.substr(at).starts_with(kMessage)) {
        return 0;
    }
    at += kMessage.size();

    const std::size_t width = json_object_length(body.substr(at));
    if (width == 0) {
        return 0;
    }

    out.name = std::string{name};
    out.arguments = std::string{body.substr(at, width)};
    return at + width;
}

}  // namespace

const std::vector<std::string>& tool_call_openers() {
    // One entry, and that is a characterization result rather than an
    // oversight: gpt-oss is the only family whose native calls have been seen
    // here. A second entry belongs to whichever family is run next.
    static const std::vector<std::string> openers{"<|channel|>commentary to="};
    return openers;
}

std::vector<harness::ToolCall> parse_native_tool_calls(std::string_view text) {
    std::vector<harness::ToolCall> calls;

    for (std::size_t at = 0; at < text.size();) {
        // The earliest opener at or after `at`.
        std::size_t start = std::string_view::npos;
        std::size_t opener_width = 0;
        for (const std::string& opener : tool_call_openers()) {
            const std::size_t found = text.find(opener, at);
            if (found != std::string_view::npos && found < start) {
                start = found;
                opener_width = opener.size();
            }
        }
        if (start == std::string_view::npos) {
            break;
        }

        const std::size_t body_at = start + opener_width;
        std::string_view body = text.substr(body_at);

        // Bound the body at the NEXT opener. The close marker cannot be the
        // sole boundary -- it is an end-of-generation token and so never
        // reaches the text at all -- and without this bound one truncated call
        // would swallow the well-formed call that follows it.
        std::size_t next = std::string_view::npos;
        for (const std::string& opener : tool_call_openers()) {
            next = std::min(next, body.find(opener));
        }
        if (next != std::string_view::npos) {
            body = body.substr(0, next);
        }

        harness::ToolCall call;
        const std::size_t consumed = parse_one(body, call);
        if (consumed == 0) {
            // Not well-formed. Step past this opener rather than abandoning the
            // scan, so one malformed call cannot hide a later valid one.
            at = body_at;
            continue;
        }
        calls.push_back(std::move(call));
        at = body_at + consumed;
    }

    return calls;
}

std::string ToolCallGate::write(std::string_view chunk) {
    if (!enabled_) {
        return std::string{chunk};
    }
    if (holding_) {
        held_ += chunk;
        return {};
    }

    pending_ += chunk;

    // The earliest complete opener.
    std::size_t at = std::string::npos;
    for (const std::string& opener : tool_call_openers()) {
        at = std::min(at, pending_.find(opener));
    }
    if (at != std::string::npos) {
        std::string out = pending_.substr(0, at);
        held_ = pending_.substr(at);
        pending_.clear();
        holding_ = true;
        return out;
    }

    // No complete opener, but a tail of `pending_` may still be growing into
    // one -- gpt-oss tokenizes `commentary` as `comment` then `ary`, so an
    // opener split across reads is the normal case rather than an edge one.
    std::size_t keep = 0;
    for (const std::string& opener : tool_call_openers()) {
        const std::size_t most = std::min(opener.size() - 1, pending_.size());
        for (std::size_t take = most; take > keep; --take) {
            if (std::string_view{pending_}.substr(pending_.size() - take) ==
                std::string_view{opener}.substr(0, take)) {
                keep = take;
                break;
            }
        }
    }

    std::string out = pending_.substr(0, pending_.size() - keep);
    pending_.erase(0, pending_.size() - keep);
    return out;
}

std::string ToolCallGate::flush() {
    if (!enabled_) {
        return {};
    }
    if (!holding_) {
        // A partial opener at end of stream was never an opener.
        std::string out = std::move(pending_);
        pending_.clear();
        return out;
    }

    calls_ = parse_native_tool_calls(held_);
    if (!calls_.empty()) {
        held_.clear();
        return {};
    }
    // The safety net: it looked like a call and was not one, so show it.
    std::string out = std::move(held_);
    held_.clear();
    holding_ = false;
    return out;
}

}  // namespace apogee::backends
