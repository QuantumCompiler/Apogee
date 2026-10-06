#include "embedstore/graph_code.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include "embedstore/store.h"
#include "embedstore/store_impl.h"
#include "platform/platform.h"

namespace apogee::embedstore {

using detail::bind_text;
using detail::column_text;
using detail::exec;
using detail::fail;
using detail::in_transaction;
using detail::prepare;
using detail::StatementPtr;

namespace {

/// When a file was parsed: UTC, RFC 3339, through the platform seam -- the
/// one place a time-zone call is spelled per platform.
[[nodiscard]] std::string now_rfc3339() {
    return platform::utc_time(std::chrono::system_clock::now(), "%Y-%m-%dT%H:%M:%SZ");
}

void step_done(sqlite3* handle, sqlite3_stmt* statement, const char* what) {
    if (sqlite3_step(statement) != SQLITE_DONE) {
        fail(handle, what);
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
}

/// The code kinds as an SQL list, for the statements that touch only them.
constexpr std::string_view kCodeKindsSql = "('file','module','class','function','name')";

}  // namespace

CodeSyncResult Store::sync_code_graph(const std::vector<CodeNodeRow>& nodes,
                                      const std::vector<CodeEdgeRow>& edges) {
    sqlite3* handle = impl_->connection.get();
    CodeSyncResult out;
    in_transaction(handle, [&] {
        // The provenance is rewritten whole; the nodes and edges it supports
        // are kept where they survive, so their ids -- and whatever hangs off
        // them, a community membership among it -- stay put.
        exec(handle, "DELETE FROM kg_code_mentions");
        exec(handle, "DELETE FROM kg_edge_sites");

        StatementPtr lookup = prepare(handle,
                                      "SELECT id, name, description, COALESCE(metadata, '')"
                                      " FROM kg_nodes WHERE name_norm = ? AND type = ?");
        StatementPtr insert = prepare(handle,
                                      "INSERT INTO kg_nodes (name, name_norm, type, description,"
                                      " embedding, dim, metadata) VALUES (?, ?, ?, ?, NULL, 0, ?)");
        // A changed text clears the vector, as everywhere: a stale embedding
        // must never outlive its text.
        StatementPtr retext = prepare(handle,
                                      "UPDATE kg_nodes SET name = ?, description = ?, metadata = ?,"
                                      " embedding = NULL, dim = 0 WHERE id = ?");
        StatementPtr remeta =
            prepare(handle, "UPDATE kg_nodes SET name = ?, metadata = ? WHERE id = ?");
        StatementPtr mention =
            prepare(handle,
                    "INSERT OR IGNORE INTO kg_code_mentions (node_id, collection,"
                    " file, line, end_line, role) VALUES (?, ?, ?, ?, ?, ?)");
        std::map<std::pair<std::string, std::string>, std::int64_t> ids;
        for (const CodeNodeRow& node : nodes) {
            const std::string norm = code_identity(node.name);
            bind_text(lookup.get(), 1, norm);
            bind_text(lookup.get(), 2, node.type);
            std::int64_t id = 0;
            if (sqlite3_step(lookup.get()) == SQLITE_ROW) {
                id = sqlite3_column_int64(lookup.get(), 0);
                const std::string name = column_text(lookup.get(), 1);
                const std::string description = column_text(lookup.get(), 2);
                const std::string metadata = column_text(lookup.get(), 3);
                sqlite3_reset(lookup.get());
                if (description != node.description) {
                    bind_text(retext.get(), 1, node.name);
                    bind_text(retext.get(), 2, node.description);
                    bind_text(retext.get(), 3, node.metadata);
                    sqlite3_bind_int64(retext.get(), 4, id);
                    step_done(handle, retext.get(), "could not update a code node");
                    ++out.nodes_changed;
                } else if (metadata != node.metadata || name != node.name) {
                    bind_text(remeta.get(), 1, node.name);
                    bind_text(remeta.get(), 2, node.metadata);
                    sqlite3_bind_int64(remeta.get(), 3, id);
                    step_done(handle, remeta.get(), "could not update a code node");
                    ++out.nodes_changed;
                }
            } else {
                sqlite3_reset(lookup.get());
                bind_text(insert.get(), 1, node.name);
                bind_text(insert.get(), 2, norm);
                bind_text(insert.get(), 3, node.type);
                bind_text(insert.get(), 4, node.description);
                bind_text(insert.get(), 5, node.metadata);
                step_done(handle, insert.get(), "could not insert a code node");
                id = sqlite3_last_insert_rowid(handle);
                ++out.nodes_added;
            }
            sqlite3_clear_bindings(lookup.get());
            ids[{node.type, norm}] = id;
            for (const CodeMentionRow& row : node.mentions) {
                sqlite3_bind_int64(mention.get(), 1, id);
                bind_text(mention.get(), 2, row.collection);
                bind_text(mention.get(), 3, row.file);
                sqlite3_bind_int64(mention.get(), 4, row.line);
                sqlite3_bind_int64(mention.get(), 5, row.end_line);
                bind_text(mention.get(), 6, row.role);
                step_done(handle, mention.get(), "could not record a code mention");
                ++out.mentions;
            }
        }

        StatementPtr find_edge = prepare(handle,
                                         "SELECT id, origin FROM kg_edges"
                                         " WHERE source_id = ? AND target_id = ? AND relation = ?");
        StatementPtr add_edge = prepare(handle,
                                        "INSERT INTO kg_edges (source_id, target_id, relation,"
                                        " description, weight, origin, confidence)"
                                        " VALUES (?, ?, ?, '', 0, 'extracted', ?)");
        // An edge a parse states is `extracted` whatever stated it before: an
        // extracted edge never merges down to inferred, and an inferred one
        // the source now states is promoted.
        StatementPtr promote = prepare(
            handle, "UPDATE kg_edges SET origin = 'extracted', confidence = ? WHERE id = ?");
        StatementPtr site = prepare(handle,
                                    "INSERT OR IGNORE INTO kg_edge_sites (edge_id, collection,"
                                    " file, line) VALUES (?, ?, ?, ?)");
        for (const CodeEdgeRow& edge : edges) {
            const auto source = ids.find({edge.source_type, code_identity(edge.source_name)});
            const auto target = ids.find({edge.target_type, code_identity(edge.target_name)});
            if (source == ids.end() || target == ids.end() || source->second == target->second) {
                continue;
            }
            sqlite3_bind_int64(find_edge.get(), 1, source->second);
            sqlite3_bind_int64(find_edge.get(), 2, target->second);
            bind_text(find_edge.get(), 3, edge.relation);
            std::int64_t id = 0;
            if (sqlite3_step(find_edge.get()) == SQLITE_ROW) {
                id = sqlite3_column_int64(find_edge.get(), 0);
                const std::string origin = column_text(find_edge.get(), 1);
                sqlite3_reset(find_edge.get());
                if (origin != kOriginExtracted) {
                    sqlite3_bind_double(promote.get(), 1, kExtractedConfidence);
                    sqlite3_bind_int64(promote.get(), 2, id);
                    step_done(handle, promote.get(), "could not promote an edge");
                }
            } else {
                sqlite3_reset(find_edge.get());
                sqlite3_bind_int64(add_edge.get(), 1, source->second);
                sqlite3_bind_int64(add_edge.get(), 2, target->second);
                bind_text(add_edge.get(), 3, edge.relation);
                sqlite3_bind_double(add_edge.get(), 4, kExtractedConfidence);
                step_done(handle, add_edge.get(), "could not insert a code edge");
                id = sqlite3_last_insert_rowid(handle);
                ++out.edges_added;
            }
            sqlite3_clear_bindings(find_edge.get());
            for (const CodeSiteRow& row : edge.sites) {
                sqlite3_bind_int64(site.get(), 1, id);
                bind_text(site.get(), 2, row.collection);
                bind_text(site.get(), 3, row.file);
                sqlite3_bind_int64(site.get(), 4, row.line);
                step_done(handle, site.get(), "could not record an edge site");
                ++out.sites;
            }
        }

        // An extracted edge is as heavy as the sites that state it, and gone
        // when none does any longer.
        exec(handle,
             "UPDATE kg_edges SET weight ="
             " (SELECT COUNT(*) FROM kg_edge_sites WHERE edge_id = kg_edges.id)"
             " WHERE origin = 'extracted'");
        exec(handle,
             "DELETE FROM kg_edges WHERE origin = 'extracted' AND NOT EXISTS"
             " (SELECT 1 FROM kg_edge_sites WHERE edge_id = kg_edges.id)");
        out.edges_removed = sqlite3_changes(handle);
        exec(handle,
             ("UPDATE kg_nodes SET mention_count = " + std::string{detail::kMentionCountSql})
                 .c_str());
        // A code node nothing mentions any longer goes, with its edges and any
        // community membership (the schema cascades). A prose node is the
        // prose build's reconcile's to retire, never this.
        exec(handle, ("DELETE FROM kg_nodes WHERE mention_count = 0 AND type IN " +
                      std::string{kCodeKindsSql})
                         .c_str());
        out.nodes_removed = sqlite3_changes(handle);
    });
    return out;
}

std::map<std::string, CodeFileState> Store::code_file_states(std::string_view collection) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT collection, source_file, content_hash, model,"
                                  " COALESCE(facts, ''), extracted_at FROM kg_state"
                                  " WHERE collection = ? AND content_hash != ''");
    bind_text(select.get(), 1, collection);
    std::map<std::string, CodeFileState> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        CodeFileState state;
        state.collection = column_text(select.get(), 0);
        state.file = column_text(select.get(), 1);
        state.content_hash = column_text(select.get(), 2);
        state.extractor = column_text(select.get(), 3);
        state.facts = column_text(select.get(), 4);
        state.parsed_at = column_text(select.get(), 5);
        out.emplace(state.file, std::move(state));
    }
    return out;
}

