#include "backends/openai_wire.h"

#include <utility>

#include "harness/errors.h"

namespace apogee::backends::openai {
namespace {

nlohmann::json content_parts(const harness::MessageContent& content, bool is_assistant) {
    // Input and output text are named differently, which is easy to get wrong
    // and produces a silent "no content" rather than an error.
    const char* text_type = is_assistant ? "output_text" : "input_text";

    if (content.parts().empty()) {
        return nlohmann::json::array({{{"type", text_type}, {"text", content.plain_text()}}});
    }

    nlohmann::json parts = nlohmann::json::array();
    for (const harness::ContentPart& part : content.parts()) {
        if (part.kind == harness::ContentPart::Kind::Text) {
            parts.push_back({{"type", text_type}, {"text", part.text}});
        } else if (!part.image_url.empty()) {
            // A data: URI works here directly -- no base64/media-type split, as
            // Anthropic needs.
            parts.push_back({{"type", "input_image"}, {"image_url", part.image_url}});
        }
    }
    return parts;
}

}  // namespace

std::string effort_for_budget(std::int64_t budget_tokens) {
    // OpenAI takes a band, not a token count. Mapping one knob onto three
    // vendor spellings is the boundary's job -- the IR carries no thinking
    // field, so the loop never learns they differ.
    if (budget_tokens <= 0) {
        return {};
    }
    if (budget_tokens < 2048) {
        return "low";
    }
    if (budget_tokens < 8192) {
        return "medium";
    }
    return "high";
}

nlohmann::json input_items(const harness::ChatMessage& message) {
    nlohmann::json items = nlohmann::json::array();

    switch (message.role) {
        case harness::Role::System:
            // Lifted to `instructions` by build_request; never an input item.
            break;

        case harness::Role::User:
            items.push_back({{"type", "message"},
                             {"role", "user"},
                             {"content", content_parts(message.content, false)}});
            break;

        case harness::Role::Assistant: {
            const std::string text = message.content.plain_text();
            if (!text.empty()) {
                items.push_back({{"type", "message"},
                                 {"role", "assistant"},
                                 {"content", content_parts(message.content, true)}});
            }
            // Each tool call is its OWN top-level item, not a field on the
            // message -- the shape difference from Anthropic that breaks a
            // naive port.
            for (const harness::ToolCall& call : message.tool_calls) {
                items.push_back({{"type", "function_call"},
                                 {"call_id", call.id},
                                 {"name", call.name},
                                 {"arguments", call.arguments}});
            }
            break;
        }

        case harness::Role::Tool:
            // And the result is its own item too, not a message with a role.
            items.push_back({{"type", "function_call_output"},
                             {"call_id", message.tool_call_id},
                             {"output", message.content.plain_text()}});
            break;
    }
    return items;
}

nlohmann::json build_request(const harness::ChatRequest& request, const RequestOptions& options) {
    nlohmann::json body{{"model", options.model}, {"max_output_tokens", options.max_output_tokens}};

    std::string instructions;
    nlohmann::json input = nlohmann::json::array();

    for (const harness::ChatMessage& message : request.messages) {
        if (message.role == harness::Role::System) {
            if (!instructions.empty()) {
                instructions += "\n\n";
            }
            instructions += message.content.plain_text();
            continue;
        }
        for (nlohmann::json& item : input_items(message)) {
            input.push_back(std::move(item));
        }
    }

    body["input"] = std::move(input);
    if (!instructions.empty()) {
        body["instructions"] = instructions;
    }
    if (request.temperature.has_value()) {
        body["temperature"] = *request.temperature;
    }
    if (options.stream) {
        body["stream"] = true;
    }

    nlohmann::json tools = nlohmann::json::array();
    for (const harness::Tool& tool : request.tools) {
        nlohmann::json parameters = nlohmann::json::parse(tool.parameters_schema, nullptr, false);
        if (parameters.is_discarded() || !parameters.is_object()) {
            parameters =
                nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}};
        }
        // Flat, unlike Chat Completions' {type:function, function:{...}} nesting.
        tools.push_back({{"type", "function"},
                         {"name", tool.name},
                         {"description", tool.description},
                         {"parameters", std::move(parameters)}});
    }
    if (options.web_search) {
        tools.push_back({{"type", "web_search"}});
    }
    if (!tools.empty()) {
        body["tools"] = std::move(tools);
    }

    if (!options.reasoning_effort.empty()) {
        // `summary: auto` is what makes reasoning summaries stream at all.
        // Without it the effort applies but nothing is emitted to display.
        body["reasoning"] = {{"effort", options.reasoning_effort}, {"summary", "auto"}};
    }

    return body;
}

