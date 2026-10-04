#include "embedstore/vector.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "embedstore/store.h"

/// Vectors in the store: bytes, cosine, rank fusion, the per-store binding,
/// and the v1 -> v2 migration against a real file the previous build wrote.
namespace {

using apogee::embedstore::cosine;
using apogee::embedstore::from_blob;
using apogee::embedstore::fuse_rrf;
using apogee::embedstore::kRrfK;
using apogee::embedstore::SearchHit;
using apogee::embedstore::Store;
using apogee::embedstore::to_blob;

struct Scratch {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("apogee-vector-" + std::to_string(counter()));

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
        return dir / "c.db";
    }

    static int counter() {
        static int next = 0;
        return ++next;
    }
};

SearchHit hit(std::int64_t id, double score = 0.0) {
    SearchHit out;
    out.chunk.id = id;
    out.chunk.text = "chunk " + std::to_string(id);
    out.score = score;
    return out;
}

}  // namespace

TEST_CASE("vectors round-trip through little-endian float32 blobs", "[embedstore][vector]") {
    const std::vector<float> original{1.0F, -0.5F, 0.25F, 3.0e-8F};
    const std::string blob = to_blob(original);
    CHECK(blob.size() == 16);
    CHECK(from_blob(blob) == original);
    // Little-endian on the wire: 1.0f is 0x3F800000.
    CHECK(static_cast<unsigned char>(blob[3]) == 0x3F);
    CHECK(static_cast<unsigned char>(blob[0]) == 0x00);
}

TEST_CASE("cosine is 1 for identical, 0 for orthogonal, and 0 across widths",
          "[embedstore][vector]") {
    CHECK(cosine({1, 0, 0}, {1, 0, 0}) == 1.0);
    CHECK(cosine({1, 0, 0}, {0, 1, 0}) == 0.0);
    CHECK(cosine({1, 0, 0}, {2, 0, 0}) == 1.0);
    // A width mismatch is the last line, not the policy: the resolver refused
    // to query across spaces long before this, and this answers 0 rather than
    // reading past the shorter vector.
    CHECK(cosine({1, 0, 0}, {1, 0}) == 0.0);
    CHECK(cosine({}, {}) == 0.0);
    CHECK(cosine({0, 0}, {1, 1}) == 0.0);
}

TEST_CASE("RRF fuses by rank alone and a chunk in both lists outranks a single first place",
          "[embedstore][vector][rrf]") {
    // Scores are deliberately wild and contradictory: fusion must not read them.
    const std::vector<SearchHit> lexical{hit(1, 0.9), hit(2, 0.8), hit(3, 0.7)};
    const std::vector<SearchHit> vector{hit(4, 99.0), hit(2, 0.01), hit(3, 0.02)};
    const std::vector<SearchHit> fused = fuse_rrf({lexical, vector}, 0);

    REQUIRE(fused.size() == 4);
    // Chunk 2: rank 2 in both = 2/(k+2). Chunk 1: rank 1 in one = 1/(k+1).
    // Consistent mid-list presence beats a single first place -- the point.
    CHECK(fused[0].chunk.id == 2);
    CHECK(fused[1].chunk.id == 3);
    for (const SearchHit& entry : fused) {
        CHECK(entry.retriever == "hybrid");
    }
    const double expected = 2.0 / (kRrfK + 2);
    CHECK(fused[0].score == expected);
}

TEST_CASE("RRF ties break by chunk id and the limit cuts the fused list",
          "[embedstore][vector][rrf]") {
    const std::vector<SearchHit> a{hit(7), hit(3)};
    const std::vector<SearchHit> b{hit(3), hit(7)};
    const std::vector<SearchHit> fused = fuse_rrf({a, b}, 0);
    REQUIRE(fused.size() == 2);
    CHECK(fused[0].chunk.id == 3);  // equal scores: lower id first, deterministically
    CHECK(fuse_rrf({a, b}, 1).size() == 1);
}

