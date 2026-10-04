#include "markdown/stream_renderer.h"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>
#include <vector>

#include "ansi/text_width.h"
#include "markdown/inline.h"
#include "markdown/layout.h"

/// The renderer's operations, construct by construct, with no terminal.
namespace {

using apogee::markdown::plain_text;
using apogee::markdown::RenderOps;
using apogee::markdown::Row;
using apogee::markdown::StreamRenderer;

/// Every row the renderer commits for `text`, fed in pieces of `piece`
/// bytes (0: whole), then finished.
[[nodiscard]] std::vector<Row> committed(std::string_view text, std::size_t width,
                                         std::size_t piece = 0) {
    StreamRenderer renderer;
    std::vector<Row> rows;
    const auto take = [&rows](const RenderOps& ops) {
        rows.insert(rows.end(), ops.commit.begin(), ops.commit.end());
    };
    const std::size_t step = piece == 0 ? text.size() : piece;
    for (std::size_t at = 0; at < text.size(); at += step) {
        take(renderer.feed(text.substr(at, step), width));
    }
    const RenderOps last = renderer.finish(width);
    take(last);
    CHECK(last.open.empty());
    return rows;
}

[[nodiscard]] std::vector<std::string> lines(std::string_view text, std::size_t width = 60) {
    std::vector<std::string> out;
    for (const Row& row : committed(text, width)) {
        out.push_back(plain_text(row));
    }
    return out;
}

/// The first span in `rows` whose text contains `needle`.
[[nodiscard]] const apogee::markdown::Span* find(const std::vector<Row>& rows,
                                                 std::string_view needle) {
    for (const Row& row : rows) {
        for (const auto& span : row) {
            if (span.text.find(needle) != std::string::npos) {
                return &span;
            }
        }
    }
    return nullptr;
}

constexpr std::string_view kSample =
    "# Setting up the project\n"
    "\n"
    "You'll need **three** things, and _one_ of them is `cmake`:\n"
    "\n"
    "1. A compiler with C++20 support\n"
    "2. CMake 3.25 or newer, which you can install with a package manager\n"
    "   such as Homebrew or apt\n"
    "3. Git\n"
    "   - with LFS for the models\n"
    "   - [ ] configured\n"
    "\n"
    "> Note: the first build downloads llama.cpp.\n"
    "\n"
    "```bash\n"
    "cmake --preset default\n"
    "\tcmake --build build/default -j 16\n"
    "```\n"
    "\n"
    "| Target | Status | Time |\n"
    "|:-------|:------:|-----:|\n"
    "| macOS  | ✅ ok  | 4 m  |\n"
    "| Linux  | ok     | 12 m |\n"
    "\n"
    "---\n"
    "See [the guide](https://x.test/guide) for ~~more~~ everything.\n";

}  // namespace

TEST_CASE("a paragraph wraps between words and never past the width", "[markdown][render]") {
    const std::vector<std::string> out =
        lines("The quick brown fox jumps over the lazy dog, twice over.", 20);
    CHECK(out == std::vector<std::string>{"The quick brown fox", "jumps over the lazy",
                                          "dog, twice over."});
    for (const Row& row : committed(kSample, 30)) {
        CHECK(apogee::markdown::row_width(row) <= 30);
    }
}

TEST_CASE("headings drop their hashes and are bold, the top two coloured", "[markdown][render]") {
    const std::vector<Row> rows = committed("# Title\n## Section\n### Detail ###\n", 40);
    REQUIRE(rows.size() == 3);
    CHECK(plain_text(rows[0]) == "Title");
    CHECK(rows[0][0].attributes.bold);
    CHECK(rows[0][0].attributes.underline);
    CHECK(rows[0][0].attributes.color == apogee::ansi::Color::Cyan);
    CHECK(plain_text(rows[1]) == "Section");
    CHECK(rows[1][0].attributes.color == apogee::ansi::Color::Cyan);
    CHECK_FALSE(rows[1][0].attributes.underline);
    CHECK(plain_text(rows[2]) == "Detail");
    CHECK(rows[2][0].attributes.bold);
    CHECK(rows[2][0].attributes.color == apogee::ansi::Color::Default);
    // "#hashtag" is text, not a heading.
    CHECK(lines("#hashtag") == std::vector<std::string>{"#hashtag"});
}

