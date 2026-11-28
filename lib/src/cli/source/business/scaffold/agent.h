#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"

/// The TTY-free core that creates an agent: the same function `apogee agents
/// create` and `POST /v1/admin/agents` call, so an agent made over HTTP is
/// byte-identical to one made on the command line -- the prompt file, the
/// schema file, and the config entry.
///
/// Paths derive from the config path (`harness::home_for_config`), never
/// `$HOME`, so a `--config` temp tree stays hermetic; the entry names its
/// files by RELATIVE path (`prompts/<name>.txt`), which is what keeps it
/// portable and what the bundled agents do too.
namespace apogee::scaffold {

struct AgentSpec {
    std::string name;
    std::string description;
    /// Backend override; empty means the chat role.
    std::string model;
    /// `read-only` (the default), `all`, or `none`.
    std::string tools;
    /// A prose-only agent: no schema file, no `schemas:` key.
    bool no_schema = false;
    /// Custom bodies; empty means the starter text.
    std::string prompt_body;
    std::string schema_body;
    /// Replace an existing agent and its files.
    bool force = false;
    /// `auto`, `json`, or `markdown`; empty means auto.
    std::string output_format;
    std::string collection;
    bool questions = false;
    std::string save_dir;
    std::string save_filename;
    /// Empty means the agent's own name, so reports never glob together.
    std::string save_subdir;
    std::vector<std::string> mcp;
};

struct AgentResult {
    std::string name;
    std::filesystem::path prompt_path;
    /// Empty for a prose-only agent.
    std::filesystem::path schema_path;
    std::filesystem::path config_path;
};

/// The longest new agent name, in bytes (CLI-M52, CLI-M46's cap): the
/// schema's temporary file, `<name>-output.json.tmp.<10 digits>`, then fits
/// NAME_MAX's 255. An existing entry with a longer name keeps working, and is
/// replaced under `force`.
inline constexpr std::size_t kNewAgentNameMax = 200;

/// Throws std::runtime_error with the reason on any refusal: an empty or
/// unusable name, an invalid policy or format, a new name (one the add
/// would not replace) past `kNewAgentNameMax`, a file that exists without
/// `force`, or what the
/// config's add refuses. A refusal the config is certain to give -- a
/// missing or broken config, the name rule, a case-folded collision, an
/// existing entry without `force` -- is found by running that add on the
/// config's text first, so it writes nothing (CLI-M52); when the sanitizer
/// mapped the name, the words name both forms. Only trouble after that --
/// a write failure, a lock held past `lock_wait`, an edit raced in between
/// -- leaves files, and the words name every one written.
[[nodiscard]] AgentResult create_agent(
    const std::filesystem::path& config_path, const AgentSpec& spec,
    std::chrono::milliseconds lock_wait = harness::kConfigLockWait);

/// `.` and `:` become `-`; anything outside letters, digits, `_`, `-` is
/// refused -- the same rule as an MCP server name, so a name that is one
/// is the other.
[[nodiscard]] std::string sanitize_agent_name(std::string_view name);

/// The starter system prompt and the permissive starter schema.
[[nodiscard]] std::string starter_prompt(std::string_view name, std::string_view description);
[[nodiscard]] std::string starter_schema(std::string_view name);

/// An agent's prompt and schema files, resolved against the config's data
/// directory -- what `agents edit` opens and `agents delete --purge` removes.
[[nodiscard]] std::vector<std::filesystem::path> agent_files(
    const std::filesystem::path& config_path, const harness::AgentConfig& agent);

}  // namespace apogee::scaffold
