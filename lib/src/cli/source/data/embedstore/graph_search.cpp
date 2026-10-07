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
    out.total_chunks =
        count_of(handle, "SELECT COUNT(*) FROM chunks WHERE source NOT LIKE 'graph://%'");
    out.communities = count_of(handle, "SELECT COUNT(*) FROM kg_communities");
    out.communities_unsummarised =
        count_of(handle, "SELECT COUNT(*) FROM kg_communities WHERE summary = ''");
    out.edges_extracted =
        count_of(handle, "SELECT COUNT(*) FROM kg_edges WHERE origin = 'extracted'");
    out.edges_inferred = out.edges - out.edges_extracted;
    out.code_mentions = count_of(handle, "SELECT COUNT(*) FROM kg_code_mentions");
    out.unresolved_names = count_of(handle, "SELECT COUNT(*) FROM kg_nodes WHERE type = 'name'");
    {
        // A code file's extractor id opens with its language (`cpp:...`).
        StatementPtr languages =
            prepare(handle, "SELECT model FROM kg_state WHERE content_hash != ''");
        while (sqlite3_step(languages.get()) == SQLITE_ROW) {
            const std::string extractor = column_text(languages.get(), 0);
            const std::size_t colon = extractor.find(':');
            ++out.code_files_by_language[colon == std::string::npos ? extractor
                                                                    : extractor.substr(0, colon)];
            ++out.code_files;
        }
    }
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

GraphStatsMulti Store::graph_stats_multi(const MemberStores& members) const {
    sqlite3* handle = impl_->connection.get();
    GraphStatsMulti out;
    out.totals = graph_stats();
    // The own-chunks coverage reads zero on a graph database (its chunks
    // table holds only pseudo-chunks); recompute it across the members.
    out.totals.total_chunks = 0;
    out.totals.chunks_with_mentions = 0;
    out.totals.stale_files = 0;
    for (const auto& [collection, member] : members) {
        MemberStats stats;
        stats.collection = collection;
        stats.missing = member == nullptr;
        {
            StatementPtr select =
                prepare(handle, "SELECT COUNT(*) FROM kg_mentions WHERE collection = ?");
            bind_text(select.get(), 1, collection);
            if (sqlite3_step(select.get()) == SQLITE_ROW) {
                stats.mentions = sqlite3_column_int64(select.get(), 0);
            }
        }
        {
            StatementPtr select = prepare(
                handle, "SELECT COUNT(DISTINCT chunk_id) FROM kg_mentions WHERE collection = ?");
            bind_text(select.get(), 1, collection);
            if (sqlite3_step(select.get()) == SQLITE_ROW) {
                stats.chunks_with_mentions = sqlite3_column_int64(select.get(), 0);
            }
        }
        if (member != nullptr) {
            const std::map<std::string, SourceState> states = source_states(collection);
            for (const auto& [source, span] : member->source_chunk_spans()) {
                stats.total_chunks += span.count;
                const auto it = states.find(source);
                if (it == states.end() || it->second.chunk_count != span.count ||
                    it->second.model != out.totals.extract_model ||
                    (it->second.max_chunk_id != 0 && it->second.max_chunk_id != span.max_id)) {
                    ++stats.stale_files;
                }
            }
        }
        out.totals.total_chunks += stats.total_chunks;
        out.totals.chunks_with_mentions += stats.chunks_with_mentions;
        out.totals.stale_files += stats.stale_files;
        out.members.push_back(std::move(stats));
    }
    return out;
}

