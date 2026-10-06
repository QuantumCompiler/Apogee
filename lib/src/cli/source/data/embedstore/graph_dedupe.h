#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "embedstore/graph.h"

/// Embedding-similarity entity dedup: the exact (normalised name, type)
/// upsert key cannot know that "K8s" and "Kubernetes" are the same thing,
/// but their entity vectors can. `Store::dedupe_nodes` merges same-type nodes
/// whose vectors agree beyond a cosine threshold.
///
/// **Never automatic.** A merge is a judgement over vectors: it runs only
/// when asked (`apogee graph dedupe`, `POST /v1/admin/graph/{name}/dedupe`),
/// previewed by a dry run, and commits in one transaction. Nodes without a
/// vector are never considered, and neither are `decision` nodes: two
/// knowledge records with near-identical text are still two decisions, each
/// keyed by its own record id.
namespace apogee::embedstore {

/// The CLI's `--threshold` and the route's `threshold` default.
inline constexpr double kDefaultDedupeThreshold = 0.92;

/// What a dedupe pass has to work with, per layer (27k) -- read once, so the
/// CLI and the admin route say the same thing. A **code** entity's identity
/// is its exact qualified name: one node per name by construction, a
/// declaration and its definition, or two trees' copies, already one node --
/// never merged by resemblance. A **prose** entity merges by vector
/// agreement, and with no vector anywhere there is nothing to compare: the
/// pass is skipped, and said.
struct DedupeScope {
    /// Code entities: file, module, class and function nodes (an unresolved
    /// reference's `name` node is not an entity).
    std::int64_t code_entities = 0;
    /// Of those, the ones stated at more than one place -- each unified by
    /// identity as it was built.
    std::int64_t code_identity_merges = 0;
    /// Prose entities: every node that is neither code nor a decision.
    std::int64_t prose_entities = 0;
    std::int64_t prose_with_vectors = 0;

    /// Why the vector pass is skipped, or empty when it runs.
    [[nodiscard]] std::string prose_skip_reason() const;
};

/// One dedup cluster: the surviving node and the nodes merged into it.
struct MergeGroup {
    GraphNode kept;
    std::vector<GraphNode> merged;
};

}  // namespace apogee::embedstore
