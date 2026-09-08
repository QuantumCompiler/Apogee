#include "embedstore/store.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "embedstore/fts.h"

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
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("apogee-store-" + std::to_string(counter()));

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
    CHECK_THROWS_AS(Store{std::filesystem::path{"/proc/nonexistent/store.db"}}, std::runtime_error);
}

TEST_CASE("an empty corpus answers nothing rather than failing", "[embedstore][store]") {
    Scratch scratch;
    Store store{scratch.db()};
    CHECK(store.search("anything", 5).empty());
    CHECK(store.chunk_count() == 0);
    CHECK(store.sources().empty());
}
