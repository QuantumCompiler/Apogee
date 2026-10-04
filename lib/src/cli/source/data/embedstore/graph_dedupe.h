#pragma once

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

/// One dedup cluster: the surviving node and the nodes merged into it.
struct MergeGroup {
    GraphNode kept;
    std::vector<GraphNode> merged;
};

}  // namespace apogee::embedstore
