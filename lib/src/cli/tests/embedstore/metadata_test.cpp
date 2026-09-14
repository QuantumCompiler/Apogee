#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "embedstore/store.h"
#include "embedstore/vector.h"
#include "support/env_guard.h"

/// Schema v3: the nullable `metadata` column, its migration from a real v2
/// file, and the accessors the knowledge store reads and writes through.
namespace {

using apogee::embedstore::Chunk;
using apogee::embedstore::Store;

struct Scratch {
    apogee::testing::TempDir dir{"embedstore-metadata-" + std::to_string(std::random_device{}())};

    [[nodiscard]] std::filesystem::path db() const {
        return dir.path() / "collection.db";
    }
};

/// Builds the v2 schema by hand -- exactly what a v2 build created -- with
/// one vectorised row, so the migration runs against a real predecessor
/// rather than a store this build made and could not have left un-migrated.
void write_v2_database(const std::filesystem::path& path, const std::vector<float>& vector) {
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK);
    const auto exec = [raw](const char* sql) {
        char* message = nullptr;
        const int rc = sqlite3_exec(raw, sql, nullptr, nullptr, &message);
        INFO((message == nullptr ? "" : message));
        sqlite3_free(message);
        REQUIRE(rc == SQLITE_OK);
    };
    exec("CREATE TABLE store_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
    exec("INSERT INTO store_meta(key, value) VALUES('schema_version', '2')");
    exec("INSERT INTO store_meta(key, value) VALUES('embed_model', 'old-embedder')");
    exec("INSERT INTO store_meta(key, value) VALUES('embed_dim', '4')");
    exec(
        "CREATE TABLE chunks (id INTEGER PRIMARY KEY, source TEXT NOT NULL, ordinal INTEGER NOT "
        "NULL, text TEXT NOT NULL, embedding BLOB, dim INTEGER NOT NULL DEFAULT 0)");
    exec("CREATE INDEX chunks_by_source ON chunks(source, ordinal)");
    exec(
        "CREATE VIRTUAL TABLE chunks_fts USING fts5(text, content='chunks', content_rowid='id', "
        "tokenize='unicode61')");
    exec(
        "CREATE TRIGGER chunks_ai AFTER INSERT ON chunks BEGIN INSERT INTO chunks_fts(rowid, "
        "text) VALUES (new.id, new.text); END");
    exec(
        "CREATE TRIGGER chunks_ad AFTER DELETE ON chunks BEGIN INSERT INTO "
        "chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text); END");
    exec(
        "CREATE TRIGGER chunks_au AFTER UPDATE ON chunks BEGIN INSERT INTO "
        "chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text); INSERT INTO "
        "chunks_fts(rowid, text) VALUES (new.id, new.text); END");
    sqlite3_stmt* insert = nullptr;
    REQUIRE(sqlite3_prepare_v2(raw,
                               "INSERT INTO chunks(source, ordinal, text, embedding, dim) "
                               "VALUES('legacy.md', 0, 'the old corpus talks about tulips', ?, 4)",
                               -1, &insert, nullptr) == SQLITE_OK);
    const std::string blob = apogee::embedstore::to_blob(vector);
    sqlite3_bind_blob(insert, 1, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    REQUIRE(sqlite3_step(insert) == SQLITE_DONE);
    sqlite3_finalize(insert);
    sqlite3_close(raw);
}

}  // namespace

TEST_CASE(
    "a v2 store gains the metadata column on open, keeps its vectors and its index, "
    "and migrates once",
    "[embedstore][store][migration][metadata]") {
    const Scratch scratch;
    const std::vector<float> vector{1.0F, 0.0F, 0.5F, -0.5F};
    write_v2_database(scratch.db(), vector);

    {
        Store store{scratch.db()};
        CHECK(store.schema_version() == apogee::embedstore::kSchemaVersion);
        CHECK(apogee::embedstore::kSchemaVersion == 3);
        // The old row reads as an ordinary chunk with no metadata.
        const std::optional<Chunk> chunk = store.chunk_by_id(1);
        REQUIRE(chunk.has_value());
        CHECK(chunk->source == "legacy.md");
        CHECK(chunk->metadata.empty());
        CHECK(store.chunks_with_metadata().empty());
        // Vectors and the recorded model survive the migration.
        CHECK(store.chunk_vector(1) == vector);
        CHECK(store.embedding_model().model == "old-embedder");
        CHECK(store.stats().dimension == 4);
        // The FTS index built by the old triggers still answers.
        REQUIRE(store.search("tulips", 5).size() == 1);
        CHECK(store.search("tulips", 5).front().chunk.metadata.empty());
        CHECK(store.verify_index().empty());
    }
    // Idempotent: a second open finds the column and adds nothing.
    Store again{scratch.db()};
    CHECK(again.schema_version() == 3);
    CHECK(again.chunk_by_id(1)->text == "the old corpus talks about tulips");
    CHECK(again.chunk_vector(1) == vector);
}

TEST_CASE(
    "metadata is stored per chunk, NULL when empty, carried on every hit, and never "
    "indexed",
    "[embedstore][store][metadata]") {
    const Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("kr-1", {"alpha reasoning"}, {}, {R"({"id":"kr-1","secret":"zebra"})"});
    store.replace_source("notes.md", {"beta one", "beta two"}, {}, {"", R"({"n":2})"});
    store.replace_source("plain.md", {"gamma"});

    const std::vector<Chunk> with = store.chunks_with_metadata();
    REQUIRE(with.size() == 2);
    CHECK(with[0].source == "kr-1");
    CHECK(with[0].metadata == R"({"id":"kr-1","secret":"zebra"})");
    CHECK(with[1].source == "notes.md");
    CHECK(with[1].ordinal == 1);
    CHECK(with[1].metadata == R"({"n":2})");

    // Every hit carries it, so a reader never needs a second query.
    const std::vector<apogee::embedstore::SearchHit> hits = store.search("alpha", 5);
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().chunk.metadata == R"({"id":"kr-1","secret":"zebra"})");
    // But the metadata is not what the index sees.
    CHECK(store.search("zebra", 5).empty());
    CHECK(store.search("secret", 5).empty());

