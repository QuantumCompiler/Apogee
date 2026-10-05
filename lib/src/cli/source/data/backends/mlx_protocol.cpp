#include "backends/mlx_protocol.h"

#include <nlohmann/json.hpp>

namespace apogee::backends::mlx {
namespace {

/// A JSON text as a value where it parses, else as the string it is: tool
/// arguments and parameter schemas cross as text in the IR, and a template
/// reads them as objects (`arguments | tojson`), so an object is what it gets
/// whenever there is one.
[[nodiscard]] nlohmann::json json_or_text(const std::string& text) {
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded()) {
        return text;
    }
    return parsed;
}

/// One IR message in the shape a Hugging Face chat template takes.
[[nodiscard]] nlohmann::json template_message(const harness::ChatMessage& message) {
    nlohmann::json out{{"role", std::string{harness::to_string(message.role)}},
                       {"content", message.content.plain_text()}};
    if (message.role == harness::Role::Assistant && !message.tool_calls.empty()) {
        nlohmann::json calls = nlohmann::json::array();
        for (const harness::ToolCall& call : message.tool_calls) {
            calls.push_back(
                {{"id", call.id},
                 {"type", "function"},
                 {"function", {{"name", call.name}, {"arguments", json_or_text(call.arguments)}}}});
        }
        out["tool_calls"] = std::move(calls);
    }
    if (message.role == harness::Role::Tool) {
        out["tool_call_id"] = message.tool_call_id;
        out["name"] = message.name;
    }
    return out;
}

[[nodiscard]] nlohmann::json template_tool(const harness::Tool& tool) {
    nlohmann::json parameters = json_or_text(tool.parameters_schema);
    if (!parameters.is_object()) {
        parameters = nlohmann::json::object();
    }
    return {{"type", "function"},
            {"function",
             {{"name", tool.name},
              {"description", tool.description},
              {"parameters", std::move(parameters)}}}};
}

[[nodiscard]] std::string string_field(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

[[nodiscard]] std::int64_t count_field(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<std::int64_t>() : 0;
}

}  // namespace

std::string generate_line(const GenerateRequest& request) {
    nlohmann::json messages = nlohmann::json::array();
    for (const harness::ChatMessage& message : request.messages) {
        messages.push_back(template_message(message));
    }
    nlohmann::json tools = nlohmann::json::array();
    for (const harness::Tool& tool : request.tools) {
        tools.push_back(template_tool(tool));
    }
    const ResolvedSampling& sampling = request.sampling;
    nlohmann::json values{{"temperature", sampling.temperature.value},
                          {"top_p", sampling.top_p.value},
                          {"top_k", sampling.top_k.value},
                          {"min_p", sampling.min_p.value},
                          {"repetition_penalty", sampling.repeat_penalty.value},
                          {"presence_penalty", sampling.presence_penalty.value},
                          {"seed", nullptr}};
    if (sampling.seed.has_value()) {
        values["seed"] = *sampling.seed;
    }
    const nlohmann::json line{{"type", "generate"},
                              {"id", request.id},
                              {"messages", std::move(messages)},
                              {"tools", std::move(tools)},
                              {"sampling", std::move(values)},
                              {"max_tokens", request.max_tokens},
                              {"stop", request.stop},
                              {"thinking", request.thinking},
                              {"session", request.session}};
    return line.dump();
}

std::string cancel_line(std::int64_t id) {
    return nlohmann::json{{"type", "cancel"}, {"id", id}}.dump();
}

std::optional<Event> parse_event(std::string_view line) {
    const nlohmann::json object = nlohmann::json::parse(line, nullptr, false);
    if (object.is_discarded() || !object.is_object()) {
        return std::nullopt;
    }
    const std::string type = string_field(object, "type");
    Event event;
    if (const auto it = object.find("id"); it != object.end() && it->is_number_integer()) {
        event.id = it->get<std::int64_t>();
    }
    if (type == "text" || type == "reasoning") {
        event.kind = type == "text" ? Event::Kind::Text : Event::Kind::Reasoning;
        event.text = string_field(object, "text");
        return event;
    }
    if (type == "tool_call") {
        event.kind = Event::Kind::ToolCall;
        event.name = string_field(object, "name");
        if (event.name.empty()) {
            return std::nullopt;
        }
        // Text as the IR carries it: a driver that sent a string sent JSON text.
        const auto arguments = object.find("arguments");
        event.arguments = "{}";
        if (arguments != object.end() && arguments->is_string()) {
            event.arguments = arguments->get<std::string>();
        } else if (arguments != object.end() && arguments->is_object()) {
            event.arguments = arguments->dump();
        }
        return event;
    }
    if (type == "done") {
        event.kind = Event::Kind::Done;
        event.finish = string_field(object, "finish");
        event.prompt_tokens = count_field(object, "prompt_tokens");
        event.cached_tokens = count_field(object, "cached_tokens");
        event.completion_tokens = count_field(object, "completion_tokens");
        return event;
    }
    if (type == "error") {
        event.kind = Event::Kind::Error;
        event.error_kind = string_field(object, "kind");
        event.text = string_field(object, "message");
        return event;
    }
    if (type == "ready") {
        event.kind = Event::Kind::Ready;
        event.protocol = static_cast<int>(count_field(object, "protocol"));
        event.model_type = string_field(object, "model_type");
        event.tool_parser = string_field(object, "tool_parser");
        event.mlx_lm_version = string_field(object, "mlx_lm");
        const auto boolean = [&object](const char* key) {
            const auto it = object.find(key);
            return it != object.end() && it->is_boolean() && it->get<bool>();
        };
        event.chat_template = boolean("chat_template");
        event.thinking = boolean("thinking");
        return event;
    }
    // An unknown type is a newer driver's, and carries nothing this side reads.
    return std::nullopt;
}

std::string describe(const Event& event) {
    const std::string id = event.id.has_value() ? "#" + std::to_string(*event.id) + " " : "";
    switch (event.kind) {
        case Event::Kind::Ready:
            return "ready " + event.model_type;
        case Event::Kind::Text:
            return id + "text '" + event.text + "'";
        case Event::Kind::Reasoning:
            return id + "reasoning '" + event.text + "'";
        case Event::Kind::ToolCall:
            return id + "tool_call " + event.name + " " + event.arguments;
        case Event::Kind::Done:
            return id + "done " + event.finish + " " + std::to_string(event.prompt_tokens) + "/" +
                   std::to_string(event.cached_tokens) + "/" +
                   std::to_string(event.completion_tokens);
        case Event::Kind::Error:
            return id + "error " + event.error_kind + ": " + event.text;
    }
    return "?";
}

harness::FinishReason finish_reason(std::string_view finish) noexcept {
    if (finish == "length") {
        return harness::FinishReason::Length;
    }
    if (finish == "tool_calls") {
        return harness::FinishReason::ToolCalls;
    }
    if (finish == "cancelled") {
        return harness::FinishReason::Cancelled;
    }
    return harness::FinishReason::Stop;
}

}  // namespace apogee::backends::mlx
