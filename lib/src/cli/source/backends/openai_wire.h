#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include "harness/types.h"

/// Translation between Apogee's IR and the OpenAI **Responses API**.
///
/// `/v1/responses`, not `/v1/chat/completions` (decided 2026-08-26, confirmed
/// against the live docs). Reasoning summaries and the server-side `web_search`
/// tool exist only on Responses; Chat Completions exposes no reasoning summary
/// at all, which would leave OpenAI the one provider whose thinking display
/// silently did nothing.
///
/// Three shape differences from Anthropic that this layer absorbs:
///
///  1. **Messages are `input` items**, and the system prompt is an
///     `instructions` field rather than a message.
///  2. **Streaming is semantic events**, not `choices[].delta` — each event has
///     a `type` naming exactly what changed. Usage appears on `response.completed`
///     and nowhere else.
///  3. **A tool call is an output ITEM**, announced by `response.output_item.added`
///     with its `name` and `call_id`, whose arguments then stream as deltas.
namespace apogee::backends::openai {

struct RequestOptions {
    std::string model;
    std::int64_t max_output_tokens = 4096;

    /// Maps to `reasoning.effort`. Empty leaves reasoning off.
    ///
    /// A budget in tokens does not exist here -- OpenAI takes a coarse effort
    /// level -- so the provider translates its single `thinking_budget_tokens`
    /// knob into a band. One knob per provider, mapped at the boundary.
    std::string reasoning_effort;

    bool web_search = false;
    bool stream = false;
};

/// The effort band for a token budget: "" (off), "low", "medium", or "high".
[[nodiscard]] std::string effort_for_budget(std::int64_t budget_tokens);

/// Builds the JSON body for POST /v1/responses.
[[nodiscard]] nlohmann::json build_request(const harness::ChatRequest& request,
                                           const RequestOptions& options);

/// Converts one IR message into Responses `input` items.
///
/// Returns an array because a single IR message can become several items: an
/// assistant turn with tool calls is a message item plus one `function_call`
/// item per call.
[[nodiscard]] nlohmann::json input_items(const harness::ChatMessage& message);

/// Parses a non-streaming /v1/responses body into the IR.
/// `thinking_out`, when non-null, receives concatenated reasoning summary text.
[[nodiscard]] harness::ChatResponse parse_response(const nlohmann::json& body,
                                                   std::string* thinking_out = nullptr);

/// Maps a Responses `status` / `incomplete_details.reason` to the IR.
[[nodiscard]] harness::FinishReason finish_reason_from_status(std::string_view status,
                                                              std::string_view incomplete_reason);

/// Extracts a human-readable message from an OpenAI error body.
[[nodiscard]] std::string error_message(long status, std::string_view body);

}  // namespace apogee::backends::openai
