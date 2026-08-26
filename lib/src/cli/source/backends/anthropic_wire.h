#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "harness/types.h"

/// Translation between Apogee's IR and the Anthropic Messages API wire format.
///
/// Its own translation unit because this is where the dialect lives, and
/// keeping it separate from the provider makes it testable with no transport at
/// all. Three mappings here are not obvious and are the ones that break:
///
///  1. **The system prompt is not a message.** Anthropic takes it as a
///     top-level `system` field, so system messages are lifted out of the
///     conversation rather than sent in it.
///
///  2. **Tool results are USER messages.** The IR has a `Tool` role, mirroring
///     OpenAI; Anthropic has no such role and expects a `tool_result` block
///     inside a user turn. Sending role "tool" is rejected outright.
///
///  3. **Thinking blocks must be replayed verbatim.** With extended thinking
///     on, an assistant turn that called a tool must have its `thinking` block
///     — signature and all — sent back in the following request, or the API
///     refuses the turn. This is an Anthropic requirement Ommi never met
///     because it never used the API's thinking. Since thinking must never
///     enter the IR or persisted history, the provider caches the raw blocks
///     and this layer splices them back in. See `ThinkingCache`.
namespace apogee::backends::anthropic {

/// Raw assistant content blocks from one turn, kept so they can be replayed.
///
/// Provider-local, never serialized, never in the IR. Keyed by the `tool_use`
/// id the turn produced: the next request identifies the turn by the
/// `tool_result` that answers it.
class ThinkingCache {
public:
    /// Remembers `blocks` (the whole assistant `content` array) under every
    /// tool_use id it contains.
    void remember(const nlohmann::json& blocks);

    /// The blocks for the turn that produced `tool_use_id`, or nullptr.
    [[nodiscard]] const nlohmann::json* find(const std::string& tool_use_id) const;

    /// Drops everything. Called when a conversation restarts.
    void clear();

    [[nodiscard]] std::size_t size() const noexcept {
        return by_tool_use_id_.size();
    }

private:
    std::map<std::string, nlohmann::json> by_tool_use_id_;
};

/// Options that shape a request beyond the IR's own fields.
struct RequestOptions {
    std::string model;
    std::int64_t max_tokens = 4096;

    /// Enables extended thinking with this budget. 0 leaves it off.
    std::int64_t thinking_budget_tokens = 0;

    /// Adds Anthropic's server-side web_search tool.
    bool web_search = false;
    /// Cap on server-side searches per request; 0 leaves it to the API.
    std::int64_t web_search_max_uses = 0;

    bool stream = false;
};

/// Builds the JSON body for POST /v1/messages.
[[nodiscard]] nlohmann::json build_request(const harness::ChatRequest& request,
                                           const RequestOptions& options,
                                           const ThinkingCache& thinking);

/// Converts one IR message's content into Anthropic content blocks.
/// Exposed for testing the image and tool-block mappings directly.
[[nodiscard]] nlohmann::json content_blocks(const harness::MessageContent& content);

/// Parses a non-streaming /v1/messages response into the IR.
/// `thinking_out`, when non-null, receives concatenated thinking text.
[[nodiscard]] harness::ChatResponse parse_response(const nlohmann::json& body,
                                                   std::string* thinking_out = nullptr);

/// Maps an Anthropic `stop_reason` to the IR's FinishReason.
[[nodiscard]] harness::FinishReason finish_reason_from_stop_reason(std::string_view stop_reason);

/// Extracts a human-readable message from an Anthropic error body.
/// Falls back to the status line when the body is not the documented shape --
/// a gateway 502 is HTML, not JSON.
[[nodiscard]] std::string error_message(long status, std::string_view body);

}  // namespace apogee::backends::anthropic
