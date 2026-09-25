#include "commands/answer_view.h"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "ansi/text_width.h"
#include "markdown/layout.h"
#include "markdown/stream_renderer.h"
#include "support/terminal_model.h"

/// The painter, replayed through a terminal model: what the screen ends up
/// showing, whatever the chunking, the width or the height.
namespace {

using apogee::commands::AnswerView;
using apogee::testing::TerminalModel;

constexpr std::string_view kAnswer =
    "## The short version\n"
    "\n"
    "The **violin** is usually named the hardest, for three reasons:\n"
    "\n"
    "- **No frets.** Pitch is entirely up to your ear and your finger placement; a "
    "millimetre off and you are flat or sharp.\n"
    "- **Bowing** is its own instrument: tone, dynamics and articulation all live in the "
    "right arm.\n"
    "  - and it takes *years* to sound good\n"
    "\n"
    "| Instrument | Why it is hard |\n"
    "|---|---|\n"
    "| French horn | tiny mouthpiece, tricky intonation |\n"
    "| Pipe organ | two keyboards and the pedals |\n"
    "\n"
    "> Hardest depends on the player.\n"
    "\n"
    "```text\n"
    "practice → progress\n"
    "```\n"
    "Were you thinking about picking one up? 🎻\n";

struct Painted {
    std::string bytes;
    std::size_t painted_rows_at_end = 0;
};

/// Paints `text` in pieces of `piece` bytes (0: whole) and returns the bytes.
[[nodiscard]] Painted paint(std::string_view text, std::size_t width, std::size_t piece = 0,
                            std::size_t height = 0, bool hyperlinks = false, bool color = false) {
    std::ostringstream out;
    AnswerView::Options options;
    options.out = &out;
    options.style = apogee::ansi::Style{color};
    options.hyperlinks = hyperlinks;
    options.width = width;
    if (height > 0) {
        options.measure_height = [height] { return height; };
    }
    AnswerView view{options};
    view.begin();
    const std::size_t step = piece == 0 ? text.size() : piece;
    for (std::size_t at = 0; at < text.size(); at += step) {
        view.write(text.substr(at, step));
    }
    view.finish();
    return Painted{out.str(), view.painted_rows()};
}

[[nodiscard]] TerminalModel replay(const std::string& bytes, std::size_t width,
                                   std::size_t height = 1000) {
    TerminalModel model{width, height};
    model.feed(bytes);
    CHECK(model.unhandled().empty());
    CHECK_FALSE(model.climbed_past_top());
    return model;
}

/// What the renderer alone commits for `text` at `width`, as plain lines.
[[nodiscard]] std::vector<std::string> expected_lines(std::string_view text, std::size_t width) {
    apogee::markdown::StreamRenderer renderer;
    std::vector<std::string> out;
    for (const auto& row : renderer.feed(text, width).commit) {
        out.push_back(apogee::markdown::plain_text(row));
    }
    for (const auto& row : renderer.finish(width).commit) {
        out.push_back(apogee::markdown::plain_text(row));
    }
    // The terminal model trims trailing spaces; so does this.
    for (std::string& line : out) {
        line.erase(line.find_last_not_of(' ') + 1);
    }
    while (!out.empty() && out.back().empty()) {
        out.pop_back();
    }
    return out;
}

}  // namespace

TEST_CASE("the screen ends up showing exactly what was committed", "[ux][answer]") {
    const Painted painted = paint(kAnswer, 80, 5);
    CHECK(painted.painted_rows_at_end == 0);
    const TerminalModel screen = replay(painted.bytes, 80);
    // One short of the width: the view never uses the last column.
    CHECK(screen.lines() == expected_lines(kAnswer, 79));
}

TEST_CASE("the same answer in any chunking ends in the same screen", "[ux][answer]") {
    // Tokens split words, markers, codepoints and table rows wherever they
    // like; the scrollback must not care.
    const std::string whole = replay(paint(kAnswer, 72).bytes, 72).text();
    CHECK(replay(paint(kAnswer, 72, 1).bytes, 72).text() == whole);
    CHECK(replay(paint(kAnswer, 72, 2).bytes, 72).text() == whole);
    std::mt19937 random{925};
    for (int trial = 0; trial < 10; ++trial) {
        std::ostringstream out;
        AnswerView::Options options;
        options.out = &out;
        options.width = 72;
        AnswerView view{options};
        view.begin();
        for (std::size_t at = 0; at < kAnswer.size();) {
            const std::size_t length =
                std::min<std::size_t>(1 + random() % 11, kAnswer.size() - at);
            view.write(kAnswer.substr(at, length));
            at += length;
        }
        view.finish();
        CHECK(replay(out.str(), 72).text() == whole);
    }
}

TEST_CASE("no painted row reaches the terminal's last column", "[ux][answer]") {
    for (const std::size_t width : {40U, 80U, 120U}) {
        INFO("width " << width);
        const TerminalModel screen = replay(paint(kAnswer, width, 3).bytes, width);
        CHECK(screen.widest_column() + 1 < width);
    }
}

TEST_CASE("an open line taller than the screen never makes the erase climb out of it",
          "[ux][answer]") {
    // A long paragraph is one source line; while it streams, the view shows
    // only what fits, and commits the whole of it when its newline arrives.
    std::string paragraph;
    for (int i = 0; i < 120; ++i) {
        paragraph += "word" + std::to_string(i) + " ";
    }
    paragraph += "\nafter\n";
    const std::size_t height = 8;
    const TerminalModel screen = replay(paint(paragraph, 40, 7, height).bytes, 40, height);
    CHECK(screen.lines() == expected_lines(paragraph, 39));
}

TEST_CASE("a resize mid-answer changes the width of what is painted next", "[ux][answer]") {
    std::ostringstream out;
    std::size_t width = 60;
    AnswerView::Options options;
    options.out = &out;
    options.measure_width = [&width] { return width; };
    AnswerView view{options};
    view.begin();
    view.write("A first line that is long enough to need wrapping at forty columns.\n");
    width = 30;
    view.write("A second line, painted after the terminal narrowed to thirty columns.\n");
    view.finish();
    const TerminalModel screen = replay(out.str(), 60);
    const std::vector<std::string> lines = screen.lines();
    REQUIRE(lines.size() >= 3);
    CHECK(lines[0].size() > 30);  // painted at the old width
    for (std::size_t i = 1; i < lines.size(); ++i) {
        CHECK(apogee::ansi::display_width(lines[i]) < 30);  // and the new
    }
}

TEST_CASE("links are hyperlinks only where the terminal supports them", "[ux][answer]") {
    const std::string answer = "See [the guide](https://x.test/g).\n";
    const std::string linked = paint(answer, 80, 0, 0, /*hyperlinks=*/true, /*color=*/true).bytes;
    CHECK(linked.find("\033]8;;https://x.test/g") != std::string::npos);
    CHECK(linked.find("(https://x.test/g)") == std::string::npos);
    const std::string plain = paint(answer, 80, 0, 0, /*hyperlinks=*/false, /*color=*/true).bytes;
    CHECK(plain.find("\033]8;;") == std::string::npos);
    CHECK(replay(plain, 80).text() == "See the guide (https://x.test/g).\n");
}

TEST_CASE("an answer of only whitespace paints nothing, and finish is idempotent", "[ux][answer]") {
    std::ostringstream out;
    AnswerView::Options options;
    options.out = &out;
    AnswerView view{options};
    view.begin();
    view.write("\n\n  \n");
    view.finish();
    view.finish();
    CHECK(out.str().empty());
    CHECK_FALSE(view.began());
}
