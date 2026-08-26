#include "backends/anthropic_wire.h"

#include <utility>

#include "harness/errors.h"

namespace apogee::backends::anthropic {
namespace {

/// Splits a `data:` URI into media type and payload.
/// Returns false for anything that is not `data:<media-type>;base64,<data>`.
bool split_data_uri(std::string_view url, std::string& media_type, std::string& data) {
    constexpr std::string_view kDataUriPrefix = "data:";
    if (!url.starts_with(kDataUriPrefix)) {
        return false;
    }
    const std::size_t comma = url.find(',');
    if (comma == std::string_view::npos) {
        return false;
    }
    const std::string_view meta = url.substr(kDataUriPrefix.size(), comma - kDataUriPrefix.size());
    const std::size_t semicolon = meta.find(';');
    media_type =
        std::string{semicolon == std::string_view::npos ? meta : meta.substr(0, semicolon)};
    data = std::string{url.substr(comma + 1)};
    return !media_type.empty() && !data.empty();
}

nlohmann::json image_block(const harness::ContentPart& part) {
    std::string media_type;
    std::string data;
    if (split_data_uri(part.image_url, media_type, data)) {
        return nlohmann::json{
            {"type", "image"},
            {"source", {{"type", "base64"}, {"media_type", media_type}, {"data", data}}}};
    }
    return nlohmann::json{{"type", "image"},
                          {"source", {{"type", "url"}, {"url", part.image_url}}}};
}

/// Parses a tool call's arguments, which the IR holds as text.
/// A malformed argument string becomes an empty object rather than failing the
/// whole request -- the API will reject it with a better message than we can.
nlohmann::json parse_arguments(const std::string& arguments) {
    nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return nlohmann::json::object();
    }
    return parsed;
}

nlohmann::json tool_definition(const harness::Tool& tool) {
    nlohmann::json schema = nlohmann::json::parse(tool.parameters_schema, nullptr, false);
    if (schema.is_discarded() || !schema.is_object()) {
        schema = nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}};
    }
    // Anthropic calls it input_schema; OpenAI calls it parameters. The IR uses
    // neither name, so this is the only place the difference exists.
    return nlohmann::json{{"name", tool.name},
                          {"description", tool.description},
                          {"input_schema", std::move(schema)}};
}

}  // namespace

// ---------------------------------------------------------------------------
// ThinkingCache
// ---------------------------------------------------------------------------

void ThinkingCache::remember(const nlohmann::json& blocks) {
    if (!blocks.is_array()) {
        return;
    }
    bool has_thinking = false;
    for (const auto& block : blocks) {
        const std::string type = block.value("type", std::string{});
        if (type == "thinking" || type == "redacted_thinking") {
            has_thinking = true;
            break;
        }
    }
    // Only worth remembering a turn that actually produced thinking -- without
    // it there is nothing to replay, and caching every turn would grow without
    // bound over a long conversation.
    if (!has_thinking) {
        return;
    }
    for (const auto& block : blocks) {
        if (block.value("type", std::string{}) != "tool_use") {
            continue;
        }
        const std::string id = block.value("id", std::string{});
        if (!id.empty()) {
            by_tool_use_id_[id] = blocks;
        }
    }
}

const nlohmann::json* ThinkingCache::find(const std::string& tool_use_id) const {
    const auto it = by_tool_use_id_.find(tool_use_id);
    return it == by_tool_use_id_.end() ? nullptr : &it->second;
}

void ThinkingCache::clear() {
    by_tool_use_id_.clear();
}

// ---------------------------------------------------------------------------
// IR -> wire
// ---------------------------------------------------------------------------

nlohmann::json content_blocks(const harness::MessageContent& content) {
    if (content.parts().empty()) {
        // A bare string is valid Anthropic content and keeps the common case
        // small on the wire.
        return content.plain_text();
    }
    nlohmann::json blocks = nlohmann::json::array();
    for (const harness::ContentPart& part : content.parts()) {
        if (part.kind == harness::ContentPart::Kind::Text) {
            blocks.push_back({{"type", "text"}, {"text", part.text}});
            continue;
        }
        if (part.image_url.empty()) {
            continue;
        }
        blocks.push_back(image_block(part));
    }
    return blocks;
}

