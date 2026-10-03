#include "commands/status_line.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ansi/ansi.h"
#include "ansi/text_width.h"
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

// ---- the busy line (M1) ------------------------------------------------------

namespace {

using apogee::commands::BusyCount;
using apogee::commands::BusyLine;

/// A buffer the spinner thread writes while the test reads it: every write
/// and every look taken under one lock -- a recursive one, since the
/// stringbuf's own `xsputn` calls back into `overflow`.
class LockedBuffer final : public std::stringbuf {
public:
    [[nodiscard]] std::string snapshot() {
        const std::scoped_lock lock{mutex_};
        return str();
    }

protected:
    std::streamsize xsputn(const char* text, std::streamsize count) override {
        const std::scoped_lock lock{mutex_};
        return std::stringbuf::xsputn(text, count);
    }

    int_type overflow(int_type character) override {
        const std::scoped_lock lock{mutex_};
        return std::stringbuf::overflow(character);
    }

private:
    std::recursive_mutex mutex_;
};

struct BusyHarness {
    LockedBuffer buffer;
    std::ostream out{&buffer};

    /// A busy line that paints at once and often, at a fixed width.
    [[nodiscard]] static BusyLine::Options live(std::size_t width = 80) {
        BusyLine::Options options;
        options.active = true;
        options.delay = std::chrono::milliseconds{0};
        options.interval = std::chrono::milliseconds{5};
        options.width = [width] { return width; };
        return options;
    }

    /// Waits until the bytes so far satisfy `done`; false after ten seconds,
    /// which only a failure ever waits out -- a loaded machine can starve the
    /// spinner thread for longer than a frame.
    template <typename Predicate>
    bool wait_for(Predicate done) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (std::chrono::steady_clock::now() < until) {
            if (done(buffer.snapshot())) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        return false;
    }
};

/// The frames in `bytes`: what follows each erase, up to the next.
[[nodiscard]] std::vector<std::string> frames_of(const std::string& bytes) {
    std::vector<std::string> frames;
    std::size_t at = bytes.find(kEraseLine);
    while (at != std::string::npos) {
        const std::size_t start = at + kEraseLine.size();
        const std::size_t next = bytes.find(kEraseLine, start);
        frames.push_back(
            bytes.substr(start, next == std::string::npos ? std::string::npos : next - start));
        at = next;
    }
    return frames;
}

}  // namespace

TEST_CASE("the busy frame: spinner, label, the count when known, the time from two seconds",
          "[ux][status][busy]") {
    CHECK(spinner_frame("reading", 0, 0, std::nullopt, 0) == "✻ reading");
    CHECK(spinner_frame("reading", 0, 1, std::nullopt, 0) == "✻ reading");
    CHECK(spinner_frame("reading", 0, 2, std::nullopt, 0) == "✻ reading (2s)");
    CHECK(spinner_frame("reading", 1, 0, BusyCount{.done = 12, .total = 40}, 0) ==
          "✽ reading (12/40)");
    CHECK(spinner_frame("reading", 0, 3, BusyCount{.done = 12, .total = 40}, 0) ==
          "✻ reading (12/40 · 3s)");
    // A frame that fits is the same frame measured.
    CHECK(spinner_frame("reading", 0, 3, BusyCount{.done = 12, .total = 40}, 80) ==
          "✻ reading (12/40 · 3s)");
    // It animates, and cycles.
    CHECK(spinner_frame("x", 0, 0, std::nullopt, 0) == spinner_frame("x", 4, 0, std::nullopt, 0));
}

TEST_CASE("the busy frame never reaches the last column, and the label gives way first",
          "[ux][status][busy]") {
    const BusyCount count{.done = 12, .total = 40};
    // The label is cut, never the count or the time.
    CHECK(spinner_frame("reading model headers: gemma.gguf", 0, 3, count, 20) ==
          "✻ rea… (12/40 · 3s)");
    // Wide characters are measured as the cells they take.
    CHECK(spinner_frame("读取模型头", 0, 0, BusyCount{.done = 1, .total = 2}, 12) == "✻ 读… (1/2)");
    for (const std::string& label :
         {std::string{"reading model headers: Qwen3-VL-8B-Instruct-Q4_K_M.gguf"},
          std::string{"读取模型头读取模型头读取模型头读取模型头"}}) {
        for (std::size_t width = 1; width <= 80; ++width) {
            INFO(label << " at width " << width);
            const std::string frame = spinner_frame(label, 0, 3, count, width);
            CHECK(apogee::ansi::display_width(frame) <= width - 1);
            if (width > 17) {
                CHECK(frame.ends_with("(12/40 · 3s)"));
            }
        }
    }
}

TEST_CASE("an inactive busy line writes nothing, and still prints what stays",
          "[ux][status][busy]") {
    BusyHarness h;
    {
        BusyLine busy{h.out, "reading", BusyLine::Options{}};
        CHECK_FALSE(busy.active());
        busy.report("reading a.gguf", 1, 2);
        busy.set("checking");
        busy.above([&] { h.out << "row\n"; });
    }
    CHECK(h.buffer.snapshot() == "row\n");
    // Nothing that silences the status line lets this one through.
    CHECK_FALSE(apogee::commands::busy_options(/*quiet=*/true).active);
}

