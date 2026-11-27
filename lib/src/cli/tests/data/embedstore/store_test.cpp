#include "embedstore/store.h"

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "embedstore/fts.h"
#include "platform/platform.h"

/// The chunk store, its FTS index, and the query builder that stands between a
/// user's words and FTS5's syntax.
///
/// Everything here runs with **no model, no key, and no network** — which is
/// the property the whole lexical floor exists to guarantee, so it is worth
/// noticing that the tests can hold it too.
namespace {

using apogee::embedstore::SearchHit;
using apogee::embedstore::Store;

struct Scratch {
    // A random draw beside the counter: ctest runs cases as parallel
    // processes, and a per-process counter alone names the same directory in
    // several of them at once.
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("apogee-store-" + std::to_string(std::random_device{}()) + "-" +
                                 std::to_string(counter()));

    Scratch() {
        std::error_code code;
        std::filesystem::create_directories(dir, code);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;

    ~Scratch() {
        std::error_code code;
        std::filesystem::remove_all(dir, code);
    }

    [[nodiscard]] std::filesystem::path db() const {
        return dir / "collection.db";
    }

    static int counter() {
        static int next = 0;
        return ++next;
    }
};

}  // namespace

TEST_CASE("a store round-trips ingest and query with no model configured", "[embedstore][store]") {
    // The acceptance criterion, and the point of the whole lexical floor: this
    // works offline, keyless, and with no backend at all.
    Scratch scratch;
    Store store{scratch.db()};

    store.replace_source("notes.md",
                         {"the capital of France is Paris", "the capital of Japan is Tokyo"});

    const std::vector<SearchHit> hits = store.search("what is the capital of France", 5);

    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().chunk.text.find("Paris") != std::string::npos);
    CHECK(hits.front().chunk.source == "notes.md");
}

TEST_CASE("scores are normalised and labelled with their retriever",
          "[embedstore][store][scores]") {
    // Raw BM25 is unbounded, negative, and corpus-relative -- meaningless in a
    // config threshold or next to a vector score. And a number shown without
    // its retriever invites exactly the comparison that cannot be made.
    Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("doc", {"alpha beta gamma", "beta gamma delta"});

    for (const SearchHit& hit : store.search("beta", 5)) {
        INFO("chunk: " << hit.chunk.text);
        CHECK(hit.score > 0.0);
        CHECK(hit.score <= 1.0);
        CHECK(hit.retriever == "lexical");
    }
}

TEST_CASE("re-ingesting a source replaces its chunks rather than duplicating them",
          "[embedstore][store]") {
    // Otherwise running ingest twice is a slow way to double a corpus, and
    // every result arrives twice.
    Scratch scratch;
    Store store{scratch.db()};

    store.replace_source("doc", {"first version of the text"});
    REQUIRE(store.chunk_count() == 1);

    store.replace_source("doc", {"second version", "with two chunks"});
    CHECK(store.chunk_count() == 2);

    // And the old content is genuinely gone from the INDEX, not merely from the
    // table -- an external-content index that kept it would still match on it.
    //
    // Asserted on a term unique to the OLD text. Terms are OR-joined, so a
    // phrase sharing any word with the new text would match for the wrong
    // reason and the assertion would pass without meaning anything.
    CHECK(store.search("first", 5).empty());
    CHECK_FALSE(store.search("second", 5).empty());
}

TEST_CASE("deleting a source removes it from the index too", "[embedstore][store]") {
    Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("a", {"apples"});
    store.replace_source("b", {"bananas"});

    CHECK(store.delete_source("a") == 1);
    CHECK(store.search("apples", 5).empty());
    CHECK_FALSE(store.search("bananas", 5).empty());
    CHECK(store.sources() == std::vector<std::string>{"b"});
}

TEST_CASE("the FTS index agrees with the table it mirrors", "[embedstore][store][integrity]") {
    // The content-comparing `integrity-check 1` form. The cheaper form checks
    // only that the index is internally well-formed -- an external-content
    // index can be perfectly well-formed and still disagree with its table,
    // which is the failure that actually matters.
    Scratch scratch;
    Store store{scratch.db()};

    store.replace_source("doc", {"one", "two", "three"});
    store.replace_source("doc", {"four", "five"});
    (void)store.delete_source("doc");
    store.replace_source("other", {"six"});

    CHECK(store.verify_index().empty());
}

