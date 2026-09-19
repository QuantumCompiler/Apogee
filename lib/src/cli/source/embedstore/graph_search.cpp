#include "embedstore/graph_search.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "embedstore/fts.h"
#include "embedstore/store.h"
#include "embedstore/store_impl.h"

namespace apogee::embedstore {

using detail::bind_text;
using detail::column_text;
using detail::placeholders;
using detail::prepare;
using detail::StatementPtr;

namespace {

constexpr std::string_view kNodeColumns =
    "id, name, name_norm, type, description, dim, mention_count, COALESCE(metadata, '')";

[[nodiscard]] GraphNode node_row(sqlite3_stmt* statement) {
    GraphNode node;
    node.id = sqlite3_column_int64(statement, 0);
    node.name = column_text(statement, 1);
    node.name_norm = column_text(statement, 2);
    node.type = column_text(statement, 3);
    node.description = column_text(statement, 4);
    node.dim = sqlite3_column_int64(statement, 5);
    node.mention_count = sqlite3_column_int64(statement, 6);
    node.metadata = column_text(statement, 7);
    return node;
}

[[nodiscard]] std::int64_t count_of(sqlite3* handle, const char* sql) {
    StatementPtr select = prepare(handle, sql);
    if (sqlite3_step(select.get()) != SQLITE_ROW) {
        return 0;
    }
    return sqlite3_column_int64(select.get(), 0);
}

void bind_ids(sqlite3_stmt* statement, int first_index, const std::vector<std::int64_t>& ids) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        sqlite3_bind_int64(statement, first_index + static_cast<int>(i), ids[i]);
    }
}

}  // namespace

GraphStats Store::graph_stats() const {
    sqlite3* handle = impl_->connection.get();
    GraphStats out;
    out.nodes = count_of(handle, "SELECT COUNT(*) FROM kg_nodes");
    out.edges = count_of(handle, "SELECT COUNT(*) FROM kg_edges");
    out.mentions = count_of(handle, "SELECT COUNT(*) FROM kg_mentions");
    out.nodes_with_vectors = count_of(handle, "SELECT COUNT(*) FROM kg_nodes WHERE dim > 0");
    out.total_chunks = count_of(handle, "SELECT COUNT(*) FROM chunks");
    out.chunks_with_mentions =
        count_of(handle,
                 "SELECT COUNT(DISTINCT chunk_id) FROM kg_mentions"
                 " WHERE collection = '' AND chunk_id IN (SELECT id FROM chunks)");
    StatementPtr by_type = prepare(handle, "SELECT type, COUNT(*) FROM kg_nodes GROUP BY type");
    while (sqlite3_step(by_type.get()) == SQLITE_ROW) {
        out.nodes_by_type[column_text(by_type.get(), 0)] = sqlite3_column_int64(by_type.get(), 1);
    }
    out.extract_model = graph_meta(kGraphMetaExtractModel);
    const std::string failed = graph_meta(kGraphMetaFailedChunks);
    if (!failed.empty()) {
        try {
            out.failed_chunks = std::stoll(failed);
        } catch (const std::exception&) {
            out.failed_chunks = 0;  // a malformed record reads as none
        }
    }
    // The same staleness rule the planner applies, against the last build's
    // model: no row, a moved fingerprint, or another model.
    const std::map<std::string, SourceState> states = source_states();
    for (const auto& [source, span] : source_chunk_spans()) {
        const auto it = states.find(source);
        if (it == states.end() || it->second.chunk_count != span.count ||
            it->second.model != out.extract_model ||
            (it->second.max_chunk_id != 0 && it->second.max_chunk_id != span.max_id)) {
            ++out.stale_files;
        }
    }
    return out;
}

std::vector<GraphNode> Store::find_nodes(std::string_view name) const {
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT " + std::string{kNodeColumns} +
                                             " FROM kg_nodes WHERE name_norm = ?"
                                             " ORDER BY mention_count DESC, type");
    bind_text(select.get(), 1, normalize_entity_name(name));
    std::vector<GraphNode> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(node_row(select.get()));
    }
    return out;
}

std::vector<NodeResult> Store::search_nodes(std::string_view query, int limit) const {
    const std::string match = fts_match_query(query);
    if (match.empty()) {
        return {};
    }
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT n.id, n.name, n.name_norm, n.type, n.description, n.dim,"
                                  "       n.mention_count, COALESCE(n.metadata, ''),"
                                  "       bm25(kg_nodes_fts)"
                                  "  FROM kg_nodes_fts"
                                  "  JOIN kg_nodes n ON n.id = kg_nodes_fts.rowid"
                                  " WHERE kg_nodes_fts MATCH ?"
                                  " ORDER BY bm25(kg_nodes_fts)"
                                  " LIMIT ?");
    bind_text(select.get(), 1, match);
    sqlite3_bind_int(select.get(), 2, limit > 0 ? limit : -1);
    std::vector<NodeResult> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        NodeResult result;
        result.node = node_row(select.get());
        result.score = normalize_bm25(sqlite3_column_double(select.get(), 8));
        out.push_back(std::move(result));
    }
    return out;
}

