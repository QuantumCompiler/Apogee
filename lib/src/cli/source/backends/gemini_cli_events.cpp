#include "backends/gemini_cli_events.h"

#include <nlohmann/json.hpp>

#include "backends/jsonl_framer.h"

namespace apogee::backends::gemini_cli {
namespace {

/// Picks the model that produced the answer out of `stats.models`.
///
/// The CLI routes one turn across several models and reports a per-model
/// breakdown. The one with the most output tokens is the one that wrote what
/// the user is reading; a tie or an empty map yields "", which the provider
/// then falls back to its configured model for.
[[nodiscard]] std::string dominant_model(const nlohmann::json& models) {
    std::string best;
    std::int64_t best_output = -1;
    for (const auto& [name, entry] : models.items()) {
        if (!entry.is_object()) {
            continue;
        }
        const std::int64_t output = entry.value("output_tokens", std::int64_t{0});
        if (output > best_output) {
            best_output = output;
            best = name;
        }
    }
    return best_output > 0 ? best : std::string{};
}

[[nodiscard]] CliEvent parse_result(const nlohmann::json& root) {
    TurnComplete complete;

    // "success" is the only value seen; anything else is treated as failure
    // rather than allowlisted, so a new failure spelling fails loudly.
    const std::string status = root.value("status", std::string{"success"});
    if (status != "success") {
        complete.is_error = true;
        complete.error_subtype = status;
    }

    const auto stats = root.find("stats");
    if (stats != root.end() && stats->is_object()) {
        complete.input_tokens = stats->value("input_tokens", std::int64_t{0});
        complete.output_tokens = stats->value("output_tokens", std::int64_t{0});

        const auto models = stats->find("models");
        if (models != stats->end() && models->is_object()) {
            complete.model = dominant_model(*models);
        }
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

    if (type == "init") {
        // The session handle. Apogee CHOSE this id via --session-id, so this is
        // a confirmation rather than a capture -- but it is carried anyway, so
        // a mismatch is detectable instead of assumed away.
        Notice notice;
        notice.kind = "session";
        notice.detail = root.value("session_id", std::string{});
        return CliEvent{std::move(notice)};
    }

    if (type == "message") {
        // The CLI echoes the prompt back as a user message. Keeping it would
        // prefix every answer with its own question.
        if (root.value("role", std::string{}) != "assistant") {
            return std::nullopt;
        }
        const std::string content = root.value("content", std::string{});
        if (content.empty()) {
            return std::nullopt;
        }
        return CliEvent{TextDelta{content}};
    }

    if (type == "tool_use") {
        // This CLI is an agent and runs its own tools while answering, which is
        // why the argv pins --approval-mode plan. Surfacing them is honest
        // about what the child is doing on the user's machine.
        return CliEvent{ToolUseStart{.id = root.value("tool_id", std::string{}),
                                     .name = root.value("tool_name", std::string{})}};
    }

    if (type == "tool_result") {
        ToolOutcome outcome;
        outcome.id = root.value("tool_id", std::string{});
        outcome.is_error = root.value("status", std::string{"success"}) != "success";
        outcome.content = root.value("output", std::string{});
        return CliEvent{std::move(outcome)};
    }

    if (type == "result") {
        return parse_result(root);
    }

    if (type == "error") {
        TurnComplete complete;
        complete.is_error = true;
        complete.error_subtype = root.value("message", std::string{"failed"});
        return CliEvent{std::move(complete)};
    }

    // Anything a later release invents.
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
            } else if constexpr (std::is_same_v<T, ToolOutcome>) {
                return std::string{"tool_result:"} + (value.is_error ? "error" : "ok");
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

}  // namespace apogee::backends::gemini_cli
