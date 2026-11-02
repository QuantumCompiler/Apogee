#pragma once

#include "contracts/cancellation.h"

/// Ctrl-C -- and SIGTERM -- for a command that runs a long child process.
///
/// A training run can hold a GPU for an hour and a model conversion can write
/// fifty gigabytes; Ctrl-C must terminate the child cleanly and let the command
/// clean up after it -- record the run as cancelled, remove a half-written
/// file, let go of its locks -- never leave a driver running behind a dead
/// parent. The handler only flips the token; the read loop notices between
/// chunks and terminates the child. A third signal while the child is still
/// winding down exits at once: the user means it.
///
/// **SIGTERM is the same cancel** (57g): launchd sends it when a job is
/// unloaded or the user logs out, and a pass stopped that way must unwind as
/// a Ctrl-C does -- its pipeline recorded `aborted`, `cycle.lock` removed --
/// rather than leave the crashed-cycle case behind. A command cancelled by it
/// exits 143 (`exit_code`), SIGINT keeping 130. The scope catches SIGTERM only
/// while it is at its default action (`platform::termination_signal_free`):
/// a session's termination watch, `serve`, or an inherited ignore keeps it.
/// SIGKILL stays the crashed case.
///
/// **Scopes nest, and the outermost one owns the token** (57i): a scope made
/// while another lives on the same process folds into it -- no fresh token,
/// no handler of its own, nothing restored when it goes. A register chain
/// holds one scope across all its stages while each stage's verb keeps its
/// own, so a Ctrl-C in the gap between two stages is the next stage's
/// cancel, never lost to a re-armed token; a command's copy of the token sees
/// every signal its run receives.
namespace apogee::commands {

class InterruptScope {
public:
    /// Arms a fresh token and installs the handler for the scope's lifetime
    /// -- or, inside a live scope, joins it, the token and handler its own.
    InterruptScope();
    /// Restores whatever handlers were there before; a nested scope leaves
    /// them to the one it joined.
    ~InterruptScope();

    InterruptScope(const InterruptScope&) = delete;
    InterruptScope& operator=(const InterruptScope&) = delete;
    InterruptScope(InterruptScope&&) = delete;
    InterruptScope& operator=(InterruptScope&&) = delete;

    /// The token the innermost live scope cancels on Ctrl-C or SIGTERM.
    [[nodiscard]] static const harness::CancellationToken& token() noexcept;

    /// The exit code a command that ended with `code` reports: a cancel
    /// (`kCancelled`) that SIGTERM caused is `kTerminated`; anything else is
    /// `code`. Read once, as the command ends -- it forgets the signal.
    [[nodiscard]] static int exit_code(int code) noexcept;

private:
    void (*previous_)(int) = nullptr;
    void (*previous_term_)(int) = nullptr;
    bool term_armed_ = false;
    /// Made inside a live scope: it armed nothing, and restores nothing.
    bool nested_ = false;
};

}  // namespace apogee::commands
