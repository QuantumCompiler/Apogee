#include "embedstore/chunk.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// The chunker, and the one property that actually matters: it never splits a
/// codepoint.
///
/// Cutting UTF-8 at an arbitrary byte offset corrupts the chunk that ends AND
/// the one that begins, and the halves reach FTS5 as invalid text that
/// tokenises into nonsense. The failure is silent and language-specific — an
/// English corpus looks fine while a Japanese one quietly stops matching.
namespace {

using apogee::embedstore::chunk_text;
using apogee::embedstore::codepoint_count;

/// Whether `text` is well-formed UTF-8 — specifically, that it neither begins
/// with a continuation byte nor ends mid-sequence.
[[nodiscard]] bool intact_utf8(std::string_view text) {
    if (text.empty()) {
        return true;
    }
    if ((static_cast<unsigned char>(text.front()) & 0xC0U) == 0x80U) {
        return false;  // starts inside a codepoint
    }
    // Walk to the last lead byte and check the sequence it promises is present.
    std::size_t index = text.size();
    while (index > 0 && (static_cast<unsigned char>(text[index - 1]) & 0xC0U) == 0x80U) {
        --index;
    }
    if (index == 0) {
        return false;
    }
    const auto lead = static_cast<unsigned char>(text[index - 1]);
    std::size_t expected = 1;
    if ((lead & 0xE0U) == 0xC0U) {
        expected = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
        expected = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
        expected = 4;
    }
    return text.size() - (index - 1) == expected;
}

}  // namespace

TEST_CASE("short text becomes exactly one chunk", "[embedstore][chunk]") {
    const std::vector<std::string> chunks = chunk_text("a short document");
    REQUIRE(chunks.size() == 1);
    CHECK(chunks.front() == "a short document");
}

TEST_CASE("empty text becomes no chunks", "[embedstore][chunk]") {
    CHECK(chunk_text("").empty());
}

TEST_CASE("long text is split with overlap", "[embedstore][chunk]") {
    const std::string text(1000, 'x');
    const std::vector<std::string> chunks = chunk_text(text, {.size = 100, .overlap = 20});

    REQUIRE(chunks.size() > 1);
    for (const std::string& chunk : chunks) {
        CHECK(codepoint_count(chunk) <= 100);
    }
    // Overlap exists so a passage straddling a boundary is still findable: the
    // stride must be smaller than the chunk, or the sentence at the cut appears
    // whole in neither chunk and matches neither.
    CHECK(chunks.size() >= 1000 / 80);
}

TEST_CASE("multi-byte characters are never split", "[embedstore][chunk][unicode]") {
    // THE property. Every chunk of a corpus that is entirely multi-byte must
    // still be valid UTF-8 -- at every chunk size, because the interesting
    // boundaries are the ones that land inside a character.
    const std::string japanese = [] {
        std::string out;
        for (int i = 0; i < 400; ++i) {
            out += "日本語のテキストです。";
        }
        return out;
    }();

    for (const std::size_t size :
         {std::size_t{7}, std::size_t{16}, std::size_t{63}, std::size_t{100}, std::size_t{512}}) {
        INFO("chunk size: " << size);
        const std::vector<std::string> chunks =
            chunk_text(japanese, {.size = size, .overlap = size / 4});
        REQUIRE_FALSE(chunks.empty());
        for (const std::string& chunk : chunks) {
            CHECK(intact_utf8(chunk));
            CHECK(codepoint_count(chunk) <= size);
        }
    }
}

TEST_CASE("mixed-width text is counted by codepoint, not by byte", "[embedstore][chunk][unicode]") {
    // "héllo 🎉" is 7 codepoints and 11 bytes -- h, l, l, o and the space are
    // one byte each, é is two, and the emoji is four. A byte-based chunker
    // would cut in the wrong place and silently give English and non-English
    // text different boundaries.
    CHECK(codepoint_count("héllo 🎉") == 7);
    CHECK(std::string_view{"héllo 🎉"}.size() == 11);

    const std::vector<std::string> chunks = chunk_text("héllo 🎉", {.size = 7, .overlap = 0});
    REQUIRE(chunks.size() == 1);
    CHECK(chunks.front() == "héllo 🎉");
}

TEST_CASE("an overlap at or above the size cannot hang", "[embedstore][chunk][regression]") {
    // A value a user can write in a config must never be a value that loops
    // forever. Both of these would make zero forward progress unclamped.
    const std::string text(500, 'y');

    const std::vector<std::string> equal = chunk_text(text, {.size = 100, .overlap = 100});
    CHECK(equal.size() > 1);

    const std::vector<std::string> larger = chunk_text(text, {.size = 100, .overlap = 500});
    CHECK(larger.size() > 1);
}

TEST_CASE("a zero chunk size is clamped rather than dividing by nothing",
          "[embedstore][chunk][regression]") {
    CHECK_FALSE(chunk_text("some text", {.size = 0, .overlap = 0}).empty());
}

TEST_CASE("the whole text is recoverable from its chunks", "[embedstore][chunk]") {
    // With no overlap the chunks concatenate back to the original -- the
    // simplest statement of "nothing was lost or duplicated at a boundary".
    const std::string text = "the quick brown fox jumps over the lazy dog, repeatedly and often";
    std::string rejoined;
    for (const std::string& chunk : chunk_text(text, {.size = 10, .overlap = 0})) {
        rejoined += chunk;
    }
    CHECK(rejoined == text);
}

TEST_CASE("invalid bytes are chunked leniently rather than rejected",
          "[embedstore][chunk][unicode]") {
    // This runs over arbitrary user files. A document with one bad byte should
    // be chunked slightly oddly, not refused.
    const std::string broken = std::string("good text ") + '\xC3' + " more text";
    CHECK_NOTHROW((void)chunk_text(broken, {.size = 5, .overlap = 1}));
    CHECK_FALSE(chunk_text(broken, {.size = 5, .overlap = 1}).empty());
}
