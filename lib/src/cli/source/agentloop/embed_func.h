#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
#include "harness/config.h"
#include "harness/harness.h"

/// The one embedding seam the retrieval layer consumes.
///
/// Everything above the store asks for vectors through this and nothing else:
/// a function from texts to vectors, plus the three facts the resolver needs
/// about where they come from -- which model (so a store's recorded space can
/// be matched), how wide, and whether each call costs money. The function is
/// injectable, so every resolution and injection test runs with no backend.
namespace apogee::agentloop {

using EmbedFunc = std::function<std::vector<std::vector<float>>(const std::vector<std::string>&,
                                                                const harness::CancellationToken&)>;

struct Embedder {
    EmbedFunc embed;
    /// The backend entry the vectors come from.
    std::string backend;
    /// The model, as a store records it.
    std::string model;
    /// 0 when not yet known.
    std::size_t dimensions = 0;
    /// Whether each call is billed -- the spend policy reads this.
    bool metered = true;
};

/// Resolves the embedder a collection would use, or nullopt when nothing that
/// can embed resolves.
///
/// `collection_backend` is the collection's own `backend:` pin; empty defers
/// to the embedding role (`models.default_embedding`, then `models.default`).
/// The answer comes from `Harness::embedder_for`, never from a type: a chat
/// backend that cannot embed resolves to nothing here, and says so through
/// `reason`.
[[nodiscard]] std::optional<Embedder> resolve_embedder(const harness::Harness& harness,
                                                       const harness::Config& config,
                                                       std::string_view collection_backend,
                                                       std::string& reason);

}  // namespace apogee::agentloop