std::vector<GraphNode> Store::nodes_by_ids(const std::vector<std::int64_t>& ids) const {
    std::vector<GraphNode> out;
    if (ids.empty()) {
        return out;
    }
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT " + std::string{kNodeColumns} +
                                             " FROM kg_nodes WHERE id IN (" +
                                             placeholders(ids.size()) + ") ORDER BY id");
    bind_ids(select.get(), 1, ids);
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(node_row(select.get()));
    }
    return out;
}

std::vector<Neighbor> Store::node_neighbors(std::int64_t node_id) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT e.relation, e.description, e.weight,"
                                  "       (e.source_id = ?) AS outgoing, p.id, p.name, p.type"
                                  "  FROM kg_edges e"
                                  "  JOIN kg_nodes p ON p.id ="
                                  "       CASE WHEN e.source_id = ? THEN e.target_id"
                                  "            ELSE e.source_id END"
                                  " WHERE e.source_id = ? OR e.target_id = ?"
                                  " ORDER BY e.relation, e.weight DESC, p.name");
    for (int index = 1; index <= 4; ++index) {
        sqlite3_bind_int64(select.get(), index, node_id);
    }
    std::vector<Neighbor> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        Neighbor neighbor;
        neighbor.relation = column_text(select.get(), 0);
        neighbor.description = column_text(select.get(), 1);
        neighbor.weight = sqlite3_column_int64(select.get(), 2);
        neighbor.outgoing = sqlite3_column_int64(select.get(), 3) != 0;
        neighbor.peer_id = sqlite3_column_int64(select.get(), 4);
        neighbor.peer_name = column_text(select.get(), 5);
        neighbor.peer_type = column_text(select.get(), 6);
        out.push_back(std::move(neighbor));
    }
    return out;
}

std::vector<Chunk> Store::node_chunks(std::int64_t node_id, int limit) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT c.id, c.source, c.ordinal, c.text, c.metadata"
                                  "  FROM kg_mentions m"
                                  "  JOIN chunks c ON c.id = m.chunk_id"
                                  " WHERE m.node_id = ? AND m.collection = ''"
                                  " ORDER BY c.source, c.ordinal"
                                  " LIMIT ?");
    sqlite3_bind_int64(select.get(), 1, node_id);
    sqlite3_bind_int(select.get(), 2, limit > 0 ? limit : -1);
    std::vector<Chunk> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        Chunk chunk;
        chunk.id = sqlite3_column_int64(select.get(), 0);
        chunk.source = column_text(select.get(), 1);
        chunk.ordinal = sqlite3_column_int64(select.get(), 2);
        chunk.text = column_text(select.get(), 3);
        chunk.metadata = column_text(select.get(), 4);
        out.push_back(std::move(chunk));
    }
    return out;
}

