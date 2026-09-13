#include "agentloop/embed_func.h"

#include "harness/provider.h"
#include "harness/roles.h"

namespace apogee::agentloop {

std::optional<Embedder> resolve_embedder(const harness::Harness& harness,
                                         const harness::Config& config,
                                         std::string_view collection_backend, std::string& reason) {
    harness::RoleRequest request;
    request.role = harness::ModelRole::Embedding;
    request.entry_backend = collection_backend;
    const std::string key = harness::resolve_backend_key(config, request);
    if (key.empty()) {
        reason =
            "no embedding backend is configured (set `default_embedding` under `models:`, or a "
            "collection's backend:)";
        return std::nullopt;
    }

    harness::EmbeddingCapable* capable = harness.embedder_for(key);
    if (capable == nullptr) {
        reason = "backend '" + key + "' cannot embed";
        return std::nullopt;
    }

    Embedder embedder;
    embedder.backend = key;
    embedder.model = capable->embedding_model_name();
    embedder.dimensions = capable->embedding_dimensions();
    embedder.metered = capable->embedding_is_metered();
    embedder.embed = [capable](const std::vector<std::string>& texts,
                               const harness::CancellationToken& cancellation) {
        return capable->embed(texts, cancellation);
    };
    return embedder;
}

}  // namespace apogee::agentloop