std::vector<std::string> Store::code_members() const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT DISTINCT collection FROM kg_state"
                                  " WHERE content_hash != '' ORDER BY collection");
    std::vector<std::string> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(column_text(select.get(), 0));
    }
    return out;
}

void Store::set_code_file_state(const CodeFileState& state) {
    sqlite3* handle = impl_->connection.get();
    if (state.content_hash.empty()) {
        throw std::invalid_argument("a code file's state needs its content hash");
    }
    StatementPtr upsert = prepare(handle,
                                  "INSERT INTO kg_state (collection, source_file, chunk_count,"
                                  " max_chunk_id, extracted_at, model, content_hash, facts)"
                                  " VALUES (?, ?, 0, 0, ?, ?, ?, ?)"
                                  " ON CONFLICT(collection, source_file) DO UPDATE SET"
                                  "   extracted_at = excluded.extracted_at,"
                                  "   model = excluded.model,"
                                  "   content_hash = excluded.content_hash,"
                                  "   facts = excluded.facts");
    bind_text(upsert.get(), 1, state.collection);
    bind_text(upsert.get(), 2, state.file);
    bind_text(upsert.get(), 3, state.parsed_at.empty() ? now_rfc3339() : state.parsed_at);
    bind_text(upsert.get(), 4, state.extractor);
    bind_text(upsert.get(), 5, state.content_hash);
    bind_text(upsert.get(), 6, state.facts);
    if (sqlite3_step(upsert.get()) != SQLITE_DONE) {
        fail(handle, "could not record a code file's state");
    }
}

