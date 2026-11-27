#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// The MCP servers an ACP host hands a session (56b): `session/new`'s
/// `mcpServers`, read here, and their handover to the session's child.
///
/// **Stdio only.** ACP lets a host name stdio, http and sse servers; Apogee's
/// MCP client speaks stdio alone, and `initialize` advertises neither remote
/// transport, so an http or sse entry is set aside and named in the session's
/// one MCP notice -- never dialled.
///
/// **The values are the host's, and they are credentials.** ACP calls an
/// entry's `env` values credentials. They reach the server's child and
/// nothing else: never argv (which `ps` shows), never the session child's
/// environment (which every server and vendor CLI it starts would inherit),
/// never a notice, a log line or stderr -- which name a server and an env
/// variable's name, never its value. So the session's child is handed them
/// in a file only its owner can read (0600), in a fresh folder only its owner
/// can enter (0700), named by the hidden `execute` option
/// `--acp-host-servers` and removed once read.
namespace apogee::acp {

/// The hidden `execute` option naming a session's handover: plumbing, never
/// completed, never in help.
inline constexpr std::string_view kHandoverOption = "--acp-host-servers";

/// One stdio server the host named, as it named it: the name is made a
/// server name by the session (`mcp create`'s rule), not here.
struct HostServer {
    std::string name{};
    std::string command{};
    std::vector<std::string> args{};
    /// `name=value`, each name checked here: never empty, never holding `=`.
    std::vector<std::string> env{};
};

/// What a session takes of the host's `mcpServers`.
struct HostServers {
    std::vector<HostServer> servers{};
    /// Why entries were set aside, each said in the session's one MCP notice
    /// -- naming servers, never a value of `env` or `headers`.
    std::vector<std::string> notes{};

    [[nodiscard]] bool empty() const noexcept {
        return servers.empty() && notes.empty();
    }
};

/// `session/new`'s `mcpServers`, read: each stdio entry (no `type`, or
/// `stdio`) with a command, its arguments strings and its `env` a list of
/// `{name, value}` taken; one whose `env` has a name that is empty or holds
/// `=`, or that is not that shape, skipped and said; every http, sse or
/// other entry set aside, named once -- "Apogee connects stdio MCP servers
/// only; skipped: <names>". Values pass as given: a host's `${VAR}` is the
/// host's, never expanded.
[[nodiscard]] HostServers host_servers(const nlohmann::json& mcp_servers);

/// The names and env variable names of `servers`, for `acp`'s stderr --
/// `xcode-tools (/usr/bin/xcrun; env MCP_XCODE_SESSION_ID)` -- never a
/// value, nor an argument.
[[nodiscard]] std::string describe(const HostServers& servers);

/// Hands `servers` to a session's child: a fresh folder `apogee-acp-<hex>`
/// under `parent` (the system's temporary folder when empty), owner-only,
/// holding `servers.json`, owner-only before its first byte. Returns the
/// file, or empty with `error` filled and nothing left behind.
[[nodiscard]] std::filesystem::path write_handover(const HostServers& servers,
                                                   const std::filesystem::path& parent,
                                                   std::string& error);

/// The child's half: the handover at `file`, read -- and, when it is one
/// `write_handover` made (its folder named `apogee-acp-`), removed with its
/// folder, so the values are on disk no longer than it takes to read them.
/// Empty with `error` filled for a file that cannot be read or is not one.
[[nodiscard]] HostServers read_handover(const std::filesystem::path& file, std::string& error);

/// Removes a handover `write_handover` made, if it is still there -- a child
/// that never read it. Quiet.
void discard_handover(const std::filesystem::path& file) noexcept;

}  // namespace apogee::acp
