#include "embedstore/store.h"

#include <sqlite3.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include "embedstore/fts.h"
#include "embedstore/store_impl.h"
#include "embedstore/vector.h"

namespace apogee::embedstore {

using detail::bind_text;
using detail::column_text;
using detail::exec;
using detail::fail;
using detail::has_column;
using detail::in_transaction;
using detail::prepare;
using detail::StatementPtr;

namespace {

/// How long a statement waits for another connection's lock before failing.
constexpr int kBusyTimeoutMs = 5000;

/// Whether `chunks` was created without AUTOINCREMENT -- the shape every
/// store before schema v4 has.
[[nodiscard]] bool chunk_ids_reusable(sqlite3* handle) {
    StatementPtr select =
        prepare(handle, "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'chunks'");
    if (sqlite3_step(select.get()) != SQLITE_ROW) {
        return false;
    }
    return column_text(select.get(), 0).find("AUTOINCREMENT") == std::string::npos;
}

/// Rebuilds a pre-v4 `chunks` table with AUTOINCREMENT, rowids preserved.
/// The triggers reference the table by name and would follow the rename, so
/// they are dropped first and re-created by the IF NOT EXISTS statements
/// that follow in the constructor; the sequence picks up past the highest
/// id ever stored, because inserting explicit ids records them.
void ensure_chunk_ids_never_reused(sqlite3* handle) {
    if (!chunk_ids_reusable(handle)) {
        return;
    }
    for (const char* sql :
         {"DROP TRIGGER IF EXISTS chunks_ai", "DROP TRIGGER IF EXISTS chunks_ad",
          "DROP TRIGGER IF EXISTS chunks_au", "DROP INDEX IF EXISTS chunks_by_source",
          "ALTER TABLE chunks RENAME TO chunks_old",
          "CREATE TABLE chunks ("
          "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  source    TEXT NOT NULL,"
          "  ordinal   INTEGER NOT NULL,"
          "  text      TEXT NOT NULL,"
          "  embedding BLOB,"
          "  dim       INTEGER NOT NULL DEFAULT 0,"
          "  metadata  TEXT)",
          "INSERT INTO chunks (id, source, ordinal, text, embedding, dim, metadata)"
          " SELECT id, source, ordinal, text, embedding, dim, metadata FROM chunks_old",
          "DROP TABLE chunks_old"}) {
        exec(handle, sql);
    }
}

/// The schema version `handle`'s store records, read with a plain read --
/// no write lock -- or 0 when it records none: a new file, which has no
/// `store_meta` yet. Anything but a missing table (a lock held past the
/// busy timeout) fails the open.
[[nodiscard]] int recorded_schema_version(sqlite3* handle) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(handle, "SELECT value FROM store_meta WHERE key = 'schema_version'", -1,
                           &raw, nullptr) != SQLITE_OK) {
        const StatementPtr owned{raw};
        if (sqlite3_errcode(handle) == SQLITE_ERROR) {
            return 0;  // no such table: nothing has been made here yet
        }
        fail(handle, "could not read the schema version");
    }
    const StatementPtr select{raw};
    const int step = sqlite3_step(select.get());
    if (step == SQLITE_DONE) {
        return 0;
    }
    if (step != SQLITE_ROW) {
        fail(handle, "could not read the schema version");
    }
    try {
        return std::stoi(column_text(select.get(), 0));
    } catch (const std::exception&) {
        return 0;  // a malformed record migrates, and the migration rewrites it
    }
}