std::int64_t Store::remove_code_files(std::string_view collection,
                                      const std::vector<std::string>& files) {
    sqlite3* handle = impl_->connection.get();
    std::int64_t removed = 0;
    in_transaction(handle, [&] {
        StatementPtr remove = prepare(handle,
                                      "DELETE FROM kg_state WHERE collection = ? AND"
                                      " source_file = ? AND content_hash != ''");
        for (const std::string& file : files) {
            bind_text(remove.get(), 1, collection);
            bind_text(remove.get(), 2, file);
            step_done(handle, remove.get(), "could not forget a code file");
            removed += sqlite3_changes(handle);
        }
    });
    return removed;
}

std::int64_t Store::remove_code_member(std::string_view collection) {
    sqlite3* handle = impl_->connection.get();
    StatementPtr remove =
        prepare(handle, "DELETE FROM kg_state WHERE collection = ? AND content_hash != ''");
    bind_text(remove.get(), 1, collection);
    if (sqlite3_step(remove.get()) != SQLITE_DONE) {
        fail(handle, "could not forget a source member");
    }
    return sqlite3_changes(handle);
}

std::vector<CodeMention> Store::node_code_mentions(std::int64_t node_id, int limit) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT collection, file, line, end_line, role"
                                  " FROM kg_code_mentions WHERE node_id = ?"
                                  " ORDER BY CASE role WHEN 'definition' THEN 0"
                                  "   WHEN 'declaration' THEN 1 ELSE 2 END,"
                                  " collection, file, line LIMIT ?");
    sqlite3_bind_int64(select.get(), 1, node_id);
    sqlite3_bind_int(select.get(), 2, limit > 0 ? limit : -1);
    std::vector<CodeMention> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(CodeMention{.collection = column_text(select.get(), 0),
                                  .file = column_text(select.get(), 1),
                                  .line = sqlite3_column_int64(select.get(), 2),
                                  .end_line = sqlite3_column_int64(select.get(), 3),
                                  .role = column_text(select.get(), 4)});
    }
    return out;
}

std::vector<EdgeSite> Store::edge_sites(std::int64_t edge_id, int limit) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT collection, file, line FROM kg_edge_sites"
                                  " WHERE edge_id = ? ORDER BY collection, file, line LIMIT ?");
    sqlite3_bind_int64(select.get(), 1, edge_id);
    sqlite3_bind_int(select.get(), 2, limit > 0 ? limit : -1);
    std::vector<EdgeSite> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(EdgeSite{.collection = column_text(select.get(), 0),
                               .file = column_text(select.get(), 1),
                               .line = sqlite3_column_int64(select.get(), 2)});
    }
    return out;
}

