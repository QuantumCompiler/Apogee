#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#include "agent/tool.h"

/// The shell toolset: one tool, `run_command`.
///
/// Ported from Ommi's `ommi-mcp-shell`, with one divergence: Ommi shipped it
/// with a docstring warning and no gate; here `run_command` declares `writes`
/// and goes through the permission gate like `write_file`, default `ask`.
/// `permissions.run_command: allow` is one line away for a user who trusts
/// the model.
namespace apogee::tools {

struct ShellResult {
    std::string rendered;
    bool timed_out = false;
    bool failed_to_start = false;
};

/// Runs `command` through the platform shell (`/bin/sh -c` on POSIX,
/// `cmd /C` on Windows) in `cwd`, bounded by `timeout`. Renders stdout, then
/// `[stderr]` when there was any, then `[exit N]`; a timeout renders as
/// `[timed out after Ns]` and is a result, not an error.
[[nodiscard]] ShellResult run_shell(std::string_view command, const std::filesystem::path& cwd,
                                    std::chrono::milliseconds timeout);

void register_shell_tool(agent::ToolRegistry& registry, const std::filesystem::path& default_cwd,
                         std::chrono::milliseconds timeout);

}  // namespace apogee::tools
