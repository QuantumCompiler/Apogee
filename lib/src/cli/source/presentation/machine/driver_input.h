#pragma once

#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>

#include "contracts/cancellation.h"

/// A driver's stdin, read while a turn runs (28f).
///
/// Machine mode used to read its driver one line at a time on the thread
/// that ran the turn, so nothing a driver sent mid-turn was seen until the
/// turn ended -- which left a host no way out of a turn short of killing the
/// child. This reads the lines on a thread of their own, as they arrive:
///
///   - each `user` line is a turn, given its own cancellation token as it is
///     read, so a `cancel` sent right after it reaches it even before the
///     session has started it;
///   - a `{"type":"cancel"}` line cancels the turn of the last `user` line
///     that has not ended -- through the token the loop already honours for
///     Ctrl-C -- and is consumed; with no turn open it is ignored, so a
///     cancel racing a turn that just finished is harmless;
///   - every other line is queued, in order, for the session loop and for a
///     pending question or permission prompt, which take them as they took
///     them from the stream before.
///
/// One reader for the whole session: the loop, `ask_user` and the permission
/// prompt all take their lines from here, so no line is read twice or lost
/// between them.
namespace apogee::commands {

/// One line a driver sent. For a `user` line, `turn` is that turn's token
/// and `sequence` the turn it opened; otherwise `sequence` is 0.
struct DriverLine {
    std::string text;
    harness::CancellationToken turn;
    std::uint64_t sequence = 0;
};

class DriverInput {
public:
    /// Starts reading `in` -- stdin in practice -- at once. `in` must outlive
    /// the reader, which for `std::cin` it does.
    explicit DriverInput(std::istream& in);
    /// A reader still blocked on a driver that has not closed its end is left
    /// to finish on its own: it holds nothing this object frees.
    ~DriverInput();

    DriverInput(const DriverInput&) = delete;
    DriverInput& operator=(const DriverInput&) = delete;
    DriverInput(DriverInput&&) = delete;
    DriverInput& operator=(DriverInput&&) = delete;

    /// The next line that is not a `cancel`, waiting for one; nullopt once
    /// the driver closed its end and every line was taken.
    [[nodiscard]] std::optional<DriverLine> next_line();

    /// The next line, or nullopt as soon as the open turn is cancelled or the
    /// input ends -- what a pending question waits on, so a `cancel` fails
    /// the question as a closed stdin does, without ending the session.
    /// `cancelled` says which.
    [[nodiscard]] std::optional<DriverLine> next_line_in_turn(bool& cancelled);

    /// The turn `sequence` opened has ended -- its `result` or `error` is
    /// out: a `cancel` now no longer reaches it.
    void end_turn(std::uint64_t sequence);

private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace apogee::commands
