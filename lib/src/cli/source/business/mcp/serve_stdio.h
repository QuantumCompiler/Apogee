#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "mcp/types.h"

/// The server side of the same four methods, over a process's own stdin and
/// stdout -- so Apogee can host an MCP server as a hidden subcommand with
/// nothing to install: `command: apogee, args: ["__mcp-tools"]`.
///
/// **stdout carries JSON-RPC and nothing else.** Anything a served tool
/// wants to say goes to stderr. Malformed frames are skipped, a notification
/// gets no reply, and an unknown method with an id gets `-32601`.
namespace apogee::mcp {

struct ServedTool {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
    bool read_only = true;
};

using Dispatch = std::function<ToolCallResult(std::string_view name, const nlohmann::json& args)>;

/// Serves until `in` hits EOF. Returns 0 on a clean EOF.
int serve_stdio(std::istream& in, std::ostream& out, std::string_view server_name,
                std::string_view version, const std::vector<ServedTool>& tools,
                const Dispatch& dispatch);

/// The read-only tools of a registry, as served tools; the dispatch that
/// runs them. What `apogee __mcp-tools` serves: a writing tool is withheld
/// because the permission gate lives in the loop, not in a server.
[[nodiscard]] std::vector<ServedTool> read_only_tools(const agent::ToolRegistry& registry);
[[nodiscard]] Dispatch dispatch_through(const agent::ToolRegistry& registry);

}  // namespace apogee::mcp
