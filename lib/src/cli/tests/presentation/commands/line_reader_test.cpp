#include "commands/line_reader.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <string_view>

#include "ansi/text_width.h"
#include "support/env_guard.h"

using apogee::commands::PlainLineReader;
using apogee::testing::EnvGuard;
using apogee::testing::TempDir;

TEST_CASE("the plain reader behaves exactly as getline did", "[chat][reader]") {
    // A piped conversation is a first-class way to use `apogee chat` -- it is
    // how the crash-safety suite drives it -- and adding a line editor must not
    // change it at all.
    std::istringstream in{"first\nsecond\nthird\n"};
    PlainLineReader reader{in};

    CHECK(reader.read({}) == "first");
    CHECK(reader.read({}) == "second");
    CHECK(reader.read({}) == "third");
    // End of input.
    CHECK_FALSE(reader.read({}).has_value());
    CHECK_FALSE(reader.interactive());
}

TEST_CASE("the plain reader writes no prompt", "[chat][reader]") {
    // On a pipe there is nobody to read a prompt, and whatever IS reading the
    // output would receive "You: " interleaved with the answers.
    std::istringstream in{"line\n"};
    PlainLineReader reader{in};

    CHECK(reader.read("You: ") == "line");
    // Nothing to assert on stdout beyond this: the reader is given no output
    // stream at all, which is the structural guarantee.
}

TEST_CASE("a CRLF source does not leave a carriage return", "[chat][reader]") {
    // A script file written on Windows would otherwise put '\r' at the end of
    // every prompt the model receives.
    std::istringstream in{"windows line\r\nunix line\n"};
    PlainLineReader reader{in};

    CHECK(reader.read({}) == "windows line");
    CHECK(reader.read({}) == "unix line");
}

TEST_CASE("a final line without a newline is still read", "[chat][reader]") {
    // `printf 'hello' | apogee chat` -- no trailing newline.
    std::istringstream in{"no trailing newline"};
    PlainLineReader reader{in};

    CHECK(reader.read({}) == "no trailing newline");
    CHECK_FALSE(reader.read({}).has_value());
}

TEST_CASE("an empty line reads as empty, not as end of input", "[chat][reader]") {
    // The REPL skips blanks; conflating them with EOF would end the session on
    // a stray Enter.
    std::istringstream in{"\n\nafter blanks\n"};
    PlainLineReader reader{in};

    CHECK(reader.read({}) == "");
    CHECK(reader.read({}) == "");
    CHECK(reader.read({}) == "after blanks");
}

TEST_CASE("remember is a no-op without an editor", "[chat][reader]") {
    std::istringstream in{"x\n"};
    PlainLineReader reader{in};
    CHECK_NOTHROW(reader.remember("something"));
}

TEST_CASE("make_line_reader falls back to plain without a terminal", "[chat][reader]") {
    // The test process has no terminal, so this exercises the selection that
    // matters: a piped run must never construct the editor.
    std::istringstream in{"piped\n"};
    const auto reader = apogee::commands::make_line_reader({}, in);

    REQUIRE(reader != nullptr);
    CHECK_FALSE(reader->interactive());
    CHECK(reader->read({}) == "piped");
}

TEST_CASE("the history path sits under APOGEE_HOME", "[chat][reader]") {
    // Per-user, not per-session: a session file records the conversation, not
    // the keystrokes that produced it.
    const TempDir dir{"history"};
    const EnvGuard home{"APOGEE_HOME", dir.path().string()};

    const auto path = apogee::commands::default_history_path();
    CHECK(path.parent_path() == dir.path());
    CHECK(path.filename() == "chat_history");
}

// --- suggestions ---------------------------------------------------------------

using apogee::commands::apply_suggestion;
using apogee::commands::layout_hints;
using apogee::commands::Suggestion;
using apogee::commands::Suggestions;

namespace {

std::size_t codepoints(std::string_view text) {
    std::size_t count = 0;
    for (const char c : text) {
        count += (static_cast<unsigned char>(c) & 0xC0U) != 0x80U ? 1 : 0;
    }
    return count;
}

/// A row as replxx draws it: one space per prompt cell and per codepoint
/// before the span, the span as typed, then the hint past its first
/// `context` codepoints.
std::string drawn_row(std::string_view before, std::size_t from, std::size_t prompt,
                      const std::string& hint, int context) {
    std::string row(prompt + codepoints(before.substr(0, from)), ' ');
    row += before.substr(from);
    std::size_t at = 0;
    for (int seen = 0; at < hint.size() && seen < context; ++seen) {
        do {
            ++at;
        } while (at < hint.size() && (static_cast<unsigned char>(hint[at]) & 0xC0U) == 0x80U);
    }
    row += hint.substr(at);
    return row;
}

Suggestions commands() {
    Suggestions s;
    s.candidates = {{"/model ", "/model", "Show the backend answering, or switch to another"},
                    {"/models", {}, "List the configured backends"}};
    return s;
}

}  // namespace

