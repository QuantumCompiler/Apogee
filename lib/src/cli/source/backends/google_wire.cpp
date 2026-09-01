#include "backends/google_wire.h"

#include <utility>

#include "harness/errors.h"

namespace apogee::backends::google {
namespace {

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

/// Splits a `data:` URI into media type and payload.
bool split_data_uri(std::string_view url, std::string& media_type, std::string& data) {
    constexpr std::string_view kPrefix = "data:";
    if (!starts_with(url, kPrefix)) {
        return false;
    }
    const std::size_t comma = url.find(',');
    if (comma == std::string_view::npos) {
        return false;
    }
    std::string_view meta = url.substr(kPrefix.size(), comma - kPrefix.size());
    const std::size_t semicolon = meta.find(';');
    media_type =
        std::string{semicolon == std::string_view::npos ? meta : meta.substr(0, semicolon)};
    data = std::string{url.substr(comma + 1)};
    return !media_type.empty() && !data.empty();
}

nlohmann::json parse_arguments(const std::string& arguments) {
    nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return nlohmann::json::object();
    }
    return parsed;
}

nlohmann::json text_and_image_parts(const harness::MessageContent& content) {
    nlohmann::json parts = nlohmann::json::array();
    if (content.parts().empty()) {
        if (!content.plain_text().empty()) {
            parts.push_back({{"text", content.plain_text()}});
        }
        return parts;
    }
    for (const harness::ContentPart& part : content.parts()) {
        if (part.kind == harness::ContentPart::Kind::Text) {
            parts.push_back({{"text", part.text}});
            continue;
        }
        std::string media_type;
        std::string data;
        if (split_data_uri(part.image_url, media_type, data)) {
            parts.push_back({{"inlineData", {{"mimeType", media_type}, {"data", data}}}});
        } else if (!part.image_url.empty()) {
            parts.push_back({{"fileData", {{"mimeType", "image/*"}, {"fileUri", part.image_url}}}});
        }
    }
    return parts;
}

}  // namespace

std::optional<nlohmann::json> content_entry(const harness::ChatMessage& message) {
    switch (message.role) {
        case harness::Role::System:
            return std::nullopt;  // lifted to systemInstruction

        case harness::Role::User:
            return nlohmann::json{{"role", "user"},
                                  {"parts", text_and_image_parts(message.content)}};

        case harness::Role::Assistant: {
            nlohmann::json parts = text_and_image_parts(message.content);
            for (const harness::ToolCall& call : message.tool_calls) {
                parts.push_back(
                    {{"functionCall",
                      {{"name", call.name}, {"args", parse_arguments(call.arguments)}}}});
            }
            if (parts.empty()) {
                parts.push_back({{"text", ""}});
            }
            // "model", not "assistant". Sending "assistant" is rejected.
            return nlohmann::json{{"role", "model"}, {"parts", std::move(parts)}};
        }

        case harness::Role::Tool:
            // There is no tool role: a result is a functionResponse part in a
            // USER turn, and it is matched by NAME rather than by a call id --
            // Gemini's functionCall carries no id at all.
            return nlohmann::json{
                {"role", "user"},
                {"parts", nlohmann::json::array(
                              {{{"functionResponse",
                                 {{"name", message.name},
                                  {"response", {{"result", message.content.plain_text()}}}}}}})}};
    }
    return std::nullopt;
}