std::vector<GraphNode> Store::find_nodes(std::string_view name) const {
    // A prose entity by its folded name; a code entity (27k) by its exact
    // qualified name -- its identity is case-sensitive -- or, when no code
    // entity is spelled exactly so, by the fold: `store::add` finds
    // `Store::add`, and a fold two code entities share lists both.
    StatementPtr select =
        prepare(impl_->connection.get(),
                "SELECT " + std::string{kNodeColumns} +
                    ", (type IN ('file', 'module', 'class', 'function', 'name')) AS code,"
                    " (name_norm = ?2) AS exact"
                    " FROM kg_nodes WHERE name_norm = ?1 OR (type IN ('file', 'module', 'class',"
                    " 'function', 'name') AND (name_norm = ?2 OR lower(name_norm) = ?1))"
                    " ORDER BY mention_count DESC, type");
    bind_text(select.get(), 1, normalize_entity_name(name));
    bind_text(select.get(), 2, code_identity(name));
    std::vector<GraphNode> out;
    std::vector<bool> folded;
    bool exact_code = false;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        const bool code = sqlite3_column_int(select.get(), 8) != 0;
        const bool exact = sqlite3_column_int(select.get(), 9) != 0;
        exact_code = exact_code || (code && exact);
        folded.push_back(code && !exact);
        out.push_back(node_row(select.get()));
    }
    if (exact_code) {
        std::vector<GraphNode> kept;
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (!folded[i]) {
                kept.push_back(std::move(out[i]));
            }
        }
        return kept;
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
                                  "       (e.source_id = ?) AS outgoing, p.id, p.name, p.type,"
                                  "       e.id, e.origin, e.confidence"
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
        neighbor.edge_id = sqlite3_column_int64(select.get(), 7);
        neighbor.origin = column_text(select.get(), 8);
        neighbor.confidence = sqlite3_column_type(select.get(), 9) == SQLITE_NULL
                                  ? -1.0
                                  : sqlite3_column_double(select.get(), 9);
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

std::vector<ChunkRef> Store::node_mention_refs(std::int64_t node_id, int limit) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT collection, chunk_id FROM kg_mentions"
                                  " WHERE node_id = ? ORDER BY collection, chunk_id LIMIT ?");
    sqlite3_bind_int64(select.get(), 1, node_id);
    sqlite3_bind_int(select.get(), 2, limit > 0 ? limit : -1);
    std::vector<ChunkRef> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(ChunkRef{.collection = column_text(select.get(), 0),
                               .chunk_id = sqlite3_column_int64(select.get(), 1)});
    }
    return out;
}

bool Store::has_graph() const {
    return count_of(impl_->connection.get(), "SELECT EXISTS (SELECT 1 FROM kg_nodes)") != 0;
}

std::vector<RelationCount> Store::relation_counts(std::int64_t node_id) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT relation, (source_id = ?1) AS outgoing, COUNT(*)"
                                  "  FROM kg_edges WHERE source_id = ?1 OR target_id = ?1"
                                  " GROUP BY relation, outgoing ORDER BY relation, outgoing DESC");
    sqlite3_bind_int64(select.get(), 1, node_id);
    std::vector<RelationCount> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(RelationCount{.relation = column_text(select.get(), 0),
                                    .outgoing = sqlite3_column_int64(select.get(), 1) != 0,
                                    .count = sqlite3_column_int64(select.get(), 2)});
    }
    return out;
}

