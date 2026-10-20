#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// MCP presets (47c): the servers this box ships, ready to register by name.
///
/// **Data, compiled in, over the one create path.** A preset is a name, what
/// it is, what it provides and how to try it; its command and arguments are
/// resolved on this machine (an interpreter, a seeded script, this binary's
/// path) and handed to the same create core a typed `--command` reaches, so
/// `mcp create code --from code-exec` writes the config byte for byte as the
/// hand-typed registration would. Never seeded files, never a second writer.
///
/// **Honest about this machine.** A preset whose command cannot run here
/// still lists, saying what is missing and the command that provides it;
/// `--from` refuses it with the same words.
///
/// **`self` is informational.** It is Apogee's own read-only tools served
/// over stdio (`apogee __mcp-tools`), listed with the entry another MCP host
/// takes. Registered into Apogee it would serve Apogee the tools it already
/// has natively, so `--from self` is refused with that reason.
namespace apogee::operations {

/// What `mcp list` and the shell's MCP view say with no server configured:
/// the presets first, then the scaffold.
inline constexpr std::string_view kNoMcpServers =
    "No MCP servers configured. See what Apogee ships ready to register: apogee mcp presets "
    "-- or create one: apogee mcp create <name>";

struct McpPreset {
    std::string_view name;
    /// One line: what it is.
    std::string_view description;
    /// What it gives a model, as the model sees it.
    std::string_view provides;
    /// The name the documented registration uses (`code`).
    std::string_view server_name;
    /// The tool and JSON arguments `mcp test` tries it with.
    std::string_view try_tool;
    std::string_view try_arguments;
    /// Empty when it registers into Apogee; else why it never does.
    std::string_view refusal;
};

/// The presets, in the order `mcp presets` lists them.
[[nodiscard]] std::span<const McpPreset> mcp_presets() noexcept;

/// The preset `name` (exactly), or nullptr.
[[nodiscard]] const McpPreset* find_mcp_preset(std::string_view name) noexcept;

/// Every preset's name, for `--from`'s completion and its refusal.
[[nodiscard]] std::vector<std::string_view> mcp_preset_names();

/// What a preset resolves against: the data directory, this binary, and
/// the PATH lookup (null: the platform's).
struct McpPresetHost {
    std::filesystem::path home;
    std::filesystem::path executable;
    std::function<std::string(std::string_view)> find_on_path;
};

/// The host a preset resolves against for the config at `config_path`: its
/// data directory and this running binary -- what both surfaces pass.
[[nodiscard]] McpPresetHost mcp_preset_host(const std::filesystem::path& config_path);

/// Where a preset stands on this machine.
struct McpPresetResolution {
    /// What it registers: the command and its arguments. Set even when it
    /// cannot run, wherever the parts are known.
    std::string command;
    std::vector<std::string> args;
    /// What this machine lacks to run it; empty when it can.
    std::string missing;
    /// The command that provides what is missing.
    std::string remedy;

    [[nodiscard]] bool runnable() const noexcept {
        return missing.empty();
    }
};

[[nodiscard]] McpPresetResolution resolve_mcp_preset(const McpPreset& preset,
                                                     const McpPresetHost& host);

/// The command and arguments `--from <name>` registers: the resolution,
/// or `std::runtime_error` saying why not -- an unknown name (the presets
/// named), `self`'s circularity, or what this machine lacks and its fix.
[[nodiscard]] McpPresetResolution resolve_preset_for_create(std::string_view name,
                                                            const McpPresetHost& host);

/// The server entry another MCP host takes: `{"command": ..., "args": [...]}`.
[[nodiscard]] nlohmann::json mcp_host_entry(const McpPresetResolution& resolution);

/// `mcp presets --output-format json`: `{"object": "list", "data": [...]}`,
/// one row per preset -- its facts, what it registers here, whether it can
/// run and why not, the `--from` line or the refusal, and another host's
/// entry.
[[nodiscard]] nlohmann::json mcp_presets_document(const McpPresetHost& host);

}  // namespace apogee::operations
