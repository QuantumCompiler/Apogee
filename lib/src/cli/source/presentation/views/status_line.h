#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>

#include "ansi/ansi.h"
#include "views/terminal.h"

/// One self-overwriting line that all transient output goes through.
///
/// **Startup speaks on one line.** No construction-time notice may write raw
/// stderr on an interactive path — a backend loading, a subprocess starting, a
/// config warning. Ommi retrofitted that rule (OMMI-14) after the fact; here it
/// is the reason `set()` exists, and why every surface takes a `StatusLine`
/// rather than reaching for `std::cerr`.
///
/// **And every slow command speaks on it too** (M1). A conversation always had
/// a line; an ordinary command had none, so `apogee models list` read forty
/// headers in silence. `BusyLine` below is the same painter, opened by any
/// command around work that may take a while.
namespace apogee::commands {

/// Renders the spinner's text. Pure, so the frame format is testable without
/// running a thread.
[[nodiscard]] std::string spinner_frame(std::string_view label, std::size_t tick,
                                        std::int64_t elapsed_seconds,
                                        std::int64_t estimated_tokens);

/// How far a sweep is: on item `done` of `total`.
struct BusyCount {
    std::size_t done = 0;
    std::size_t total = 0;
};

/// The busy line's elapsed time shows from this many seconds: under it the
/// count, or the spinner alone, says enough (M1, default taken).
inline constexpr std::int64_t kBusyElapsedFrom = 2;

/// The general frame (M1): spinner · label · `(done/total · elapsed)`, the
/// count only when `count` is given, the time only from `kBusyElapsedFrom`.
///
/// **Never the last column**: the frame takes at most `width - 1` display
/// cells, the label cut to fit -- never the count or the time, which are what
/// tell a stalled sweep from a working one. A width of 0 means unmeasured,
/// and nothing is cut. Pure, so every shape is a golden.
[[nodiscard]] std::string spinner_frame(std::string_view label, std::size_t tick,
                                        std::int64_t elapsed_seconds,
                                        std::optional<BusyCount> count, std::size_t width);

class StatusLine {
public:
    struct Options {
        /// Whether transient rendering happens at all. False on a pipe: a
        /// non-TTY run emits no spinner frames and no escape codes.
        bool active = true;
        ansi::Verbosity verbosity = ansi::Verbosity::Line;
        ansi::Style style;
        /// How often the spinner repaints.
        std::chrono::milliseconds spinner_interval{120};
    };

    StatusLine(TerminalWriter& writer, Options options);
    ~StatusLine();

    StatusLine(const StatusLine&) = delete;
    StatusLine& operator=(const StatusLine&) = delete;
    StatusLine(StatusLine&&) = delete;
    StatusLine& operator=(StatusLine&&) = delete;

    /// Shows `text` as the transient line, replacing whatever was there.
    /// Suppressed entirely under Quiet.
    void set(std::string_view text);

    /// Erases the transient line. Idempotent.
    void clear();

    /// Prints a line that STAYS. Clears the transient line first and bumps the
    /// generation counter so an in-flight spinner repaint cannot paint over it.
    ///
    /// That counter is the whole trick: without it, a spinner tick that was
    /// already scheduled lands after the permanent text and leaves a spinner
    /// frame sitting in the scrollback forever.
    void print_line(std::string_view text);

    /// Starts the animated spinner. `label` is re-read each frame.
    void start_spinner(std::string label);

    /// Starts the busy spinner (M1): the general frame, first painted only
    /// once `delay` has passed -- so work that finishes sooner paints nothing
    /// -- and measured against `width()` at every repaint. Unlike the chat
    /// spinner it outlives what is printed above it (`print_above`); it ends
    /// at `stop_spinner`. Nothing at all on a pipe or under Verbose or Quiet.
    void start_busy(std::string label, std::chrono::milliseconds delay,
                    std::function<std::size_t()> width);

    /// What the running spinner says, and how far it is when `total` is not
    /// zero. Safe from any thread; the next frame shows it.
    void set_spinner_label(std::string label, std::size_t done = 0, std::size_t total = 0);

    /// Updates the live token estimate shown in the spinner.
    void set_token_estimate(std::int64_t tokens);

