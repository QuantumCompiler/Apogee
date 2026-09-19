#include "embedstore/graph.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <ctime>
#include <string>
#include <utility>

#include "embedstore/store.h"
#include "embedstore/store_impl.h"
#include "embedstore/vector.h"

namespace apogee::embedstore {

using detail::bind_text;
using detail::column_text;
using detail::exec;
using detail::fail;
using detail::in_transaction;
using detail::prepare;
using detail::StatementPtr;

namespace detail {

void ensure_graph_schema(sqlite3* handle) {
    // Tables first, every one IF NOT EXISTS: a store made by this build
    // already has them, and an older one gains them inside the caller's
    // transaction -- whole or not at all. `kg_mentions.chunk_id` is
    // deliberately not a foreign key: chunk ids churn on re-ingest by design,
    // and dead references are what `reconcile_graph` exists to prune. The
    // node references cascade so a node can never be deleted from under its
    // rows.
    exec(handle,
         "CREATE TABLE IF NOT EXISTS kg_nodes ("
         "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  name          TEXT    NOT NULL,"
         "  name_norm     TEXT    NOT NULL,"
         "  type          TEXT    NOT NULL,"
         "  description   TEXT    NOT NULL DEFAULT '',"
         "  embedding     BLOB,"
         "  dim           INTEGER NOT NULL DEFAULT 0,"
         "  mention_count INTEGER NOT NULL DEFAULT 0,"
         "  metadata      TEXT,"
         "  UNIQUE(name_norm, type))");
    exec(handle,
         "CREATE TABLE IF NOT EXISTS kg_edges ("
         "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  source_id   INTEGER NOT NULL REFERENCES kg_nodes(id) ON DELETE CASCADE,"
         "  target_id   INTEGER NOT NULL REFERENCES kg_nodes(id) ON DELETE CASCADE,"
         "  relation    TEXT    NOT NULL,"
         "  description TEXT    NOT NULL DEFAULT '',"
         "  weight      INTEGER NOT NULL DEFAULT 1,"
         "  metadata    TEXT,"
         "  UNIQUE(source_id, target_id, relation))");
    exec(handle, "CREATE INDEX IF NOT EXISTS kg_edges_source ON kg_edges(source_id)");
    exec(handle, "CREATE INDEX IF NOT EXISTS kg_edges_target ON kg_edges(target_id)");
    exec(handle,
         "CREATE TABLE IF NOT EXISTS kg_mentions ("
         "  node_id    INTEGER NOT NULL REFERENCES kg_nodes(id) ON DELETE CASCADE,"
         "  collection TEXT    NOT NULL DEFAULT '',"
         "  chunk_id   INTEGER NOT NULL,"
         "  PRIMARY KEY (node_id, collection, chunk_id))");
    exec(handle,
         "CREATE INDEX IF NOT EXISTS kg_mentions_chunk ON kg_mentions(collection, chunk_id)");
    exec(handle,
         "CREATE TABLE IF NOT EXISTS kg_state ("
         "  collection   TEXT    NOT NULL DEFAULT '',"
         "  source_file  TEXT    NOT NULL,"
         "  chunk_count  INTEGER NOT NULL,"
         "  max_chunk_id INTEGER NOT NULL DEFAULT 0,"
         "  extracted_at TEXT    NOT NULL,"
         "  model        TEXT    NOT NULL,"
         "  PRIMARY KEY (collection, source_file))");
    exec(handle,
         "CREATE TABLE IF NOT EXISTS graph_meta ("
         "  key   TEXT PRIMARY KEY,"
         "  value TEXT NOT NULL)");

    // The entity index: external-content FTS5 over name and description, the
    // same tokenizer as the chunk index so `fts_match_query` serves both.
    StatementPtr probe = prepare(handle,
                                 "SELECT name FROM sqlite_master"
                                 " WHERE type = 'table' AND name = 'kg_nodes_fts'");
    const bool create = sqlite3_step(probe.get()) != SQLITE_ROW;
    if (create) {
        exec(handle,
             "CREATE VIRTUAL TABLE kg_nodes_fts USING fts5("
             "  name,"
             "  description,"
             "  content='kg_nodes',"
             "  content_rowid='id',"
             "  tokenize='unicode61')");
    }
    // Triggers created idempotently even when the index exists, so a dropped
    // trigger self-heals. The update trigger fires only OF name, description:
    // a mention-count bump or an embedding write must not churn the index.
    exec(handle,
         "CREATE TRIGGER IF NOT EXISTS kg_nodes_fts_ai AFTER INSERT ON kg_nodes BEGIN"
         "  INSERT INTO kg_nodes_fts(rowid, name, description)"
         "  VALUES (new.id, new.name, new.description);"
         "END");
    exec(handle,
         "CREATE TRIGGER IF NOT EXISTS kg_nodes_fts_ad AFTER DELETE ON kg_nodes BEGIN"
         "  INSERT INTO kg_nodes_fts(kg_nodes_fts, rowid, name, description)"
         "  VALUES ('delete', old.id, old.name, old.description);"
         "END");
    exec(handle,
         "CREATE TRIGGER IF NOT EXISTS kg_nodes_fts_au"
         " AFTER UPDATE OF name, description ON kg_nodes BEGIN"
         "  INSERT INTO kg_nodes_fts(kg_nodes_fts, rowid, name, description)"
         "  VALUES ('delete', old.id, old.name, old.description);"
         "  INSERT INTO kg_nodes_fts(rowid, name, description)"
         "  VALUES (new.id, new.name, new.description);"
         "END");
    if (create) {
        // A no-op on a fresh table; heals a database whose index was dropped
        // out of band while kg_nodes kept its rows.
        exec(handle, "INSERT INTO kg_nodes_fts(kg_nodes_fts) VALUES ('rebuild')");
    }
}

}  // namespace detail

namespace {

[[nodiscard]] std::string now_rfc3339() {
    const std::time_t seconds =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
        return {};
    }
    return buffer;
}

[[nodiscard]] std::int64_t changes_of(sqlite3* handle) {
    return sqlite3_changes(handle);
}

}  // namespace

