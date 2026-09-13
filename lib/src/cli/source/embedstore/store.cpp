#include "embedstore/store.h"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "embedstore/fts.h"
#include "embedstore/vector.h"

namespace apogee::embedstore {
namespace {

struct ConnectionDeleter {
    void operator()(sqlite3* handle) const noexcept {
        sqlite3_close(handle);
    }
};

using ConnectionPtr = std::unique_ptr<sqlite3, ConnectionDeleter>;

struct StatementDeleter {
    void operator()(sqlite3_stmt* statement) const noexcept {
        sqlite3_finalize(statement);
    }
};

using StatementPtr = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

[[noreturn]] void fail(sqlite3* handle, std::string_view what) {
    throw std::runtime_error(std::string{what} + ": " + sqlite3_errmsg(handle));
}

void exec(sqlite3* handle, const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(handle, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        // sqlite3_free, not delete: the message is SQLite's allocation.
        const std::string detail = message == nullptr ? "unknown error" : message;
        sqlite3_free(message);
        throw std::runtime_error("database error: " + detail);
    }
}

[[nodiscard]] StatementPtr prepare(sqlite3* handle, std::string_view sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(handle, sql.data(), static_cast<int>(sql.size()), &raw, nullptr) !=
        SQLITE_OK) {
        fail(handle, "could not prepare a statement");
    }
    return StatementPtr{raw};
}

void bind_text(sqlite3_stmt* statement, int index, std::string_view value) {
    // SQLITE_TRANSIENT: SQLite copies, so the caller's buffer need not outlive
    // the step. The alternative is a dangling read that works until it does not.
    sqlite3_bind_text(statement, index, value.data(), static_cast<int>(value.size()),
                      SQLITE_TRANSIENT);
}

[[nodiscard]] std::string column_text(sqlite3_stmt* statement, int index) {
    const auto* bytes = sqlite3_column_text(statement, index);
    if (bytes == nullptr) {
        return {};
    }
    // SQLite hands back unsigned char*; the same bytes as char* is what a
    // std::string holds. There is no way to cross that without a cast.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return std::string{reinterpret_cast<const char*>(bytes),
                       static_cast<std::size_t>(sqlite3_column_bytes(statement, index))};
}

/// Whether `table` already has `column` -- the guard a schema migration needs,
/// since SQLite's ADD COLUMN has no IF NOT EXISTS.
[[nodiscard]] bool has_column(sqlite3* handle, std::string_view table, std::string_view column) {
    StatementPtr info = prepare(handle, "PRAGMA table_info(" + std::string{table} + ")");
    while (sqlite3_step(info.get()) == SQLITE_ROW) {
        if (column_text(info.get(), 1) == column) {
            return true;
        }
    }
    return false;
}

/// Runs `body` inside a transaction, rolling back if it throws.
template <typename Body>
void in_transaction(sqlite3* handle, Body&& body) {
    exec(handle, "BEGIN IMMEDIATE");
    try {
        body();
    } catch (...) {
        exec(handle, "ROLLBACK");
        throw;
    }
    exec(handle, "COMMIT");
}

}  // namespace

struct Store::Impl {
    ConnectionPtr connection;
};

