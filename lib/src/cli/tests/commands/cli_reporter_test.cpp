#include "commands/cli_reporter.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

using apogee::ansi::Style;
using apogee::ansi::Verbosity;
using apogee::commands::CliReporter;
using apogee::commands::TerminalWriter;

namespace {

struct Harness {
    std::ostringstream answer;    // stdout
    std::ostringstream progress;  // stderr
    TerminalWriter writer{progress};

    [[nodiscard]] CliReporter make(bool decorate = true, Verbosity verbosity = Verbosity::Line) {
        CliReporter::Options options;
        options.answer_stream = &answer;
        options.decorate = decorate;
        options.verbosity = verbosity;
        options.style = Style{false};
        options.width = 40;
        return CliReporter{writer, options};
    }
};

}  // namespace

TEST_CASE("the answer goes to stdout and progress to stderr", "[ux][reporter]") {
    // The split that keeps `apogee complete "..." | jq` working. Conflating
    // the two streams is how spinner frames end up in piped output.
    Harness h;
    CliReporter reporter = h.make();

    reporter.on_thinking();
    reporter.on_tool_status("[tool] search");
    reporter.on_clear_status();
    reporter.on_answer_start();
    reporter.on_answer_token("the answer");
    reporter.on_answer_end();

    CHECK(h.answer.str() == "the answer\n");
    CHECK(h.answer.str().find("search") == std::string::npos);
    CHECK(h.progress.str().find("search") != std::string::npos);
    CHECK(reporter.emitted_answer());
}

TEST_CASE("an undecorated reporter writes no escape codes anywhere", "[ux][reporter]") {
    // The whole non-TTY contract in one assertion.
    Harness h;
    CliReporter reporter = h.make(/*decorate=*/false);

    reporter.on_thinking();
    reporter.on_thinking_token("private reasoning");
    reporter.on_tool_status("[tool] x");
    reporter.on_clear_status();
    reporter.on_answer_start();
    reporter.on_answer_token("answer");
    reporter.on_answer_end();

    CHECK(h.answer.str() == "answer\n");
    CHECK(h.answer.str().find('\033') == std::string::npos);
    CHECK(h.progress.str().find('\033') == std::string::npos);
    // Reasoning never reaches either stream on a pipe.
    CHECK(h.answer.str().find("private reasoning") == std::string::npos);
    CHECK(h.progress.str().find("private reasoning") == std::string::npos);
}

TEST_CASE("thinking never reaches the answer stream", "[ux][reporter]") {
    // Display-only, always. If it reached the answer it would land in persisted
    // history and be re-sent on every later turn.
    Harness h;
    CliReporter reporter = h.make();

    reporter.on_thinking();
    reporter.on_thinking_token("secret deliberation");
    reporter.on_clear_status();
    reporter.on_answer_start();
    reporter.on_answer_token("public answer");
    reporter.on_answer_end();

    CHECK(h.answer.str() == "public answer\n");
    CHECK(h.answer.str().find("secret deliberation") == std::string::npos);
}

TEST_CASE("an empty thinking token opens nothing", "[ux][reporter]") {
    // Redacted-thinking models send empty payloads on every turn.
    Harness h;
    CliReporter reporter = h.make();

    reporter.on_thinking_token("");
    reporter.on_thinking_token("");

    CHECK(h.progress.str().find("✻ Thinking…") == std::string::npos);
}

TEST_CASE("no answer tokens means no trailing newline", "[ux][reporter]") {
    // A turn that produced nothing must not emit a stray blank line into a
    // pipe.
    Harness h;
    CliReporter reporter = h.make();

    reporter.on_thinking();
    reporter.on_clear_status();
    reporter.on_answer_end();

    CHECK(h.answer.str().empty());
    CHECK_FALSE(reporter.emitted_answer());
}

TEST_CASE("the status line is reachable so surfaces need no raw stderr", "[ux][reporter]") {
    // "Startup speaks on one line": a surface routes its own notices through
    // here rather than writing stderr directly.
    Harness h;
    CliReporter reporter = h.make();

    reporter.status().print_line("a startup notice");
    CHECK(h.progress.str().find("a startup notice") != std::string::npos);
    CHECK(h.answer.str().empty());
}

TEST_CASE("a reporter with no answer stream drops answer tokens safely", "[ux][reporter]") {
    std::ostringstream progress;
    TerminalWriter writer{progress};
    CliReporter::Options options;
    options.answer_stream = nullptr;
    options.style = Style{false};
    CliReporter reporter{writer, options};

    CHECK_NOTHROW(reporter.on_answer_token("text"));
    CHECK_NOTHROW(reporter.on_answer_end());
    CHECK_FALSE(reporter.emitted_answer());
}
