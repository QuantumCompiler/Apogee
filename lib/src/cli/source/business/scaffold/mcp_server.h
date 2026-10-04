#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// The TTY-free core that creates a local MCP server: the same function
/// `apogee mcp create` and `POST /v1/admin/mcp-servers` call, so a server
/// made over HTTP is byte-identical to one made on the command line -- the
/// files and the config entry both.
///
/// Templates are compiled in, so nothing lands under the data directory at
/// install time; only what the user creates. Paths derive from the config
/// path, so a `--config` temp tree stays hermetic.
namespace apogee::scaffold {

struct McpServerSpec {
    std::string name;
    /// Register-only: an existing executable, no files written.
    std::string command;
    std::vector<std::string> args;
    bool force = false;
};

struct McpServerResult {
    std::string name;
    /// Empty for a register-only entry.
    std::filesystem::path directory;
    std::filesystem::path config_path;
    std::string command;
};

/// Throws std::runtime_error with the reason on any refusal: an empty or
/// unusable name, a directory that exists without `force`, a config
/// collision, or a write failure. On a config failure the scaffold files are
/// left on disk and the error says so.
[[nodiscard]] McpServerResult create_mcp_server(const std::filesystem::path& config_path,
                                                const McpServerSpec& spec);

/// `.` and `:` become `-`; anything outside letters, digits, `_`, `-` is
/// refused. Mirrors what a config entry name may be.
[[nodiscard]] std::string sanitize_server_name(std::string_view name);

/// The scaffold's Python server, test and README, with `__NAME__` replaced.
[[nodiscard]] std::string python_server_template(std::string_view name);
[[nodiscard]] std::string python_test_template(std::string_view name);
[[nodiscard]] std::string readme_template(std::string_view name);

}  // namespace apogee::scaffold
