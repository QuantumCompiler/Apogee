#include "harness/types.h"

#include <nlohmann/json.hpp>

#include <array>
#include <utility>

#include "harness/errors.h"

namespace apogee::harness {
namespace {

constexpr std::array<std::pair<std::string_view, Role>, 4> kRoleNames{{
    {"system", Role::System},
    {"user", Role::User},
    {"assistant", Role::Assistant},
    {"tool", Role::Tool},
}};

constexpr std::array<std::pair<std::string_view, FinishReason>, 6> kFinishReasonNames{{
    {"stop", FinishReason::Stop},
    {"length", FinishReason::Length},
    {"tool_calls", FinishReason::ToolCalls},
    {"content_filter", FinishReason::ContentFilter},
    {"cancelled", FinishReason::Cancelled},
    {"other", FinishReason::Other},
}};

constexpr std::array<std::pair<std::string_view, StatusEvent::Type>, 8> kStatusTypeNames{{
    {"model_loading", StatusEvent::Type::ModelLoading},
    {"model_ready", StatusEvent::Type::ModelReady},
    {"thinking", StatusEvent::Type::Thinking},
    {"rag_search", StatusEvent::Type::RagSearch},
    {"rag_result", StatusEvent::Type::RagResult},
    {"tool_call", StatusEvent::Type::ToolCall},
    {"token_count", StatusEvent::Type::TokenCount},
    {"context_warning", StatusEvent::Type::ContextWarning},
}};

/// Reads an optional string field, tolerating null.
std::string optional_string(const nlohmann::json& in, std::string_view key) {
    const auto it = in.find(key);
    if (it == in.end() || it->is_null()) {
        return {};
    }
    if (!it->is_string()) {
        throw InvalidRequestError(std::string{key} + ": expected a string");
    }
    return it->get<std::string>();
}

}  // namespace

std::string_view to_string(Role role) noexcept {
    for (const auto& [name, value] : kRoleNames) {
        if (value == role) {
            return name;
        }
    }
    return "user";
}

std::optional<Role> role_from_string(std::string_view name) noexcept {
    for (const auto& [candidate, value] : kRoleNames) {
        if (candidate == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string_view to_string(FinishReason reason) noexcept {
    for (const auto& [name, value] : kFinishReasonNames) {
        if (value == reason) {
            return name;
        }
    }
    return "other";
}

std::optional<FinishReason> finish_reason_from_string(std::string_view name) noexcept {
    for (const auto& [candidate, value] : kFinishReasonNames) {
        if (candidate == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string_view to_string(StatusEvent::Type type) noexcept {
    for (const auto& [name, value] : kStatusTypeNames) {
        if (value == type) {
            return name;
        }
    }
    return "thinking";
}

std::string_view to_string(StatusEvent::Phase phase) noexcept {
    switch (phase) {
        case StatusEvent::Phase::Start:
            return "start";
        case StatusEvent::Phase::Done:
            return "done";
        case StatusEvent::Phase::Error:
            return "error";
    }
    return "start";
}

// ---------------------------------------------------------------------------
// ContentPart / MessageContent
// ---------------------------------------------------------------------------

ContentPart ContentPart::from_text(std::string value) {
    ContentPart part;
    part.kind = Kind::Text;
    part.text = std::move(value);
    return part;
}

ContentPart ContentPart::from_image_url(std::string url, std::string detail) {
    ContentPart part;
    part.kind = Kind::ImageUrl;
    part.image_url = std::move(url);
    part.detail = std::move(detail);
    return part;
}

MessageContent::MessageContent(std::string text) : text_{std::move(text)} {}

MessageContent::MessageContent(const char* text) : text_{text == nullptr ? "" : text} {}

MessageContent MessageContent::from_parts(std::vector<ContentPart> parts) {
    MessageContent content;
    content.parts_ = std::move(parts);
    return content;
}

std::string MessageContent::plain_text() const {
    if (parts_.empty()) {
        return text_;
    }
    std::string out;
    for (const ContentPart& part : parts_) {
        if (part.kind == ContentPart::Kind::Text) {
            out += part.text;
        }
    }
    return out;
}

bool MessageContent::is_rich() const noexcept {
    for (const ContentPart& part : parts_) {
        if (part.kind != ContentPart::Kind::Text) {
            return true;
        }
    }
    return false;
}

bool MessageContent::empty() const noexcept {
    return text_.empty() && parts_.empty();
}

bool operator==(const MessageContent& lhs, const MessageContent& rhs) {
    return lhs.text_ == rhs.text_ && lhs.parts_ == rhs.parts_;
}

// ---------------------------------------------------------------------------
// ChatMessage / ChatRequest
// ---------------------------------------------------------------------------

ChatMessage ChatMessage::system(MessageContent content) {
    return ChatMessage{Role::System, std::move(content), {}, {}, {}};
}

ChatMessage ChatMessage::user(MessageContent content) {
    return ChatMessage{Role::User, std::move(content), {}, {}, {}};
}

ChatMessage ChatMessage::assistant(MessageContent content) {
    return ChatMessage{Role::Assistant, std::move(content), {}, {}, {}};
}

ChatMessage ChatMessage::from_tool_result(const ToolResult& result) {
    ChatMessage message;
    message.role = Role::Tool;
    message.content = result.content;
    message.tool_call_id = result.tool_call_id;
    message.name = result.name;
    return message;
}

bool ChatRequest::is_transient(std::size_t index) const noexcept {
    if (transient.length == 0) {
        return false;
    }
    return index >= transient.start && index < transient.start + transient.length;
}

std::vector<ChatMessage> ChatRequest::durable_messages() const {
    if (transient.length == 0) {
        return messages;
    }
    std::vector<ChatMessage> durable;
    durable.reserve(messages.size());
    for (std::size_t i = 0; i < messages.size(); ++i) {
        if (!is_transient(i)) {
            durable.push_back(messages[i]);
        }
    }
    return durable;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool operator==(const ContentPart& lhs, const ContentPart& rhs) {
    return lhs.kind == rhs.kind && lhs.text == rhs.text && lhs.image_url == rhs.image_url &&
           lhs.detail == rhs.detail;
}

bool operator==(const ToolCall& lhs, const ToolCall& rhs) {
    return lhs.id == rhs.id && lhs.name == rhs.name && lhs.arguments == rhs.arguments;
}

bool operator==(const ChatMessage& lhs, const ChatMessage& rhs) {
    return lhs.role == rhs.role && lhs.content == rhs.content && lhs.tool_calls == rhs.tool_calls &&
           lhs.tool_call_id == rhs.tool_call_id && lhs.name == rhs.name;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

void to_json(nlohmann::json& out, const ContentPart& value) {
    if (value.kind == ContentPart::Kind::Text) {
        out = nlohmann::json{{"type", "text"}, {"text", value.text}};
        return;
    }
    nlohmann::json image{{"url", value.image_url}};
    if (!value.detail.empty()) {
        image["detail"] = value.detail;
    }
    out = nlohmann::json{{"type", "image_url"}, {"image_url", std::move(image)}};
}

void from_json(const nlohmann::json& in, ContentPart& value) {
    if (!in.is_object()) {
        throw InvalidRequestError("content part must be an object");
    }
    const std::string type = optional_string(in, "type");
    if (type == "image_url") {
        value.kind = ContentPart::Kind::ImageUrl;
        const auto image = in.find("image_url");
        if (image == in.end() || !image->is_object()) {
            throw InvalidRequestError("image_url part is missing its image_url object");
        }
        value.image_url = optional_string(*image, "url");
        value.detail = optional_string(*image, "detail");
        return;
    }
    // Anything else is treated as text. Unknown part types degrade to their
    // text field rather than failing the whole message: a provider that adds a
    // new part kind should not break parsing a session file.
    value.kind = ContentPart::Kind::Text;
    value.text = optional_string(in, "text");
}

void to_json(nlohmann::json& out, const MessageContent& value) {
    // A bare string in the common case. Keeps payloads small and keeps every
    // OpenAI-compatible client that only understands strings working.
    if (value.parts().empty()) {
        out = value.plain_text();
        return;
    }
    out = value.parts();
}

void from_json(const nlohmann::json& in, MessageContent& value) {
    if (in.is_null()) {
        value = MessageContent{};
        return;
    }
    if (in.is_string()) {
        value = MessageContent{in.get<std::string>()};
        return;
    }
    if (in.is_array()) {
        value = MessageContent::from_parts(in.get<std::vector<ContentPart>>());
        return;
    }
    throw InvalidRequestError("message content must be a string or an array of content parts");
}

void to_json(nlohmann::json& out, const Tool& value) {
    out = nlohmann::json{
        {"type", "function"},
        {"function",
         {{"name", value.name},
          {"description", value.description},
          {"parameters", nlohmann::json::parse(value.parameters_schema, nullptr, false)}}}};
    // A schema that does not parse is passed through as an empty object rather
    // than throwing: the tool is still callable, and a provider will reject it
    // with a far better message than we can produce here.
    if (out["function"]["parameters"].is_discarded()) {
        out["function"]["parameters"] = nlohmann::json::object();
    }
}

void from_json(const nlohmann::json& in, Tool& value) {
    const auto function = in.find("function");
    const nlohmann::json& source = function == in.end() ? in : *function;
    value.name = optional_string(source, "name");
    value.description = optional_string(source, "description");
    const auto parameters = source.find("parameters");
    value.parameters_schema =
        parameters == source.end() || parameters->is_null() ? "{}" : parameters->dump();
}

void to_json(nlohmann::json& out, const ToolCall& value) {
    out = nlohmann::json{{"id", value.id},
                         {"type", "function"},
                         {"function", {{"name", value.name}, {"arguments", value.arguments}}}};
}

void from_json(const nlohmann::json& in, ToolCall& value) {
    value.id = optional_string(in, "id");
    const auto function = in.find("function");
    const nlohmann::json& source = function == in.end() ? in : *function;
    value.name = optional_string(source, "name");
    const auto arguments = source.find("arguments");
    if (arguments == source.end() || arguments->is_null()) {
        value.arguments = "{}";
    } else if (arguments->is_string()) {
        value.arguments = arguments->get<std::string>();
    } else {
        // Some providers send arguments as an object rather than a JSON string.
        value.arguments = arguments->dump();
    }
}

void to_json(nlohmann::json& out, const ChatMessage& value) {
    out = nlohmann::json{{"role", to_string(value.role)}, {"content", value.content}};
    if (!value.tool_calls.empty()) {
        out["tool_calls"] = value.tool_calls;
    }
    if (!value.tool_call_id.empty()) {
        out["tool_call_id"] = value.tool_call_id;
    }
    if (!value.name.empty()) {
        out["name"] = value.name;
    }
}

void from_json(const nlohmann::json& in, ChatMessage& value) {
    if (!in.is_object()) {
        throw InvalidRequestError("a chat message must be an object");
    }
    const std::string role = optional_string(in, "role");
    const std::optional<Role> parsed = role_from_string(role);
    if (!parsed.has_value()) {
        throw InvalidRequestError("unknown message role '" + role +
                                  "' (accepted: system, user, assistant, tool)");
    }
    value.role = *parsed;

    const auto content = in.find("content");
    value.content = content == in.end() ? MessageContent{} : content->get<MessageContent>();

    const auto tool_calls = in.find("tool_calls");
    value.tool_calls = tool_calls == in.end() || tool_calls->is_null()
                           ? std::vector<ToolCall>{}
                           : tool_calls->get<std::vector<ToolCall>>();

    value.tool_call_id = optional_string(in, "tool_call_id");
    value.name = optional_string(in, "name");
}

void to_json(nlohmann::json& out, const ChatRequest& value) {
    // NOTE: `transient` is absent, and there is no to_json for it anywhere --
    // serializing it does not compile. See ChatRequest::Transient.
    out = nlohmann::json{{"model", value.model}, {"messages", value.messages}};
    if (value.temperature.has_value()) {
        out["temperature"] = *value.temperature;
    }
    if (value.max_tokens.has_value()) {
        out["max_tokens"] = *value.max_tokens;
    }
    if (!value.tools.empty()) {
        out["tools"] = value.tools;
    }
}

void from_json(const nlohmann::json& in, ChatRequest& value) {
    value.model = optional_string(in, "model");

    const auto messages = in.find("messages");
    value.messages = messages == in.end() || messages->is_null()
                         ? std::vector<ChatMessage>{}
                         : messages->get<std::vector<ChatMessage>>();

    if (const auto it = in.find("temperature"); it != in.end() && !it->is_null()) {
        value.temperature = it->get<double>();
    }
    if (const auto it = in.find("max_tokens"); it != in.end() && !it->is_null()) {
        value.max_tokens = it->get<std::int64_t>();
    }
    if (const auto it = in.find("tools"); it != in.end() && !it->is_null()) {
        value.tools = it->get<std::vector<Tool>>();
    }
    // `transient` is intentionally never read back: it is process-local state,
    // so a deserialized request always starts with an empty one.
}

void to_json(nlohmann::json& out, const ChatResponse& value) {
    out = nlohmann::json{{"message", value.message},
                         {"finish_reason", to_string(value.finish_reason)},
                         {"model", value.model}};
    if (value.usage.reported()) {
        out["usage"] = {{"prompt_tokens", value.usage.prompt_tokens},
                        {"completion_tokens", value.usage.completion_tokens},
                        {"total_tokens", value.usage.total_tokens()}};
    }
}

void from_json(const nlohmann::json& in, ChatResponse& value) {
    const auto message = in.find("message");
    if (message != in.end()) {
        value.message = message->get<ChatMessage>();
    }
    const std::optional<FinishReason> reason =
        finish_reason_from_string(optional_string(in, "finish_reason"));
    value.finish_reason = reason.value_or(FinishReason::Other);
    value.model = optional_string(in, "model");

    if (const auto usage = in.find("usage"); usage != in.end() && usage->is_object()) {
        value.usage.prompt_tokens = usage->value("prompt_tokens", std::int64_t{0});
        value.usage.completion_tokens = usage->value("completion_tokens", std::int64_t{0});
    }
}

void to_json(nlohmann::json& out, const ModelInfo& value) {
    out = nlohmann::json{{"id", value.id},
                         {"name", value.name},
                         {"provider", value.provider},
                         {"backend", value.backend}};
}

void from_json(const nlohmann::json& in, ModelInfo& value) {
    value.id = optional_string(in, "id");
    value.name = optional_string(in, "name");
    value.provider = optional_string(in, "provider");
    value.backend = optional_string(in, "backend");
}

}  // namespace apogee::harness