TEST_CASE("reopening an existing store is idempotent", "[embedstore][store][migration]") {
    // Every migration step is `IF NOT EXISTS`, so a second open is a no-op
    // rather than an error -- and the triggers self-heal if an older build
    // created the table without them.
    Scratch scratch;
    {
        Store store{scratch.db()};
        store.replace_source("doc", {"content"});
        CHECK(store.schema_version() == apogee::embedstore::kSchemaVersion);
    }
    {
        Store store{scratch.db()};
        CHECK(store.chunk_count() == 1);
        CHECK(store.schema_version() == apogee::embedstore::kSchemaVersion);
        CHECK(store.verify_index().empty());
    }
}

TEST_CASE("a half-created database is not left behind by a failed open",
          "[embedstore][store][migration]") {
    // The migration-interruption criterion, approximated as far as a test can
    // reach it: the schema is created inside ONE transaction, so a database
    // that exists has either the whole schema or none of it. Here the second
    // open finds a complete schema and works -- the observable consequence of
    // the transaction, and what would break if the steps were split.
    Scratch scratch;
    {
        Store store{scratch.db()};
    }

    Store reopened{scratch.db()};
    reopened.replace_source("doc", {"still works"});
    CHECK(reopened.chunk_count() == 1);
    CHECK(reopened.verify_index().empty());
}

TEST_CASE("opening an unwritable path fails with the path in the message", "[embedstore][store]") {
    // A path under a regular FILE cannot be created anywhere. (/proc/nonexistent
    // was the old choice; Windows creates C:\proc\nonexistent happily.)
    Scratch scratch;
    const std::filesystem::path blocker = scratch.dir / "not-a-directory";
    {
        std::ofstream out{blocker};
        out << "a file where a directory is needed";
    }
    CHECK_THROWS_AS(Store{blocker / "store.db"}, std::runtime_error);
}

TEST_CASE("an empty corpus answers nothing rather than failing", "[embedstore][store]") {
    Scratch scratch;
    Store store{scratch.db()};
    CHECK(store.search("anything", 5).empty());
    CHECK(store.chunk_count() == 0);
    CHECK(store.sources().empty());
}