TEST_CASE("lists get bullets by depth, their numbers, and hanging indents", "[markdown][render]") {
    CHECK(lines("- one\n  - two\n    - three\n      - four\n") ==
          std::vector<std::string>{"• one", "  ◦ two", "    ▪ three", "      • four"});
    CHECK(lines("1. first\n2. second\n10. tenth\n") ==
          std::vector<std::string>{"1. first", "2. second", "10. tenth"});
    CHECK(lines("- [ ] todo\n- [x] done\n") == std::vector<std::string>{"• ☐ todo", "• ☑ done"});
    // A wrapped item lines up under its text, not under its bullet.
    CHECK(
        lines("- a list item long enough to wrap onto a second row\n", 24) ==
        std::vector<std::string>{"• a list item long", "  enough to wrap onto a", "  second row"});
    // An indented line continues the item; a lazy one straight after it does
    // too; a paragraph after a blank line at the margin ends the list.
    CHECK(
        lines("1. Install it\n   with brew\nnow\n\nAfter the list.\n") ==
        std::vector<std::string>{"1. Install it", "   with brew", "   now", "", "After the list."});
}

TEST_CASE("quotes sit behind a gutter, one per level", "[markdown][render]") {
    const std::vector<Row> rows = committed("> quoted text\n> > deeper\n>\n", 40);
    REQUIRE(rows.size() == 3);
    CHECK(plain_text(rows[0]) == "│ quoted text");
    CHECK(rows[0][0].attributes.dim);
    CHECK(plain_text(rows[1]) == "│ │ deeper");
    CHECK(plain_text(rows[2]) == "│ ");
}

TEST_CASE("fenced code is a dim block with its language named, cut not wrapped",
          "[markdown][render]") {
    const std::vector<Row> rows =
        committed("```python\ndef f():\n\treturn 'a very long string indeed'\n```\nafter\n", 24);
    std::vector<std::string> text;
    for (const Row& row : rows) {
        text.push_back(plain_text(row));
    }
    CHECK(text == std::vector<std::string>{"  python", "  def f():", "      return 'a very lon",
                                           "  g string indeed'", "after"});
    CHECK(rows[0].back().attributes.italic);
    CHECK(rows[1].back().attributes.dim);
    // Markdown inside code is code.
    CHECK(lines("```\n**not bold**\n```\n") == std::vector<std::string>{"  **not bold**"});
}

TEST_CASE("a rule spans the width", "[markdown][render]") {
    const std::vector<std::string> out = lines("above\n\n---\n", 12);
    REQUIRE(out.size() == 3);
    CHECK(apogee::ansi::display_width(out[2]) == 12);
    CHECK(out[2].find("─") == 0);
}

TEST_CASE("a setext underline is a rule, not a row of equals signs", "[markdown][render]") {
    // Llama 3.2 underlines its headings; the heading's text is committed
    // before its underline arrives, so the underline becomes a rule.
    const std::vector<std::string> out = lines("**Title**\n=====\n\n**Section**\n-----\n", 8);
    CHECK(out == std::vector<std::string>{"Title", "════════", "", "Section", "────────"});
}

TEST_CASE("a table is laid out in columns with its alignment", "[markdown][render]") {
    const std::vector<std::string> out = lines(
        "| Name | Score | Note |\n|:--|--:|:-:|\n| ada | 9 | top |\n| grace | 10 | x |\n", 60);
    CHECK(out == std::vector<std::string>{"Name  │ Score │ Note", "──────┼───────┼─────",
                                          "ada   │     9 │ top", "grace │    10 │  x"});
    // Wider than the screen: the narrow column keeps its width, the wide one
    // wraps within what is left.
    CHECK(lines("| id | description |\n|---|---|\n| 7 | a cell long enough to wrap |\n", 24) ==
          std::vector<std::string>{"id │ description", "───┼────────────────────",
                                   "7  │ a cell long enough", "   │ to wrap"});
    // So many columns that even narrow ones cannot fit: shown as written.
    const std::vector<std::string> crowded =
        lines("| a | b | c | d |\n|---|---|---|---|\n| 1 | 2 | 3 | 4 |\n", 20);
    CHECK(crowded.front() == "| a | b | c | d |");
}

TEST_CASE("a line with a pipe that heads no table is ordinary text", "[markdown][render]") {
    CHECK(lines("Pipe it: cat log | grep error\nThen stop.\n") ==
          std::vector<std::string>{"Pipe it: cat log | grep error", "Then stop."});
    CHECK(lines("| not a table\nplain\n") == std::vector<std::string>{"| not a table", "plain"});
}

TEST_CASE("blank lines: none before, one between, none after", "[markdown][render]") {
    CHECK(lines("\n\n\nHello\n\n\n\nWorld\n\n\n") ==
          std::vector<std::string>{"Hello", "", "World"});
}

TEST_CASE("control characters are shown, never obeyed", "[markdown][render]") {
    // An escape sequence in an answer must not drive the terminal, and must
    // not throw off the width arithmetic.
    const std::vector<std::string> out = lines("clear\x1b[2Jscreen\x07\n");
    REQUIRE(out.size() == 1);
    CHECK(out[0].find('\x1b') == std::string::npos);
    CHECK(out[0].find('\x07') == std::string::npos);
    CHECK(out[0] == "clear\xEF\xBF\xBD[2Jscreen\xEF\xBF\xBD");
}