/// Deletes `source`'s chunks and writes `chunks` in their place, inside the
/// caller's transaction.
void write_source_rows(sqlite3* handle, std::string_view source,
                       const std::vector<std::string>& chunks,
                       const std::vector<std::vector<float>>& vectors,
                       const std::vector<std::string>& metadata) {
    StatementPtr remove = prepare(handle, "DELETE FROM chunks WHERE source = ?");
    bind_text(remove.get(), 1, source);
    if (sqlite3_step(remove.get()) != SQLITE_DONE) {
        fail(handle, "could not clear the previous chunks");
    }

    StatementPtr insert =
        prepare(handle,
                "INSERT INTO chunks(source, ordinal, text, embedding, dim, metadata)"
                " VALUES(?, ?, ?, ?, ?, ?)");
    for (std::size_t index = 0; index < chunks.size(); ++index) {
        sqlite3_reset(insert.get());
        bind_text(insert.get(), 1, source);
        sqlite3_bind_int64(insert.get(), 2, static_cast<sqlite3_int64>(index));
        bind_text(insert.get(), 3, chunks[index]);
        const std::string blob = vectors.empty() ? std::string{} : to_blob(vectors[index]);
        if (blob.empty()) {
            sqlite3_bind_null(insert.get(), 4);
            sqlite3_bind_int64(insert.get(), 5, 0);
        } else {
            sqlite3_bind_blob(insert.get(), 4, blob.data(), static_cast<int>(blob.size()),
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(insert.get(), 5, static_cast<sqlite3_int64>(vectors[index].size()));
        }
        if (metadata.empty() || metadata[index].empty()) {
            sqlite3_bind_null(insert.get(), 6);
        } else {
            bind_text(insert.get(), 6, metadata[index]);
        }
        if (sqlite3_step(insert.get()) != SQLITE_DONE) {
            fail(handle, "could not store a chunk");
        }
    }
}

/// Removes what `source`'s ingest recorded, inside the caller's transaction.
void forget_source_ingest(sqlite3* handle, std::string_view source) {
    StatementPtr remove = prepare(handle, "DELETE FROM source_ingests WHERE source = ?");
    bind_text(remove.get(), 1, source);
    if (sqlite3_step(remove.get()) != SQLITE_DONE) {
        fail(handle, "could not clear the source's ingest record");
    }
}

/// Checks the shape of a replacement before anything is written.
void check_replacement(const std::vector<std::string>& chunks,
                       const std::vector<std::vector<float>>& vectors,
                       const std::vector<std::string>& metadata) {
    if (!vectors.empty() && vectors.size() != chunks.size()) {
        throw std::runtime_error("replace_source: " + std::to_string(vectors.size()) +
                                 " vector(s) for " + std::to_string(chunks.size()) + " chunk(s)");
    }
    if (!metadata.empty() && metadata.size() < chunks.size()) {
        throw std::runtime_error("replace_source: " + std::to_string(metadata.size()) +
                                 " metadata string(s) for " + std::to_string(chunks.size()) +
                                 " chunk(s)");
    }
}

}  // namespace

