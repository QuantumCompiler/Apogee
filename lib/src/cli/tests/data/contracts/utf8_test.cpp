#include "contracts/utf8.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

/// UTF-8 out of whatever bytes arrive: whole, or as stream pieces.
///
/// The expectations for ill-formed input are the Unicode Standard's own
/// examples of "substitution of maximal subparts" (chapter 3), which the
/// WHATWG decoder implements -- one U+FFFD per maximal subpart, so a
/// truncated sequence is one replacement and an overlong or surrogate form is
/// one per byte. Every split of a character across pieces is tried, because
/// the bug this guards is a character cut at a byte boundary nobody chose.
namespace {

using apogee::harness::is_valid_utf8;
using apogee::harness::Utf8Stream;
using apogee::harness::valid_utf8;

/// `count` replacement characters, U+FFFD, as UTF-8.
[[nodiscard]] std::string replaced(int count = 1) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        out += "\xEF\xBF\xBD";
    }
    return out;
}

/// Every piece `pieces` hands back from one stream, then its flush.
[[nodiscard]] std::vector<std::string> streamed(const std::vector<std::string>& pieces) {
    Utf8Stream stream;
    std::vector<std::string> out;
    out.reserve(pieces.size() + 1);
    for (const std::string& piece : pieces) {
        out.push_back(stream.feed(piece));
    }
    out.push_back(stream.flush());
    return out;
}

[[nodiscard]] std::string joined(const std::vector<std::string>& pieces) {
    std::string out;
    for (const std::string& piece : pieces) {
        out += piece;
    }
    return out;
}

/// Whether `text` survives the strict dump the whole bug class is about.
[[nodiscard]] bool dumps(const std::string& text) {
    try {
        (void)nlohmann::json(text).dump();
        return true;
    } catch (const nlohmann::json::type_error&) {
        return false;
    }
}

}  // namespace

TEST_CASE("valid text passes through byte for byte", "[contracts][utf8]") {
    // ASCII, every sequence length, the edges of each range, the last
    // scalar value, and the controls a JSON dump escapes.
    for (const std::string& text :
         {std::string{}, std::string{"plain ASCII, \"quoted\"\n\t"}, std::string{"\x7F"},
          std::string{"caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80"}, std::string{"\xC2\x80\xDF\xBF"},
          std::string{"\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF"},
          std::string{"\xF0\x90\x80\x80\xF4\x8F\xBF\xBF"}, std::string{"\0nul", 4}}) {
        INFO(text);
        CHECK(is_valid_utf8(text));
        CHECK(valid_utf8(text) == text);
        CHECK(joined(streamed({text})) == text);
        CHECK(streamed({text}).back().empty());
    }
}

TEST_CASE("a character split across pieces arrives whole in the piece that completes it",
          "[contracts][utf8]") {
    // A two-, three- and four-byte character between ASCII, cut at every
    // byte boundary into two pieces and into three. No piece handed back is
    // ever half a character, and together they are the text exactly.
    for (const std::string character : {"\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"}) {
        const std::string text = "a" + character + "z";
        for (std::size_t first = 1; first < text.size(); ++first) {
            for (std::size_t second = first; second < text.size(); ++second) {
                std::vector<std::string> pieces{text.substr(0, first)};
                if (second > first) {
                    pieces.push_back(text.substr(first, second - first));
                }
                pieces.push_back(text.substr(second));
                INFO("cut at " << first << " and " << second);
                const std::vector<std::string> out = streamed(pieces);
                CHECK(joined(out) == text);
                CHECK(out.back().empty());  // nothing left at the end
                for (const std::string& piece : out) {
                    CHECK(dumps(piece));
                    CHECK(valid_utf8(piece) == piece);
                }
            }
        }
    }
}

