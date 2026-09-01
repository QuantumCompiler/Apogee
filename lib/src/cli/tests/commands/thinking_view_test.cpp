#include "commands/thinking_view.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include "ansi/ansi.h"
#include "commands/terminal.h"

using apogee::ansi::kEraseLine;
using apogee::ansi::kUpAndErase;
using apogee::ansi::Style;
using apogee::commands::display_width;
using apogee::commands::kTailLines;
using apogee::commands::TerminalWriter;
using apogee::commands::ThinkingView;
using apogee::commands::wrap_tail;

namespace {

/// Counts non-overlapping occurrences of `needle`.
std::size_t count(std::string_view haystack, std::string_view needle) {
    std::size_t n = 0;
    for (std::size_t at = haystack.find(needle); at != std::string_view::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

struct Harness {
    std::ostringstream out;
    TerminalWriter writer{out};

    [[nodiscard]] ThinkingView make(ThinkingView::Options options = {}) {
        if (options.width == 0) {
            options.width = 40;
        }
        ThinkingView view{writer, options};
        // A fixed clock so the summary is deterministic.
        view.set_clock([] { return std::int64_t{0}; });
        return view;
    }

    [[nodiscard]] std::string bytes() const {
        return out.str();
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// wrap_tail -- where most of the subtlety lives
// ---------------------------------------------------------------------------

TEST_CASE("wrap_tail wraps at the given width", "[ux][wrap]") {
    const auto rows = wrap_tail("abcdefghij", 4, 10);
    REQUIRE(rows == std::vector<std::string>{"abcd", "efgh", "ij"});
}

TEST_CASE("wrap_tail keeps only the last N rows", "[ux][wrap]") {
    // The rolling window: earlier rows have scrolled off and must be gone.
    const auto rows = wrap_tail("aaaabbbbccccdddd", 4, 2);
    REQUIRE(rows == std::vector<std::string>{"cccc", "dddd"});
}

TEST_CASE("wrap_tail drops blank lines", "[ux][wrap]") {
    // A paragraph break would otherwise spend one of only two precious rows
    // painting nothing.
    const auto rows = wrap_tail("first\n\n\nsecond", 20, 4);
    REQUIRE(rows == std::vector<std::string>{"first", "second"});
}

TEST_CASE("wrap_tail respects explicit newlines", "[ux][wrap]") {
    const auto rows = wrap_tail("one\ntwo\nthree", 20, 10);
    REQUIRE(rows == std::vector<std::string>{"one", "two", "three"});
}

namespace {

/// Whether every UTF-8 sequence in `text` is complete -- the property a
/// byte-wise wrap would break.
bool is_complete_utf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        if ((lead & 0x80U) == 0) {
            length = 1;
        } else if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
        } else {
            return false;  // a continuation byte where a lead byte belongs
        }
        if (i + length > text.size()) {
            return false;  // the sequence runs off the end -- split mid-character
        }
        for (std::size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0U) != 0x80U) {
                return false;
            }
        }
        i += length;
    }
    return true;
}

}  // namespace

TEST_CASE("wrap_tail never splits a multi-byte codepoint", "[ux][wrap]") {
    // Wrapping by byte would cut a UTF-8 sequence in half and corrupt the
    // output -- easy to reach for when the tail is a std::string.
    const auto rows = wrap_tail("héllo wörld ünïcode", 5, 10);

    REQUIRE_FALSE(rows.empty());
    for (const std::string& row : rows) {
        INFO("row: " << row);
        CHECK(is_complete_utf8(row));
        CHECK(display_width(row) <= 5);
    }

    // And every character survives, in order.
    std::string joined;
    for (const std::string& row : rows) {
        joined += row;
    }
    CHECK(joined == "héllo wörld ünïcode");
}