Store::Store(const std::filesystem::path& path) : impl_{std::make_unique<Impl>()} {
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);

    // SQLite reads the name it is given as UTF-8 on every platform (57j):
    // `path.string()` is the ANSI code page's bytes under libc++ on Windows
    // (CI's arm64 build), which named another file -- or none.
    const std::u8string utf8_name = path.u8string();
    const std::string name(utf8_name.begin(), utf8_name.end());
    sqlite3* raw = nullptr;
    if (sqlite3_open(name.c_str(), &raw) != SQLITE_OK) {
        detail::ConnectionPtr owned{raw};
        throw std::runtime_error("could not open the chunk store at " + path.string() + ": " +
                                 (raw == nullptr ? "out of memory" : sqlite3_errmsg(raw)));
    }
    impl_->connection.reset(raw);
    sqlite3* handle = impl_->connection.get();

    // A busy timeout so a concurrent `apogee embed` waits rather than failing
    // outright -- set FIRST: the WAL pragma already reads the file, and the
    // last connection to close a WAL database checkpoints it under the file's
    // exclusive lock, so an open landing in that window (the plane listing a
    // graph a job's store was just closing) failed "database is locked" at
    // once. WAL so a reader during an ingest is not blocked.
    sqlite3_busy_timeout(handle, kBusyTimeoutMs);
    exec(handle, "PRAGMA journal_mode=WAL");
    exec(handle, "PRAGMA foreign_keys=ON");

    // --- migrations, in ONE transaction, and only when there are some ------
    //
    // A store this build already made current is opened with the plain read
    // above and nothing else (57s): `BEGIN IMMEDIATE` is SQLite's one writer
    // lock, and taking it on every open put each listing -- `embed list`,
    // RAG's collection list, the app's poll -- behind an ingest's
    // per-source transaction, until the busy timeout failed it as "database
    // is locked" and the collection read as empty. A store a newer build
    // made is read as it is: rewriting its version down would only make
    // that build migrate again. So a step added below without a bump of
    // `kSchemaVersion` never reaches a current store.
    if (recorded_schema_version(handle) >= kSchemaVersion) {
        return;
    }

    // The acceptance criterion is that an interrupted first open rolls back
    // whole. SQLite's DDL is transactional, so wrapping every step means a
    // process killed midway leaves a database with the entire schema or none
    // of it -- never the half-built state that fails much later with a missing
    // table nobody can explain.
    in_transaction(handle, [&] {
        exec(handle,
             "CREATE TABLE IF NOT EXISTS store_meta ("
             "  key TEXT PRIMARY KEY,"
             "  value TEXT NOT NULL)");

        // AUTOINCREMENT, so a chunk id is NEVER reused: re-ingesting a
        // source deletes and re-creates its rows, and the graph's staleness
        // fingerprint reads the highest id to catch a same-count re-ingest.
        // A plain INTEGER PRIMARY KEY hands the deleted maximum straight
        // back to the next insert, and the fingerprint would miss exactly
        // the case it exists for.
        exec(handle,
             "CREATE TABLE IF NOT EXISTS chunks ("
             "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
             "  source  TEXT NOT NULL,"
             "  ordinal INTEGER NOT NULL,"
             "  text    TEXT NOT NULL)");

        // v1 -> v2: vectors beside the text. Guarded, because a store made by
        // this build already has them and ADD COLUMN cannot say IF NOT EXISTS.
        // Existing rows read as `dim = 0` -- lexical-only -- which is exactly
        // what they are.
        if (!has_column(handle, "chunks", "embedding")) {
            exec(handle, "ALTER TABLE chunks ADD COLUMN embedding BLOB");
        }
        if (!has_column(handle, "chunks", "dim")) {
            exec(handle, "ALTER TABLE chunks ADD COLUMN dim INTEGER NOT NULL DEFAULT 0");
        }

        // v2 -> v3: structured data beside the text. Nullable, so every
        // existing row reads as "no metadata" -- an ordinary chunk -- and
        // the FTS triggers below never see the column: metadata is never
        // indexed, which is the archive-rich / surface-thin rule at the
        // storage layer.
        if (!has_column(handle, "chunks", "metadata")) {
            exec(handle, "ALTER TABLE chunks ADD COLUMN metadata TEXT");
        }

        // v3 -> v4: ids never reused (see the CREATE above). A table made
        // by an earlier build is rebuilt in place -- rowids preserved, so
        // the external-content FTS index stays valid -- and the sequence
        // picks up past the highest id ever stored.
        ensure_chunk_ids_never_reused(handle);
        exec(handle, "CREATE INDEX IF NOT EXISTS chunks_by_source ON chunks(source, ordinal)");

        // An EXTERNAL-CONTENT index: the text lives once, in `chunks`, and FTS5
        // holds only the inverted index over it. Storing it twice would double
        // a corpus on disk and create a second copy to fall out of step.
        exec(handle,
             "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
             "  text,"
             "  content='chunks',"
             "  content_rowid='id',"
             "  tokenize='unicode61')");

        // Triggers, so the index is maintained on EVERY write rather than
        // rebuilt on demand. `IF NOT EXISTS` makes them self-healing: a store
        // an older build made that lacked one gets it at its migration --
        // the only time a store can lack one.
        exec(handle,
             "CREATE TRIGGER IF NOT EXISTS chunks_ai AFTER INSERT ON chunks BEGIN"
             "  INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text);"
             "END");
        exec(
            handle,
            "CREATE TRIGGER IF NOT EXISTS chunks_ad AFTER DELETE ON chunks BEGIN"
            "  INSERT INTO chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text);"
            "END");
        exec(
            handle,
            "CREATE TRIGGER IF NOT EXISTS chunks_au AFTER UPDATE ON chunks BEGIN"
            "  INSERT INTO chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text);"
            "  INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text);"
            "END");

        // v3 -> v4: the knowledge-graph tables beside the chunks, and the
        // entity full-text index. Empty until `apogee graph build` fills them.
        detail::ensure_graph_schema(handle);

        // v6 -> v7 (57s): the folder each source was ingested from, so a
        // same-named document from another folder is told from a re-ingest
        // of the same one, and the model its vectors are from, so a store
        // partly re-embedded under another model knows which spaces it
        // holds. Empty on a migrated store: what an earlier build stored has
        // no folder recorded, and replaces as it always did.
        exec(handle,
             "CREATE TABLE IF NOT EXISTS source_ingests ("
             "  source TEXT PRIMARY KEY,"
             "  root   TEXT NOT NULL,"
             "  model  TEXT)");

        StatementPtr set = prepare(handle,
                                   "INSERT INTO store_meta(key, value) VALUES('schema_version', ?)"
                                   " ON CONFLICT(key) DO UPDATE SET value=excluded.value");
        bind_text(set.get(), 1, std::to_string(kSchemaVersion));
        if (sqlite3_step(set.get()) != SQLITE_DONE) {
            fail(handle, "could not record the schema version");
        }
    });
}

