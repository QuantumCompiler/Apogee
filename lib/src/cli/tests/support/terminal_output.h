#pragma once

#include <functional>
#include <string>

namespace apogee::testing {

/// Everything written to file descriptors 1 and 2 while `action` runs (56d):
/// below `std::cout` and `std::cerr`, whose buffers a test's capture swaps
/// (`CliHome::run`), so what a child that inherited the process's stdout or
/// stderr wrote is here and nowhere else. A test holds a child's stderr off
/// the terminal by asserting its lines are absent from this.
///
/// Both descriptors point at a file of the call's own for the action's
/// length, and are put back however it ends. POSIX only: on Windows it throws
/// rather than answer an empty capture that would pass anything -- its
/// callers skip there before it, having no child processes to run.
[[nodiscard]] std::string terminal_output(const std::function<void()>& action);

}  // namespace apogee::testing