harness::FinishReason finish_reason_from_status(std::string_view status,
                                                std::string_view incomplete_reason) {
    if (incomplete_reason == "max_output_tokens") {
        return harness::FinishReason::Length;
    }
    if (incomplete_reason == "content_filter") {
        return harness::FinishReason::ContentFilter;
    }
    if (status == "completed") {
        return harness::FinishReason::Stop;
    }
    if (status == "incomplete") {
        return harness::FinishReason::Length;
    }
    return harness::FinishReason::Other;
}

harness::ChatResponse parse_response(const nlohmann::json& body, std::string* thinking_out) {
    harness::ChatResponse response;
    response.model = body.value("model", std::string{});

    std::string text;
    std::vector<harness::ToolCall> calls;

    if (const auto output = body.find("output"); output != body.end() && output->is_array()) {
        for (const auto& item : *output) {
            const std::string type = item.value("type", std::string{});

            if (type == "message") {
                if (const auto content = item.find("content");
                    content != item.end() && content->is_array()) {
                    for (const auto& part : *content) {
                        if (part.value("type", std::string{}) == "output_text") {
                            text += part.value("text", std::string{});
                        }
                    }
                }
            } else if (type == "reasoning") {
                // Never appended to `text`: reasoning is display-only and must
                // not reach the returned content or persisted history.
                if (thinking_out != nullptr) {
                    if (const auto summary = item.find("summary");
                        summary != item.end() && summary->is_array()) {
                        for (const auto& part : *summary) {
                            *thinking_out += part.value("text", std::string{});
                        }
                    }
                }
            } else if (type == "function_call") {
                harness::ToolCall call;
                // call_id, not id: `id` is the item's own identity, and using it
                // makes every tool result fail to match its call.
                call.id = item.value("call_id", std::string{});
                call.name = item.value("name", std::string{});
                call.arguments = item.value("arguments", std::string{"{}"});
                calls.push_back(std::move(call));
            }
        }
    }

    response.message = harness::ChatMessage::assistant(text);
    response.message.tool_calls = std::move(calls);

    std::string incomplete;
    if (const auto details = body.find("incomplete_details");
        details != body.end() && details->is_object()) {
        incomplete = details->value("reason", std::string{});
    }
    response.finish_reason =
        finish_reason_from_status(body.value("status", std::string{}), incomplete);
    if (!response.message.tool_calls.empty()) {
        response.finish_reason = harness::FinishReason::ToolCalls;
    }

    if (const auto usage = body.find("usage"); usage != body.end() && usage->is_object()) {
        response.usage.prompt_tokens = usage->value("input_tokens", std::int64_t{0});
        response.usage.completion_tokens = usage->value("output_tokens", std::int64_t{0});
    }
    return response;
}

std::string error_message(long status, std::string_view body) {
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
        if (const auto error = parsed.find("error"); error != parsed.end() && error->is_object()) {
            const std::string type = error->value("type", std::string{});
            const std::string message = error->value("message", std::string{});
            if (!message.empty()) {
                return type.empty() ? message : type + ": " + message;
            }
        }
    }
    return "HTTP " + std::to_string(status);
}

}  // namespace apogee::backends::openai