Store::~Store() = default;
Store::Store(Store&&) noexcept = default;
Store& Store::operator=(Store&&) noexcept = default;

void Store::replace_source(std::string_view source, const std::vector<std::string>& chunks) {
    replace_source(source, chunks, {});
}

void Store::replace_source(std::string_view source, const std::vector<std::string>& chunks,
                           const std::vector<std::vector<float>>& vectors) {
    replace_source(source, chunks, vectors, {});
}

void Store::replace_source(std::string_view source, const std::vector<std::string>& chunks,
                           const std::vector<std::vector<float>>& vectors,
                           const std::vector<std::string>& metadata) {
    check_replacement(chunks, vectors, metadata);
    sqlite3* handle = impl_->connection.get();

    // One transaction for the delete AND the insert. Replacing a source in two
    // steps leaves a window where the corpus answers from neither version, and
    // a crash inside that window leaves it answering from half of each. A
    // source written without a folder keeps none: an old one would name the
    // folder of text that is no longer there.
    in_transaction(handle, [&] {
        write_source_rows(handle, source, chunks, vectors, metadata);
        forget_source_ingest(handle, source);
    });
}

void Store::ingest_source(std::string_view source, std::string_view root, std::string_view model,
                          const std::vector<std::string>& chunks,
                          const std::vector<std::vector<float>>& vectors) {
    check_replacement(chunks, vectors, {});
    sqlite3* handle = impl_->connection.get();
    in_transaction(handle, [&] {
        write_source_rows(handle, source, chunks, vectors, {});
        StatementPtr record = prepare(
            handle,
            "INSERT INTO source_ingests(source, root, model) VALUES(?, ?, ?)"
            " ON CONFLICT(source) DO UPDATE SET root = excluded.root, model = excluded.model");
        bind_text(record.get(), 1, source);
        bind_text(record.get(), 2, root);
        if (vectors.empty() || model.empty()) {
            sqlite3_bind_null(record.get(), 3);
        } else {
            bind_text(record.get(), 3, model);
        }
        if (sqlite3_step(record.get()) != SQLITE_DONE) {
            fail(handle, "could not record the source's ingest");
        }
    });
}

