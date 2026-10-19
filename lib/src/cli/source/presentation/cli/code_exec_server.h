#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"

/// The code-execution server's registration (47b): the seeded script, the
/// interpreter it runs under, and the one `mcp create` line that registers
/// it. `apogee check` says it with the paths filled in, never guessing an
/// interpreter that is not there. The server itself is generic -- it
/// registers through `mcp create --command` like any other, and nothing here
/// writes the config.
namespace apogee::commands {

/// The name the documented registration uses.
inline constexpr std::string_view kCodeExecServerName = "code";

struct CodeExecRegistration {
    /// `<home>/training/scripts/code_exec_server.py`, the seeded copy.
    std::filesystem::path script;
    /// The training environment's interpreter when it exists, else `python3`
    /// on PATH; empty when there is neither.
    std::filesystem::path interpreter;
    /// Where `interpreter` came from: "the training environment" or
    /// "python3 on PATH".
    std::string interpreter_source;
    /// Why there is no interpreter, when `interpreter` is empty.
    std::string missing;

    /// `mcp create <name> --command <interpreter> --args <script>`, as
    /// arguments after the program name. Empty without an interpreter.
    [[nodiscard]] std::vector<std::string> mcp_create_arguments(std::string_view name) const;

    /// The same, as one line a shell takes: `apogee mcp create code
    /// --command ... --args ...`, a path quoted where it needs it.
    [[nodiscard]] std::string mcp_create_line(std::string_view name) const;
};

/// `find_on_path` is `platform::find_on_path` but for tests.
[[nodiscard]] CodeExecRegistration code_exec_registration(
    const std::filesystem::path& home,
    const std::function<std::string(std::string_view)>& find_on_path = nullptr);

/// The configured MCP server that runs `script` -- as its command or among
/// its arguments -- or empty when none does.
[[nodiscard]] std::string registered_code_exec_server(const harness::Config& config,
                                                      const std::filesystem::path& script);

}  // namespace apogee::commands