TEST_CASE("vector search skips lexical-only chunks and ranks by cosine",
          "[embedstore][vector][store]") {
    Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("doc.md", {"north", "east", "no vector"},
                         {{1.0F, 0.0F}, {0.0F, 1.0F}, {}});
    const std::vector<SearchHit> hits = store.search_vector({0.9F, 0.1F}, 10);
    REQUIRE(hits.size() == 2);  // the dim=0 chunk has no position in this space
    CHECK(hits[0].chunk.text == "north");
    CHECK(hits[0].retriever == "vector");
    CHECK(hits[0].score > hits[1].score);

    const Store::Stats stats = store.stats();
    CHECK(stats.chunk_count == 3);
    CHECK(stats.dimension == 2);
    CHECK(stats.lexical_only == 1);
    CHECK(stats.vector_dims == 1);
}

TEST_CASE("hybrid search fuses the two halves and labels the result",
          "[embedstore][vector][store]") {
    Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("doc.md", {"the zarquon protocol", "unrelated widgets", "zarquon again"},
                         {{1.0F, 0.0F}, {0.0F, 1.0F}, {0.7F, 0.7F}});
    const std::vector<SearchHit> hits = store.search_hybrid({1.0F, 0.0F}, "zarquon", 10);
    REQUIRE_FALSE(hits.empty());
    for (const SearchHit& entry : hits) {
        CHECK(entry.retriever == "hybrid");
    }
    // "the zarquon protocol" is first in BOTH halves, so it leads the fusion.
    CHECK(hits.front().chunk.text == "the zarquon protocol");
}

TEST_CASE("mixed widths are reported, never averaged", "[embedstore][vector][store]") {
    Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("a.md", {"a"}, {{1.0F, 0.0F}});
    store.replace_source("b.md", {"b"}, {{1.0F, 0.0F, 0.0F}});
    CHECK(store.stats().vector_dims == 2);
}

TEST_CASE("the embedding binding is recorded, read back, and cleared",
          "[embedstore][vector][store]") {
    Scratch scratch;
    Store store{scratch.db()};
    CHECK_FALSE(store.embedding_model().recorded());
    store.set_embedding_model("text-embedding-3-small", 1536);
    CHECK(store.embedding_model().model == "text-embedding-3-small");
    CHECK(store.embedding_model().dimension == 1536);
    store.set_embedding_model("other", 8);  // upsert, not duplicate
    CHECK(store.embedding_model().model == "other");
    store.clear_embedding_model();
    CHECK_FALSE(store.embedding_model().recorded());
}

TEST_CASE("a vector count that does not match the chunks is refused",
          "[embedstore][vector][store]") {
    Scratch scratch;
    Store store{scratch.db()};
    CHECK_THROWS(store.replace_source("doc.md", {"a", "b"}, {{1.0F}}));
}

TEST_CASE("a schema-v1 store written by the previous build opens, migrates, and searches",
          "[embedstore][vector][migration]") {
    // The fixture is a real file `apogee embed ingest` wrote before vectors
    // existed -- not a hand-made schema. Opening it must add the columns,
    // leave its chunk readable by text, and report it as lexical-only.
    const std::filesystem::path fixture =
        std::filesystem::path{APOGEE_SOURCE_DIR} / "../tests/fixtures/embedstore/schema-v1.db";
    REQUIRE(std::filesystem::exists(fixture));

    Scratch scratch;
    std::filesystem::copy_file(fixture, scratch.db());
    Store store{scratch.db()};

    CHECK(store.schema_version() == apogee::embedstore::kSchemaVersion);
    const Store::Stats stats = store.stats();
    CHECK(stats.chunk_count == 1);
    CHECK(stats.dimension == 0);
    CHECK(stats.lexical_only == 1);
    CHECK_FALSE(store.embedding_model().recorded());
    CHECK_FALSE(store.search("zarquon", 5).empty());
    CHECK(store.search_vector({1.0F}, 5).empty());
    // And it can take vectors from here on.
    store.replace_source("notes.md", {"the zarquon protocol requires seventeen widgets"},
                         {{1.0F, 0.0F}});
    CHECK(store.stats().dimension == 2);
    CHECK(store.verify_index().empty());
}