nlohmann::json build_request(const harness::ChatRequest& request, const RequestOptions& options) {
    nlohmann::json body;

    std::string system_text;
    nlohmann::json contents = nlohmann::json::array();

    for (const harness::ChatMessage& message : request.messages) {
        if (message.role == harness::Role::System) {
            if (!system_text.empty()) {
                system_text += "\n\n";
            }
            system_text += message.content.plain_text();
            continue;
        }
        if (std::optional<nlohmann::json> entry = content_entry(message); entry.has_value()) {
            contents.push_back(std::move(*entry));
        }
    }

    body["contents"] = std::move(contents);
    if (!system_text.empty()) {
        body["systemInstruction"] = {{"parts", nlohmann::json::array({{{"text", system_text}}})}};
    }

    nlohmann::json generation{{"maxOutputTokens", options.max_output_tokens}};
    if (request.temperature.has_value()) {
        generation["temperature"] = *request.temperature;
    }
    if (options.thinking_budget_tokens > 0) {
        // includeThoughts is what makes reasoning parts appear at all; without
        // it the budget applies but nothing is emitted to display.
        generation["thinkingConfig"] = {{"includeThoughts", true},
                                        {"thinkingBudget", options.thinking_budget_tokens}};
    }
    body["generationConfig"] = std::move(generation);

    nlohmann::json declarations = nlohmann::json::array();
    for (const harness::Tool& tool : request.tools) {
        nlohmann::json parameters = nlohmann::json::parse(tool.parameters_schema, nullptr, false);
        if (parameters.is_discarded() || !parameters.is_object()) {
            parameters =
                nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}};
        }
        declarations.push_back({{"name", tool.name},
                                {"description", tool.description},
                                {"parameters", std::move(parameters)}});
    }

    nlohmann::json tools = nlohmann::json::array();
    if (!declarations.empty()) {
        tools.push_back({{"functionDeclarations", std::move(declarations)}});
    }
    if (options.web_search) {
        tools.push_back({{"google_search", nlohmann::json::object()}});
    }
    if (!tools.empty()) {
        body["tools"] = std::move(tools);
    }

    return body;
}

harness::FinishReason finish_reason_from_string(std::string_view reason) {
    if (reason == "STOP") {
        return harness::FinishReason::Stop;
    }
    if (reason == "MAX_TOKENS") {
        return harness::FinishReason::Length;
    }
    if (reason == "SAFETY" || reason == "RECITATION" || reason == "PROHIBITED_CONTENT") {
        return harness::FinishReason::ContentFilter;
    }
    return harness::FinishReason::Other;
}

harness::ChatResponse parse_response(const nlohmann::json& body, std::string* thinking_out) {
    harness::ChatResponse response;
    response.model = body.value("modelVersion", std::string{});

    std::string text;
    std::vector<harness::ToolCall> calls;
    std::string finish;

    if (const auto candidates = body.find("candidates");
        candidates != body.end() && candidates->is_array() && !candidates->empty()) {
        const auto& candidate = candidates->front();
        finish = candidate.value("finishReason", std::string{});

        if (const auto content = candidate.find("content"); content != candidate.end()) {
            if (const auto parts = content->find("parts");
                parts != content->end() && parts->is_array()) {
                for (const auto& part : *parts) {
                    if (const auto call = part.find("functionCall"); call != part.end()) {
                        harness::ToolCall tool_call;
                        tool_call.name = call->value("name", std::string{});
                        // Gemini emits no call id. The loop links a result to
                        // its call by id, so one is synthesised from the name --
                        // and the result goes back matched by name, which is
                        // what Gemini itself expects.
                        tool_call.id = tool_call.name;
                        if (const auto args = call->find("args"); args != call->end()) {
                            tool_call.arguments = args->dump();
                        }
                        calls.push_back(std::move(tool_call));
                        continue;
                    }
                    const auto text_field = part.find("text");
                    if (text_field == part.end() || !text_field->is_string()) {
                        continue;
                    }
                    // THE check: reasoning arrives in the same parts array as
                    // the answer, flagged only by `thought`. Missing it emits
                    // the model's private reasoning as the answer.
                    if (part.value("thought", false)) {
                        if (thinking_out != nullptr) {
                            *thinking_out += text_field->get<std::string>();
                        }
                        continue;
                    }
                    text += text_field->get<std::string>();
                }
            }
        }
    }

    response.message = harness::ChatMessage::assistant(text);
    response.message.tool_calls = calls;
    response.finish_reason =
        calls.empty() ? finish_reason_from_string(finish) : harness::FinishReason::ToolCalls;

    if (const auto usage = body.find("usageMetadata"); usage != body.end() && usage->is_object()) {
        response.usage.prompt_tokens = usage->value("promptTokenCount", std::int64_t{0});
        response.usage.completion_tokens = usage->value("candidatesTokenCount", std::int64_t{0});
    }
    return response;
}

std::string error_message(long status, std::string_view body) {
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
        if (const auto error = parsed.find("error"); error != parsed.end() && error->is_object()) {
            const std::string message = error->value("message", std::string{});
            const std::string status_text = error->value("status", std::string{});
            if (!message.empty()) {
                return status_text.empty() ? message : status_text + ": " + message;
            }
        }
    }
    return "HTTP " + std::to_string(status);
}

}  // namespace apogee::backends::google