std::map<std::string, Store::SourceIngest> Store::source_ingests() const {
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT source, root, model FROM source_ingests");
    std::map<std::string, SourceIngest> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.emplace(column_text(select.get(), 0),
                    SourceIngest{.root = column_text(select.get(), 1),
                                 .model = column_text(select.get(), 2)});
    }
    return out;
}

std::vector<std::string> Store::vectored_sources() const {
    StatementPtr select =
        prepare(impl_->connection.get(),
                "SELECT DISTINCT source FROM chunks WHERE dim > 0 ORDER BY source");
    std::vector<std::string> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(column_text(select.get(), 0));
    }
    return out;
}

std::int64_t Store::delete_source(std::string_view source) {
    sqlite3* handle = impl_->connection.get();
    std::int64_t gone = 0;
    in_transaction(handle, [&] {
        StatementPtr remove = prepare(handle, "DELETE FROM chunks WHERE source = ?");
        bind_text(remove.get(), 1, source);
        if (sqlite3_step(remove.get()) != SQLITE_DONE) {
            fail(handle, "could not delete the source");
        }
        gone = sqlite3_changes(handle);
        forget_source_ingest(handle, source);
    });
    return gone;
}

std::int64_t Store::update_metadata(std::string_view source, std::string_view metadata) {
    sqlite3* handle = impl_->connection.get();
    // An UPDATE that names only `metadata` fires the FTS update trigger with
    // old.text == new.text: the index is rewritten to the same bytes, and the
    // vector column is not on the statement at all. The test that reads the
    // vector back after an edit is what holds this to "not re-embedded".
    StatementPtr update = prepare(handle, "UPDATE chunks SET metadata = ? WHERE source = ?");
    if (metadata.empty()) {
        sqlite3_bind_null(update.get(), 1);
    } else {
        bind_text(update.get(), 1, metadata);
    }
    bind_text(update.get(), 2, source);
    if (sqlite3_step(update.get()) != SQLITE_DONE) {
        fail(handle, "could not update the metadata");
    }
    return sqlite3_changes(handle);
}

namespace {

/// Reads the `id, source, ordinal, text, metadata` columns of a stepped row.
[[nodiscard]] Chunk chunk_row(sqlite3_stmt* statement) {
    Chunk chunk;
    chunk.id = sqlite3_column_int64(statement, 0);
    chunk.source = column_text(statement, 1);
    chunk.ordinal = sqlite3_column_int64(statement, 2);
    chunk.text = column_text(statement, 3);
    chunk.metadata = column_text(statement, 4);
    return chunk;
}

}  // namespace

std::vector<Chunk> Store::chunks_with_metadata() const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT id, source, ordinal, text, metadata FROM chunks"
                                  " WHERE metadata IS NOT NULL ORDER BY id");
    std::vector<Chunk> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(chunk_row(select.get()));
    }
    return out;
}