TEST_CASE("the word suggester completes the last word from its list", "[chat][reader]") {
    const auto suggest = apogee::commands::word_suggester({"/help", "/rag", "/rags"});
    const Suggestions last = suggest("/rag no");
    CHECK(last.from == 5);
    CHECK(last.candidates.empty());

    const Suggestions rag = suggest("x /ra");
    CHECK(rag.from == 2);
    REQUIRE(rag.candidates.size() == 2);
    CHECK(rag.candidates[0].text == "/rag");
    CHECK(rag.candidates[1].text == "/rags");
}

TEST_CASE("rows put the label, then the description in a shared column", "[chat][reader]") {
    const auto layout = layout_hints(commands(), "/mo", 5, 80, 5);
    CHECK(layout.context == 3);
    REQUIRE(layout.hints.size() == 2);
    CHECK(layout.hints[0] == "/model   Show the backend answering, or switch to another");
    CHECK(layout.hints[1] == "/models  List the configured backends");
}

TEST_CASE("the context is counted in codepoints, as replxx counts it", "[chat][reader]") {
    Suggestions s;
    s.from = 0;
    s.candidates = {{"@café/", {}, {}}, {"@cafés/", {}, {}}};
    CHECK(layout_hints(s, "@caf\xC3\xA9", 5, 80, 5).context == 5);
}

TEST_CASE("no row ever reaches the last column", "[chat][reader]") {
    // A row that does leaves the cursor in the terminal's deferred-wrap state
    // and replxx's row count one short, which is a row the next repaint
    // fails to erase.
    Suggestions s = commands();
    s.candidates.push_back({"/max-tokens ", "/max-tokens",
                            "A description long enough to need cutting at any width here"});
    for (const std::string_view before : {"/m", "/mo", "please read @do"}) {
        Suggestions here = s;
        here.from = before.rfind(before.front() == '/' ? '/' : '@');
        for (std::size_t width = 4; width <= 90; ++width) {
            const auto layout = layout_hints(here, before, 5, width, 5);
            for (const std::string& hint : layout.hints) {
                const std::string row = drawn_row(before, here.from, 5, hint, layout.context);
                INFO("width " << width << ": '" << row << "'");
                CHECK(apogee::ansi::display_width(row) <= width - 1);
                CHECK(codepoints(row) <= width - 1);
            }
        }
    }
}

TEST_CASE("a cut description or label says so", "[chat][reader]") {
    const auto layout = layout_hints(commands(), "/mo", 5, 30, 5);
    REQUIRE(layout.hints.size() == 2);
    CHECK(layout.hints[0] == "/model   Show the backe…");
    CHECK(layout.hints[1] == "/models  List the confi…");

    Suggestions path;
    path.candidates = {{"@a-very-long-folder-name/with/a/deep/path.txt", {}, {}},
                       {"@a-very-long-other.txt", {}, {}}};
    const auto cut = layout_hints(path, "@a", 5, 24, 5);
    REQUIRE(cut.hints.size() == 2);
    CHECK(cut.hints[0] == "@a-very-long-fold…");
}

TEST_CASE("candidates past the row limit are counted, not dropped silently", "[chat][reader]") {
    Suggestions s;
    for (const char* name : {"/a", "/b", "/c", "/d", "/e", "/f", "/g"}) {
        s.candidates.push_back({name, {}, "x"});
    }
    s.from = 0;
    const auto layout = layout_hints(s, "/", 5, 80, 5);
    REQUIRE(layout.hints.size() == 6);
    CHECK(layout.hints[4] == "/e  x");
    CHECK(layout.hints[5] == "/…  2 more — type to narrow");
}

TEST_CASE("no room means no rows", "[chat][reader]") {
    // A line already at the edge, or one with a newline in it, whose rows
    // replxx would place by a column it no longer knows.
    CHECK(layout_hints(commands(), "/mo", 76, 80, 5).hints.empty());
    // Room for the span and an ellipsis only: rows reading "/mo…" say nothing.
    CHECK(layout_hints(commands(), "/mo", 73, 80, 5).hints.empty());
    CHECK_FALSE(layout_hints(commands(), "/mo", 72, 80, 5).hints.empty());
    CHECK(layout_hints(commands(), "/mo", 5, 80, 0).hints.empty());
    Suggestions s = commands();
    s.from = 6;
    CHECK(layout_hints(s, "line\nx /mo", 5, 80, 5).hints.empty());
    CHECK(layout_hints(Suggestions{}, "/mo", 5, 80, 5).hints.empty());
}

TEST_CASE("Tab replaces the span before the cursor and keeps the rest", "[chat][reader]") {
    Suggestions s;
    s.from = 10;
    const Suggestion chosen{"@report.pdf", {}, {}};
    const auto applied = apply_suggestion("summarize @rep and more", 14, s, chosen);
    CHECK(applied.line == "summarize @report.pdf and more");
    CHECK(applied.cursor == 21);

    const auto at_end = apply_suggestion("/mo", 3, Suggestions{}, Suggestion{"/model ", {}, {}});
    CHECK(at_end.line == "/model ");
    CHECK(at_end.cursor == 7);
}