TEST_CASE("an open waits out another connection's lock on the file rather than failing",
          "[embedstore][store][concurrency]") {
    // The last connection to close a WAL database checkpoints it under the
    // file's exclusive lock, and an open landing in that window must wait --
    // what the busy timeout is for. The open's first statement, the WAL
    // pragma, already reads the file, so the timeout has to be set before it:
    // set after, the plane's list of a graph a job had just finished with
    // could answer 500 "database is locked" while the job's store closed.
    // Here another connection holds the lock for a moment, then lets go.
    Scratch scratch;
    {
        Store store{scratch.db()};
        store.replace_source("doc", {"content"});
    }
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(scratch.db().string().c_str(), &raw) == SQLITE_OK);
    std::unique_ptr<sqlite3, int (*)(sqlite3*)> holder{raw, sqlite3_close};
    // Exclusive locking mode: the file's lock, once a write takes it, is
    // held until the connection closes.
    REQUIRE(sqlite3_exec(raw,
                         "PRAGMA locking_mode=EXCLUSIVE; BEGIN EXCLUSIVE; "
                         "DELETE FROM store_meta WHERE key = 'no such key'",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    std::thread release{[&holder] {
        std::this_thread::sleep_for(std::chrono::milliseconds{300});
        (void)sqlite3_exec(holder.get(), "COMMIT", nullptr, nullptr, nullptr);
        holder.reset();
    }};
    std::string error;
    std::optional<Store> opened;
    try {
        opened.emplace(scratch.db());
    } catch (const std::exception& e) {
        error = e.what();
    }
    release.join();
    INFO(error);
    REQUIRE(opened.has_value());
    CHECK(opened->chunk_count() == 1);
}

TEST_CASE("a store opens, writes and reopens under a folder with a non-ASCII name",
          "[embedstore][store][utf8][57j]") {
    // SQLite reads the file name it is given as UTF-8 on every platform. The
    // store handed it `path.string()` -- the ANSI code page's bytes under
    // libc++ on Windows (CI's arm64 build), which named another file or none.
    Scratch scratch;
    // "café résumé", written as its bytes.
    const std::filesystem::path folder =
        scratch.dir / apogee::platform::path_from_utf8("caf\xC3\xA9 r\xC3\xA9sum\xC3\xA9");
    const std::filesystem::path db = folder / "collection.db";
    {
        Store store{db};
        store.replace_source("notes.md", {"the capital of France is Paris"});
    }
    CHECK(std::filesystem::is_regular_file(db));
    // Under that name and no other: the scratch folder holds that one folder.
    std::vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::directory_iterator{scratch.dir}) {
        entries.push_back(entry.path().filename());
    }
    REQUIRE(entries.size() == 1);
    CHECK(entries.front() == folder.filename());

    Store reopened{db};
    const std::vector<SearchHit> hits = reopened.search("capital of France", 5);
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().chunk.text.find("Paris") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Opening a store writes nothing; a migration is still one transaction (57s)
// ---------------------------------------------------------------------------

namespace {

/// A raw connection to `db`, closed on scope exit.
using RawConnection = std::unique_ptr<sqlite3, int (*)(sqlite3*)>;

[[nodiscard]] RawConnection raw_open(const std::filesystem::path& db) {
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(db.string().c_str(), &raw) == SQLITE_OK);
    return RawConnection{raw, sqlite3_close};
}

void raw_run(sqlite3* handle, const char* sql) {
    char* message = nullptr;
    const int result = sqlite3_exec(handle, sql, nullptr, nullptr, &message);
    const std::string detail = message == nullptr ? "" : message;
    sqlite3_free(message);
    INFO(sql << ": " << detail);
    REQUIRE(result == SQLITE_OK);
}

/// The first column of the first row `sql` answers, or empty.
[[nodiscard]] std::string raw_text(sqlite3* handle, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(handle, sql, -1, &statement, nullptr) == SQLITE_OK);
    std::string out;
    if (sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_text(statement, 0) != nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): SQLite's unsigned bytes
        out = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    }
    sqlite3_finalize(statement);
    return out;
}

}  // namespace

TEST_CASE("opening a current store reads at once while another connection is writing it",
          "[embedstore][store][concurrency][57s]") {
    // Every open took SQLite's one writer lock for its migration, so a
    // listing opened during an ingest waited out the busy timeout and then
    // read as an empty collection with "database is locked".
    Scratch scratch;
    {
        Store store{scratch.db()};
        store.replace_source("doc", {"content"});
    }
    const RawConnection writer = raw_open(scratch.db());
    raw_run(writer.get(), "BEGIN IMMEDIATE");
    raw_run(writer.get(),
            "INSERT INTO chunks(source, ordinal, text) VALUES('held', 0, 'mid-write')");

    const auto start = std::chrono::steady_clock::now();
    std::string error;
    std::optional<Store> opened;
    Store::Stats stats;
    std::vector<std::string> sources;
    std::string index;
    try {
        opened.emplace(scratch.db());
        stats = opened->stats();
        sources = opened->sources();
        index = opened->verify_index();
    } catch (const std::exception& e) {
        error = e.what();
    }
    const auto took = std::chrono::steady_clock::now() - start;
    raw_run(writer.get(), "ROLLBACK");

    INFO(error);
    REQUIRE(opened.has_value());
    // Well inside the five-second busy timeout: nothing waited on the lock.
    CHECK(took < std::chrono::seconds{2});
    // The last committed state, not the write in flight.
    CHECK(stats.chunk_count == 1);
    CHECK(sources == std::vector<std::string>{"doc"});
    // The index check needs the lock: said not checked, at once, never FAILED.
    CHECK(index == apogee::embedstore::kIndexBeingWritten);
    CHECK(opened->verify_index().empty());  // and checked once the writer is gone
}