std::string normalize_entity_name(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    bool pending_space = false;
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isspace(byte) != 0) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(static_cast<char>(std::tolower(byte)));
    }
    return out;
}

std::string decision_node_metadata_json(std::string_view status, std::string_view discipline) {
    nlohmann::json out{{"kind", std::string{kDecisionNodeKind}}, {"status", std::string{status}}};
    if (!discipline.empty()) {
        out["discipline"] = std::string{discipline};
    }
    return out.dump();
}

DecisionNodeMetadata parse_decision_node_metadata(std::string_view json) {
    DecisionNodeMetadata out;
    if (json.empty()) {
        return out;
    }
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() ||
        parsed.value("kind", std::string{}) != kDecisionNodeKind) {
        return out;
    }
    out.status = parsed.value("status", std::string{});
    out.discipline = parsed.value("discipline", std::string{});
    return out;
}

UpsertResult Store::upsert_node(std::string_view name, std::string_view type,
                                std::string_view description) {
    sqlite3* handle = impl_->connection.get();
    const std::string norm = normalize_entity_name(name);
    StatementPtr lookup =
        prepare(handle, "SELECT id, description FROM kg_nodes WHERE name_norm = ? AND type = ?");
    bind_text(lookup.get(), 1, norm);
    bind_text(lookup.get(), 2, type);
    if (sqlite3_step(lookup.get()) == SQLITE_ROW) {
        UpsertResult out;
        out.id = sqlite3_column_int64(lookup.get(), 0);
        const std::string existing = column_text(lookup.get(), 1);
        if (existing.empty() && !description.empty()) {
            // A description change clears the vector: a stale embedding
            // must never outlive its text.
            StatementPtr update = prepare(
                handle,
                "UPDATE kg_nodes SET description = ?, embedding = NULL, dim = 0 WHERE id = ?");
            bind_text(update.get(), 1, description);
            sqlite3_bind_int64(update.get(), 2, out.id);
            if (sqlite3_step(update.get()) != SQLITE_DONE) {
                fail(handle, "could not update a graph node");
            }
            out.mutated = true;
        }
        return out;
    }
    StatementPtr insert =
        prepare(handle,
                "INSERT INTO kg_nodes (name, name_norm, type, description, embedding, dim)"
                " VALUES (?, ?, ?, ?, NULL, 0)");
    bind_text(insert.get(), 1, name);
    bind_text(insert.get(), 2, norm);
    bind_text(insert.get(), 3, type);
    bind_text(insert.get(), 4, description);
    if (sqlite3_step(insert.get()) != SQLITE_DONE) {
        fail(handle, "could not insert a graph node");
    }
    return UpsertResult{.id = sqlite3_last_insert_rowid(handle), .mutated = true};
}