    /// Runs `write` -- output meant to stay, on any stream -- with the
    /// transient line erased and the terminal held, so no frame lands in the
    /// middle of it. A running busy spinner repaints below it on its next
    /// tick. `write` is given this line's own stream.
    void print_above(const std::function<void(std::ostream&)>& write);

    /// Stops the spinner and erases its line. Idempotent, and prompt: a
    /// spinner waiting out its delay or interval is woken, not waited for.
    void stop_spinner();

    [[nodiscard]] bool spinner_running() const noexcept {
        return spinner_running_.load();
    }

    /// Current generation. Exposed so a test can assert a permanent print
    /// invalidated the spinner rather than merely racing it.
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_.load();
    }

private:
    void paint(std::string_view text);
    /// Waits `duration`, or until the spinner is stopped; false once stopped.
    bool wait_running(std::chrono::milliseconds duration);
    [[nodiscard]] std::string frame(std::size_t tick, std::int64_t elapsed);

    TerminalWriter& writer_;
    Options options_;

    std::atomic<std::uint64_t> generation_{0};
    std::atomic<bool> spinner_running_{false};
    std::atomic<std::int64_t> token_estimate_{0};
    std::atomic<bool> line_shown_{false};

    /// The busy frame's state, read by the spinner thread and set by any.
    std::mutex label_mutex_;
    std::string spinner_label_;
    BusyCount count_;
    bool busy_ = false;
    std::function<std::size_t()> width_;

    /// Wakes a waiting spinner when it is stopped.
    std::mutex wake_mutex_;
    std::condition_variable wake_;
    std::unique_ptr<std::thread> spinner_;
};

/// A sweep's progress as it goes: what it is doing, and which item of how
/// many it is on -- `total` 0 when it does not know, and then no count is
/// shown. The shape every slow command's loop reports through (M1).
using BusyProgress =
    std::function<void(std::string_view label, std::size_t done, std::size_t total)>;

/// The busy line (M1): one transient stderr line a command opens around work
/// that may take a while -- `reading model headers: x.gguf (12/40)` -- and
/// that is gone when it is done.
///
/// Constructing it means the line *may* appear: nothing is painted until the
/// work has lasted `Options::delay`, so a fast command never flickers.
/// Destroying it (or `finish`) clears it without residue, so what the command
/// prints next starts on a clean line. Inactive -- stderr not a terminal,
/// `--quiet`, JSON output -- it is a no-op that writes no byte.
class BusyLine {
public:
    struct Options {
        /// Whether the line may appear at all. Use `busy_options`.
        bool active = false;
        /// How long work lasts before the first frame (150 ms, default taken).
        std::chrono::milliseconds delay{150};
        std::chrono::milliseconds interval{120};
        /// The terminal's width, asked at every repaint; null asks stderr's,
        /// and assumes 80 when it cannot be told.
        std::function<std::size_t()> width;
    };

    BusyLine(std::ostream& out, std::string label, Options options);
    ~BusyLine();

    BusyLine(const BusyLine&) = delete;
    BusyLine& operator=(const BusyLine&) = delete;
    BusyLine(BusyLine&&) = delete;
    BusyLine& operator=(BusyLine&&) = delete;

    /// Whether the line can appear. A caller with output of its own for a
    /// pipe -- a log line per step -- writes it only when this is false.
    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

    /// The phase, with no count.
    void set(std::string label);

    /// The phase and the item it is on, of how many (`total` 0: unknown).
    void report(std::string label, std::size_t done, std::size_t total);

    /// `report`, as the callback a sweep takes. Valid while this line lives.
    [[nodiscard]] BusyProgress sink();

    /// Runs `write` -- a line meant to stay, on stdout or stderr -- with the
    /// busy line out of its way; the line comes back below it. Runs `write`
    /// even when inactive. Flushes stdout, so the line cannot overtake it.
    void above(const std::function<void()>& write);

    /// Clears the line now, for good. Idempotent; the destructor calls it.
    void finish();

private:
    TerminalWriter writer_;
    StatusLine status_;
    bool active_ = false;
};

/// The options a command's busy line takes: active only when stderr is a
/// terminal and the caller is not `quiet` (`--quiet`, or output a program
/// reads, such as JSON). One verbosity model: what silences the status line
/// silences this (M1, default taken).
[[nodiscard]] BusyLine::Options busy_options(bool quiet);

}  // namespace apogee::commands
