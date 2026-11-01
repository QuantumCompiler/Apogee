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
/// stdout -- so Apogee hosts an MCP server with nothing to install: `command:
/// apogee, args: ["mcp", "serve"]` (54a; the hidden `__mcp-tools` its alias).
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
/// runs them. What `apogee mcp serve` serves first: a writing tool is
/// withheld because the permission gate lives in the loop, not in a server.
[[nodiscard]] std::vector<ServedTool> read_only_tools(const agent::ToolRegistry& registry);
[[nodiscard]] Dispatch dispatch_through(const agent::ToolRegistry& registry);

/// A read served beside a registry's tools (54a): listed as a tool marked
/// read-only, and answered by its own function -- the document the CLI
/// prints for the same question, or an error result saying why not. How
/// `apogee mcp serve` serves Apogee's status reads with no rendering of its
/// own: each `answer` calls the function its command prints from.
struct ServedRead {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
    std::function<ToolCallResult(const nlohmann::json& args)> answer;
};

/// What a server lists and how it answers.
struct ServedSet {
    std::vector<ServedTool> tools;
    Dispatch dispatch;
};

/// `tools` and `dispatch`, grown by `reads` (54a): each read listed after the
/// tools, in order, marked read-only, and called by its own name -- its text
/// mended to UTF-8 where it enters, and an exception it throws said as an
/// error result, never the server's end. Every other name goes to `dispatch`.
/// Tools are only ever added, never shadowed: a read named like a served
/// tool, or like another read, throws `std::invalid_argument`.
[[nodiscard]] ServedSet with_reads(std::vector<ServedTool> tools, Dispatch dispatch,
                                   std::vector<ServedRead> reads);

}  // namespace apogee::mcp
