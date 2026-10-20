#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platform/child_process.h"

/// One forge CLI call, run to completion (48a): the child spawned with its
/// three pipes, the request body written to its stdin, stdout kept whole up
/// to a bound and stderr's tail kept for the refusal -- captured, never
/// inherited, so nothing a CLI says reaches the terminal as itself.
namespace apogee::forge {

/// How a transport starts a child. Null means `platform::start_child`; a
/// test hands in scripted children.
using Spawn = std::function<std::unique_ptr<platform::ChildProcess>(
    const platform::ChildCommand& command, std::string& error)>;

/// How a transport finds its CLI on PATH. Null means `platform::find_on_path`.
using FindOnPath = std::function<std::string(std::string_view program)>;

/// The largest response kept: a diff past it is refused, never cut short.
inline constexpr std::size_t kMaxResponseBytes = 64 * 1024 * 1024;

/// How much of a CLI's stderr a refusal quotes: its last bytes.
inline constexpr std::size_t kStderrTailBytes = 8 * 1024;

/// How long one call may take.
inline constexpr std::chrono::milliseconds kCallTimeout{120000};

struct CliCall {
    /// The resolved executable.
    std::string program;
    std::vector<std::string> arguments;
    /// Written to the child's stdin, then closed; empty writes nothing.
    std::string input;
};

struct CliResult {
    /// The exit status; nullopt when the child never started, timed out, or
    /// its output passed the bound.
    std::optional<int> exit_code;
    std::string out;
    /// stderr's last `kStderrTailBytes`, trailing whitespace trimmed.
    std::string err;
    /// Why the child could not start, when it could not.
    std::string start_error;
    bool timed_out = false;
    bool too_large = false;
};

/// Runs `call` through `spawn` (null: the platform's) until the child exits,
/// `timeout` passes, or stdout passes `max_out` bytes -- the child stopped in
/// the last two cases.
[[nodiscard]] CliResult run_cli(const CliCall& call, const Spawn& spawn,
                                std::chrono::milliseconds timeout = kCallTimeout,
                                std::size_t max_out = kMaxResponseBytes);

}  // namespace apogee::forge
