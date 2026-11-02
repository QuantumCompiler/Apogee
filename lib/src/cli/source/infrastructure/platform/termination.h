#pragma once

#include <chrono>
#include <functional>
#include <memory>

/// A session asked to end by a signal (57a): SIGTERM, and SIGHUP -- the
/// terminal window closed -- end it the way a closed input does, so what its
/// scope holds is released before the process goes: a private chat's
/// temporary folder above all, whose index holds its attachments' text.
///
/// The handler does one async-signal-safe thing: it writes a byte to a pipe.
/// A thread of the watch's own reads it, and calls `end` there -- where
/// anything may be done: a turn cancelled, a reader woken. The process then
/// ends as the signal ends it, once the watch does: the signal is raised
/// again on the action that was there before, so a supervisor still sees a
/// process ended by SIGTERM. A session that cannot be reached -- a read no
/// one can wake, a close that runs on -- is not waited for: a second signal,
/// or the grace running out, calls `force` (what must go before the process
/// does) and raises the signal at once.
///
/// POSIX only. Windows has no SIGHUP, and nothing sends a console program
/// SIGTERM; there the watch is inert, and a private chat's folder left by a
/// run that did not end cleanly is swept by the next private chat.
namespace apogee::platform {

class TerminationWatch {
public:
    struct Hooks {
        /// The first signal: end the session as a closed input ends it.
        std::function<void()> end;
        /// A second signal, or the session still running once the grace
        /// is out: what must go before the process does.
        std::function<void()> force;
    };

    /// Catches SIGTERM and SIGHUP while it lives -- each unless it is
    /// ignored (`nohup`), which stays so. One watch at a time: a second, made
    /// while one is live, watches nothing.
    TerminationWatch(Hooks hooks, std::chrono::milliseconds grace);
    /// The handlers put back as they were. A signal caught is raised again
    /// on them, after everything the caller's scope declared after the watch
    /// has been released.
    ~TerminationWatch();

    TerminationWatch(const TerminationWatch&) = delete;
    TerminationWatch& operator=(const TerminationWatch&) = delete;
    TerminationWatch(TerminationWatch&&) = delete;
    TerminationWatch& operator=(TerminationWatch&&) = delete;

    /// SIGINT too, while `on`: a session idle at a terminal's prompt ends
    /// on it the same way. Off again, SIGINT does what it did before.
    void catch_interrupt(bool on);

    /// The signal caught, or 0.
    [[nodiscard]] int caught() const noexcept;

    /// Whether this platform's watch catches anything.
    [[nodiscard]] static bool supported() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace apogee::platform