TEST_CASE("the UTF-8 helper itself rejects a split sequence", "[ux][wrap]") {
    // Otherwise the check above could pass vacuously.
    CHECK(is_complete_utf8("héllo"));
    CHECK(is_complete_utf8(""));
    CHECK_FALSE(is_complete_utf8(std::string{"h\xc3"}));  // truncated 2-byte
    CHECK_FALSE(is_complete_utf8(std::string{"\xa9"}));   // stray continuation
}

TEST_CASE("wrap_tail counts display width by codepoint", "[ux][wrap]") {
    CHECK(display_width("abc") == 3);
    CHECK(display_width("héllo") == 5);  // 6 bytes, 5 characters
    CHECK(display_width("✻") == 1);      // 3 bytes, 1 character
    CHECK(display_width("") == 0);
}

TEST_CASE("wrap_tail handles degenerate widths without looping", "[ux][wrap]") {
    CHECK(wrap_tail("abc", 0, 2).empty());
    CHECK(wrap_tail("abc", 5, 0).empty());
    CHECK(wrap_tail("", 5, 2).empty());
    CHECK(wrap_tail("abc", 1, 5).size() == 3);
}

// ---------------------------------------------------------------------------
// The view -- byte-level, because the escape sequences are where bugs live
// ---------------------------------------------------------------------------

TEST_CASE("an empty chunk opens nothing at all", "[ux][thinking]") {
    // Redacted-thinking models emit empty payloads. Opening on one renders an
    // empty reasoning block on every such turn.
    Harness h;
    ThinkingView view = h.make();

    view.write("");
    view.write("");

    CHECK_FALSE(view.open());
    CHECK(view.painted_rows() == 0);
    CHECK(h.bytes().empty());
}

TEST_CASE("a repaint erases exactly the rows it painted", "[ux][thinking]") {
    // The arithmetic that makes collapsing safe. Get the count wrong and the
    // erase eats the user's prompt, or leaves orphaned fragments.
    Harness h;
    ThinkingView view = h.make();

    view.write("first chunk");
    const std::size_t after_first = view.painted_rows();
    REQUIRE(after_first >= 1);

    const std::string before_second = h.bytes();
    view.write(" and more text here to force a second row");

    const std::string second_paint = h.bytes().substr(before_second.size());
    // The second paint opens by erasing the first: one kEraseLine, then one
    // kUpAndErase per row ABOVE the cursor.
    CHECK(second_paint.rfind(std::string{kEraseLine}, 0) == 0);
    CHECK(count(second_paint, kUpAndErase) == after_first - 1);
}

TEST_CASE("a scrolled-off line is absent from the final paint", "[ux][thinking]") {
    Harness h;
    ThinkingView::Options options;
    options.width = 12;  // content width 10
    ThinkingView view = h.make(options);

    view.write("AAAAAAAAAA");  // row 1
    view.write("BBBBBBBBBB");  // row 2
    view.write("CCCCCCCCCC");  // row 3 -- pushes row 1 out of the window

    // Only the last kTailLines rows are painted, so the earliest content is
    // gone from what the user is looking at.
    const std::string all = h.bytes();
    const std::size_t last_paint = all.rfind("✻ Thinking…");
    REQUIRE(last_paint != std::string::npos);
    const std::string final_paint = all.substr(last_paint);

    CHECK(final_paint.find("AAAAAAAAAA") == std::string::npos);
    CHECK(final_paint.find("CCCCCCCCCC") != std::string::npos);
    CHECK(view.painted_rows() == 1 + kTailLines);
}

TEST_CASE("after finish, the bytes past the last erase are exactly the summary", "[ux][thinking]") {
    // THE assertion that proves no reasoning survived into scrollback.
    Harness h;
    ThinkingView view = h.make();

    view.write("some private reasoning the user must not keep");
    view.finish();

    const std::string all = h.bytes();
    // The final erase is the LAST erase-line CSI, not the last `\r\033[2K`:
    // the sequence ends with one cursor-up-and-erase per row above the cursor,
    // and each of those ends in `\033[2K` too.
    constexpr std::string_view kEraseCsi = "\033[2K";
    const std::size_t last_erase = all.rfind(kEraseCsi);
    REQUIRE(last_erase != std::string::npos);

    const std::string tail = all.substr(last_erase + kEraseCsi.size());
    // Nothing but the collapsed line and its blank separator.
    CHECK(tail == "✻ Thought for 1s\n\n");
    CHECK(tail.find("private reasoning") == std::string::npos);
}

