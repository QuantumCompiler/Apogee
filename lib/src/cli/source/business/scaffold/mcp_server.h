#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config_edit.h"

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

/// The longest new scaffolded server name, in bytes (CLI-M52, CLI-M46's
/// cap). A register-only name writes no file, so it has none; an existing
/// entry with a longer name keeps working, and is replaced under `force`.
inline constexpr std::size_t kNewServerNameMax = 200;

/// Throws std::runtime_error with the reason on any refusal: an empty or
/// unusable name, a directory that exists without `force`, what the config's
/// add refuses, or a write failure. A scaffold (no `command`) first runs
/// that add on the config's text, so a refusal the config is certain to give
/// -- a missing or broken config, the name rule, a case-folded collision, an
/// existing entry without `force` -- or a new name (one the add would not
/// replace) past `kNewServerNameMax` writes nothing (CLI-M52); when the sanitizer mapped the name,
/// the words name both forms. Only trouble after that -- a write failure, a lock held past
/// `lock_wait`, an edit raced in between -- leaves the folder, and the words name it and every file
/// in it. Register-only takes no dry run: it writes no file, and its words are as they were.
[[nodiscard]] McpServerResult create_mcp_server(
    const std::filesystem::path& config_path, const McpServerSpec& spec,
    std::chrono::milliseconds lock_wait = harness::kConfigLockWait);

/// `.` and `:` become `-`; anything outside letters, digits, `_`, `-` is
/// refused. Mirrors what a config entry name may be.
[[nodiscard]] std::string sanitize_server_name(std::string_view name);

/// The scaffold's Python server, test and README, with `__NAME__` replaced.
[[nodiscard]] std::string python_server_template(std::string_view name);
[[nodiscard]] std::string python_test_template(std::string_view name);
[[nodiscard]] std::string readme_template(std::string_view name);

}  // namespace apogee::scaffold