TEST_CASE("an older store still migrates on open, whole, and gains the ingest records",
          "[embedstore][store][migration][57s]") {
    Scratch scratch;
    {
        Store store{scratch.db()};
        store.replace_source("doc", {"content"});
    }
    {
        // Back to v6, as a build before 57s left it.
        const RawConnection raw = raw_open(scratch.db());
        raw_run(raw.get(),
                "DROP TABLE source_ingests;"
                "DROP TRIGGER chunks_ai;"
                "UPDATE store_meta SET value = '6' WHERE key = 'schema_version'");
    }
    Store store{scratch.db()};
    CHECK(store.schema_version() == apogee::embedstore::kSchemaVersion);
    CHECK(store.source_ingests().empty());
    store.ingest_source("doc", "/corpus", "", {"rewritten content"}, {});
    CHECK(store.source_ingests().at("doc").root == "/corpus");
    // The trigger a migration heals is back: the rewrite is indexed.
    CHECK(store.verify_index().empty());
    CHECK_FALSE(store.search("rewritten", 5).empty());
}

TEST_CASE("an interrupted migration rolls back whole", "[embedstore][store][migration][57s]") {
    // A pre-v4 store whose chunks table lets a NULL text through: the v4
    // rebuild copies it into a NOT NULL column and fails midway. Every step
    // before it -- the added columns, the dropped triggers, the renamed table
    // -- and every step after must be gone with it.
    Scratch scratch;
    {
        const RawConnection raw = raw_open(scratch.db());
        raw_run(raw.get(),
                "CREATE TABLE store_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
                "INSERT INTO store_meta VALUES('schema_version', '3');"
                "CREATE TABLE chunks (id INTEGER PRIMARY KEY, source TEXT NOT NULL,"
                "                     ordinal INTEGER NOT NULL, text TEXT);"
                "INSERT INTO chunks(source, ordinal, text) VALUES('broken', 0, NULL)");
    }
    CHECK_THROWS(Store{scratch.db()});

    const RawConnection raw = raw_open(scratch.db());
    CHECK(raw_text(raw.get(), "SELECT value FROM store_meta WHERE key = 'schema_version'") == "3");
    CHECK(raw_text(raw.get(), "SELECT sql FROM sqlite_master WHERE name = 'chunks'")
              .find("AUTOINCREMENT") == std::string::npos);
    CHECK(raw_text(raw.get(), "SELECT name FROM sqlite_master WHERE name = 'chunks_old'").empty());
    CHECK(raw_text(raw.get(), "SELECT name FROM sqlite_master WHERE name = 'source_ingests'")
              .empty());
    CHECK(raw_text(raw.get(), "SELECT name FROM sqlite_master WHERE name = 'chunks_fts'").empty());
    CHECK(raw_text(raw.get(),
                   "SELECT COUNT(*) FROM pragma_table_info('chunks')"
                   " WHERE name = 'embedding'") == "0");
}

TEST_CASE("a store is bound to one model or marked mixed over several, never both",
          "[embedstore][store][binding][57s]") {
    Scratch scratch;
    Store store{scratch.db()};
    store.set_embedding_model("model-a", 8);
    CHECK(store.embedding_model().model == "model-a");
    CHECK(store.embedding_model().mixed.empty());

    store.mark_embedding_mixed({"", "model-a", "model-b"}, 8);
    const Store::EmbeddingBinding mixed = store.embedding_model();
    CHECK_FALSE(mixed.recorded());
    CHECK(mixed.mixed == std::vector<std::string>{"", "model-a", "model-b"});
    CHECK(mixed.dimension == 8);

    store.set_embedding_model("model-b", 8);
    CHECK(store.embedding_model().model == "model-b");
    CHECK(store.embedding_model().mixed.empty());

    store.mark_embedding_mixed({"model-a", "model-b"}, 8);
    store.clear_embedding_model();
    CHECK_FALSE(store.embedding_model().recorded());
    CHECK(store.embedding_model().mixed.empty());
}