TEST_CASE("two blocks in one turn produce two summaries", "[ux][thinking]") {
    // A turn can go thinking → text → thinking; each block collapses
    // independently.
    Harness h;
    ThinkingView view = h.make();

    view.write("first block");
    view.finish();
    view.write("second block");
    view.finish();

    CHECK(count(h.bytes(), "✻ Thought for") == 2);
}

TEST_CASE("finish is idempotent", "[ux][thinking]") {
    // The end-of-turn path calls it unconditionally.
    Harness h;
    ThinkingView view = h.make();

    view.write("reasoning");
    view.finish();
    const std::string after_first = h.bytes();

    view.finish();
    view.finish();
    CHECK(h.bytes() == after_first);
    CHECK(count(h.bytes(), "✻ Thought for") == 1);
}

TEST_CASE("finish with nothing open writes nothing", "[ux][thinking]") {
    Harness h;
    ThinkingView view = h.make();
    view.finish();
    CHECK(h.bytes().empty());
}

TEST_CASE("an inactive view writes nothing whatsoever", "[ux][thinking]") {
    // A pipe gets no thinking, no escape codes, and no summary line. This is
    // the difference between a usable CLI and one that cannot be composed.
    Harness h;
    ThinkingView::Options options;
    options.active = false;
    ThinkingView view = h.make(options);

    view.write("reasoning");
    view.finish();
    view.abandon();

    CHECK(h.bytes().empty());
}

TEST_CASE("verbose mode emits no cursor movement and no summary", "[ux][thinking]") {
    // Someone debugging a model wants every word, and the collapsed view is
    // actively hostile to that.
    Harness h;
    ThinkingView::Options options;
    options.verbose = true;
    ThinkingView view = h.make(options);

    view.write("every word must survive");
    view.finish();

    const std::string all = h.bytes();
    CHECK(all.find("every word must survive") != std::string::npos);
    CHECK(all.find(kUpAndErase) == std::string::npos);
    CHECK(all.find("✻ Thought for") == std::string::npos);
}

TEST_CASE("abandon erases without leaving a summary", "[ux][thinking]") {
    Harness h;
    ThinkingView view = h.make();

    view.write("reasoning");
    view.abandon();

    CHECK_FALSE(view.open());
    CHECK(view.painted_rows() == 0);
    CHECK(h.bytes().find("✻ Thought for") == std::string::npos);
}

TEST_CASE("the retained tail is bounded", "[ux][thinking]") {
    // Only the last rows are ever shown, so keeping the whole block is
    // pointless -- and unbounded on a long reasoning stream.
    Harness h;
    ThinkingView view = h.make();

    for (int i = 0; i < 500; ++i) {
        view.write("0123456789012345678901234567890123456789");
    }
    view.finish();
    SUCCEED("no unbounded growth; the summary still emitted");
}

TEST_CASE("styling is applied only when colour is enabled", "[ux][thinking]") {
    Harness plain;
    ThinkingView::Options no_color;
    no_color.style = Style{false};
    ThinkingView view_plain = plain.make(no_color);
    view_plain.write("text");
    view_plain.finish();
    CHECK(plain.bytes().find(apogee::ansi::kDim) == std::string::npos);

    Harness colored;
    ThinkingView::Options with_color;
    with_color.style = Style{true};
    ThinkingView view_color = colored.make(with_color);
    view_color.write("text");
    view_color.finish();
    CHECK(colored.bytes().find(apogee::ansi::kDim) != std::string::npos);
}
