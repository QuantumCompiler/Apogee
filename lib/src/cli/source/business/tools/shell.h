#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "agent/tool.h"
#include "tools/process.h"

/// The shell toolset: one tool, `run_command`.
///
/// `run_command` declares `writes` and goes through the permission gate like
/// `write_file`, default `ask`: a shell command can change anything a file
/// write can, and more. `permissions.run_command: allow` is one line away for
/// a user who trusts the model.
namespace apogee::tools {

/// How much of a command's output reaches the model: its first and its last
/// this many bytes (25d). Enough for a compiler's first error and a test
/// runner's summary; a model reading more is read at ~100 tokens/s locally.
inline constexpr std::size_t kCommandOutputKeep = 8 * 1024;

struct ShellResult {
    std::string rendered;
    bool timed_out = false;
    bool failed_to_start = false;
};

/// The shell `run_command` runs through: `/bin/sh` on POSIX, `cmd` on
/// Windows.
[[nodiscard]] std::string_view shell_program() noexcept;

/// Renders a finished command: stdout, then `[stderr]` when there was any,
/// then `[exit N]`. Longer than twice `keep`, it is cut to its first and
/// last `keep` bytes -- at line boundaries where one is near -- joined by a
/// line naming how many bytes were left out and how to read them.
[[nodiscard]] std::string render_command_output(const ProcessOutcome& outcome,
                                                std::size_t keep = kCommandOutputKeep);

/// Runs `command` through the platform shell (`/bin/sh -c` on POSIX,
/// `cmd /C` on Windows) in `cwd`, bounded by `timeout`, and renders it with
/// `render_command_output`; a timeout renders as `[timed out after Ns]` and
/// is a result, not an error.
[[nodiscard]] ShellResult run_shell(std::string_view command, const std::filesystem::path& cwd,
                                    std::chrono::milliseconds timeout,
                                    std::size_t keep = kCommandOutputKeep);

void register_shell_tool(agent::ToolRegistry& registry, const std::filesystem::path& default_cwd,
                         std::chrono::milliseconds timeout);

}  // namespace apogee::tools