std::vector<Neighbor> Store::node_neighbors(std::int64_t node_id, const NeighborFilter& filter,
                                            int limit) const {
    // Numbered parameters throughout: ?1 the node, then the relations, the
    // peer type and the limit, each at a number the filter decides.
    std::string sql =
        "SELECT e.relation, e.description, e.weight, (e.source_id = ?1) AS outgoing, p.id,"
        "       p.name, p.type, e.id, e.origin, e.confidence"
        "  FROM kg_edges e"
        "  JOIN kg_nodes p ON p.id ="
        "       CASE WHEN e.source_id = ?1 THEN e.target_id ELSE e.source_id END"
        " WHERE ";
    switch (filter.direction) {
        case EdgeDirection::Out:
            sql += "e.source_id = ?1";
            break;
        case EdgeDirection::In:
            sql += "e.target_id = ?1";
            break;
        case EdgeDirection::Both:
            sql += "(e.source_id = ?1 OR e.target_id = ?1)";
            break;
    }
    int next = 2;
    if (!filter.relations.empty()) {
        sql += " AND e.relation IN (";
        for (std::size_t i = 0; i < filter.relations.size(); ++i) {
            sql += (i == 0 ? "?" : ", ?") + std::to_string(next++);
        }
        sql += ")";
    }
    const int type_index = next++;
    if (!filter.peer_type.empty()) {
        sql += " AND p.type = ?" + std::to_string(type_index);
    }
    const int limit_index = next;
    // Structure first: an unresolved name (27k) is navigable but stands for
    // something outside the tree, so it never crowds a parsed neighbour out
    // of a capped list.
    sql +=
        " ORDER BY (p.type = 'name'), e.weight DESC, p.name, p.type, e.relation, outgoing DESC,"
        " e.id LIMIT ?" +
        std::to_string(limit_index);
    StatementPtr select = prepare(impl_->connection.get(), sql);
    sqlite3_bind_int64(select.get(), 1, node_id);
    for (std::size_t i = 0; i < filter.relations.size(); ++i) {
        bind_text(select.get(), 2 + static_cast<int>(i), filter.relations[i]);
    }
    if (!filter.peer_type.empty()) {
        bind_text(select.get(), type_index, filter.peer_type);
    }
    sqlite3_bind_int(select.get(), limit_index, limit > 0 ? limit : -1);
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
        neighbor.edge_id = sqlite3_column_int64(select.get(), 7);
        neighbor.origin = column_text(select.get(), 8);
        neighbor.confidence = sqlite3_column_type(select.get(), 9) == SQLITE_NULL
                                  ? -1.0
                                  : sqlite3_column_double(select.get(), 9);
        out.push_back(std::move(neighbor));
    }
    return out;
}

std::vector<NodeResult> Store::search_node_names(std::string_view query, int limit,
                                                 bool include_unresolved) const {
    const std::string terms = fts_match_query(query);
    if (terms.empty()) {
        return {};
    }
    // A column filter: the question's words against what entities are
    // called, never what a description happens to mention.
    const std::string match = "name : (" + terms + ")";
    StatementPtr select = prepare(impl_->connection.get(),
                                  std::string{"SELECT n.id, n.name, n.name_norm, n.type,"
                                              "       n.description, n.dim, n.mention_count,"
                                              "       COALESCE(n.metadata, ''),"
                                              "       bm25(kg_nodes_fts)"
                                              "  FROM kg_nodes_fts"
                                              "  JOIN kg_nodes n ON n.id = kg_nodes_fts.rowid"
                                              " WHERE kg_nodes_fts MATCH ?"} +
                                      (include_unresolved ? "" : " AND n.type != 'name'") +
                                      " ORDER BY bm25(kg_nodes_fts), n.name, n.type LIMIT ?");
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

std::vector<GraphNode> Store::code_nodes_ending(std::string_view unqualified) const {
    if (unqualified.empty()) {
        return {};
    }
    // GLOB, not LIKE: GLOB is case-sensitive, as code identity is, and its
    // only metacharacters are escaped by bracketing them.
    std::string escaped;
    for (const char c : unqualified) {
        if (c == '*' || c == '?' || c == '[') {
            escaped += '[';
            escaped += c;
            escaped += ']';
        } else {
            escaped += c;
        }
    }
    StatementPtr select =
        prepare(impl_->connection.get(),
                "SELECT " + std::string{kNodeColumns} +
                    " FROM kg_nodes WHERE (type IN ('module', 'class', 'function', 'name')"
                    " AND (name GLOB ?1 OR name GLOB ?2)) OR (type = 'file' AND name GLOB ?3)"
                    " ORDER BY mention_count DESC, name, type");
    bind_text(select.get(), 1, "*::" + escaped);
    bind_text(select.get(), 2, "*." + escaped);
    bind_text(select.get(), 3, "*/" + escaped);
    std::vector<GraphNode> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(node_row(select.get()));
    }
    return out;
}

std::vector<CodeFile> Store::code_files() const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT DISTINCT collection, file FROM kg_code_mentions"
                                  " ORDER BY collection, file");
    std::vector<CodeFile> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(CodeFile{.collection = column_text(select.get(), 0),
                               .file = column_text(select.get(), 1)});
    }
    return out;
}