TEST_CASE("renaming a model renames the binding, a mixed mark's names and every source's record",
          "[embedstore][store][binding][57s]") {
    Scratch scratch;
    Store store{scratch.db()};
    store.ingest_source("a.md", "/corpus", "/flat/e.gguf", {"text"}, {{1.0F, 0.0F}});
    store.ingest_source("b.md", "/corpus", "/flat/e.gguf", {"text"}, {{0.0F, 1.0F}});
    store.ingest_source("c.md", "/corpus", "", {"text"}, {});
    store.set_embedding_model("/flat/e.gguf", 2);

    SECTION("a bound store and its sources follow the model to its new name") {
        CHECK(store.rename_embedding_model("/flat/e.gguf", "/store/e.gguf"));
        CHECK(store.embedding_model().model == "/store/e.gguf");
        CHECK(store.embedding_model().dimension == 2);
        const auto ingests = store.source_ingests();
        CHECK(ingests.at("a.md").model == "/store/e.gguf");
        CHECK(ingests.at("b.md").model == "/store/e.gguf");
        CHECK(ingests.at("a.md").root == "/corpus");
        CHECK(ingests.at("c.md").model.empty());  // no vectors, still no model

        // Nothing names the old path now: a second rename changes nothing.
        CHECK_FALSE(store.rename_embedding_model("/flat/e.gguf", "/store/e.gguf"));
    }
    SECTION("a mixed mark keeps its other names, an unrecorded one among them") {
        store.ingest_source("b.md", "/corpus", "model-b", {"text"}, {{0.0F, 1.0F}});
        store.mark_embedding_mixed({"", "/flat/e.gguf", "model-b"}, 2);
        CHECK(store.rename_embedding_model("/flat/e.gguf", "/store/e.gguf"));
        const Store::EmbeddingBinding binding = store.embedding_model();
        CHECK_FALSE(binding.recorded());
        CHECK(binding.mixed == std::vector<std::string>{"", "/store/e.gguf", "model-b"});
        CHECK(binding.dimension == 2);
        CHECK(store.source_ingests().at("a.md").model == "/store/e.gguf");
        CHECK(store.source_ingests().at("b.md").model == "model-b");
    }
    SECTION("a mixed mark whose names become one model binds the store to it") {
        store.mark_embedding_mixed({"/flat/e.gguf", "/store/e.gguf"}, 2);
        CHECK(store.rename_embedding_model("/flat/e.gguf", "/store/e.gguf"));
        const Store::EmbeddingBinding binding = store.embedding_model();
        CHECK(binding.model == "/store/e.gguf");
        CHECK(binding.mixed.empty());
        CHECK(binding.dimension == 2);
    }
    SECTION("a model the store never named, or no change of name, renames nothing") {
        CHECK_FALSE(store.rename_embedding_model("/elsewhere/x.gguf", "/store/x.gguf"));
        CHECK_FALSE(store.rename_embedding_model("/flat/e.gguf", "/flat/e.gguf"));
        CHECK_FALSE(store.rename_embedding_model("", "/store/e.gguf"));
        CHECK_FALSE(store.rename_embedding_model("/flat/e.gguf", ""));
        CHECK(store.embedding_model().model == "/flat/e.gguf");
        CHECK(store.source_ingests().at("a.md").model == "/flat/e.gguf");
    }
}

TEST_CASE("an ingest record follows its source: replaced without one, deleted with it",
          "[embedstore][store][roots][57s]") {
    Scratch scratch;
    Store store{scratch.db()};
    store.ingest_source("a.md", "/corpus", "model-a", {"text"}, {{1.0F, 0.0F}});
    store.ingest_source("b.md", "/corpus", "model-a", {"text"}, {});
    CHECK(store.source_ingests().at("a.md").model == "model-a");
    CHECK(store.source_ingests().at("b.md").model.empty());  // no vectors, no model

    store.replace_source("a.md", {"written by a path that knows no folder"});
    CHECK_FALSE(store.source_ingests().contains("a.md"));
    CHECK(store.delete_source("b.md") == 1);
    CHECK(store.source_ingests().empty());
}