std::optional<Chunk> Store::chunk_by_id(std::int64_t id) const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT id, source, ordinal, text, metadata FROM chunks"
                                  " WHERE id = ?");
    sqlite3_bind_int64(select.get(), 1, id);
    if (sqlite3_step(select.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    return chunk_row(select.get());
}

std::vector<float> Store::chunk_vector(std::int64_t id) const {
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT embedding FROM chunks WHERE id = ? AND dim > 0");
    sqlite3_bind_int64(select.get(), 1, id);
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

std::vector<SearchHit> Store::search(std::string_view query, int limit) const {
    const std::string match = fts_match_query(query);
    if (match.empty()) {
        // No usable terms. An empty MATCH is itself a syntax error, so this is
        // "no results" rather than something to hand to SQLite.
        return {};
    }

    sqlite3* handle = impl_->connection.get();
    StatementPtr select = prepare(handle,
                                  "SELECT c.id, c.source, c.ordinal, c.text, bm25(chunks_fts),"
                                  "       c.metadata"
                                  "  FROM chunks_fts"
                                  "  JOIN chunks c ON c.id = chunks_fts.rowid"
                                  " WHERE chunks_fts MATCH ?"
                                  " ORDER BY bm25(chunks_fts)"
                                  " LIMIT ?");
    bind_text(select.get(), 1, match);
    // SQLite reads a negative LIMIT as "no limit".
    sqlite3_bind_int(select.get(), 2, limit > 0 ? limit : -1);

    std::vector<SearchHit> hits;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        SearchHit hit;
        hit.chunk.id = sqlite3_column_int64(select.get(), 0);
        hit.chunk.source = column_text(select.get(), 1);
        hit.chunk.ordinal = sqlite3_column_int64(select.get(), 2);
        hit.chunk.text = column_text(select.get(), 3);
        hit.score = normalize_bm25(sqlite3_column_double(select.get(), 4));
        hit.chunk.metadata = column_text(select.get(), 5);
        hit.retriever = "lexical";
        hits.push_back(std::move(hit));
    }
    return hits;
}

std::vector<SearchHit> Store::search_vector(const std::vector<float>& query_vector,
                                            int limit) const {
    std::vector<SearchHit> hits;
    if (query_vector.empty()) {
        return hits;
    }
    sqlite3* handle = impl_->connection.get();
    // Brute force over every vector: the recorded default, revisited when a
    // collection outgrows a linear scan. `dim > 0` leaves lexical-only rows
    // out rather than scoring them at zero and letting them sink the list.
    StatementPtr select = prepare(
        handle, "SELECT id, source, ordinal, text, embedding, metadata FROM chunks WHERE dim > 0");
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        const void* bytes = sqlite3_column_blob(select.get(), 4);
        const int size = sqlite3_column_bytes(select.get(), 4);
        if (bytes == nullptr || size <= 0) {
            continue;
        }
        SearchHit hit;
        hit.chunk.id = sqlite3_column_int64(select.get(), 0);
        hit.chunk.source = column_text(select.get(), 1);
        hit.chunk.ordinal = sqlite3_column_int64(select.get(), 2);
        hit.chunk.text = column_text(select.get(), 3);
        hit.chunk.metadata = column_text(select.get(), 5);
        hit.score =
            cosine(query_vector, from_blob(std::string_view{static_cast<const char*>(bytes),
                                                            static_cast<std::size_t>(size)}));
        hit.retriever = "vector";
        hits.push_back(std::move(hit));
    }
    std::stable_sort(hits.begin(), hits.end(), [](const SearchHit& lhs, const SearchHit& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.chunk.id < rhs.chunk.id;
    });
    if (limit > 0 && static_cast<std::size_t>(limit) < hits.size()) {
        hits.resize(static_cast<std::size_t>(limit));
    }
    return hits;
}

std::vector<SearchHit> Store::search_hybrid(const std::vector<float>& query_vector,
                                            std::string_view query, int limit) const {
    // Each half widened, then fused by rank alone. See vector.h for why depth
    // matters and why scores are never added.
    return fuse_rrf(
        {search_vector(query_vector, kHybridFetchDepth), search(query, kHybridFetchDepth)}, limit);
}

Store::Stats Store::stats() const {
    sqlite3* handle = impl_->connection.get();
    Stats out;
    out.chunk_count = chunk_count();
    // Lexical-only rows are ignored here on purpose, so a mixed store reports
    // its vector width regardless of insert order; 0 means no vectors at all.
    StatementPtr dim = prepare(handle, "SELECT dim FROM chunks WHERE dim > 0 LIMIT 1");
    if (sqlite3_step(dim.get()) == SQLITE_ROW) {
        out.dimension = sqlite3_column_int64(dim.get(), 0);
    }
    StatementPtr lexical = prepare(handle, "SELECT COUNT(*) FROM chunks WHERE dim = 0");
    if (sqlite3_step(lexical.get()) == SQLITE_ROW) {
        out.lexical_only = sqlite3_column_int64(lexical.get(), 0);
    }
    StatementPtr spaces = prepare(handle, "SELECT COUNT(DISTINCT dim) FROM chunks WHERE dim > 0");
    if (sqlite3_step(spaces.get()) == SQLITE_ROW) {
        out.vector_dims = sqlite3_column_int64(spaces.get(), 0);
    }
    return out;
}