void Store::upsert_edge(std::int64_t source_id, std::int64_t target_id, std::string_view relation,
                        std::string_view description) {
    sqlite3* handle = impl_->connection.get();
    StatementPtr upsert =
        prepare(handle,
                "INSERT INTO kg_edges (source_id, target_id, relation, description,"
                " weight) VALUES (?, ?, ?, ?, 1)"
                " ON CONFLICT(source_id, target_id, relation) DO UPDATE SET"
                "   weight = kg_edges.weight + 1,"
                "   description = CASE"
                "     WHEN kg_edges.description = '' AND excluded.description != ''"
                "     THEN excluded.description ELSE kg_edges.description END");
    sqlite3_bind_int64(upsert.get(), 1, source_id);
    sqlite3_bind_int64(upsert.get(), 2, target_id);
    bind_text(upsert.get(), 3, relation);
    bind_text(upsert.get(), 4, description);
    if (sqlite3_step(upsert.get()) != SQLITE_DONE) {
        fail(handle, "could not upsert a graph edge");
    }
}

UpsertResult Store::upsert_decision_node(std::string_view record_id, std::string_view description,
                                         std::string_view metadata) {
    sqlite3* handle = impl_->connection.get();
    const std::string norm = normalize_entity_name(record_id);
    StatementPtr lookup = prepare(handle,
                                  "SELECT id, description, COALESCE(metadata, '') FROM kg_nodes"
                                  " WHERE name_norm = ? AND type = ?");
    bind_text(lookup.get(), 1, norm);
    bind_text(lookup.get(), 2, kNodeTypeDecision);
    if (sqlite3_step(lookup.get()) == SQLITE_ROW) {
        UpsertResult out;
        out.id = sqlite3_column_int64(lookup.get(), 0);
        const std::string existing_description = column_text(lookup.get(), 1);
        const std::string existing_metadata = column_text(lookup.get(), 2);
        if (existing_description != description) {
            StatementPtr update = prepare(handle,
                                          "UPDATE kg_nodes SET description = ?, metadata = ?,"
                                          " embedding = NULL, dim = 0 WHERE id = ?");
            bind_text(update.get(), 1, description);
            bind_text(update.get(), 2, metadata);
            sqlite3_bind_int64(update.get(), 3, out.id);
            if (sqlite3_step(update.get()) != SQLITE_DONE) {
                fail(handle, "could not update a decision node");
            }
            out.mutated = true;
        } else if (existing_metadata != metadata) {
            StatementPtr update = prepare(handle, "UPDATE kg_nodes SET metadata = ? WHERE id = ?");
            bind_text(update.get(), 1, metadata);
            sqlite3_bind_int64(update.get(), 2, out.id);
            if (sqlite3_step(update.get()) != SQLITE_DONE) {
                fail(handle, "could not update a decision node");
            }
        }
        return out;
    }
    StatementPtr insert = prepare(handle,
                                  "INSERT INTO kg_nodes (name, name_norm, type, description,"
                                  " embedding, dim, metadata) VALUES (?, ?, ?, ?, NULL, 0, ?)");
    bind_text(insert.get(), 1, record_id);
    bind_text(insert.get(), 2, norm);
    bind_text(insert.get(), 3, kNodeTypeDecision);
    bind_text(insert.get(), 4, description);
    bind_text(insert.get(), 5, metadata);
    if (sqlite3_step(insert.get()) != SQLITE_DONE) {
        fail(handle, "could not insert a decision node");
    }
    return UpsertResult{.id = sqlite3_last_insert_rowid(handle), .mutated = true};
}

