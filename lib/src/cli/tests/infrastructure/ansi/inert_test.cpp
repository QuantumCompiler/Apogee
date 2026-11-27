#include "ansi/inert.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "ansi/text_width.h"

using apogee::ansi::display_width;
using apogee::ansi::inert_line;
using apogee::ansi::inert_text;
using apogee::ansi::kReplacementCharacter;
using apogee::ansi::without_controls;

/// The one neutraliser (57p): text a model, a tool's caller or a third party
/// chose, shown and never obeyed -- every control character drawn as one
/// U+FFFD, byte by byte, in a line and in text.
namespace {

/// Whether `text` holds a byte a terminal would obey: below 0x20 (LF allowed
/// where `newline` says), DEL, or C1 in either form.
bool obeyable(std::string_view text, bool newline) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if ((byte < 0x20 && !(newline && byte == '\n')) || byte == 0x7F) {
            return true;
        }
        if (byte == 0xC2 && i + 1 < text.size()) {
            const auto next = static_cast<unsigned char>(text[i + 1]);
            if (next >= 0x80 && next <= 0x9F) {
                return true;
            }
        }
    }
    return false;
}

std::string fffd(std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        out += kReplacementCharacter;
    }
    return out;
}

}  // namespace

TEST_CASE("every control character is drawn as U+FFFD in a line, and nothing is obeyed",
          "[ux][ansi][inert][57p]") {
    struct Case {
        std::string raw;
        std::string line;
        const char* why;
    };

    const Case cases[] = {
        {"a\x1b"
         "b",
         "a" + fffd(1) + "b", "ESC"},
        {"a\a", "a" + fffd(1), "BEL"},
        {"\x1b[2Kx", fffd(1) + "[2Kx", "a CSI erase"},
        {"\x1b[3Aup", fffd(1) + "[3Aup", "a CSI cursor move"},
        {"go\x1b]52;c;cm0gLXJmIH4K\x07!", "go" + fffd(1) + "]52;c;cm0gLXJmIH4K" + fffd(1) + "!",
         "an OSC 52"},
        {"x\xC2\x9B"
         "2J",
         "x" + fffd(1) + "2J", "C1 CSI as UTF-8"},
        {"x\x9B"
         "2J",
         "x" + fffd(1) + "2J", "C1 CSI as a lone byte"},
        {"x\x7Fy", "x" + fffd(1) + "y", "DEL"},
        {"a\rb", "a" + fffd(1) + "b", "CR"},
        {"a\nb", "a" + fffd(1) + "b", "LF"},
        {"a\tb", "a" + fffd(1) + "b", "TAB"},
    };
    for (const Case& c : cases) {
        INFO(c.why);
        const std::string line = inert_line(c.raw);
        CHECK(line == c.line);
        CHECK_FALSE(obeyable(line, false));
        // One cell per control: what the width table counts is what is drawn.
        CHECK(display_width(line) == display_width(c.line));
    }
}

TEST_CASE("text keeps its newlines, expands its tabs and drops CR; every other control is U+FFFD",
          "[ux][ansi][inert][57p]") {
    struct Case {
        std::string raw;
        std::string text;
        const char* why;
    };

    const Case cases[] = {
        {"a\x1b"
         "b",
         "a" + fffd(1) + "b", "ESC"},
        {"a\a", "a" + fffd(1), "BEL"},
        {"\x1b[2Kx", fffd(1) + "[2Kx", "a CSI erase"},
        {"Let me think\x1b]52;c;cm0gLXJmIH4K\x07 about it\x1b[3Aup",
         "Let me think" + fffd(1) + "]52;c;cm0gLXJmIH4K" + fffd(1) + " about it" + fffd(1) +
             "[3Aup",
         "the sweep's reasoning chunk"},
        {"x\xC2\x9B"
         "2J",
         "x" + fffd(1) + "2J", "C1 CSI as UTF-8"},
        {"x\x9B"
         "2J",
         "x" + fffd(1) + "2J", "C1 CSI as a lone byte"},
        {"x\x7Fy", "x" + fffd(1) + "y", "DEL"},
        {"a\rb", "ab", "CR, dropped"},
        {"a\nb", "a\nb", "LF, kept"},
        {"a\tb", "a   b", "TAB, to the next four-column stop"},
        {"abcd\tx\n\ty", "abcd    x\n    y", "a stop past a full one, and a new row"},
    };
    for (const Case& c : cases) {
        INFO(c.why);
        const std::string text = inert_text(c.raw);
        CHECK(text == c.text);
        CHECK_FALSE(obeyable(text, true));
    }
}

TEST_CASE("valid text with no control character passes byte for byte, in both forms",
          "[ux][ansi][inert][57p]") {
    for (const std::string_view text :
         {std::string_view{"plain ascii"}, std::string_view{"café — naïve ✅ 日本語 🚀"},
          std::string_view{"\xC2\xA0nbsp and \xC2\xA9"},
          std::string_view{"\xE2\x80\x8B zero width"}}) {
        INFO(text);
        CHECK(inert_line(text) == text);
        CHECK(inert_text(text) == text);
        CHECK(without_controls(text) == text);
    }
}

TEST_CASE("a character's own continuation bytes are never read as C1", "[ux][ansi][inert][57p]") {
    // U+0100 is C4 80, U+2019 is E2 80 99: their 80 and 99 follow a lead, so
    // they are the character, not a control.
    const std::string text = "\xC4\x80 \xE2\x80\x99 \xF0\x9F\x9A\x80";
    CHECK(inert_line(text) == text);
    // A lead cut short by a control: the control is still caught.
    CHECK(inert_line("\xE2\x1b[2J") == "\xE2" + fffd(1) + "[2J");
}

TEST_CASE("a streamed chunk keeps its tab stops from where the last one ended",
          "[ux][ansi][inert][57p]") {
    std::size_t column = 0;
    CHECK(inert_text("ab", column) == "ab");
    CHECK(column == 2);
    CHECK(inert_text("\tc", column) == "  c");
    CHECK(column == 5);
    CHECK(inert_text("\nd\t", column) == "\nd   ");
    CHECK(column == 4);
    // A wide character is two cells toward the stop.
    column = 0;
    CHECK(inert_text("日\t", column) == "日  ");
    CHECK(column == 4);
}

TEST_CASE("without_controls drops every control, for a title that is stored",
          "[ux][ansi][inert][57p]") {
    CHECK(without_controls("Benign \x1b]0;OWNED\x07Title\x1b[2J") == "Benign ]0;OWNEDTitle[2J");
    CHECK(without_controls("a\r\n\tb\x7F\xC2\x9B\x9B"
                           "c") == "abc");
    CHECK_FALSE(obeyable(without_controls("x\x1b\x07\xC2\x85y"), false));
}
