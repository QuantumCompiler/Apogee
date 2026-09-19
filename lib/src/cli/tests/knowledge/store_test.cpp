#include "knowledge/store.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "embedstore/store.h"
#include "knowledge/record.h"
#include "support/env_guard.h"

/// The knowledge store: one record is one chunk keyed by its id, the raw
/// conversation archived privately beside it, and every edit a metadata
/// rewrite that leaves the index text and the vector alone.
namespace {

using apogee::knowledge::Record;
using apogee::knowledge::Store;

struct Scratch {
    apogee::testing::TempDir dir{"knowledge-store-" + std::to_string(std::random_device{}())};

    [[nodiscard]] std::filesystem::path db() const {
        return dir.path() / "embeddings" / "knowledge.db";
    }

    [[nodiscard]] std::filesystem::path raw() const {
        return dir.path() / "knowledge" / "raw";
    }

    [[nodiscard]] Store open() const {
        return Store{db(), raw()};
    }
};

Record make(std::string id, std::string intent, std::string timestamp) {
    Record record;
    record.id = std::move(id);
    record.intent = std::move(intent);
    record.decision = "the choice";
    record.status = "shipped";
    record.discipline = "eng";
    record.provenance.source = "meeting";
    record.provenance.attribution = "Ada";
    record.timestamp = std::move(timestamp);
    return record;
}

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}  // namespace

