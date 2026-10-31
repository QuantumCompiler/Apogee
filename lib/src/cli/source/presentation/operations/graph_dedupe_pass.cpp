#include "operations/graph_dedupe_pass.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace apogee::operations {

std::size_t GraphDedupePass::merged_nodes() const {
    std::size_t merged = 0;
    for (const embedstore::MergeGroup& group : groups) {
        merged += group.merged.size();
    }
    return merged;
}

GraphDedupePass plan_graph_dedupe(const embedstore::Store& store, double threshold, bool dry_run) {
    GraphDedupePass pass;
    pass.scope = store.dedupe_scope();
    pass.prose_skipped = pass.scope.prose_skip_reason();
    pass.threshold = threshold;
    pass.dry_run = dry_run;
    return pass;
}

void run_graph_dedupe(embedstore::Store& store, GraphDedupePass& pass) {
    // Per layer (27k): code entities were merged by exact qualified name as
    // they were built; the vector pass over prose runs only when there is
    // something to compare.
    if (pass.prose_skipped.empty()) {
        pass.groups = store.dedupe_nodes(pass.threshold, pass.dry_run);
    }
}

nlohmann::json graph_dedupe_document(const GraphDedupePass& pass) {
    nlohmann::json groups = nlohmann::json::array();
    for (const embedstore::MergeGroup& group : pass.groups) {
        nlohmann::json names = nlohmann::json::array();
        for (const embedstore::GraphNode& node : group.merged) {
            names.push_back(node.name);
        }
        groups.push_back(nlohmann::json{{"kept", group.kept.name},
                                        {"kept_type", group.kept.type},
                                        {"merged", std::move(names)}});
    }
    nlohmann::json out{{"groups", std::move(groups)},
                       {"merged_nodes", pass.merged_nodes()},
                       {"threshold", pass.threshold},
                       {"dry_run", pass.dry_run},
                       {"code_entities", pass.scope.code_entities},
                       {"code_identity_merges", pass.scope.code_identity_merges}};
    if (!pass.prose_skipped.empty()) {
        out["prose_skipped"] = pass.prose_skipped;
    }
    return out;
}

}  // namespace apogee::operations