std::string Store::graph_dump() const {
    sqlite3* handle = impl_->connection.get();
    std::string out;
    // Nodes in identity order, each with its provenance; no row ids, no
    // vectors, no timestamps -- what two stores holding the same graph agree
    // on byte for byte.
    std::map<std::int64_t, std::pair<std::string, std::string>> labels;
    {
        StatementPtr nodes = prepare(handle,
                                     "SELECT id, type, name, description, COALESCE(metadata, ''),"
                                     " mention_count FROM kg_nodes ORDER BY type, name_norm, name");
        StatementPtr chunks = prepare(handle,
                                      "SELECT collection, chunk_id FROM kg_mentions"
                                      " WHERE node_id = ? ORDER BY collection, chunk_id");
        StatementPtr code = prepare(handle,
                                    "SELECT collection, file, line, end_line, role FROM"
                                    " kg_code_mentions WHERE node_id = ?"
                                    " ORDER BY collection, file, line, role");
        while (sqlite3_step(nodes.get()) == SQLITE_ROW) {
            const std::int64_t id = sqlite3_column_int64(nodes.get(), 0);
            const std::string type = column_text(nodes.get(), 1);
            const std::string name = column_text(nodes.get(), 2);
            labels[id] = {type, name};
            out += "node " + type + " " + name + "\n";
            out += "  description " + column_text(nodes.get(), 3) + "\n";
            out += "  metadata " + column_text(nodes.get(), 4) + "\n";
            out += "  mentions " + std::to_string(sqlite3_column_int64(nodes.get(), 5)) + "\n";
            sqlite3_bind_int64(chunks.get(), 1, id);
            while (sqlite3_step(chunks.get()) == SQLITE_ROW) {
                out += "  chunk " + column_text(chunks.get(), 0) + ":" +
                       std::to_string(sqlite3_column_int64(chunks.get(), 1)) + "\n";
            }
            sqlite3_reset(chunks.get());
            sqlite3_bind_int64(code.get(), 1, id);
            while (sqlite3_step(code.get()) == SQLITE_ROW) {
                out += "  at " + column_text(code.get(), 0) + ":" + column_text(code.get(), 1) +
                       ":" + std::to_string(sqlite3_column_int64(code.get(), 2)) + "-" +
                       std::to_string(sqlite3_column_int64(code.get(), 3)) + " " +
                       column_text(code.get(), 4) + "\n";
            }
            sqlite3_reset(code.get());
        }
    }

    struct EdgeLine {
        std::tuple<std::string, std::string, std::string, std::string, std::string> key;
        std::string text;
    };

    std::vector<EdgeLine> lines;
    {
        StatementPtr edges = prepare(handle,
                                     "SELECT id, source_id, target_id, relation, description,"
                                     " weight, origin, confidence FROM kg_edges");
        StatementPtr sites = prepare(handle,
                                     "SELECT collection, file, line FROM kg_edge_sites"
                                     " WHERE edge_id = ? ORDER BY collection, file, line");
        while (sqlite3_step(edges.get()) == SQLITE_ROW) {
            const std::int64_t id = sqlite3_column_int64(edges.get(), 0);
            const auto& source = labels[sqlite3_column_int64(edges.get(), 1)];
            const auto& target = labels[sqlite3_column_int64(edges.get(), 2)];
            const std::string relation = column_text(edges.get(), 3);
            EdgeLine line;
            line.key = {source.first, source.second, relation, target.first, target.second};
            line.text = "edge " + source.first + " " + source.second + " -[" + relation + "]-> " +
                        target.first + " " + target.second + "\n";
            line.text += "  description " + column_text(edges.get(), 4) + "\n";
            line.text += "  weight " + std::to_string(sqlite3_column_int64(edges.get(), 5)) +
                         " origin " + column_text(edges.get(), 6) + " confidence " +
                         (sqlite3_column_type(edges.get(), 7) == SQLITE_NULL
                              ? std::string{"-"}
                              : std::to_string(sqlite3_column_double(edges.get(), 7))) +
                         "\n";
            sqlite3_bind_int64(sites.get(), 1, id);
            while (sqlite3_step(sites.get()) == SQLITE_ROW) {
                line.text += "  site " + column_text(sites.get(), 0) + ":" +
                             column_text(sites.get(), 1) + ":" +
                             std::to_string(sqlite3_column_int64(sites.get(), 2)) + "\n";
            }
            sqlite3_reset(sites.get());
            lines.push_back(std::move(line));
        }
    }
    std::ranges::sort(lines, [](const EdgeLine& a, const EdgeLine& b) { return a.key < b.key; });
    for (const EdgeLine& line : lines) {
        out += line.text;
    }
    return out;
}

}  // namespace apogee::embedstore
