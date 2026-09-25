#pragma once

#include "harness/cancellation.h"

/// Ctrl-C for a command that runs a long child process.
///
/// A training run can hold a GPU for an hour and a model conversion can write
/// fifty gigabytes; Ctrl-C must terminate the child cleanly and let the command
/// clean up after it -- record the run as cancelled, remove a half-written
/// file -- never leave a driver running behind a dead parent. The handler only
/// flips the token; the read loop notices between chunks and terminates the
/// child. A third Ctrl-C while the child is still winding down exits at once:
/// the user means it.
namespace apogee::commands {

class InterruptScope {
public:
    /// Arms a fresh token and installs the handler for the scope's lifetime.
    InterruptScope();
    /// Restores whatever SIGINT handler was there before.
    ~InterruptScope();

    InterruptScope(const InterruptScope&) = delete;
    InterruptScope& operator=(const InterruptScope&) = delete;
    InterruptScope(InterruptScope&&) = delete;
    InterruptScope& operator=(InterruptScope&&) = delete;

    /// The token the innermost live scope cancels on Ctrl-C.
    [[nodiscard]] static const harness::CancellationToken& token() noexcept;

private:
    void (*previous_)(int) = nullptr;
};

}  // namespace apogee::commands
