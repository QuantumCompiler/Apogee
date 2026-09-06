#include "backends/claude_cli_events.h"

#include <nlohmann/json.hpp>

#include "backends/jsonl_framer.h"

namespace apogee::backends::claude_cli {
namespace {

/// A `stream_event`'s inner Anthropic event, which is where deltas live.
[[nodiscard]] std::optional<CliEvent> parse_stream_event(const nlohmann::json& root) {
    const auto event = root.find("event");
    if (event == root.end() || !event->is_object()) {
        return std::nullopt;
    }
    // Only content_block_delta carries content. message_start,
    // content_block_start/stop, message_delta and message_stop are framing --
    // see the header on why those are dropped rather than switched over.
    if (event->value("type", std::string{}) != "content_block_delta") {
        return std::nullopt;
    }

    const auto delta = event->find("delta");
    if (delta == event->end() || !delta->is_object()) {
        return std::nullopt;
    }

    const std::string kind = delta->value("type", std::string{});
    if (kind == "text_delta") {
        return CliEvent{TextDelta{delta->value("text", std::string{})}};
    }
    if (kind == "thinking_delta") {
        // May legitimately be EMPTY on a redacted-thinking model. Emitted
        // anyway: the provider needs to know reasoning is happening, and the
        // empty-payload case is exactly what the redacted fixture pins.
        return CliEvent{ThinkingDelta{delta->value("thinking", std::string{})}};
    }
    // signature_delta carries no payload; input_json_delta streams tool-call
    // arguments, which nothing consumes yet.
    return std::nullopt;
}

[[nodiscard]] std::optional<CliEvent> parse_system(const nlohmann::json& root) {
    const std::string subtype = root.value("subtype", std::string{});

    if (subtype == "init") {
        Notice notice;
        notice.kind = "init";
        notice.detail = root.value("model", std::string{});
        return CliEvent{std::move(notice)};
    }
    if (subtype == "thinking_tokens") {
        return CliEvent{ThinkingTokens{root.value("estimated_tokens", std::int64_t{0})}};
    }
    if (subtype == "api_retry") {
        Notice notice;
        notice.kind = "api_retry";
        notice.detail = root.value("message", std::string{});
        return CliEvent{std::move(notice)};
    }
    // status and post_turn_summary are periodic accounting, not content.
    return std::nullopt;
}

/// A `user` wire message carries tool results being fed back to the model.
[[nodiscard]] std::optional<CliEvent> parse_user(const nlohmann::json& root) {
    const auto message = root.find("message");
    if (message == root.end() || !message->is_object()) {
        return std::nullopt;
    }
    const auto content = message->find("content");
    if (content == message->end() || !content->is_array()) {
        return std::nullopt;
    }
    for (const nlohmann::json& block : *content) {
        if (!block.is_object() || block.value("type", std::string{}) != "tool_result") {
            continue;
        }
        ToolOutcome outcome;
        outcome.id = block.value("tool_use_id", std::string{});
        outcome.is_error = block.value("is_error", false);
        const auto inner = block.find("content");
        if (inner != block.end()) {
            outcome.content = inner->is_string() ? inner->get<std::string>() : inner->dump();
        }
        return CliEvent{std::move(outcome)};
    }
    return std::nullopt;
}

/// An `assistant` message is normally already streamed, but a tool_use block
/// is the one thing the delta stream does not announce in a usable form.
[[nodiscard]] std::optional<CliEvent> parse_assistant(const nlohmann::json& root) {
    const auto message = root.find("message");
    if (message == root.end() || !message->is_object()) {
        return std::nullopt;
    }
    const auto content = message->find("content");
    if (content == message->end() || !content->is_array()) {
        return std::nullopt;
    }
    for (const nlohmann::json& block : *content) {
        if (!block.is_object() || block.value("type", std::string{}) != "tool_use") {
            continue;
        }
        return CliEvent{ToolUseStart{.id = block.value("id", std::string{}),
                                     .name = block.value("name", std::string{})}};
    }
    return std::nullopt;
}

[[nodiscard]] CliEvent parse_result(const nlohmann::json& root) {
    TurnComplete complete;
    complete.session_id = root.value("session_id", std::string{});
    complete.num_turns = root.value("num_turns", std::int64_t{0});
    complete.cost_usd = root.value("total_cost_usd", 0.0);

    const auto result = root.find("result");
    if (result != root.end()) {
        complete.final_text = result->is_string() ? result->get<std::string>() : result->dump();
    }

    // The schema-conforming value, when --json-schema was set. Kept as raw
    // JSON: the caller hands it to whoever asked for the schema, and parsing
    // it here would only create a place to lose fidelity.
    const auto structured = root.find("structured_output");
    if (structured != root.end() && !structured->is_null()) {
        complete.structured_output =
            structured->is_string() ? structured->get<std::string>() : structured->dump();
    }

    const auto usage = root.find("usage");
    if (usage != root.end() && usage->is_object()) {
        complete.input_tokens = usage->value("input_tokens", std::int64_t{0});
        complete.output_tokens = usage->value("output_tokens", std::int64_t{0});
    }

    const std::string subtype = root.value("subtype", std::string{});
    // `is_error` is authoritative when present; otherwise any subtype that is
    // not "success" is a failure -- a new error subtype must not read as one.
    complete.is_error = root.value("is_error", !subtype.empty() && subtype != "success");
    if (complete.is_error) {
        complete.error_subtype = subtype;
    }
    return CliEvent{std::move(complete)};
}

}  // namespace

std::optional<CliEvent> parse_line(std::string_view line) {
    if (!looks_like_json_object(line)) {
        return std::nullopt;  // a banner, a warning, a blank -- not our business
    }

    // Non-throwing: a half-written object during a crash costs one event, not
    // the turn.
    const nlohmann::json root = nlohmann::json::parse(line, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }

    const std::string type = root.value("type", std::string{});
    if (type == "stream_event") {
        return parse_stream_event(root);
    }
    if (type == "system") {
        return parse_system(root);
    }
    if (type == "result") {
        return parse_result(root);
    }
    if (type == "user") {
        return parse_user(root);
    }
    if (type == "assistant") {
        return parse_assistant(root);
    }
    if (type == "rate_limit_event") {
        Notice notice;
        notice.kind = "rate_limit";
        notice.detail = root.value("status", std::string{});
        return CliEvent{std::move(notice)};
    }
    return std::nullopt;  // unknown type: dropped, so a CLI upgrade is not an outage
}

std::vector<CliEvent> parse_stream(std::string_view bytes) {
    std::vector<CliEvent> events;
    JsonlFramer framer;
    const auto handle = [&events](std::string_view line) {
        if (std::optional<CliEvent> event = parse_line(line)) {
            events.push_back(std::move(*event));
        }
    };
    framer.feed(bytes, handle);
    framer.flush(handle);
    return events;
}

std::string describe(const CliEvent& event) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, TextDelta>) {
                return "text:" + value.text;
            } else if constexpr (std::is_same_v<T, ThinkingDelta>) {
                return "thinking:" + value.text;
            } else if constexpr (std::is_same_v<T, ThinkingTokens>) {
                return "thinking_tokens:" + std::to_string(value.estimated);
            } else if constexpr (std::is_same_v<T, ToolUseStart>) {
                return "tool_use:" + value.name;
            } else if constexpr (std::is_same_v<T, ToolOutcome>) {
                return std::string{"tool_result:"} + (value.is_error ? "error" : "ok");
            } else if constexpr (std::is_same_v<T, TurnComplete>) {
                return "result:" + (value.is_error ? value.error_subtype : std::string{"success"});
            } else {
                return "notice:" + value.kind;
            }
        },
        event);
}

std::string user_message_line(std::string_view text) {
    nlohmann::json line;
    line["type"] = "user";
    line["message"]["role"] = "user";
    line["message"]["content"] =
        std::vector<nlohmann::json>{nlohmann::json{{"type", "text"}, {"text", std::string{text}}}};
    return line.dump();
}

}  // namespace apogee::backends::claude_cli
