#include "embedstore/graph_dedupe.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "embedstore/store.h"
#include "embedstore/store_impl.h"
#include "embedstore/vector.h"

namespace apogee::embedstore {

using detail::bind_text;
using detail::column_text;
using detail::fail;
using detail::in_transaction;
using detail::prepare;
using detail::StatementPtr;

namespace {

struct Candidate {
    GraphNode node;
    std::vector<float> vector;
};

/// Union-find with the smallest id as every set's root, so the earliest
/// extracted node is the one that survives.
class Clusters {
public:
    void add(std::int64_t id) {
        parent_.try_emplace(id, id);
    }

    [[nodiscard]] std::int64_t find(std::int64_t id) {
        std::int64_t root = id;
        while (parent_[root] != root) {
            root = parent_[root];
        }
        while (parent_[id] != root) {
            const std::int64_t next = parent_[id];
            parent_[id] = root;
            id = next;
        }
        return root;
    }

    void unite(std::int64_t a, std::int64_t b) {
        std::int64_t ra = find(a);
        std::int64_t rb = find(b);
        if (ra == rb) {
            return;
        }
        if (ra > rb) {
            std::swap(ra, rb);
        }
        parent_[rb] = ra;
    }

    [[nodiscard]] std::map<std::int64_t, std::vector<std::int64_t>> groups() {
        std::map<std::int64_t, std::vector<std::int64_t>> out;
        for (const auto& [id, unused] : parent_) {
            out[find(id)].push_back(id);
        }
        return out;
    }

private:
    std::map<std::int64_t, std::int64_t> parent_;
};

struct EdgeRow {
    std::int64_t id = 0;
    std::int64_t source = 0;
    std::int64_t target = 0;
    std::string relation;
    std::string description;
    std::int64_t weight = 1;
    std::string origin;
};

void step_done(sqlite3* handle, sqlite3_stmt* statement, const char* what) {
    if (sqlite3_step(statement) != SQLITE_DONE) {
        fail(handle, what);
    }
}

/// Repoints every edge of `merged` to `kept`: summed into an existing
/// (source, target, relation) row when the repoint collides, dropped when it
/// would become a self-loop, moved otherwise.
void repoint_edges(sqlite3* handle, std::int64_t kept, std::int64_t merged) {
    std::vector<EdgeRow> edges;
    {
        StatementPtr select = prepare(handle,
                                      "SELECT id, source_id, target_id, relation, description,"
                                      " weight, origin FROM kg_edges"
                                      " WHERE source_id = ? OR target_id = ?");
        sqlite3_bind_int64(select.get(), 1, merged);
        sqlite3_bind_int64(select.get(), 2, merged);
        while (sqlite3_step(select.get()) == SQLITE_ROW) {
            EdgeRow edge;
            edge.id = sqlite3_column_int64(select.get(), 0);
            edge.source = sqlite3_column_int64(select.get(), 1);
            edge.target = sqlite3_column_int64(select.get(), 2);
            edge.relation = column_text(select.get(), 3);
            edge.description = column_text(select.get(), 4);
            edge.weight = sqlite3_column_int64(select.get(), 5);
            edge.origin = column_text(select.get(), 6);
            edges.push_back(std::move(edge));
        }
    }
    for (const EdgeRow& edge : edges) {
        const std::int64_t source = edge.source == merged ? kept : edge.source;
        const std::int64_t target = edge.target == merged ? kept : edge.target;
        if (source == target) {
            StatementPtr drop = prepare(handle, "DELETE FROM kg_edges WHERE id = ?");
            sqlite3_bind_int64(drop.get(), 1, edge.id);
            step_done(handle, drop.get(), "could not drop a self-loop");
            continue;
        }
        StatementPtr existing = prepare(handle,
                                        "SELECT id, description FROM kg_edges"
                                        " WHERE source_id = ? AND target_id = ? AND relation = ?"
                                        "   AND id != ?");
        sqlite3_bind_int64(existing.get(), 1, source);
        sqlite3_bind_int64(existing.get(), 2, target);
        bind_text(existing.get(), 3, edge.relation);
        sqlite3_bind_int64(existing.get(), 4, edge.id);
        if (sqlite3_step(existing.get()) == SQLITE_ROW) {
            // Fold: the weights sum, the description merges first-non-empty.
            const std::int64_t existing_id = sqlite3_column_int64(existing.get(), 0);
            std::string description = column_text(existing.get(), 1);
            if (description.empty()) {
                description = edge.description;
            }
            // Origin-aware: the fold is `extracted` when either side was --
            // an extracted edge never merges down to inferred -- and the
            // sites that stated the folded edge move with it.
            StatementPtr fold =
                prepare(handle,
                        "UPDATE kg_edges SET weight = weight + ?, description = ?,"
                        " origin = CASE WHEN ? = 'extracted' THEN 'extracted' ELSE origin END,"
                        " confidence = CASE WHEN ? = 'extracted' THEN 1.0 ELSE confidence END"
                        " WHERE id = ?");
            sqlite3_bind_int64(fold.get(), 1, edge.weight);
            bind_text(fold.get(), 2, description);
            bind_text(fold.get(), 3, edge.origin);
            bind_text(fold.get(), 4, edge.origin);
            sqlite3_bind_int64(fold.get(), 5, existing_id);
            step_done(handle, fold.get(), "could not fold an edge");
            StatementPtr move_sites =
                prepare(handle, "UPDATE OR IGNORE kg_edge_sites SET edge_id = ? WHERE edge_id = ?");
            sqlite3_bind_int64(move_sites.get(), 1, existing_id);
            sqlite3_bind_int64(move_sites.get(), 2, edge.id);
            step_done(handle, move_sites.get(), "could not move an edge's sites");
            StatementPtr drop = prepare(handle, "DELETE FROM kg_edges WHERE id = ?");
            sqlite3_bind_int64(drop.get(), 1, edge.id);
            step_done(handle, drop.get(), "could not drop a folded edge");
            continue;
        }
        StatementPtr move =
            prepare(handle, "UPDATE kg_edges SET source_id = ?, target_id = ? WHERE id = ?");
        sqlite3_bind_int64(move.get(), 1, source);
        sqlite3_bind_int64(move.get(), 2, target);
        sqlite3_bind_int64(move.get(), 3, edge.id);
        step_done(handle, move.get(), "could not repoint an edge");
    }
}

}  // namespace

std::vector<MergeGroup> Store::dedupe_nodes(double threshold, bool dry_run) {
    sqlite3* handle = impl_->connection.get();

    // Every vectorised entity, grouped by type. Decision nodes are left out
    // by type: a record is its own thing however alike another reads.
    std::map<std::string, std::vector<Candidate>> by_type;
    std::map<std::int64_t, GraphNode> nodes;
    {
        StatementPtr select = prepare(handle,
                                      "SELECT id, name, name_norm, type, description, dim,"
                                      " mention_count, COALESCE(metadata, ''), embedding"
                                      " FROM kg_nodes WHERE dim > 0 AND type != ?"
                                      " AND type NOT IN ('file', 'module', 'class', 'function',"
                                      " 'name') ORDER BY id");
        bind_text(select.get(), 1, kNodeTypeDecision);
        while (sqlite3_step(select.get()) == SQLITE_ROW) {
            Candidate candidate;
            candidate.node.id = sqlite3_column_int64(select.get(), 0);
            candidate.node.name = column_text(select.get(), 1);
            candidate.node.name_norm = column_text(select.get(), 2);
            candidate.node.type = column_text(select.get(), 3);
            candidate.node.description = column_text(select.get(), 4);
            candidate.node.dim = sqlite3_column_int64(select.get(), 5);
            candidate.node.mention_count = sqlite3_column_int64(select.get(), 6);
            candidate.node.metadata = column_text(select.get(), 7);
            const void* bytes = sqlite3_column_blob(select.get(), 8);
            const int size = sqlite3_column_bytes(select.get(), 8);
            if (bytes != nullptr && size > 0) {
                candidate.vector = from_blob(std::string_view{static_cast<const char*>(bytes),
                                                              static_cast<std::size_t>(size)});
            }
            nodes[candidate.node.id] = candidate.node;
            by_type[candidate.node.type].push_back(std::move(candidate));
        }
    }

    // Union-find per type over pairwise cosine. Mismatched widths score 0
    // and can never merge.
    Clusters clusters;
    for (const auto& [type, candidates] : by_type) {
        for (const Candidate& candidate : candidates) {
            clusters.add(candidate.node.id);
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            for (std::size_t j = i + 1; j < candidates.size(); ++j) {
                if (cosine(candidates[i].vector, candidates[j].vector) >= threshold) {
                    clusters.unite(candidates[i].node.id, candidates[j].node.id);
                }
            }
        }
    }
    std::vector<MergeGroup> groups;
    for (auto& [root, members] : clusters.groups()) {
        if (members.size() < 2) {
            continue;
        }
        std::ranges::sort(members);
        MergeGroup group;
        group.kept = nodes[root];
        for (const std::int64_t id : members) {
            if (id != root) {
                group.merged.push_back(nodes[id]);
            }
        }
        groups.push_back(std::move(group));
    }
    if (dry_run || groups.empty()) {
        return groups;
    }

    in_transaction(handle, [&] {
        for (MergeGroup& group : groups) {
            for (const GraphNode& merged : group.merged) {
                repoint_edges(handle, group.kept.id, merged.id);
                // Union the mentions (provenance rides along), then delete
                // the node: its own mentions and its derived community
                // membership go with it through the schema's ON DELETE
                // CASCADE, and the FTS delete trigger keeps the entity index
                // in step.
                StatementPtr unite =
                    prepare(handle,
                            "INSERT OR IGNORE INTO kg_mentions (node_id, collection, chunk_id)"
                            " SELECT ?, collection, chunk_id FROM kg_mentions WHERE node_id = ?");
                sqlite3_bind_int64(unite.get(), 1, group.kept.id);
                sqlite3_bind_int64(unite.get(), 2, merged.id);
                step_done(handle, unite.get(), "could not union mentions");
                StatementPtr remove = prepare(handle, "DELETE FROM kg_nodes WHERE id = ?");
                sqlite3_bind_int64(remove.get(), 1, merged.id);
                step_done(handle, remove.get(), "could not remove a merged node");
                // First non-empty description wins; a text change
                // invalidates the survivor's vector.
                if (group.kept.description.empty() && !merged.description.empty()) {
                    group.kept.description = merged.description;
                    StatementPtr describe = prepare(handle,
                                                    "UPDATE kg_nodes SET description = ?,"
                                                    " embedding = NULL, dim = 0 WHERE id = ?");
                    bind_text(describe.get(), 1, group.kept.description);
                    sqlite3_bind_int64(describe.get(), 2, group.kept.id);
                    step_done(handle, describe.get(), "could not merge a description");
                    group.kept.dim = 0;
                }
            }
            StatementPtr recount =
                prepare(handle, "UPDATE kg_nodes SET mention_count = " +
                                    std::string{detail::kMentionCountSql} + " WHERE id = ?");
            sqlite3_bind_int64(recount.get(), 1, group.kept.id);
            step_done(handle, recount.get(), "could not recount mentions");
        }
    });
    return groups;
}

std::string DedupeScope::prose_skip_reason() const {
    if (prose_entities == 0) {
        return "no prose entities -- nothing to compare by vector";
    }
    if (prose_with_vectors == 0) {
        return "none of " + std::to_string(prose_entities) +
               " prose entities has a vector -- no embedder built them";
    }
    return {};
}

DedupeScope Store::dedupe_scope() const {
    sqlite3* handle = impl_->connection.get();
    DedupeScope out;
    StatementPtr select = prepare(
        handle,
        "SELECT"
        " (SELECT COUNT(*) FROM kg_nodes"
        "   WHERE type IN ('file', 'module', 'class', 'function')),"
        " (SELECT COUNT(*) FROM kg_nodes n"
        "   WHERE n.type IN ('file', 'module', 'class', 'function') AND"
        "   (SELECT COUNT(*) FROM kg_code_mentions m WHERE m.node_id = n.id) > 1),"
        " (SELECT COUNT(*) FROM kg_nodes"
        "   WHERE type NOT IN ('file', 'module', 'class', 'function', 'name') AND type != ?1),"
        " (SELECT COUNT(*) FROM kg_nodes"
        "   WHERE type NOT IN ('file', 'module', 'class', 'function', 'name') AND type != ?1"
        "   AND dim > 0)");
    bind_text(select.get(), 1, kNodeTypeDecision);
    if (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.code_entities = sqlite3_column_int64(select.get(), 0);
        out.code_identity_merges = sqlite3_column_int64(select.get(), 1);
        out.prose_entities = sqlite3_column_int64(select.get(), 2);
        out.prose_with_vectors = sqlite3_column_int64(select.get(), 3);
    }
    return out;
}

}  // namespace apogee::embedstore