std::vector<CodeSpan> Store::code_spans_at(std::string_view collection, std::string_view file,
                                           std::int64_t line) const {
    StatementPtr select =
        prepare(impl_->connection.get(),
                "SELECT node_id, collection, file, line, MAX(end_line, line), role"
                "  FROM kg_code_mentions"
                " WHERE collection = ?1 AND file = ?2 AND line <= ?3"
                "   AND MAX(end_line, line) >= ?3 AND role IN ('definition', 'declaration')"
                " ORDER BY MAX(end_line, line) - line, line DESC, node_id");
    bind_text(select.get(), 1, collection);
    bind_text(select.get(), 2, file);
    sqlite3_bind_int64(select.get(), 3, line);
    std::vector<CodeSpan> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(CodeSpan{.node_id = sqlite3_column_int64(select.get(), 0),
                               .collection = column_text(select.get(), 1),
                               .file = column_text(select.get(), 2),
                               .line = sqlite3_column_int64(select.get(), 3),
                               .end_line = sqlite3_column_int64(select.get(), 4),
                               .role = column_text(select.get(), 5)});
    }
    return out;
}

std::vector<GraphCommunity> Store::node_communities(std::int64_t node_id) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT c.id, c.member_key, c.size, c.summary, c.model,"
                                  "       c.summarized_at"
                                  "  FROM kg_community_members m"
                                  "  JOIN kg_communities c ON c.id = m.community_id"
                                  " WHERE m.node_id = ? ORDER BY c.size DESC, c.id");
    sqlite3_bind_int64(select.get(), 1, node_id);
    std::vector<GraphCommunity> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        GraphCommunity community;
        community.id = sqlite3_column_int64(select.get(), 0);
        community.member_key = column_text(select.get(), 1);
        community.size = sqlite3_column_int64(select.get(), 2);
        community.summary = column_text(select.get(), 3);
        community.model = column_text(select.get(), 4);
        community.summarized_at = column_text(select.get(), 5);
        out.push_back(std::move(community));
    }
    return out;
}

namespace {

/// The columns `edge_row` reads, in order.
constexpr std::string_view kEdgeColumns =
    "e.id, e.source_id, e.target_id, e.relation, e.description, e.weight, e.origin, e.confidence";

[[nodiscard]] GraphEdge edge_row(sqlite3_stmt* statement) {
    GraphEdge edge;
    edge.id = sqlite3_column_int64(statement, 0);
    edge.source_id = sqlite3_column_int64(statement, 1);
    edge.target_id = sqlite3_column_int64(statement, 2);
    edge.relation = column_text(statement, 3);
    edge.description = column_text(statement, 4);
    edge.weight = sqlite3_column_int64(statement, 5);
    edge.origin = column_text(statement, 6);
    edge.confidence = sqlite3_column_type(statement, 7) == SQLITE_NULL
                          ? -1.0
                          : sqlite3_column_double(statement, 7);
    return edge;
}

}  // namespace

std::vector<GraphEdge> Store::all_edges() const {
    // An unresolved reference's name node is not structure: its edges are
    // left out, or every caller of `.push_back` would cluster as one theme.
    StatementPtr select =
        prepare(impl_->connection.get(),
                "SELECT " + std::string{kEdgeColumns} +
                    " FROM kg_edges e"
                    " JOIN kg_nodes sn ON sn.id = e.source_id"
                    " JOIN kg_nodes tn ON tn.id = e.target_id"
                    " WHERE sn.type != 'name' AND tn.type != 'name' ORDER BY e.id");
    std::vector<GraphEdge> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(edge_row(select.get()));
    }
    return out;
}

std::vector<GraphNode> Store::graph_nodes() const {
    StatementPtr select = prepare(impl_->connection.get(), "SELECT " + std::string{kNodeColumns} +
                                                               " FROM kg_nodes ORDER BY id");
    std::vector<GraphNode> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(node_row(select.get()));
    }
    return out;
}

