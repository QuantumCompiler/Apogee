#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "embedstore/fts.h"
#include "embedstore/store.h"

/// **The FTS injection corpus** — a permanent regression suite.
///
/// FTS5 has a query language, and a user asking a question is not writing in
/// it. Every string below is something a person might plausibly type, and every
/// one of them contains syntax that would make a raw `MATCH` fail or, worse,
/// mean something the user did not ask for.
///
/// The contract is one sentence: **a search never raises a syntax error.** It
/// may find nothing — that is a fine answer — but "your question was
/// ungrammatical in a language you did not know you were writing" is not.
namespace {

using apogee::embedstore::fts_match_query;
using apogee::embedstore::Store;

struct Scratch {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("apogee-fts-" + std::to_string(counter()));

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

/// Things a person types that FTS5 would read as syntax.
///
/// Built by a function rather than held as a namespace-scope vector: a static
/// with a throwing constructor cannot report failure anywhere a test can see.
[[nodiscard]] std::vector<std::string> hostile_queries() {
    return {
        // Operators, typed as ordinary English.
        "cats AND dogs",
        "this OR that",
        "NOT going to work",
        "NEAR miss",
        // Quoting, balanced and not.
        R"(the "quick" brown fox)",
        R"(an unbalanced " quote)",
        R"(")",
        R"("")",
        // Column filters and prefixes.
        "text:something",
        "rowid:1",
        "prefix*",
        "*leading",
        "^anchored",
        // Grouping.
        "(unbalanced",
        "unbalanced)",
        "(a OR b) AND c",
        // Punctuation soup, and things that look like SQL.
        "what's the deal?",
        "50% of 100 -- really?",
        "'; DROP TABLE chunks; --",
        R"(" OR "1"="1)",
        "{}[]<>|\\/~`!@#$%^&*()_+=",
        // Empty and whitespace-only.
        "",
        "   ",
        "\t\n",
        "***",
        // Non-ASCII, which must survive as searchable terms.
        "café münchen",
        "日本語のテキスト",
        "emoji 🎉 party",
    };
}

}  // namespace

TEST_CASE("no query a user can type produces a syntax error", "[embedstore][fts][injection]") {
    // THE contract. A search may find nothing; it may never fail because the
    // question was not written in FTS5.
    Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("doc", {"cats and dogs living together", "the quick brown fox",
                                 "café münchen 日本語のテキスト", "50% of the time"});

    for (const std::string& query : hostile_queries()) {
        INFO("query: " << query);
        CHECK_NOTHROW((void)store.search(query, 5));
    }
}

TEST_CASE("every term is quoted as a literal", "[embedstore][fts][injection]") {
    // The mechanism behind the contract: FTS5 reads a double-quoted term as
    // text, so an expression built only from quoted terms can express nothing
    // but "documents containing these words".
    // Joined with OR: a question is not a keyword list, and implicit AND makes
    // a corpus that answers the question return nothing.
    CHECK(fts_match_query("cats AND dogs") == R"("cats" OR "AND" OR "dogs")");
    CHECK(fts_match_query("text:something") == R"("text" OR "something")");
    CHECK(fts_match_query("prefix*") == R"("prefix")");
}

TEST_CASE("a query with no usable terms yields an empty expression",
          "[embedstore][fts][injection]") {
    // Which the caller must treat as "no results" -- an empty MATCH is itself
    // a syntax error, so it must never reach SQLite.
    CHECK(fts_match_query("").empty());
    CHECK(fts_match_query("   ").empty());
    CHECK(fts_match_query("***").empty());
    CHECK(fts_match_query(R"(""")").empty());
}

TEST_CASE("operators typed as words still find documents containing them",
          "[embedstore][fts][injection]") {
    // The usability half. Someone searching for the word "and" should find
    // documents containing it, not an error and not every document.
    Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("doc", {"cats and dogs living together"});

    CHECK_FALSE(store.search("cats AND dogs", 5).empty());
    CHECK_FALSE(store.search(R"("cats")", 5).empty());
}

TEST_CASE("a query shaped like SQL injection is inert", "[embedstore][fts][injection]") {
    // Both layers hold: the terms are bound as parameters, and the expression
    // they build is quoted literals. Neither reaches SQLite as syntax.
    Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("doc", {"harmless content"});

    CHECK_NOTHROW((void)store.search("'; DROP TABLE chunks; --", 5));
    // The table is still there, which is the assertion that matters.
    CHECK(store.chunk_count() == 1);
}

TEST_CASE("non-ASCII text is searchable", "[embedstore][fts][unicode]") {
    // Multi-byte terms must survive the query builder as terms rather than
    // being treated as separators and dropped.
    Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("doc", {"café münchen", "日本語のテキスト"});

    CHECK_FALSE(store.search("café", 5).empty());
    CHECK_FALSE(store.search("münchen", 5).empty());
}

TEST_CASE("bm25 normalisation is monotonic and bounded", "[embedstore][fts][scores]") {
    using apogee::embedstore::normalize_bm25;

    // SQLite's bm25() is negative, more relevant being more negative, so the
    // sign is flipped before scaling. Forgetting that inverts the ranking.
    CHECK(normalize_bm25(-10.0) > normalize_bm25(-1.0));
    // Bounded into (0, 1], so a threshold in a config means something.
    CHECK(normalize_bm25(-1000000.0) < 1.0);
    CHECK(normalize_bm25(0.0) == 0.0);
    CHECK(normalize_bm25(-1.0) == 0.5);
}
