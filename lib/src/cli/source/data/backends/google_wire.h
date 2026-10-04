#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/types.h"

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
    /// Thinking asked off (26i): the budget that asks for least, sent bare --
    /// 0 where the model can stop thinking, its floor where it cannot.
    std::optional<std::int64_t> thinking_off_budget;

    /// Adds the `google_search` server-side tool.
    bool web_search = false;
};

/// A JSON Schema reduced to the OpenAPI subset `responseSchema` accepts:
/// `$schema`, `title`, `additionalProperties` and every other keyword the
/// API rejects are dropped, recursively. Exposed for the wire tests.
[[nodiscard]] nlohmann::json gemini_response_schema(const nlohmann::json& schema);

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

/// The least a Pro model may think for: it cannot switch thinking off (26i).
inline constexpr std::int64_t kProMinThinkingBudget = 128;

/// Whether `model` thinks at all -- every Gemini after the 2.0 generation
/// (26i). One that does not refuses a thinking setting.
[[nodiscard]] bool model_thinks(std::string_view model) noexcept;

/// What off asks of `model`: 0 where thinking can stop, a Pro model's floor
/// where it cannot, nothing for a model that does not think.
[[nodiscard]] std::optional<std::int64_t> thinking_off_budget(std::string_view model) noexcept;

/// A budget as `model` takes it: a Pro model's floor or above.
[[nodiscard]] std::int64_t thinking_budget_for(std::string_view model,
                                               std::int64_t budget) noexcept;

}  // namespace apogee::backends::google