std::vector<GraphEdge> Store::graph_edges() const {
    StatementPtr select = prepare(impl_->connection.get(), "SELECT " + std::string{kEdgeColumns} +
                                                               " FROM kg_edges e ORDER BY e.id");
    std::vector<GraphEdge> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(edge_row(select.get()));
    }
    return out;
}

std::map<std::int64_t, std::vector<std::string>> Store::node_members() const {
    // UNION, not UNION ALL: one row per (node, member) whichever table states it.
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT node_id, collection FROM kg_mentions"
                                  " UNION SELECT node_id, collection FROM kg_code_mentions"
                                  " ORDER BY 1, 2");
    std::map<std::int64_t, std::vector<std::string>> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out[sqlite3_column_int64(select.get(), 0)].push_back(column_text(select.get(), 1));
    }
    return out;
}

Expansion Store::graph_expand(const std::vector<std::int64_t>& seed_chunks,
                              const std::vector<std::int64_t>& seed_nodes, int hops,
                              int max_entities) const {
    std::vector<ChunkRef> refs;
    refs.reserve(seed_chunks.size());
    for (const std::int64_t id : seed_chunks) {
        refs.push_back(ChunkRef{.collection = "", .chunk_id = id});
    }
    return graph_expand_labelled(refs, seed_nodes, hops, max_entities);
}

Expansion Store::graph_expand_labelled(const std::vector<ChunkRef>& seed_chunks,
                                       const std::vector<std::int64_t>& seed_nodes, int hops,
                                       int max_entities) const {
    sqlite3* handle = impl_->connection.get();

    // Seed nodes: entities mentioned by the retrieved chunks -- per member
    // collection, since a chunk id means nothing without its label -- plus
    // any query-term hits handed in directly.
    std::set<std::int64_t> seeds;
    std::map<std::string, std::vector<std::int64_t>> by_collection;
    for (const ChunkRef& ref : seed_chunks) {
        by_collection[ref.collection].push_back(ref.chunk_id);
    }
    for (const auto& [collection, ids] : by_collection) {
        StatementPtr select = prepare(handle,
                                      "SELECT DISTINCT node_id FROM kg_mentions"
                                      " WHERE collection = ? AND chunk_id IN (" +
                                          placeholders(ids.size()) + ")");
        bind_text(select.get(), 1, collection);
        bind_ids(select.get(), 2, ids);
        while (sqlite3_step(select.get()) == SQLITE_ROW) {
            seeds.insert(sqlite3_column_int64(select.get(), 0));
        }
    }
    return expand_from(std::move(seeds), seed_nodes, hops, max_entities);
}

