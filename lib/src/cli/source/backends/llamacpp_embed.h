#pragma once

#include <string>
#include <vector>

#include "backends/llama_runtime.h"
#include "harness/cancellation.h"

/// The provider side of in-process embeddings: batching and cancellation over
/// the runtime seam's `embed_batch`.
///
/// The runtime packs tokens; this packs **texts**, in slices small enough that
/// Ctrl-C between two of them is honoured within a fraction of a second rather
/// than after a whole corpus. Kept out of `llamacpp.cpp` so it can be tested
/// against the scripted runtime with no model file, which is the only place
/// it is tested on the merge-blocking target.
namespace apogee::backends {

/// Texts per `embed_batch` call. Small enough that a cancellation lands
/// promptly; large enough that the per-call overhead is nothing next to the
/// decode.
inline constexpr std::size_t kTextsPerLlamaBatch = 64;

/// Embeds `inputs` through `model`, one vector each, in order.
/// Throws std::runtime_error with the runtime's message when it cannot.
[[nodiscard]] std::vector<std::vector<float>> embed_with_llama(
    LlamaModel& model, const std::vector<std::string>& inputs,
    const harness::CancellationToken& cancellation);

}  // namespace apogee::backends
