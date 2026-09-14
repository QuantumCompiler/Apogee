#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "platform/child_process.h"

/// Running a child to completion -- what the shell and git toolsets share.
///
/// Both spawn a program, feed it nothing, collect everything it says on both
/// pipes, and want its exit status -- bounded by a deadline, after which the
/// child is killed and the fact is reported rather than the call hanging the
/// turn. That loop is written once here.
namespace apogee::tools {

struct ProcessOutcome {
    std::string out;
    std::string err;
    /// The exit status; nullopt when the child was killed or never started.
    std::optional<int> exit_code;
    bool timed_out = false;
    /// Why the child could not be started, or empty.
    std::string start_error;
};

/// Runs `command` with stdin closed and drains it until it exits or `timeout`
/// elapses. Output is capped at `max_output` bytes per stream, keeping the
/// tail -- the end of a build log is the part with the error in it.
[[nodiscard]] ProcessOutcome run_to_completion(const platform::ChildCommand& command,
                                               std::chrono::milliseconds timeout,
                                               std::size_t max_output = 256 * 1024);

}  // namespace apogee::tools