TEST_CASE("work done before the delay paints nothing, and is not kept waiting for it",
          "[ux][status][busy]") {
    BusyHarness h;
    BusyLine::Options options = BusyHarness::live();
    options.delay = std::chrono::seconds{60};
    const auto started = std::chrono::steady_clock::now();
    {
        BusyLine busy{h.out, "reading", options};
        busy.report("reading a.gguf", 1, 2);
        // Time for the spinner to be waiting out its delay, so the stop
        // below has to wake it.
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    // Far under the delay: the spinner was woken, not waited for.
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{20});
    CHECK(h.buffer.snapshot().empty());
}

TEST_CASE("past its delay the busy line repaints in place and clears without residue",
          "[ux][status][busy]") {
    BusyHarness h;
    BusyLine busy{h.out, "reading", BusyHarness::live()};
    busy.report("reading a.gguf", 1, 2);
    REQUIRE(h.wait_for([](const std::string& bytes) {
        return bytes.find("reading a.gguf (1/2)") != std::string::npos;
    }));
    busy.report("reading b.gguf", 2, 2);
    REQUIRE(h.wait_for([](const std::string& bytes) {
        return bytes.find("reading b.gguf (2/2)") != std::string::npos;
    }));
    // A phase with no total shows no count: never "(0/0)".
    busy.set("finishing");
    REQUIRE(h.wait_for([](const std::string& bytes) {
        const std::vector<std::string> frames = frames_of(bytes);
        return !frames.empty() && frames.back().ends_with(" finishing");
    }));
    busy.finish();
    busy.finish();  // idempotent

    const std::string bytes = h.buffer.snapshot();
    // In place: never a line feed, so nothing scrolls.
    CHECK(bytes.find('\n') == std::string::npos);
    // Cleared: the last thing written is the erase.
    CHECK(bytes.ends_with(kEraseLine));
    for (const std::string& frame : frames_of(bytes)) {
        CHECK(apogee::ansi::display_width(frame) <= 79);
        CHECK(frame.find("(0/0)") == std::string::npos);
    }
}

TEST_CASE("what stays is printed above the busy line, which comes back below it",
          "[ux][status][busy]") {
    BusyHarness h;
    BusyLine busy{h.out, "reading", BusyHarness::live()};
    REQUIRE(h.wait_for(
        [](const std::string& bytes) { return bytes.find("reading") != std::string::npos; }));
    busy.above([&] { h.out << "row\n"; });
    REQUIRE(h.wait_for([](const std::string& bytes) {
        const std::size_t row = bytes.find("row\n");
        return row != std::string::npos && bytes.find("reading", row) != std::string::npos;
    }));
    busy.finish();

    const std::string bytes = h.buffer.snapshot();
    // The line was erased before the row was written, so the row starts clean.
    CHECK(bytes.find(std::string{kEraseLine} + "row\n") != std::string::npos);
    CHECK(bytes.find("row\n") == bytes.rfind("row\n"));
    CHECK(bytes.ends_with(kEraseLine));
}

TEST_CASE("the busy line measures the width at every repaint", "[ux][status][busy]") {
    BusyHarness h;
    std::atomic<std::size_t> width{120};
    BusyLine::Options options = BusyHarness::live();
    options.width = [&width] { return width.load(); };
    BusyLine busy{h.out, "reading", options};
    const std::string label = "reading model headers: Qwen3-VL-8B-Instruct-Q4_K_M.gguf";
    busy.report(label, 3, 9);
    REQUIRE(h.wait_for(
        [&](const std::string& bytes) { return bytes.find(label) != std::string::npos; }));
    // The terminal narrows mid-sweep: the next frames fit the new width.
    width.store(30);
    REQUIRE(h.wait_for([](const std::string& bytes) {
        const std::vector<std::string> frames = frames_of(bytes);
        return !frames.empty() && frames.back().find("…") != std::string::npos &&
               apogee::ansi::display_width(frames.back()) <= 29;
    }));
    busy.finish();
}

TEST_CASE("a stopped spinner is woken, not waited for", "[ux][status][busy]") {
    Harness h;
    StatusLine::Options options;
    options.style = Style{false};
    options.spinner_interval = std::chrono::seconds{60};
    StatusLine status{h.writer, options};
    status.start_spinner("Thinking…");
    // Its first frame painted, the spinner is in its sixty-second wait: a
    // stop that did not wake it would wait that out.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    std::string painted;
    while (painted.empty() && std::chrono::steady_clock::now() < until) {
        h.writer.with_lock([&](std::ostream&) { painted = h.out.str(); });
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    REQUIRE_FALSE(painted.empty());
    const auto started = std::chrono::steady_clock::now();
    status.stop_spinner();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{20});
}

TEST_CASE("the busy spinner does not run on a pipe, a log or a quiet run", "[ux][status][busy]") {
    for (const auto& [verbosity, active] :
         {std::pair{Verbosity::Line, false}, std::pair{Verbosity::Verbose, true},
          std::pair{Verbosity::Quiet, true}}) {
        Harness h;
        StatusLine status = h.make(verbosity, active);
        status.start_busy("reading", std::chrono::milliseconds{0}, [] { return 80; });
        CHECK_FALSE(status.spinner_running());
        status.stop_spinner();
        CHECK(h.bytes().empty());
    }
}
