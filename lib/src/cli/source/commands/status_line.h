#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "ansi/ansi.h"
#include "commands/terminal.h"

/// One self-overwriting line that all transient output goes through.
///
/// **Startup speaks on one line.** No construction-time notice may write raw
/// stderr on an interactive path — a backend loading, a subprocess starting, a
/// config warning. Ommi retrofitted that rule (OMMI-14) after the fact; here it
/// is the reason `set()` exists, and why every surface takes a `StatusLine`
/// rather than reaching for `std::cerr`.
namespace apogee::commands {

/// Renders the spinner's text. Pure, so the frame format is testable without
/// running a thread.
[[nodiscard]] std::string spinner_frame(std::string_view label, std::size_t tick,
                                        std::int64_t elapsed_seconds,
                                        std::int64_t estimated_tokens);

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

    /// Updates the live token estimate shown in the spinner.
    void set_token_estimate(std::int64_t tokens);

    /// Stops the spinner and erases its line. Idempotent.
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

    TerminalWriter& writer_;
    Options options_;

    std::atomic<std::uint64_t> generation_{0};
    std::atomic<bool> spinner_running_{false};
    std::atomic<std::int64_t> token_estimate_{0};
    std::atomic<bool> line_shown_{false};
    std::string spinner_label_;
    std::unique_ptr<std::thread> spinner_;
};

}  // namespace apogee::commands