Expansion Store::graph_expand(const std::vector<std::int64_t>& seed_chunks,
                              const std::vector<std::int64_t>& seed_nodes, int hops,
                              int max_entities) const {
    Expansion out;
    hops = std::clamp(hops < 1 ? kDefaultGraphHops : hops, 1, kMaxGraphHops);
    if (max_entities <= 0) {
        max_entities = kDefaultGraphMaxEntities;
    }
    sqlite3* handle = impl_->connection.get();

    // Seed nodes: entities mentioned by the retrieved chunks, plus any
    // query-term hits handed in directly.
    std::set<std::int64_t> seeds;
    if (!seed_chunks.empty()) {
        StatementPtr select = prepare(handle,
                                      "SELECT DISTINCT node_id FROM kg_mentions"
                                      " WHERE collection = '' AND chunk_id IN (" +
                                          placeholders(seed_chunks.size()) + ")");
        bind_ids(select.get(), 1, seed_chunks);
        while (sqlite3_step(select.get()) == SQLITE_ROW) {
            seeds.insert(sqlite3_column_int64(select.get(), 0));
        }
    }
    const std::set<std::int64_t> hop0(seed_nodes.begin(), seed_nodes.end());
    seeds.insert(hop0.begin(), hop0.end());
    if (seeds.empty()) {
        return out;
    }

    // Walk outward hop by hop -- with hops <= 2, plain per-hop queries read
    // clearer than a recursive CTE and stay pure SQL -- accumulating each
    // newly reached node's connecting edge weight.
    struct Candidate {
        int hop = 0;
        std::int64_t weight_sum = 0;
    };

    std::set<std::int64_t> visited{seeds};
    std::vector<std::int64_t> frontier(seeds.begin(), seeds.end());
    std::map<std::int64_t, Candidate> candidates;
    for (int hop = 1; hop <= hops && !frontier.empty(); ++hop) {
        StatementPtr select = prepare(handle,
                                      "SELECT source_id, target_id, weight FROM kg_edges"
                                      " WHERE source_id IN (" +
                                          placeholders(frontier.size()) + ") OR target_id IN (" +
                                          placeholders(frontier.size()) + ")");
        bind_ids(select.get(), 1, frontier);
        bind_ids(select.get(), 1 + static_cast<int>(frontier.size()), frontier);
        std::set<std::int64_t> next;
        while (sqlite3_step(select.get()) == SQLITE_ROW) {
            const std::int64_t source = sqlite3_column_int64(select.get(), 0);
            const std::int64_t target = sqlite3_column_int64(select.get(), 1);
            const std::int64_t weight = sqlite3_column_int64(select.get(), 2);
            for (const auto& [from, to] : {std::pair{source, target}, std::pair{target, source}}) {
                if (!visited.contains(from) || visited.contains(to)) {
                    continue;
                }
                auto [it, inserted] = candidates.try_emplace(to, Candidate{.hop = hop});
                if (inserted) {
                    next.insert(to);
                }
                it->second.weight_sum += weight;
            }
        }
        frontier.assign(next.begin(), next.end());
        visited.insert(next.begin(), next.end());
    }

    // Load the candidates plus the hop-0 seed hits, score, rank, cap.
    std::vector<std::int64_t> ids;
    for (const auto& [id, unused] : candidates) {
        ids.push_back(id);
    }
    for (const std::int64_t id : hop0) {
        if (!candidates.contains(id)) {
            ids.push_back(id);
        }
    }
    for (const GraphNode& node : nodes_by_ids(ids)) {
        ExpandEntity entity;
        entity.node = node;
        // A seed is never a candidate (the walk only reaches unvisited
        // nodes), so a hop-0 hit is exactly a node the walk did not reach.
        const auto it = candidates.find(node.id);
        if (it != candidates.end()) {
            entity.hop = it->second.hop;
            entity.score = it->second.weight_sum * node.mention_count;
        } else {
            entity.hop = 0;
            entity.score = node.mention_count;
        }
        out.entities.push_back(std::move(entity));
    }
    std::stable_sort(out.entities.begin(), out.entities.end(),
                     [](const ExpandEntity& a, const ExpandEntity& b) {
                         if (a.hop != b.hop) {
                             return a.hop < b.hop;
                         }
                         if (a.score != b.score) {
                             return a.score > b.score;
                         }
                         return a.node.name < b.node.name;
                     });
    if (out.entities.size() > static_cast<std::size_t>(max_entities)) {
        out.entities.resize(static_cast<std::size_t>(max_entities));
    }
    for (ExpandEntity& entity : out.entities) {
        // One evidencing chunk per selected entity; none is "no support".
        StatementPtr select = prepare(handle,
                                      "SELECT chunk_id FROM kg_mentions WHERE node_id = ?"
                                      " ORDER BY collection, chunk_id LIMIT 1");
        sqlite3_bind_int64(select.get(), 1, entity.node.id);
        if (sqlite3_step(select.get()) == SQLITE_ROW) {
            entity.support_chunk = sqlite3_column_int64(select.get(), 0);
        }
    }

    // Every relation among the traversed neighbourhood (seeds + selected
    // entities), heaviest first -- the caller's budget truncates rendering.
    std::vector<std::int64_t> kept(seeds.begin(), seeds.end());
    for (const ExpandEntity& entity : out.entities) {
        if (!seeds.contains(entity.node.id)) {
            kept.push_back(entity.node.id);
        }
    }
    StatementPtr edges =
        prepare(handle,
                "SELECT e.source_id, e.target_id, sn.name, tn.name, e.relation,"
                "       e.description, e.weight"
                "  FROM kg_edges e"
                "  JOIN kg_nodes sn ON sn.id = e.source_id"
                "  JOIN kg_nodes tn ON tn.id = e.target_id"
                " WHERE e.source_id IN (" +
                    placeholders(kept.size()) + ") AND e.target_id IN (" +
                    placeholders(kept.size()) + ") ORDER BY e.weight DESC, sn.name, e.relation");
    bind_ids(edges.get(), 1, kept);
    bind_ids(edges.get(), 1 + static_cast<int>(kept.size()), kept);
    while (sqlite3_step(edges.get()) == SQLITE_ROW) {
        ExpandEdge edge;
        edge.source_id = sqlite3_column_int64(edges.get(), 0);
        edge.target_id = sqlite3_column_int64(edges.get(), 1);
        edge.source_name = column_text(edges.get(), 2);
        edge.target_name = column_text(edges.get(), 3);
        edge.relation = column_text(edges.get(), 4);
        edge.description = column_text(edges.get(), 5);
        edge.weight = sqlite3_column_int64(edges.get(), 6);
        out.edges.push_back(std::move(edge));
    }
    return out;
}

}  // namespace apogee::embedstore