bool Store::ensure_edge(std::int64_t source_id, std::int64_t target_id, std::string_view relation,
                        std::string_view description) {
    sqlite3* handle = impl_->connection.get();
    StatementPtr insert = prepare(handle,
                                  "INSERT OR IGNORE INTO kg_edges (source_id, target_id, relation,"
                                  " description, weight) VALUES (?, ?, ?, ?, 1)");
    sqlite3_bind_int64(insert.get(), 1, source_id);
    sqlite3_bind_int64(insert.get(), 2, target_id);
    bind_text(insert.get(), 3, relation);
    bind_text(insert.get(), 4, description);
    if (sqlite3_step(insert.get()) != SQLITE_DONE) {
        fail(handle, "could not ensure a graph edge");
    }
    return changes_of(handle) > 0;
}

bool Store::add_mention(std::int64_t node_id, std::int64_t chunk_id) {
    sqlite3* handle = impl_->connection.get();
    bool inserted = false;
    in_transaction(handle, [&] {
        StatementPtr insert = prepare(handle,
                                      "INSERT OR IGNORE INTO kg_mentions (node_id, collection,"
                                      " chunk_id) VALUES (?, '', ?)");
        sqlite3_bind_int64(insert.get(), 1, node_id);
        sqlite3_bind_int64(insert.get(), 2, chunk_id);
        if (sqlite3_step(insert.get()) != SQLITE_DONE) {
            fail(handle, "could not add a graph mention");
        }
        inserted = changes_of(handle) > 0;
        if (inserted) {
            StatementPtr bump = prepare(
                handle, "UPDATE kg_nodes SET mention_count = mention_count + 1 WHERE id = ?");
            sqlite3_bind_int64(bump.get(), 1, node_id);
            if (sqlite3_step(bump.get()) != SQLITE_DONE) {
                fail(handle, "could not count a graph mention");
            }
        }
    });
    return inserted;
}

void Store::update_node_embedding(std::int64_t node_id, const std::vector<float>& vector) {
    sqlite3* handle = impl_->connection.get();
    StatementPtr update =
        prepare(handle, "UPDATE kg_nodes SET embedding = ?, dim = ? WHERE id = ?");
    const std::string blob = to_blob(vector);
    if (blob.empty()) {
        sqlite3_bind_null(update.get(), 1);
        sqlite3_bind_int64(update.get(), 2, 0);
    } else {
        sqlite3_bind_blob(update.get(), 1, blob.data(), static_cast<int>(blob.size()),
                          SQLITE_TRANSIENT);
        sqlite3_bind_int64(update.get(), 2, static_cast<sqlite3_int64>(vector.size()));
    }
    sqlite3_bind_int64(update.get(), 3, node_id);
    if (sqlite3_step(update.get()) != SQLITE_DONE) {
        fail(handle, "could not store an entity vector");
    }
}

std::vector<float> Store::node_vector(std::int64_t node_id) const {
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT embedding FROM kg_nodes WHERE id = ? AND dim > 0");
    sqlite3_bind_int64(select.get(), 1, node_id);
    if (sqlite3_step(select.get()) != SQLITE_ROW) {
        return {};
    }
    const void* bytes = sqlite3_column_blob(select.get(), 0);
    const int size = sqlite3_column_bytes(select.get(), 0);
    if (bytes == nullptr || size <= 0) {
        return {};
    }
    return from_blob(
        std::string_view{static_cast<const char*>(bytes), static_cast<std::size_t>(size)});
}

void Store::set_source_state(std::string_view source, std::int64_t chunk_count,
                             std::int64_t max_chunk_id, std::string_view model) {
    sqlite3* handle = impl_->connection.get();
    StatementPtr upsert = prepare(handle,
                                  "INSERT INTO kg_state (collection, source_file, chunk_count,"
                                  " max_chunk_id, extracted_at, model) VALUES ('', ?, ?, ?, ?, ?)"
                                  " ON CONFLICT(collection, source_file) DO UPDATE SET"
                                  "   chunk_count = excluded.chunk_count,"
                                  "   max_chunk_id = excluded.max_chunk_id,"
                                  "   extracted_at = excluded.extracted_at,"
                                  "   model = excluded.model");
    bind_text(upsert.get(), 1, source);
    sqlite3_bind_int64(upsert.get(), 2, chunk_count);
    sqlite3_bind_int64(upsert.get(), 3, max_chunk_id);
    bind_text(upsert.get(), 4, now_rfc3339());
    bind_text(upsert.get(), 5, model);
    if (sqlite3_step(upsert.get()) != SQLITE_DONE) {
        fail(handle, "could not record a source's extraction state");
    }
}