std::vector<std::int64_t> Store::code_nodes_in_excerpts(
    const std::vector<CodeExcerptRef>& excerpts) const {
    sqlite3* handle = impl_->connection.get();
    std::set<std::int64_t> out;
    // A file's recorded content names it: an excerpt's hash finds every
    // (member, file) holding those bytes -- a code file's state, never a
    // prose source's, whose hash is empty. Only a function or a class seeds
    // -- a file or a module holds everything beside the excerpt, and a walk
    // from one lists its whole contents rather than what the excerpt is
    // about.
    StatementPtr within = prepare(handle,
                                  "SELECT DISTINCT m.node_id FROM kg_state s"
                                  "  JOIN kg_code_mentions m"
                                  "    ON m.collection = s.collection AND m.file = s.source_file"
                                  "  JOIN kg_nodes n ON n.id = m.node_id"
                                  " WHERE s.content_hash = ?1 AND s.content_hash != ''"
                                  "   AND m.role IN ('definition', 'declaration')"
                                  "   AND n.type IN ('function', 'class')"
                                  "   AND m.line >= ?2 AND m.line <= ?3");
    // The one each file's excerpt sits inside: innermost (shortest span)
    // first, as `code_spans_at` orders them.
    StatementPtr holding =
        prepare(handle,
                "SELECT s.collection, s.source_file, m.node_id FROM kg_state s"
                "  JOIN kg_code_mentions m"
                "    ON m.collection = s.collection AND m.file = s.source_file"
                "  JOIN kg_nodes n ON n.id = m.node_id"
                " WHERE s.content_hash = ?1 AND s.content_hash != ''"
                "   AND m.role IN ('definition', 'declaration')"
                "   AND n.type IN ('function', 'class')"
                "   AND m.line <= ?2 AND MAX(m.end_line, m.line) >= ?2"
                " ORDER BY s.collection, s.source_file, MAX(m.end_line, m.line) - m.line,"
                "          m.line DESC, m.node_id");
    for (const CodeExcerptRef& excerpt : excerpts) {
        sqlite3_reset(within.get());
        bind_text(within.get(), 1, excerpt.content_hash);
        sqlite3_bind_int64(within.get(), 2, excerpt.first_line);
        sqlite3_bind_int64(within.get(), 3, excerpt.last_line);
        while (sqlite3_step(within.get()) == SQLITE_ROW) {
            out.insert(sqlite3_column_int64(within.get(), 0));
        }
        sqlite3_reset(holding.get());
        bind_text(holding.get(), 1, excerpt.content_hash);
        sqlite3_bind_int64(holding.get(), 2, excerpt.first_line);
        std::string file;
        while (sqlite3_step(holding.get()) == SQLITE_ROW) {
            // The first row of each file is its innermost.
            std::string at = column_text(holding.get(), 0) + '\n' + column_text(holding.get(), 1);
            if (at != file) {
                out.insert(sqlite3_column_int64(holding.get(), 2));
                file = std::move(at);
            }
        }
    }
    return {out.begin(), out.end()};
}

Expansion Store::graph_expand_excerpts(const std::vector<CodeExcerptRef>& seed_excerpts,
                                       const std::vector<std::int64_t>& seed_nodes, int hops,
                                       int max_entities) const {
    const std::vector<std::int64_t> stated = code_nodes_in_excerpts(seed_excerpts);
    return expand_from({stated.begin(), stated.end()}, seed_nodes, hops, max_entities);
}

Expansion Store::expand_from(std::set<std::int64_t> seeds,
                             const std::vector<std::int64_t>& seed_nodes, int hops,
                             int max_entities) const {
    Expansion out;
    hops = std::clamp(hops < 1 ? kDefaultGraphHops : hops, 1, kMaxGraphHops);
    if (max_entities <= 0) {
        max_entities = kDefaultGraphMaxEntities;
    }
    sqlite3* handle = impl_->connection.get();

    // An unresolved reference's name node is never context: not a seed, not
    // a step of the walk, not an entity rendered (27k).
    std::set<std::int64_t> hop0;
    for (const GraphNode& node : nodes_by_ids(seed_nodes)) {
        if (node.type != kCodeKindName) {
            hop0.insert(node.id);
        }
    }
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
                                      "SELECT e.source_id, e.target_id, e.weight FROM kg_edges e"
                                      "  JOIN kg_nodes sn ON sn.id = e.source_id"
                                      "  JOIN kg_nodes tn ON tn.id = e.target_id"
                                      " WHERE (e.source_id IN (" +
                                          placeholders(frontier.size()) + ") OR e.target_id IN (" +
                                          placeholders(frontier.size()) +
                                          ")) AND sn.type != 'name' AND tn.type != 'name'");
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
                                      "SELECT collection, chunk_id FROM kg_mentions"
                                      " WHERE node_id = ? ORDER BY collection, chunk_id LIMIT 1");
        sqlite3_bind_int64(select.get(), 1, entity.node.id);
        if (sqlite3_step(select.get()) == SQLITE_ROW) {
            entity.support_collection = column_text(select.get(), 0);
            entity.support_chunk = sqlite3_column_int64(select.get(), 1);
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
                "       e.description, e.weight, e.origin"
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
        edge.origin = column_text(edges.get(), 7);
        out.edges.push_back(std::move(edge));
    }
    return out;
}

}  // namespace apogee::embedstore
