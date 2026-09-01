#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "harness/types.h"

/// Translation between Apogee's IR and the Google Gemini `generateContent` API.
///
/// The third dialect, and the one furthest from the other two:
///
///  * **Roles are `user` and `model`** — there is no "assistant", and no tool
///    role at all. A tool result is a `functionResponse` part inside a *user*
///    turn.
///  * **Everything is a part.** Text, images, tool calls, tool results, and
///    reasoning are all entries in `content.parts[]`, distinguished by which
///    field they carry rather than by a `type` tag.
///  * **Reasoning is a part with `thought: true`** — the same array as the
///    answer text, so a translator that does not check the flag silently emits
///    the model's private reasoning as the answer.
namespace apogee::backends::google {

struct RequestOptions {
    std::string model;
    std::int64_t max_output_tokens = 4096;

    /// Maps to `thinkingConfig.thinkingBudget`. 0 leaves thinking off.
    /// Gemini takes a real token budget, so this knob passes through.
    std::int64_t thinking_budget_tokens = 0;

    /// Adds the `google_search` server-side tool.
    bool web_search = false;
};

/// Builds the JSON body for `:generateContent` / `:streamGenerateContent`.
[[nodiscard]] nlohmann::json build_request(const harness::ChatRequest& request,
                                           const RequestOptions& options);

/// Converts one IR message into a `contents` entry, or nullopt for a system
/// message (which is lifted to `systemInstruction`).
[[nodiscard]] std::optional<nlohmann::json> content_entry(const harness::ChatMessage& message);

/// Parses one `GenerateContentResponse` into the IR.
///
/// Used for both the non-streaming body and each streamed chunk, because
/// Gemini streams the same object repeatedly rather than typed deltas.
[[nodiscard]] harness::ChatResponse parse_response(const nlohmann::json& body,
                                                   std::string* thinking_out = nullptr);

[[nodiscard]] harness::FinishReason finish_reason_from_string(std::string_view reason);

[[nodiscard]] std::string error_message(long status, std::string_view body);

}  // namespace apogee::backends::google
