#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "tui/shell.h"

/// Where the shell's loop runs, behind one seam (32b): the terminal's -- FTXUI
/// full screen -- or a test's, which runs what it is handed when told to, so
/// every view is testable with no terminal at all.
namespace apogee::tui {

class Pump {
public:
    Pump() = default;
    Pump(const Pump&) = delete;
    Pump& operator=(const Pump&) = delete;
    Pump(Pump&&) = delete;
    Pump& operator=(Pump&&) = delete;
    virtual ~Pump() = default;

    /// Runs `task` on the shell's thread between frames, then redraws. Safe
    /// from any thread: how a turn running elsewhere reaches the screen.
    virtual void post(std::function<void()> task) = 0;

    /// Ends the loop. Safe from any thread.
    virtual void exit() = 0;

    /// Runs `task` on the shell's thread every `period` while the loop runs
    /// (32e's tick). A tick that comes due again before it ran -- the loop
    /// busy, the machine asleep -- fires once, never in a burst to catch up,
    /// and none outlives the loop.
    virtual void every(std::chrono::milliseconds period, std::function<void()> task) = 0;

    /// The loop's clock: the steady clock in the terminal, a test's script in
    /// a test's.
    [[nodiscard]] virtual std::chrono::steady_clock::time_point now() const = 0;
};

/// The terminal's loop: the shell full screen on the alternate screen until
/// it quits. On the way out -- a quit key, Ctrl-C with nothing to stop,
/// SIGINT, SIGTERM, SIGHUP, and a crash's SIGABRT or SIGSEGV alike -- the
/// terminal is put back as it was: modes, cursor, the primary screen
/// (FTXUI's handlers, the crash path's async-signal-safe). What the process
/// writes to stderr meanwhile is said on the shell's notice row, never drawn
/// over the screen. The mouse is left to the terminal: selecting text works
/// as it does anywhere else.
class TerminalPump final : public Pump {
public:
    TerminalPump();
    ~TerminalPump() override;

    TerminalPump(const TerminalPump&) = delete;
    TerminalPump& operator=(const TerminalPump&) = delete;
    TerminalPump(TerminalPump&&) = delete;
    TerminalPump& operator=(TerminalPump&&) = delete;

    void post(std::function<void()> task) override;
    void exit() override;
    void every(std::chrono::milliseconds period, std::function<void()> task) override;
    [[nodiscard]] std::chrono::steady_clock::time_point now() const override;

    /// Runs `shell` until it quits. Returns the exit code: 0. The ticks run
    /// while it does, and stop -- their thread joined -- before it returns.
    int run(Shell& shell);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// A test's loop: posted work waits until `drain` runs it, on the caller's
/// thread, in order.
class ManualPump final : public Pump {
public:
    ManualPump() = default;
    ~ManualPump() override = default;

    ManualPump(const ManualPump&) = delete;
    ManualPump& operator=(const ManualPump&) = delete;
    ManualPump(ManualPump&&) = delete;
    ManualPump& operator=(ManualPump&&) = delete;

    void post(std::function<void()> task) override;
    void exit() override;
    void every(std::chrono::milliseconds period, std::function<void()> task) override;
    [[nodiscard]] std::chrono::steady_clock::time_point now() const override;

    /// Runs everything posted so far, and anything that posts; returns how
    /// many tasks ran.
    std::size_t drain();
    [[nodiscard]] bool exited() const;

    /// Moves the clock on by `by`, posting each tick that came due -- once,
    /// however many periods passed.
    void advance(std::chrono::milliseconds by);

private:
    struct Tick {
        std::chrono::milliseconds period{0};
        std::function<void()> task;
        std::chrono::steady_clock::time_point next{};
    };

    mutable std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
    std::vector<Tick> ticks_;
    std::chrono::steady_clock::time_point clock_{std::chrono::hours{1}};
    bool exited_ = false;
};

}  // namespace apogee::tui
