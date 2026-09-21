#include "commands/status_line.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include "ansi/ansi.h"
#include "commands/terminal.h"

using apogee::ansi::kEraseLine;
using apogee::ansi::Style;
using apogee::ansi::Verbosity;
using apogee::commands::spinner_frame;
using apogee::commands::StatusLine;
using apogee::commands::TerminalWriter;

namespace {

struct Harness {
    std::ostringstream out;
    TerminalWriter writer{out};

    [[nodiscard]] StatusLine make(Verbosity verbosity = Verbosity::Line, bool active = true) {
        StatusLine::Options options;
        options.active = active;
        options.verbosity = verbosity;
        options.style = Style{false};
        return StatusLine{writer, options};
    }

    [[nodiscard]] std::string bytes() const {
        return out.str();
    }
};

}  // namespace

TEST_CASE("the status line overwrites rather than accumulating", "[ux][status]") {
    Harness h;
    StatusLine status = h.make();

    status.set("first");
    status.set("second");

    // Each set begins by erasing the line, so the previous text is replaced
    // rather than scrolled past.
    const std::string all = h.bytes();
    CHECK(all.find("first") != std::string::npos);
    CHECK(all.find("second") != std::string::npos);
    CHECK(all.rfind(kEraseLine) > all.find("first"));
}

TEST_CASE("clear erases and is idempotent", "[ux][status]") {
    Harness h;
    StatusLine status = h.make();

    status.set("working");
    const std::size_t before = h.bytes().size();
    status.clear();
    const std::size_t after_first = h.bytes().size();
    CHECK(after_first > before);

    status.clear();
    status.clear();
    CHECK(h.bytes().size() == after_first);
}

TEST_CASE("a permanent print bumps the generation counter", "[ux][status]") {
    // The whole trick: an in-flight spinner tick that has already decided to
    // paint must see a stale generation and drop its frame, or it lands after
    // the permanent text and leaves a spinner frame in the scrollback forever.
    Harness h;
    StatusLine status = h.make();

    const std::uint64_t before = status.generation();
    status.print_line("this stays");
    CHECK(status.generation() > before);

    CHECK(h.bytes().find("this stays\n") != std::string::npos);
}

TEST_CASE("a permanent print erases the transient line first", "[ux][status]") {
    Harness h;
    StatusLine status = h.make();

    status.set("transient");
    status.print_line("permanent");

    const std::string all = h.bytes();
    const std::size_t permanent_at = all.find("permanent");
    REQUIRE(permanent_at != std::string::npos);
    // An erase immediately precedes it, so the transient text is gone rather
    // than left half-overwritten.
    CHECK(all.rfind(kEraseLine, permanent_at) != std::string::npos);
}

TEST_CASE("quiet suppresses status but not permanent lines", "[ux][status]") {
    // Warnings and errors still appear -- suppressing those would make a failed
    // run look like a successful silent one.
    Harness h;
    StatusLine status = h.make(Verbosity::Quiet);

    status.set("progress noise");
    CHECK(h.bytes().find("progress noise") == std::string::npos);

    status.print_line("a warning");
    CHECK(h.bytes().find("a warning") != std::string::npos);
}

TEST_CASE("verbose turns every status into a permanent line", "[ux][status]") {
    // What a log wants: no cursor movement, everything retained.
    Harness h;
    StatusLine status = h.make(Verbosity::Verbose);

    status.set("step one");
    status.set("step two");

    const std::string all = h.bytes();
    CHECK(all.find("step one\n") != std::string::npos);
    CHECK(all.find("step two\n") != std::string::npos);
}

TEST_CASE("an inactive status line emits no escape codes", "[ux][status]") {
    // A pipe must receive no spinner frames and no cursor movement.
    Harness h;
    StatusLine status = h.make(Verbosity::Line, /*active=*/false);

    status.set("progress");
    status.clear();
    status.start_spinner("Thinking…");
    status.stop_spinner();

    CHECK(h.bytes().find('\033') == std::string::npos);
    CHECK_FALSE(status.spinner_running());
}

TEST_CASE("the spinner does not run when inactive or verbose", "[ux][status]") {
    Harness inactive;
    StatusLine a = inactive.make(Verbosity::Line, false);
    a.start_spinner("x");
    CHECK_FALSE(a.spinner_running());

    Harness verbose;
    StatusLine b = verbose.make(Verbosity::Verbose);
    b.start_spinner("x");
    CHECK_FALSE(b.spinner_running());
}

TEST_CASE("the spinner starts and stops cleanly", "[ux][status]") {
    Harness h;
    StatusLine status = h.make();

    status.start_spinner("Thinking…");
    CHECK(status.spinner_running());
    status.stop_spinner();
    CHECK_FALSE(status.spinner_running());

    // Idempotent, and safe to stop twice.
    status.stop_spinner();
    CHECK_FALSE(status.spinner_running());
}

TEST_CASE("the spinner frame carries elapsed time and the token estimate", "[ux][status]") {
    // On a redacted-thinking model the counter is the ONLY live signal there
    // is, so it has to be in the frame.
    CHECK(spinner_frame("Thinking…", 0, 0, 0) == "✻ Thinking…");

    const std::string with_time = spinner_frame("Thinking…", 0, 5, 0);
    CHECK(with_time.find("(5s)") != std::string::npos);

    const std::string full = spinner_frame("Thinking…", 0, 5, 87);
    CHECK(full.find("5s") != std::string::npos);
    CHECK(full.find("~87 tokens") != std::string::npos);

    const std::string tokens_only = spinner_frame("Thinking…", 0, 0, 12);
    CHECK(tokens_only.find("~12 tokens") != std::string::npos);
    CHECK(tokens_only.find("0s") == std::string::npos);
}

TEST_CASE("the spinner animates across frames", "[ux][status]") {
    const std::string a = spinner_frame("x", 0, 0, 0);
    const std::string b = spinner_frame("x", 1, 0, 0);
    CHECK(a != b);
    // And it cycles rather than running off the end of the frame table.
    CHECK(spinner_frame("x", 0, 0, 0) == spinner_frame("x", 4, 0, 0));
}
