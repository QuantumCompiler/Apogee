#include "contracts/captions.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// Captions as SubRip text (50b): the times, the numbering, the cues left
/// out, and caption text kept as data -- valid UTF-8, no blank line inside a
/// cue, no character a renderer would read as markup.
using apogee::harness::CaptionCue;
using apogee::harness::srt_time;
using apogee::harness::write_srt;

TEST_CASE("an SRT time is hours, minutes, seconds and rounded milliseconds",
          "[contracts][captions]") {
    CHECK(srt_time(0.0) == "00:00:00,000");
    CHECK(srt_time(62.345) == "00:01:02,345");
    CHECK(srt_time(3725.0006) == "01:02:05,001");
    CHECK(srt_time(1.9996) == "00:00:02,000");
    CHECK(srt_time(-3.0) == "00:00:00,000");
}

TEST_CASE("cues are numbered from one, timed, and a blank line ends each",
          "[contracts][captions]") {
    CHECK(write_srt({}).empty());
    CHECK(write_srt({{.text = "Hello there.", .start = 0.0, .end = 1.25},
                     {.text = "How are you?", .start = 1.25, .end = 2.5}}) ==
          "1\n00:00:00,000 --> 00:00:01,250\nHello there.\n\n"
          "2\n00:00:01,250 --> 00:00:02,500\nHow are you?\n\n");
    // A cue with nothing to say, or no length, is left out -- the numbers
    // stay consecutive.
    CHECK(write_srt({{.text = "  \n", .start = 0.0, .end = 1.0},
                     {.text = "Kept.", .start = 1.0, .end = 2.0},
                     {.text = "Never shown.", .start = 2.0, .end = 2.0},
                     {.text = "Backwards.", .start = 3.0, .end = 2.5},
                     {.text = "Also kept.", .start = 3.0, .end = 4.0}}) ==
          "1\n00:00:01,000 --> 00:00:02,000\nKept.\n\n"
          "2\n00:00:03,000 --> 00:00:04,000\nAlso kept.\n\n");
}

TEST_CASE("caption text is data: valid UTF-8, no blank line, no markup", "[contracts][captions]") {
    const auto text = [](const std::string& said) {
        const std::string srt = write_srt({{.text = said, .start = 0.0, .end = 1.0}});
        const std::string head = "1\n00:00:00,000 --> 00:00:01,000\n";
        REQUIRE(srt.starts_with(head));
        REQUIRE(srt.ends_with("\n\n"));
        return srt.substr(head.size(), srt.size() - head.size() - 2);
    };
    // A blank line would end the cue: closed up, carriage returns gone, the
    // ends trimmed.
    CHECK(text("one\r\n\r\n\ntwo\n\n") == "one\ntwo");
    // Text in any script is kept as it is; a broken byte is one U+FFFD.
    CHECK(text("Café, 日本語 — ok") == "Café, 日本語 — ok");
    CHECK(text("bad \xFF byte") == "bad \xEF\xBF\xBD byte");
    // SubRip's tags, ASS's override blocks and its escapes: fullwidth.
    CHECK(text("<i>a</i> {\\an8} C:\\N") ==
          "\xEF\xBC\x9Ci\xEF\xBC\x9E"
          "a"
          "\xEF\xBC\x9C/i\xEF\xBC\x9E \xEF\xBD\x9B\xEF\xBC\xBC"
          "an8\xEF\xBD\x9D C:\xEF\xBC\xBCN");
    // An arrow inside a cue's text is only text: its `>` is fullwidth too.
    CHECK(text("before --> after") == "before --\xEF\xBC\x9E after");
}
