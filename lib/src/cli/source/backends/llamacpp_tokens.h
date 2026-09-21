#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "backends/llama_runtime.h"
#include "harness/types.h"

/// Tokenization for the local backend: exact counting, and the prefix
/// arithmetic that makes the KV cache worth having.
namespace apogee::backends::llama_tokens {

/// Renders `messages` for `model`, preferring the GGUF's own chat template.
///
/// Falls back to the name-matched registry in `chat_template.h` when the model
/// ships none. `model_name` is only used for that fallback.
[[nodiscard]] std::string render_prompt(const LlamaModel& model, std::string_view model_name,
                                        const std::vector<harness::ChatMessage>& messages,
                                        bool add_generation_prompt);

/// The token sequence a request becomes.
[[nodiscard]] std::vector<std::int32_t> tokenize_prompt(
    const LlamaModel& model, std::string_view model_name,
    const std::vector<harness::ChatMessage>& messages, bool add_generation_prompt);

/// Exact prompt-token count for `messages`.
[[nodiscard]] std::int64_t count_prompt_tokens(const LlamaModel& model, std::string_view model_name,
                                               const std::vector<harness::ChatMessage>& messages);

/// How many leading tokens `previous` and `next` share.
///
/// **This is the KV cache's entire decision.** Everything up to the common
/// prefix is already decoded and can stay; everything after it must be dropped
/// and re-decoded. Turn two of a conversation shares its whole prior transcript
/// with turn one, so the shared count is large and only the new user message
/// gets decoded -- which is precisely the acceptance criterion.
///
/// It also makes the cache **self-correcting** for free. A compaction rewrites
/// history, a `/model` switch changes the framing, a resumed session starts
/// from an empty cache: each simply yields a shorter common prefix, and the
/// right amount is re-ingested with no special case anywhere.
[[nodiscard]] std::size_t common_prefix_length(const std::vector<std::int32_t>& previous,
                                               const std::vector<std::int32_t>& next);

}  // namespace apogee::backends::llama_tokens