std::map<std::string, SourceState> Store::source_states() const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT source_file, chunk_count, max_chunk_id, extracted_at,"
                                  " model FROM kg_state WHERE collection = ''");
    std::map<std::string, SourceState> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        SourceState state;
        state.source = column_text(select.get(), 0);
        state.chunk_count = sqlite3_column_int64(select.get(), 1);
        state.max_chunk_id = sqlite3_column_int64(select.get(), 2);
        state.extracted_at = column_text(select.get(), 3);
        state.model = column_text(select.get(), 4);
        out.emplace(state.source, std::move(state));
    }
    return out;
}

std::map<std::string, ChunkSpan> Store::source_chunk_spans() const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT source, COUNT(*), MAX(id) FROM chunks GROUP BY source");
    std::map<std::string, ChunkSpan> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        ChunkSpan span;
        span.count = sqlite3_column_int64(select.get(), 1);
        span.max_id = sqlite3_column_int64(select.get(), 2);
        out.emplace(column_text(select.get(), 0), span);
    }
    return out;
}

std::vector<Chunk> Store::chunks_by_source(std::string_view source) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT id, source, ordinal, text, metadata FROM chunks"
                                  " WHERE source = ? ORDER BY ordinal");
    bind_text(select.get(), 1, source);
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

ReconcileResult Store::reconcile_graph() {
    sqlite3* handle = impl_->connection.get();
    ReconcileResult out;
    in_transaction(handle, [&] {
        const auto run = [&](const char* sql, std::int64_t& counter) {
            exec(handle, sql);
            counter += changes_of(handle);
        };
        // Mentions whose chunk is gone: re-ingesting a source deletes and
        // re-creates its rows, so this is routine, not exceptional.
        run("DELETE FROM kg_mentions WHERE collection = ''"
            " AND chunk_id NOT IN (SELECT id FROM chunks)",
            out.mentions_pruned);
        run("DELETE FROM kg_state WHERE collection = ''"
            " AND source_file NOT IN (SELECT DISTINCT source FROM chunks)",
            out.states_pruned);
        // Recompute rather than decrement: this also self-heals a count that
        // drifted for any other reason.
        exec(handle,
             "UPDATE kg_nodes SET mention_count ="
             " (SELECT COUNT(*) FROM kg_mentions WHERE node_id = kg_nodes.id)");
        run("DELETE FROM kg_edges WHERE"
            "   source_id IN (SELECT id FROM kg_nodes WHERE mention_count = 0)"
            " OR target_id IN (SELECT id FROM kg_nodes WHERE mention_count = 0)",
            out.edges_pruned);
        run("DELETE FROM kg_nodes WHERE mention_count = 0", out.nodes_pruned);
    });
    return out;
}

void Store::delete_graph() {
    sqlite3* handle = impl_->connection.get();
    in_transaction(handle, [&] {
        // Nodes go through the FTS delete trigger, keeping the entity index
        // in sync without a rebuild. Chunks are untouched.
        for (const char* sql :
             {"DELETE FROM kg_mentions", "DELETE FROM kg_edges", "DELETE FROM kg_nodes",
              "DELETE FROM kg_state", "DELETE FROM graph_meta"}) {
            exec(handle, sql);
        }
    });
}

void Store::set_graph_meta(std::string_view key, std::string_view value) {
    sqlite3* handle = impl_->connection.get();
    StatementPtr upsert = prepare(handle,
                                  "INSERT INTO graph_meta (key, value) VALUES (?, ?)"
                                  " ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    bind_text(upsert.get(), 1, key);
    bind_text(upsert.get(), 2, value);
    if (sqlite3_step(upsert.get()) != SQLITE_DONE) {
        fail(handle, "could not record graph metadata");
    }
}

std::string Store::graph_meta(std::string_view key) const {
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT value FROM graph_meta WHERE key = ?");
    bind_text(select.get(), 1, key);
    if (sqlite3_step(select.get()) != SQLITE_ROW) {
        return {};
    }
    return column_text(select.get(), 0);
}

}  // namespace apogee::embedstore