TEST_CASE("put archives the raw conversation privately and stores one chunk keyed by the id",
          "[knowledge][store]") {
    const Scratch scratch;
    Store store = scratch.open();
    Record record = make("kr-20260913T120000Z-000001", "because testers were confused",
                         "2026-09-13T12:00:00.000000Z");
    store.put(record, {}, "User: drop it?\n\nAssistant: yes, because testers were confused");

    // The archive first, and the record points at it.
    const std::filesystem::path raw = scratch.raw() / "kr-20260913T120000Z-000001.md";
    CHECK(record.raw_ref == raw.string());
    CHECK(bytes(raw) == "User: drop it?\n\nAssistant: yes, because testers were confused");
#if !defined(_WIN32)
    std::error_code code;
    const auto dir_mode =
        std::filesystem::status(scratch.raw(), code).permissions() & std::filesystem::perms::mask;
    CHECK((dir_mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
          std::filesystem::perms::none);
    const auto file_mode =
        std::filesystem::status(raw, code).permissions() & std::filesystem::perms::mask;
    CHECK((file_mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
          std::filesystem::perms::none);
#endif

    // One chunk: its text the thin index, its metadata the whole record.
    CHECK(store.chunks().chunk_count() == 1);
    const std::optional<std::int64_t> id = store.chunk_id(record.id);
    REQUIRE(id.has_value());
    const std::optional<apogee::embedstore::Chunk> chunk = store.chunks().chunk_by_id(*id);
    REQUIRE(chunk.has_value());
    CHECK(chunk->source == record.id);
    CHECK(chunk->text == apogee::knowledge::index_text(record));
    CHECK(chunk->text.find("Ada") == std::string::npos);
    CHECK(nlohmann::json::parse(chunk->metadata)["provenance"]["attribution"] == "Ada");
    CHECK(nlohmann::json::parse(chunk->metadata)["raw_ref"] == raw.string());

    const std::optional<Record> got = store.get(record.id);
    REQUIRE(got.has_value());
    CHECK(got->intent == record.intent);
    CHECK(got->provenance.attribution == "Ada");
    CHECK(got->raw_ref == raw.string());
    CHECK(store.read_raw(*got).value_or("") == bytes(raw));
    CHECK_FALSE(store.get("kr-nope").has_value());
    CHECK_FALSE(store.chunk_id("kr-nope").has_value());
}

TEST_CASE("a record captured with no embedder is findable by text at once",
          "[knowledge][store][lexical]") {
    const Scratch scratch;
    Store store = scratch.open();
    Record record = make("kr-20260913T120000Z-000002",
                         "we chose SQLite because a single file cannot half-succeed",
                         "2026-09-13T12:00:00.000000Z");
    store.put(record, {}, "raw");
    CHECK_FALSE(store.has_vectors());
    const std::vector<apogee::embedstore::SearchHit> hits =
        store.chunks().search("why did we choose SQLite", 5);
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().chunk.source == record.id);
    CHECK(hits.front().retriever == "lexical");
    // The hit carries the metadata a reader decodes the record from.
    std::string error;
    CHECK(apogee::knowledge::record_from_metadata(hits.front().chunk.source,
                                                  hits.front().chunk.metadata, error)
              .has_value());
    // And the name is not something a query can reach.
    CHECK(store.chunks().search("Ada", 5).empty());
}

TEST_CASE("re-putting a record is idempotent: one chunk, the archive replaced",
          "[knowledge][store][idempotent]") {
    const Scratch scratch;
    Store store = scratch.open();
    Record record =
        make("kr-20260913T120000Z-000003", "first words", "2026-09-13T12:00:00.000000Z");
    store.put(record, {}, "raw one");
    record.intent = "second words";
    store.put(record, {}, "raw two");
    CHECK(store.chunks().chunk_count() == 1);
    CHECK(store.get(record.id)->intent == "second words");
    CHECK(bytes(scratch.raw() / (record.id + ".md")) == "raw two");
    // An empty raw leaves the archive alone (a reindex, later).
    store.put(record, {}, "");
    CHECK(bytes(scratch.raw() / (record.id + ".md")) == "raw two");
    CHECK(store.get(record.id)->raw_ref.ends_with(record.id + ".md"));
    // Archiving off: nothing written, no reference.
    Store bare{scratch.dir.path() / "embeddings" / "bare.db", {}};
    Record unarchived = make("kr-20260913T120000Z-000004", "x", "2026-09-13T12:00:00.000000Z");
    bare.put(unarchived, {}, "raw");
    CHECK(unarchived.raw_ref.empty());
    CHECK(bare.raw_path(unarchived.id).empty());
    CHECK_FALSE(bare.read_raw(unarchived).has_value());
    Record no_id;
    no_id.intent = "x";
    CHECK_THROWS(bare.put(no_id, {}, ""));
}

TEST_CASE("list is newest first and skips chunks that are not records",
          "[knowledge][store][list]") {
    const Scratch scratch;
    Store store = scratch.open();
    Record older = make("kr-20260913T110000Z-000001", "older", "2026-09-13T11:00:00.000000Z");
    Record newer = make("kr-20260913T120000Z-000001", "newer", "2026-09-13T12:00:00.000000Z");
    Record middle = make("kr-20260913T113000Z-000001", "middle", "2026-09-13T11:30:00.000000Z");
    store.put(older, {}, "");
    store.put(newer, {}, "");
    store.put(middle, {}, "");
    // A plain document sharing the collection is not a record.
    store.chunks().replace_source("notes.md", {"just a note"});
    const std::vector<Record> listed = store.list();
    REQUIRE(listed.size() == 3);
    CHECK(listed[0].intent == "newer");
    CHECK(listed[1].intent == "middle");
    CHECK(listed[2].intent == "older");
}

TEST_CASE("link, status and supersede edit the metadata and leave the vector bytes untouched",
          "[knowledge][store][edit]") {
    const Scratch scratch;
    Store store = scratch.open();
    Record record = make("kr-20260913T120000Z-000005", "vectorised", "2026-09-13T12:00:00.000000Z");
    const std::vector<float> vector{0.25F, -0.5F, 0.75F, 1.0F};
    store.put(record, vector, "raw");
    CHECK(store.has_vectors());
    const std::int64_t chunk = *store.chunk_id(record.id);
    REQUIRE(store.chunks().chunk_vector(chunk) == vector);

    const std::optional<Record> linked = store.set_link(record.id, "  PROJ-9 ");
    REQUIRE(linked.has_value());
    CHECK(linked->downstream_link == "PROJ-9");
    const std::optional<Record> rejected = store.set_status(record.id, "rejected");
    REQUIRE(rejected.has_value());
    CHECK(rejected->status == "rejected");
    CHECK(rejected->downstream_link == "PROJ-9");
    const std::optional<Record> superseded = store.supersede(record.id);
    REQUIRE(superseded.has_value());
    CHECK(superseded->status == "superseded");
    CHECK(store.get(record.id)->status == "superseded");

    // The same chunk, the same text, the same bytes: nothing was re-embedded.
    CHECK(*store.chunk_id(record.id) == chunk);
    CHECK(store.chunks().chunk_vector(chunk) == vector);
    CHECK(store.chunks().chunk_by_id(chunk)->text == apogee::knowledge::index_text(record));
    CHECK(store.chunks().chunk_count() == 1);
    CHECK(store.chunks().search("vectorised", 5).size() == 1);

    CHECK_FALSE(store.set_link("kr-nope", "x").has_value());
    CHECK_FALSE(store.set_status("kr-nope", "shipped").has_value());
    CHECK_FALSE(store.supersede("kr-nope").has_value());
}

TEST_CASE("remove takes the chunk and the archived conversation with it", "[knowledge][store]") {
    const Scratch scratch;
    Store store = scratch.open();
    Record record = make("kr-20260913T120000Z-000006", "gone soon", "2026-09-13T12:00:00.000000Z");
    store.put(record, {}, "raw");
    const std::filesystem::path raw = scratch.raw() / (record.id + ".md");
    REQUIRE(std::filesystem::exists(raw));
    CHECK(store.remove(record.id));
    CHECK_FALSE(store.get(record.id).has_value());
    CHECK(store.chunks().chunk_count() == 0);
    CHECK_FALSE(std::filesystem::exists(raw));
    CHECK_FALSE(store.remove(record.id));
}

TEST_CASE(
    "reindex re-embeds the records, leaves the archive alone, refuses an unknown id before "
    "embedding anything, and stops naming the record an embedder failed on",
    "[knowledge][store][reindex]") {
    const Scratch scratch;
    Store store = scratch.open();
    Record lexical_one = make("kr-20260913T120000Z-000011", "one", "2026-09-13T12:00:00.000000Z");
    Record lexical_two = make("kr-20260913T120000Z-000012", "two", "2026-09-13T12:00:01.000000Z");
    Record vectored = make("kr-20260913T120000Z-000013", "three", "2026-09-13T12:00:02.000000Z");
    store.put(lexical_one, {}, "raw one");
    store.put(lexical_two, {}, "raw two");
    store.put(vectored, {0.5F, 0.5F}, "raw three");
    CHECK(store.vectorless_count() == 2);
    const std::string archive_before = bytes(scratch.raw() / (lexical_one.id + ".md"));

    int calls = 0;
    const Store::EmbedText embed = [&calls](std::string_view text) {
        ++calls;
        return std::vector<float>{static_cast<float>(text.size()), 1.0F};
    };
    const Store::ReindexOutcome all = store.reindex(embed, {});
    CHECK(all.error.empty());
    CHECK(all.reindexed == 3);
    CHECK(calls == 3);
    CHECK(store.vectorless_count() == 0);
    CHECK(store.has_vectors());
    CHECK(store.chunks().chunk_vector(*store.chunk_id(lexical_one.id)).size() == 2);
    // The archive and the reference to it are exactly as they were.
    CHECK(bytes(scratch.raw() / (lexical_one.id + ".md")) == archive_before);
    CHECK(store.get(lexical_one.id)->raw_ref == lexical_one.raw_ref);
    CHECK(store.get(lexical_one.id)->intent == "one");

    // One record only.
    calls = 0;
    const Store::ReindexOutcome one = store.reindex(embed, {lexical_two.id});
    CHECK(one.reindexed == 1);
    CHECK(calls == 1);

    // An unknown id is refused before any embedding is spent.
    calls = 0;
    const Store::ReindexOutcome unknown = store.reindex(embed, {lexical_two.id, "kr-nope"});
    CHECK(unknown.reindexed == 0);
    CHECK(unknown.error.find("kr-nope") != std::string::npos);
    CHECK(calls == 0);

    // A failing embedder stops the run naming the record, after what succeeded.
    int seen = 0;
    const Store::ReindexOutcome failed = store.reindex(
        [&seen](std::string_view) -> std::vector<float> {
            if (++seen == 2) {
                throw std::runtime_error("the embedder fell over");
            }
            return {1.0F};
        },
        {});
    CHECK(failed.reindexed == 1);
    CHECK(failed.error.find("fell over") != std::string::npos);
    CHECK(failed.error.find("kr-") != std::string::npos);
    const Store::ReindexOutcome empty =
        store.reindex([](std::string_view) { return std::vector<float>{}; }, {vectored.id});
    CHECK(empty.reindexed == 0);
    CHECK(empty.error.find("no vector") != std::string::npos);
}