TEST_CASE("the open line shows what has arrived, markers and all, until it closes",
          "[markdown][render]") {
    StreamRenderer renderer;
    RenderOps ops = renderer.feed("Some **bo", 40);
    CHECK(ops.commit.empty());
    REQUIRE(ops.open.size() == 1);
    CHECK(plain_text(ops.open[0]) == "Some **bo");

    ops = renderer.feed("ld** text\nnext", 40);
    REQUIRE(ops.commit.size() == 1);
    CHECK(plain_text(ops.commit[0]) == "Some bold text");
    CHECK(find(ops.commit, "bold")->attributes.bold);
    REQUIRE(ops.open.size() == 1);
    CHECK(plain_text(ops.open[0]) == "next");

    // A list item shows as one while it is still arriving.
    ops = renderer.feed(" line\n- item in prog", 40);
    CHECK(plain_text(ops.open.at(0)) == "• item in prog");
}

TEST_CASE("a character cut in half by a chunk waits for the rest of its bytes",
          "[markdown][render]") {
    // Found by the terminal model: a fragment painted in the open area is a
    // cell the width arithmetic cannot count, the row wraps one short, and
    // the erase leaves a line behind.
    StreamRenderer renderer;
    RenderOps ops = renderer.feed("caf\xC3", 40);
    REQUIRE(ops.open.size() == 1);
    CHECK(plain_text(ops.open[0]) == "caf");
    ops = renderer.feed("\xA9 au lait \xF0\x9F", 40);
    CHECK(plain_text(ops.open.at(0)) == "caf\xC3\xA9 au lait");
    ops = renderer.feed("\x8E\xBB\n", 40);
    REQUIRE(ops.commit.size() == 1);
    CHECK(plain_text(ops.commit[0]) == "caf\xC3\xA9 au lait \xF0\x9F\x8E\xBB");
}

TEST_CASE("a streaming table shows one placeholder until it ends", "[markdown][render]") {
    StreamRenderer renderer;
    RenderOps ops = renderer.feed("| a | b |\n|---|---|\n| 1 | 2 |\n| 3 ", 40);
    CHECK(ops.commit.empty());
    REQUIRE(ops.open.size() == 1);
    CHECK(plain_text(ops.open[0]) == "table · 2 rows…");
    CHECK(ops.open[0][0].attributes.dim);

    ops = renderer.feed("| 4 |\nDone.\n", 40);
    std::vector<std::string> text;
    for (const Row& row : ops.commit) {
        text.push_back(plain_text(row));
    }
    CHECK(text == std::vector<std::string>{"a │ b", "──┼──", "1 │ 2", "3 │ 4", "Done."});
    CHECK(ops.open.empty());
}

TEST_CASE("the same answer in any chunking commits the same rows", "[markdown][render]") {
    // THE streaming property: tokens split words and markers wherever they
    // like, and the result must not care.
    const std::vector<Row> whole = committed(kSample, 50);
    CHECK(committed(kSample, 50, 1) == whole);
    CHECK(committed(kSample, 50, 3) == whole);
    std::mt19937 random{20260925};
    for (int trial = 0; trial < 20; ++trial) {
        StreamRenderer renderer;
        std::vector<Row> rows;
        std::size_t at = 0;
        while (at < kSample.size()) {
            const std::size_t length =
                std::min<std::size_t>(1 + random() % 17, kSample.size() - at);
            const RenderOps ops = renderer.feed(kSample.substr(at, length), 50);
            rows.insert(rows.end(), ops.commit.begin(), ops.commit.end());
            at += length;
        }
        const RenderOps last = renderer.finish(50);
        rows.insert(rows.end(), last.commit.begin(), last.commit.end());
        CHECK(rows == whole);
    }
}

TEST_CASE("the sample renders every construct", "[markdown][render]") {
    const std::vector<std::string> out = lines(kSample, 60);
    const std::vector<std::string> expected{
        "Setting up the project",
        "",
        "You'll need three things, and one of them is cmake:",
        "",
        "1. A compiler with C++20 support",
        "2. CMake 3.25 or newer, which you can install with a package",
        "   manager",
        "   such as Homebrew or apt",
        "3. Git",
        "   ◦ with LFS for the models",
        "   ◦ ☐ configured",
        "",
        "│ Note: the first build downloads llama.cpp.",
        "",
        "  bash",
        "  cmake --preset default",
        "      cmake --build build/default -j 16",
        "",
        "Target │ Status │ Time",
        "───────┼────────┼─────",
        "macOS  │ ✅ ok  │  4 m",
        "Linux  │   ok   │ 12 m",
        "",
        "────────────────────────────────────────────────────────────",
        "See the guide (https://x.test/guide) for more everything."};
    CHECK(out == expected);
}
