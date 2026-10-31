#include "backends/local_prompt.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace apogee::backends {

std::vector<harness::ChatMessage> messages_with_schema(const harness::ChatRequest& request) {
    const std::string& schema = request.transient.response_schema;
    if (schema.empty()) {
        return request.messages;
    }
    const nlohmann::json parsed = nlohmann::json::parse(schema, nullptr, false);
    const std::string text = parsed.is_discarded() ? schema : parsed.dump(2);
    for (const harness::ChatMessage& message : request.messages) {
        if (message.role == harness::Role::System &&
            message.content.plain_text().find("OUTPUT FORMAT") != std::string::npos) {
            return request.messages;
        }
    }
    std::vector<harness::ChatMessage> out = request.messages;
    const std::string instruction =
        "OUTPUT FORMAT\nYour response MUST be valid JSON conforming to the following JSON "
        "Schema. Output only the JSON object -- no surrounding text or markdown code "
        "blocks.\n\n" +
        text;
    // Beside an existing system message when there is one, else first.
    std::size_t at = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i].role == harness::Role::System) {
            at = i + 1;
        }
    }
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(at),
               harness::ChatMessage::system(instruction));
    return out;
}

std::vector<harness::ChatMessage> prompt_messages(const harness::ChatRequest& request,
                                                  bool state_schema) {
    std::vector<harness::ChatMessage> messages =
        state_schema ? messages_with_schema(request) : request.messages;
    std::size_t leading = 0;
    while (leading < messages.size() && messages[leading].role == harness::Role::System) {
        ++leading;
    }
    if (leading < 2) {
        return messages;
    }
    std::string joined;
    for (std::size_t i = 0; i < leading; ++i) {
        const std::string text = messages[i].content.plain_text();
        if (text.empty()) {
            continue;
        }
        joined += (joined.empty() ? "" : "\n\n") + text;
    }
    messages.erase(messages.begin() + 1, messages.begin() + static_cast<std::ptrdiff_t>(leading));
    messages.front() = harness::ChatMessage::system(joined);
    return messages;
}

std::string decode_base64(std::string_view encoded) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(encoded.size() / 4 * 3);

    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const char c : encoded) {
        if (c == '=') {
            break;
        }
        const std::size_t value = alphabet.find(c);
        if (value == std::string_view::npos) {
            continue;  // whitespace and newlines are legal in a data URI
        }
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((accumulator >> static_cast<unsigned>(bits)) & 0xFFU));
        }
    }
    return out;
}

}  // namespace apogee::backends