TEST_CASE("a character arriving a byte at a time is held until it is whole", "[contracts][utf8]") {
    // llama.cpp's byte-fallback tokens: one byte per piece.
    CHECK(streamed({"\xF0", "\x9F", "\x98", "\x80"}) ==
          std::vector<std::string>{"", "", "", "\xF0\x9F\x98\x80", ""});
    CHECK(streamed({"ok \xE2", "\x82", "\xAC!"}) ==
          std::vector<std::string>{"ok ", "", "\xE2\x82\xAC!", ""});
}

TEST_CASE("a stream that ends inside a character flushes exactly one U+FFFD", "[contracts][utf8]") {
    for (const std::string tail :
         {"\xC3", "\xE2", "\xE2\x82", "\xF0", "\xF0\x9F", "\xF0\x9F\x98"}) {
        INFO(tail);
        const std::vector<std::string> out = streamed({"caf", tail});
        CHECK(out == std::vector<std::string>{"caf", "", replaced()});
        CHECK_FALSE(is_valid_utf8("caf" + tail));
        // The same as the whole string, read at once.
        CHECK(valid_utf8("caf" + tail) == "caf" + replaced());
    }
    // Flushed, the stream is empty again.
    Utf8Stream stream;
    CHECK(stream.feed("\xC3").empty());
    CHECK(stream.flush() == replaced());
    CHECK(stream.flush().empty());
    CHECK(stream.feed("\xC3\xA9") == "\xC3\xA9");
}

TEST_CASE("each maximal subpart of an ill-formed sequence is one U+FFFD", "[contracts][utf8]") {
    struct Case {
        std::string bytes;
        std::string expected;
    };

    const std::vector<Case> cases{
        // A byte that begins nothing.
        {"\xFF", replaced(1)},
        {"\xFE\xFF", replaced(2)},
        {"\xF5\x80", replaced(2)},
        // A continuation byte with nothing before it.
        {"\x80", replaced(1)},
        {std::string{"a\xBF"} + "b", "a" + replaced(1) + "b"},
        // Overlong forms: C0 and C1 begin nothing; E0 and F0 refuse a second
        // byte that would make one, which is then read on its own.
        {"\xC0\x80", replaced(2)},
        {"\xC1\xBF", replaced(2)},
        {"\xE0\x80\x80", replaced(3)},
        {"\xF0\x80\x80\x80", replaced(4)},
        // A surrogate, and a point past U+10FFFF.
        {"\xED\xA0\x80", replaced(3)},
        {"\xF4\x90\x80\x80", replaced(4)},
        // A sequence broken by what follows: its well-formed start is one
        // replacement, and the byte that broke it is read again.
        {std::string{"\xE2\x82"} + "A", replaced(1) + "A"},
        {std::string{"\xF0\x9F\x98"} + "!", replaced(1) + "!"},
        {"\xC3\xC3\xA9", replaced(1) + "\xC3\xA9"},
        {"\xE2\x82\xE2\x82\xAC", replaced(1) + "\xE2\x82\xAC"},
        // Latin-1 text, as a file in that encoding holds it.
        {"caf\xE9 cr\xE8me", "caf" + replaced(1) + " cr" + replaced(1) + "me"},
    };
    for (const Case& test : cases) {
        INFO(test.bytes);
        CHECK_FALSE(is_valid_utf8(test.bytes));
        CHECK(valid_utf8(test.bytes) == test.expected);
        CHECK(is_valid_utf8(valid_utf8(test.bytes)));
        // The stream agrees, whole and a byte at a time: an invalid byte is
        // replaced as it arrives, never held for a piece that cannot mend it.
        CHECK(joined(streamed({test.bytes})) == test.expected);
        std::vector<std::string> bytes;
        for (const char byte : test.bytes) {
            bytes.emplace_back(1, byte);
        }
        CHECK(joined(streamed(bytes)) == test.expected);
    }
    // Replaced at once, not at the flush.
    Utf8Stream stream;
    CHECK(stream.feed("a\xFF") == "a" + replaced());
    CHECK(stream.feed("\xC0") == replaced());
    CHECK(stream.flush().empty());
}