namespace {

/// Writes `entries` into `store_meta` and deletes `cleared`, inside the
/// caller's transaction.
void write_meta(sqlite3* handle,
                std::initializer_list<std::pair<std::string_view, std::string>> entries,
                std::string_view cleared) {
    StatementPtr set = prepare(handle,
                               "INSERT INTO store_meta(key, value) VALUES(?, ?)"
                               " ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    for (const auto& [key, value] : entries) {
        sqlite3_reset(set.get());
        bind_text(set.get(), 1, key);
        bind_text(set.get(), 2, value);
        if (sqlite3_step(set.get()) != SQLITE_DONE) {
            fail(handle, "could not record the embedding model");
        }
    }
    StatementPtr remove = prepare(handle, "DELETE FROM store_meta WHERE key = ?");
    bind_text(remove.get(), 1, cleared);
    if (sqlite3_step(remove.get()) != SQLITE_DONE) {
        fail(handle, "could not record the embedding model");
    }
}

/// The mixed models as `store_meta` keeps them: one name per line, an empty
/// line for an unrecorded model.
[[nodiscard]] std::string joined_lines(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t index = 0; index < names.size(); ++index) {
        out += (index == 0 ? "" : "\n") + names[index];
    }
    return out;
}

[[nodiscard]] std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find('\n', start);
        out.emplace_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos) {
            return out;
        }
        start = end + 1;
    }
}

}  // namespace

void Store::set_embedding_model(std::string_view model, std::int64_t dimension) {
    sqlite3* handle = impl_->connection.get();
    in_transaction(handle, [&] {
        write_meta(handle,
                   {{"embed_model", std::string{model}}, {"embed_dim", std::to_string(dimension)}},
                   "embed_mixed");
    });
}

void Store::mark_embedding_mixed(const std::vector<std::string>& models, std::int64_t dimension) {
    sqlite3* handle = impl_->connection.get();
    // No `embed_model` beside the mark: a build before 57s, reading the
    // store, then finds no model recorded and demotes it wholesale too.
    in_transaction(handle, [&] {
        write_meta(
            handle,
            {{"embed_mixed", joined_lines(models)}, {"embed_dim", std::to_string(dimension)}},
            "embed_model");
    });
}

bool Store::rename_embedding_model(std::string_view from, std::string_view to) {
    if (from.empty() || to.empty() || from == to) {
        return false;
    }
    sqlite3* handle = impl_->connection.get();
    bool renamed = false;
    const auto rename = [&](const char* sql) {
        StatementPtr update = prepare(handle, sql);
        bind_text(update.get(), 1, to);
        bind_text(update.get(), 2, from);
        if (sqlite3_step(update.get()) != SQLITE_DONE) {
            fail(handle, "could not rename the embedding model");
        }
        renamed = sqlite3_changes(handle) > 0 || renamed;
    };
    in_transaction(handle, [&] {
        rename("UPDATE store_meta SET value = ? WHERE key = 'embed_model' AND value = ?");
        rename("UPDATE source_ingests SET model = ? WHERE model = ?");

        std::optional<std::string> mixed;
        {
            StatementPtr select =
                prepare(handle, "SELECT value FROM store_meta WHERE key = 'embed_mixed'");
            if (sqlite3_step(select.get()) == SQLITE_ROW) {
                mixed = column_text(select.get(), 0);
            }
        }
        if (!mixed.has_value()) {
            return;
        }
        // Each name once, in the order the mark keeps them: the old name
        // may stand beside the new one already.
        std::vector<std::string> names;
        bool named = false;
        for (std::string& name : split_lines(*mixed)) {
            if (name == from) {
                name = std::string{to};
                named = true;
            }
            if (std::ranges::find(names, name) == names.end()) {
                names.push_back(std::move(name));
            }
        }
        if (!named) {
            return;
        }
        renamed = true;
        if (names.size() == 1 && !names.front().empty()) {
            // Every vector is from one model after all: the store is bound.
            write_meta(handle, {{"embed_model", names.front()}}, "embed_mixed");
        } else {
            write_meta(handle, {{"embed_mixed", joined_lines(names)}}, "embed_model");
        }
    });
    return renamed;
}

