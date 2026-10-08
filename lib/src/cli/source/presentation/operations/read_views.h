#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string_view>

#include "contracts/assets.h"
#include "contracts/config.h"

/// The read views the command line and the control plane share (28h): how an
/// agent and an MCP server are serialized, and the list documents built from
/// them -- one definition, so `agents list --output-format json` prints the
/// very body `GET /v1/admin/agents` serves, and `mcp list`'s JSON starts
/// from the very entry `GET /v1/admin/mcp-servers` serves. Here rather than
/// in either surface, so neither includes the other.
///
/// Secrets have no slot: an MCP server's `env` is said as `env_set`, never
/// its values; an agent carries none.
namespace apogee::operations {

/// An agent as both surfaces serialize it: every field of the entry, plus
/// whether it is bundled (no entry) or an entry overriding a bundled one.
[[nodiscard]] nlohmann::json agent_view(const harness::NamedAgent& agent);

/// `{"object": "list", "data": [agent_view...]}` -- every agent, bundled and
/// configured, in `all_agents` order.
[[nodiscard]] nlohmann::json agents_document(const harness::Config& config);

/// An MCP server's entry: name, command, args, enabled, and `env_set` --
/// whether it sets any environment, never what.
[[nodiscard]] nlohmann::json mcp_server_view(std::string_view name,
                                             const harness::McpServerConfig& server);

/// `{"object": "list", "data": [mcp_server_view...]}`.
[[nodiscard]] nlohmann::json mcp_servers_document(const harness::Config& config);

}  // namespace apogee::operations
