#include "commands/line_reader.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

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
