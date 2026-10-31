#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "embedstore/graph_dedupe.h"
#include "embedstore/store.h"

/// One dedupe pass as the command line and the control plane both run it
/// (M31, 55h's idiom): what each layer has to work with, read once (27k), the
/// pass itself, and the one document both answer with -- `graph dedupe
/// --output-format json` prints the very body `POST /v1/admin/graph/{id}/dedupe`
/// serves. Here rather than in either surface, so neither includes the other.
namespace apogee::operations {

/// A pass over one graph's store.
struct GraphDedupePass {
    /// What each layer has: code entities, merged by identity as they were
    /// built, and prose entities, merged by vector agreement.
    embedstore::DedupeScope scope{};
    /// Why the vector pass over prose is skipped -- nothing to compare --
    /// or empty when it runs.
    std::string prose_skipped{};
    /// The merge groups: those a dry run would make, or those made.
    std::vector<embedstore::MergeGroup> groups{};
    double threshold = embedstore::kDefaultDedupeThreshold;
    bool dry_run = false;

    /// How many entities the groups fold into their survivors.
    [[nodiscard]] std::size_t merged_nodes() const;
};

/// What a pass over `store` has to work with, at `threshold`: its scope, and
/// whether the vector pass is skipped. Nothing is merged yet, so a surface
/// can say what each layer is before the pass runs.
[[nodiscard]] GraphDedupePass plan_graph_dedupe(const embedstore::Store& store, double threshold,
                                                bool dry_run);

/// Runs the pass `pass` plans -- nothing at all when its vector pass is
/// skipped -- and keeps its groups: a dry run computes them and writes
/// nothing. Throws what the store throws.
void run_graph_dedupe(embedstore::Store& store, GraphDedupePass& pass);

/// `{"groups": [{kept, kept_type, merged: [names]}], "merged_nodes",
/// "threshold", "dry_run", "code_entities", "code_identity_merges"[,
/// "prose_skipped"]}` -- the route's body, and the command line's document.
[[nodiscard]] nlohmann::json graph_dedupe_document(const GraphDedupePass& pass);

}  // namespace apogee::operations
