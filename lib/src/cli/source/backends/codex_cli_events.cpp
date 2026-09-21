#include "backends/codex_cli_events.h"

#include <nlohmann/json.hpp>

#include "backends/jsonl_framer.h"

namespace apogee::backends::codex_cli {
namespace {

/// `item.completed` carries the actual content, typed by an inner `item.type`.
[[nodiscard]] std::optional<CliEvent> parse_item(const nlohmann::json& root) {
    const auto item = root.find("item");
    if (item == root.end() || !item->is_object()) {
        return std::nullopt;
    }

    const std::string kind = item->value("type", std::string{});

    if (kind == "agent_message") {
        // The whole answer, in one event. With --output-schema set, this text
        // IS the conforming JSON -- this CLI puts it here rather than in a
        // separate field the way Claude's does.
        return CliEvent{TextDelta{item->value("text", std::string{})}};
    }

    if (kind == "command_execution") {
        // codex is an AGENT: it runs shell commands while answering. Surfacing
        // them as tool activity is honest about what the backend is doing on
        // the user's machine, and is why the sandbox is pinned read-only.
        return CliEvent{ToolUseStart{item->value("id", std::string{}), "shell"}};
    }

    // reasoning items are not emitted by this CLI -- reasoning is counted in
    // usage and never surfaced as content. Anything else is dropped.
    return std::nullopt;
}

[[nodiscard]] CliEvent parse_turn_completed(const nlohmann::json& root) {
    TurnComplete complete;

    const auto usage = root.find("usage");
    if (usage != root.end() && usage->is_object()) {
        complete.input_tokens = usage->value("input_tokens", std::int64_t{0});
        complete.output_tokens = usage->value("output_tokens", std::int64_t{0});
    }
    return CliEvent{std::move(complete)};
}

}  // namespace

std::optional<CliEvent> parse_line(std::string_view line) {
    if (!looks_like_json_object(line)) {
        return std::nullopt;
    }

    const nlohmann::json root = nlohmann::json::parse(line, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }

    const std::string type = root.value("type", std::string{});

    if (type == "thread.started") {
        // The resume handle. Carried as a Notice so the provider can capture
        // it without the event union growing a codex-shaped field.
        Notice notice;
        notice.kind = "thread";
        notice.detail = root.value("thread_id", std::string{});
        return CliEvent{std::move(notice)};
    }
    if (type == "item.completed") {
        return parse_item(root);
    }
    if (type == "turn.completed") {
        return parse_turn_completed(root);
    }
    if (type == "turn.failed" || type == "error") {
        TurnComplete complete;
        complete.is_error = true;
        const auto error = root.find("error");
        complete.error_subtype = error != root.end() && error->is_object()
                                     ? error->value("message", std::string{"failed"})
                                     : root.value("message", std::string{"failed"});
        return CliEvent{std::move(complete)};
    }

    // turn.started, item.started, and anything a later release invents.
    return std::nullopt;
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
            } else if constexpr (std::is_same_v<T, ToolUseStart>) {
                return "tool:" + value.name;
            } else if constexpr (std::is_same_v<T, TurnComplete>) {
                return "turn:" + (value.is_error ? value.error_subtype : std::string{"ok"});
            } else if constexpr (std::is_same_v<T, Notice>) {
                return "notice:" + value.kind;
            } else {
                return "other";
            }
        },
        event);
}

}  // namespace apogee::backends::codex_cli