nlohmann::json build_request(const harness::ChatRequest& request, const RequestOptions& options,
                             const ThinkingCache& thinking) {
    nlohmann::json body{
        {"model", options.model},
        {"max_tokens", options.max_tokens},
    };

    std::string system_prompt;
    nlohmann::json messages = nlohmann::json::array();

    for (const harness::ChatMessage& message : request.messages) {
        switch (message.role) {
            case harness::Role::System: {
                // Lifted out of the conversation: Anthropic takes it top-level.
                // Multiple system messages concatenate rather than the last
                // one silently winning.
                if (!system_prompt.empty()) {
                    system_prompt += "\n\n";
                }
                system_prompt += message.content.plain_text();
                break;
            }
            case harness::Role::User: {
                messages.push_back(
                    {{"role", "user"}, {"content", content_blocks(message.content)}});
                break;
            }
            case harness::Role::Assistant: {
                nlohmann::json blocks = nlohmann::json::array();

                // Replay the cached thinking blocks for this turn, if it made a
                // tool call under extended thinking. They must come FIRST and
                // be byte-identical (signature included) or the API rejects the
                // turn.
                const nlohmann::json* cached = nullptr;
                for (const harness::ToolCall& call : message.tool_calls) {
                    cached = thinking.find(call.id);
                    if (cached != nullptr) {
                        break;
                    }
                }
                if (cached != nullptr) {
                    for (const auto& block : *cached) {
                        const std::string type = block.value("type", std::string{});
                        if (type == "thinking" || type == "redacted_thinking") {
                            blocks.push_back(block);
                        }
                    }
                }

                const std::string text = message.content.plain_text();
                if (!text.empty()) {
                    blocks.push_back({{"type", "text"}, {"text", text}});
                }
                for (const harness::ToolCall& call : message.tool_calls) {
                    blocks.push_back({{"type", "tool_use"},
                                      {"id", call.id},
                                      {"name", call.name},
                                      {"input", parse_arguments(call.arguments)}});
                }
                if (blocks.empty()) {
                    // An assistant turn cannot be empty on the wire.
                    blocks.push_back({{"type", "text"}, {"text", ""}});
                }
                messages.push_back({{"role", "assistant"}, {"content", std::move(blocks)}});
                break;
            }
            case harness::Role::Tool: {
                // Anthropic has no tool role: a result is a tool_result block
                // inside a USER turn. Consecutive results merge into one turn,
                // which is what the API expects when several tools ran.
                nlohmann::json block{{"type", "tool_result"},
                                     {"tool_use_id", message.tool_call_id},
                                     {"content", message.content.plain_text()}};
                if (!messages.empty() && messages.back().at("role") == "user" &&
                    messages.back().at("content").is_array() &&
                    !messages.back().at("content").empty() &&
                    messages.back().at("content").front().value("type", std::string{}) ==
                        "tool_result") {
                    messages.back().at("content").push_back(std::move(block));
                } else {
                    messages.push_back(
                        {{"role", "user"}, {"content", nlohmann::json::array({std::move(block)})}});
                }
                break;
            }
        }
    }

    body["messages"] = std::move(messages);
    if (!system_prompt.empty()) {
        body["system"] = system_prompt;
    }
    if (request.temperature.has_value()) {
        body["temperature"] = *request.temperature;
    }
    if (options.stream) {
        body["stream"] = true;
    }

    nlohmann::json tools = nlohmann::json::array();
    for (const harness::Tool& tool : request.tools) {
        tools.push_back(tool_definition(tool));
    }
    if (options.web_search) {
        // A server-side tool: Anthropic runs the search itself, so nothing has
        // to be dispatched locally. This is what replaces Ommi's dependence on
        // the claude CLI for web search.
        nlohmann::json search{{"type", "web_search_20250305"}, {"name", "web_search"}};
        if (options.web_search_max_uses > 0) {
            search["max_uses"] = options.web_search_max_uses;
        }
        tools.push_back(std::move(search));
    }
    if (!tools.empty()) {
        body["tools"] = std::move(tools);
    }

    if (options.thinking_budget_tokens > 0) {
        body["thinking"] = {{"type", "enabled"}, {"budget_tokens", options.thinking_budget_tokens}};
    }

    return body;
}

// ---------------------------------------------------------------------------
// wire -> IR
// ---------------------------------------------------------------------------

harness::FinishReason finish_reason_from_stop_reason(std::string_view stop_reason) {
    if (stop_reason == "end_turn" || stop_reason == "stop_sequence") {
        return harness::FinishReason::Stop;
    }
    if (stop_reason == "max_tokens") {
        return harness::FinishReason::Length;
    }
    if (stop_reason == "tool_use") {
        return harness::FinishReason::ToolCalls;
    }
    if (stop_reason == "refusal") {
        return harness::FinishReason::ContentFilter;
    }
    return harness::FinishReason::Other;
}

harness::ChatResponse parse_response(const nlohmann::json& body, std::string* thinking_out) {
    harness::ChatResponse response;
    response.model = body.value("model", std::string{});

    std::string text;
    std::vector<harness::ToolCall> calls;

    if (const auto content = body.find("content"); content != body.end() && content->is_array()) {
        for (const auto& block : *content) {
            const std::string type = block.value("type", std::string{});
            if (type == "text") {
                text += block.value("text", std::string{});
            } else if (type == "thinking") {
                // Never appended to `text`: thinking is display-only and must
                // not reach the returned content or persisted history.
                if (thinking_out != nullptr) {
                    *thinking_out += block.value("thinking", std::string{});
                }
            } else if (type == "tool_use") {
                harness::ToolCall call;
                call.id = block.value("id", std::string{});
                call.name = block.value("name", std::string{});
                const auto input = block.find("input");
                call.arguments = input == block.end() ? "{}" : input->dump();
                calls.push_back(std::move(call));
            }
        }
    }

    response.message = harness::ChatMessage::assistant(text);
    response.message.tool_calls = std::move(calls);
    response.finish_reason =
        finish_reason_from_stop_reason(body.value("stop_reason", std::string{}));

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
    // A gateway 502 is HTML, and a truncated body is nothing at all. Falling
    // back to the status keeps the message useful instead of dumping markup at
    // the user.
    return "HTTP " + std::to_string(status);
}

}  // namespace apogee::backends::anthropic