Store::Store(const std::filesystem::path& path) : impl_{std::make_unique<Impl>()} {
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);

    sqlite3* raw = nullptr;
    if (sqlite3_open(path.string().c_str(), &raw) != SQLITE_OK) {
        ConnectionPtr owned{raw};
        throw std::runtime_error("could not open the chunk store at " + path.string() + ": " +
                                 (raw == nullptr ? "out of memory" : sqlite3_errmsg(raw)));
    }
    impl_->connection.reset(raw);
    sqlite3* handle = impl_->connection.get();

    // WAL so a reader during an ingest is not blocked, and a busy timeout so a
    // concurrent `apogee embed` waits rather than failing outright.
    exec(handle, "PRAGMA journal_mode=WAL");
    exec(handle, "PRAGMA foreign_keys=ON");
    sqlite3_busy_timeout(handle, 5000);

    // --- migrations, in ONE transaction ------------------------------------
    //
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

        exec(handle,
             "CREATE TABLE IF NOT EXISTS chunks ("
             "  id      INTEGER PRIMARY KEY,"
             "  source  TEXT NOT NULL,"
             "  ordinal INTEGER NOT NULL,"
             "  text    TEXT NOT NULL)");
        exec(handle, "CREATE INDEX IF NOT EXISTS chunks_by_source ON chunks(source, ordinal)");

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
        // opened by an older build that lacked one gets it here.
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
    if (!vectors.empty() && vectors.size() != chunks.size()) {
        throw std::runtime_error("replace_source: " + std::to_string(vectors.size()) +
                                 " vector(s) for " + std::to_string(chunks.size()) + " chunk(s)");
    }
    sqlite3* handle = impl_->connection.get();

    // One transaction for the delete AND the insert. Replacing a source in two
    // steps leaves a window where the corpus answers from neither version, and
    // a crash inside that window leaves it answering from half of each.
    in_transaction(handle, [&] {
        StatementPtr remove = prepare(handle, "DELETE FROM chunks WHERE source = ?");
        bind_text(remove.get(), 1, source);
        if (sqlite3_step(remove.get()) != SQLITE_DONE) {
            fail(handle, "could not clear the previous chunks");
        }

        StatementPtr insert = prepare(
            handle,
            "INSERT INTO chunks(source, ordinal, text, embedding, dim) VALUES(?, ?, ?, ?, ?)");
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
                sqlite3_bind_int64(insert.get(), 5,
                                   static_cast<sqlite3_int64>(vectors[index].size()));
            }
            if (sqlite3_step(insert.get()) != SQLITE_DONE) {
                fail(handle, "could not store a chunk");
            }
        }
    });
}

std::int64_t Store::delete_source(std::string_view source) {
    sqlite3* handle = impl_->connection.get();
    StatementPtr remove = prepare(handle, "DELETE FROM chunks WHERE source = ?");
    bind_text(remove.get(), 1, source);
    if (sqlite3_step(remove.get()) != SQLITE_DONE) {
        fail(handle, "could not delete the source");
    }
    return sqlite3_changes(handle);
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
                                  "SELECT c.id, c.source, c.ordinal, c.text, bm25(chunks_fts)"
                                  "  FROM chunks_fts"
                                  "  JOIN chunks c ON c.id = chunks_fts.rowid"
                                  " WHERE chunks_fts MATCH ?"
                                  " ORDER BY bm25(chunks_fts)"
                                  " LIMIT ?");
    bind_text(select.get(), 1, match);
    sqlite3_bind_int(select.get(), 2, limit);

    std::vector<SearchHit> hits;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        SearchHit hit;
        hit.chunk.id = sqlite3_column_int64(select.get(), 0);
        hit.chunk.source = column_text(select.get(), 1);
        hit.chunk.ordinal = sqlite3_column_int64(select.get(), 2);
        hit.chunk.text = column_text(select.get(), 3);
        hit.score = normalize_bm25(sqlite3_column_double(select.get(), 4));
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
    StatementPtr select =
        prepare(handle, "SELECT id, source, ordinal, text, embedding FROM chunks WHERE dim > 0");
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

void Store::set_embedding_model(std::string_view model, std::int64_t dimension) {
    sqlite3* handle = impl_->connection.get();
    in_transaction(handle, [&] {
        StatementPtr set = prepare(handle,
                                   "INSERT INTO store_meta(key, value) VALUES(?, ?)"
                                   " ON CONFLICT(key) DO UPDATE SET value=excluded.value");
        for (const auto& [key, value] :
             {std::pair<std::string_view, std::string>{"embed_model", std::string{model}},
              std::pair<std::string_view, std::string>{"embed_dim", std::to_string(dimension)}}) {
            sqlite3_reset(set.get());
            bind_text(set.get(), 1, key);
            bind_text(set.get(), 2, value);
            if (sqlite3_step(set.get()) != SQLITE_DONE) {
                fail(handle, "could not record the embedding model");
            }
        }
    });
}

void Store::clear_embedding_model() {
    exec(impl_->connection.get(),
         "DELETE FROM store_meta WHERE key IN ('embed_model', 'embed_dim')");
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
    char* message = nullptr;
    if (sqlite3_exec(handle,
                     "INSERT INTO chunks_fts(chunks_fts, rank) VALUES('integrity-check', 1)",
                     nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string detail = message == nullptr ? "unknown error" : message;
        sqlite3_free(message);
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
