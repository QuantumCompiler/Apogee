#pragma once

#include <vector>

#include "contracts/types.h"

/// The messages a local model's prompt is rendered from -- shared by the two
/// local runtimes, in-process llama.cpp and the MLX driver (27a), so a
/// conversation reaches either model's template in the same shape.
///
/// Moved here from `llamacpp.cpp` when the second local backend arrived:
/// both the schema statement and the system join are rules about what a
/// chat template will accept, not about llama.cpp, and two copies of the
/// schema instruction's wording would drift the first time one changed.
namespace apogee::backends {

/// The prompt-level form of structured output, now the fallback (26f): a
/// grammar holds a local answer to its schema wherever the model's template
/// can take one, and only where it cannot -- no template, a format with no
/// place for a schema, a schema the converter cannot express, a turn with
/// tools, a runtime with no grammar at all (MLX) -- is the schema stated in
/// the system block, the caller validating either way. Skipped when a system
/// message already carries the schema text -- every structured caller states
/// it once itself -- so the model never reads it twice.
[[nodiscard]] std::vector<harness::ChatMessage> messages_with_schema(
    const harness::ChatRequest& request);

/// The messages a local prompt is rendered from: the schema instruction
/// where one is asked for and `state_schema` -- false when a grammar holds
/// the answer, so the schema is not stated twice -- and the system messages
/// that open the conversation joined into one, a blank line apart.
///
/// A template may take one system message, and only first: Qwen3.5 and
/// 3.8's raise "System message must be at the beginning" on a second, and
/// the render failing drops the model to the fallback template -- and its
/// tools with it. A second is the ordinary case: the environment note
/// (25d), a retrieval block or a review note ahead of a chat's own system
/// prompt. The Anthropic and Google wires join theirs the same way.
[[nodiscard]] std::vector<harness::ChatMessage> prompt_messages(const harness::ChatRequest& request,
                                                                bool state_schema);

}  // namespace apogee::backends
