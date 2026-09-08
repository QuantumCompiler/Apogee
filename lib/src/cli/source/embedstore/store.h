#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/// The chunk store: SQLite with an FTS5 index, one database per collection.
///
/// **The permanent retrieval floor.** Lexical BM25 needs no embedding model, no
/// API key, and no network — which is why it is built first and why it stays
/// even though Apogee has cloud embedders. Ommi's recorded history is the
/// argument: its RAG survived an embedding-model freeze *only* because the
/// lexical path was model-free. A retrieval story whose floor depends on a
/// model is a story that stops working when the model does.
///
/// ## What a caller can rely on
///
/// - **Open is idempotent and migrations are atomic.** Every schema step runs
///   inside one transaction, so an interrupted first open leaves a database
///   that either has the whole schema or none of it — never half.
/// - **The FTS index is maintained on every ingest**, regardless of which
///   retriever a query later uses. An index built lazily is an index that is
///   missing exactly when someone first needs it.
/// - **Re-ingesting a source replaces its chunks** rather than duplicating
///   them, so running ingest twice is not a slow way to double your corpus.
///
/// ## The connection is owned here and never escapes
///
/// SQLite hands out a raw `sqlite3*`. It is wrapped in a `unique_ptr` with a
/// custom deleter at this boundary and no accessor returns it — the Code Style
/// rule for C APIs, and what makes a throw from any query safe.
namespace apogee::embedstore {

/// One stored chunk.
struct Chunk {
    std::int64_t id = 0;
    /// The source it came from — a file path, usually.
    std::string source;
    /// Position within that source, so chunks can be re-assembled in order.
    std::int64_t ordinal = 0;
    std::string text;
};

/// A retrieval hit.
struct SearchHit {
    Chunk chunk;

    /// **Normalised to (0, 1] as `s/(1+s)`**, where `s` is the raw BM25
    /// relevance. Carried from Ommi, and the reason matters: raw BM25 is
    /// unbounded and corpus-dependent, so a raw score means nothing to a user
    /// comparing two collections and nothing to a threshold in a config file.
    double score = 0.0;

    /// Which retriever produced `score`.
    ///
    /// **Always reported, never assumed.** Lexical and vector scores are on
    /// incomparable scales, and a number shown without its retriever invites
    /// exactly the comparison that cannot be made.
    std::string retriever = "lexical";
};

/// A collection's on-disk database.
class Store {
public:
    /// Opens (creating if needed) the store at `path`.
    ///
    /// Throws `std::runtime_error` with a message naming the file when the
    /// database cannot be opened or migrated.
    explicit Store(const std::filesystem::path& path);
    ~Store();

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;

    /// Replaces every chunk belonging to `source` with `chunks`.
    ///
    /// One transaction: a source is entirely replaced or entirely untouched.
    /// Half-replacing it would leave a corpus that answers with a mixture of
    /// two document versions, which is worse than either.
    void replace_source(std::string_view source, const std::vector<std::string>& chunks);

    /// Removes every chunk belonging to `source`. Returns how many went.
    [[nodiscard]] std::int64_t delete_source(std::string_view source);

    /// Lexical search. `limit` caps the hits returned.
    ///
    /// `query` is **natural language**, not FTS5 syntax: see
    /// `fts_match_query`. A user typing `AND` or a quote gets results, never a
    /// syntax error.
    [[nodiscard]] std::vector<SearchHit> search(std::string_view query, int limit) const;

    /// Total chunks held.
    [[nodiscard]] std::int64_t chunk_count() const;

    /// Distinct sources, sorted.
    [[nodiscard]] std::vector<std::string> sources() const;

    /// Runs SQLite's own content-comparing integrity check on the FTS index.
    ///
    /// The `integrity-check 1` form specifically: the cheaper form validates
    /// only the index's internal structure, and an external-content index can
    /// be perfectly well-formed while disagreeing with the table it mirrors.
    /// Returns empty on success, or the reason.
    [[nodiscard]] std::string verify_index() const;

    /// The schema version the file carries.
    [[nodiscard]] int schema_version() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// The current schema version. Bumped when a migration is added.
inline constexpr int kSchemaVersion = 1;

}  // namespace apogee::embedstore