    // Shape checks on the write.
    CHECK_THROWS(store.replace_source("short", {"a", "b"}, {}, {"only-one"}));
    const std::optional<Chunk> missing = store.chunk_by_id(9999);
    CHECK_FALSE(missing.has_value());
    CHECK(store.chunk_vector(9999).empty());
    CHECK(store.chunk_vector(hits.front().chunk.id).empty());  // lexical-only: no vector
}

TEST_CASE("update_metadata rewrites the metadata alone: text, vector and index stay",
          "[embedstore][store][metadata][update]") {
    const Scratch scratch;
    Store store{scratch.db()};
    const std::vector<float> vector{0.1F, 0.2F, 0.3F};
    store.replace_source("kr-2", {"delta reasoning"}, {vector}, {R"({"v":1})"});
    const std::int64_t id = store.chunks_with_metadata().front().id;

    CHECK(store.update_metadata("kr-2", R"({"v":2})") == 1);
    const std::optional<Chunk> chunk = store.chunk_by_id(id);
    REQUIRE(chunk.has_value());
    CHECK(chunk->metadata == R"({"v":2})");
    CHECK(chunk->text == "delta reasoning");
    CHECK(store.chunk_vector(id) == vector);
    CHECK(store.search("delta", 5).size() == 1);
    CHECK(store.verify_index().empty());
    CHECK(store.chunk_count() == 1);

    // Clearing stores NULL: the chunk leaves the metadata listing.
    CHECK(store.update_metadata("kr-2", "") == 1);
    CHECK(store.chunks_with_metadata().empty());
    CHECK(store.chunk_by_id(id)->metadata.empty());
    CHECK(store.update_metadata("kr-none", "{}") == 0);
}