void Store::clear_embedding_model() {
    exec(impl_->connection.get(),
         "DELETE FROM store_meta WHERE key IN ('embed_model', 'embed_dim', 'embed_mixed')");
}

Store::EmbeddingBinding Store::embedding_model() const {
    sqlite3* handle = impl_->connection.get();
    EmbeddingBinding binding;
    StatementPtr model = prepare(handle, "SELECT value FROM store_meta WHERE key='embed_model'");
    if (sqlite3_step(model.get()) == SQLITE_ROW) {
        binding.model = column_text(model.get(), 0);
    }
    StatementPtr dim = prepare(handle, "SELECT value FROM store_meta WHERE key='embed_dim'");
    if (sqlite3_step(dim.get()) == SQLITE_ROW) {
        try {
            binding.dimension = std::stoll(column_text(dim.get(), 0));
        } catch (const std::exception&) {
            binding.dimension = 0;  // a malformed record reads as unknown width
        }
    }
    StatementPtr mixed = prepare(handle, "SELECT value FROM store_meta WHERE key='embed_mixed'");
    if (sqlite3_step(mixed.get()) == SQLITE_ROW) {
        binding.mixed = split_lines(column_text(mixed.get(), 0));
        binding.model.clear();  // never one model over vectors from several
    }
    return binding;
}

std::int64_t Store::chunk_count() const {
    StatementPtr count = prepare(impl_->connection.get(), "SELECT COUNT(*) FROM chunks");
    if (sqlite3_step(count.get()) != SQLITE_ROW) {
        return 0;
    }
    return sqlite3_column_int64(count.get(), 0);
}

std::vector<std::string> Store::sources() const {
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT DISTINCT source FROM chunks ORDER BY source");
    std::vector<std::string> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(column_text(select.get(), 0));
    }
    return out;
}

std::string Store::verify_index() const {
    sqlite3* handle = impl_->connection.get();
    // The `1` argument is the point: without it FTS5 checks only that the index
    // is internally well-formed. An external-content index can be perfectly
    // well-formed and still disagree with the table it mirrors, which is the
    // failure that actually matters here.
    //
    // Without waiting for the write lock it needs: a writer elsewhere -- an
    // ingest between sources -- answers "not checked" at once, never a
    // stalled read that then claims the index failed.
    sqlite3_busy_timeout(handle, 0);
    char* message = nullptr;
    const int result = sqlite3_exec(
        handle, "INSERT INTO chunks_fts(chunks_fts, rank) VALUES('integrity-check', 1)", nullptr,
        nullptr, &message);
    sqlite3_busy_timeout(handle, kBusyTimeoutMs);
    if (result != SQLITE_OK) {
        const std::string detail = message == nullptr ? "unknown error" : message;
        sqlite3_free(message);
        if ((result & 0xFF) == SQLITE_BUSY) {
            return std::string{kIndexBeingWritten};
        }
        return detail;
    }
    return {};
}

int Store::schema_version() const {
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT value FROM store_meta WHERE key='schema_version'");
    if (sqlite3_step(select.get()) != SQLITE_ROW) {
        return 0;
    }
    return std::stoi(column_text(select.get(), 0));
}

}  // namespace apogee::embedstore
