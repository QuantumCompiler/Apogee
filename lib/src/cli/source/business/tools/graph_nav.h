#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "agent/tool.h"
#include "contracts/config.h"
#include "graph/navigate.h"

/// The `graph` toolset (27l): a model walking a built knowledge graph
/// itself -- `graph_query`, `graph_path`, `graph_explain`, `graph_neighbors`
/// -- each a thin wrapper over `graph/navigate`, returning the JSON document
/// `apogee graph <verb> --output-format json` prints for the same question,
/// byte for byte.
///
/// **Read-only, so ungated.** None of them `writes` or is `outbound`: the
/// permission gate never asks about one, on any surface, and `apogee
/// __mcp-tools` -- which serves exactly the tools that do not write --
/// serves all four to any MCP client with no code of its own.
///
/// **Bounded.** Every cap is an argument with the core's default and its
/// ceiling; a tool can never return an unbounded subgraph.
namespace apogee::tools {

inline constexpr std::string_view kGraphQueryToolName = "graph_query";
inline constexpr std::string_view kGraphPathToolName = "graph_path";
inline constexpr std::string_view kGraphExplainToolName = "graph_explain";
inline constexpr std::string_view kGraphNeighborsToolName = "graph_neighbors";

/// The four names, in registry order.
[[nodiscard]] std::span<const std::string_view> graph_tool_names() noexcept;

struct GraphToolsOptions {
    /// Resolves each call's `graph`/`collection` (graphs-first, the
    /// retrieval precedence rule for a collection). Null reads as an empty
    /// config: collections on disk only.
    const harness::Config* config = nullptr;
    /// A scoped toolset: every call reads this one graph, and the tools take
    /// no `graph`/`collection` -- a chat's attachment graph (27o) handed in
    /// by path.
    std::optional<graph::GraphTarget> scope;
    /// What a scoped set's descriptions say it reads, in place of "Reads the
    /// graph '<name>'." -- a chat's attached folders by their roots (27o),
    /// so the tools rank against a question about that code (26g).
    std::string scope_note;
};

void register_graph_tools(agent::ToolRegistry& registry, const GraphToolsOptions& options);

/// `registry` with its `graph` toolset read through `options.scope` (27o):
/// the four tools replaced by the scoped set of the same names, every other
/// tool as it was, and the environment note told when to reach for them
/// (`scoped_graph_policy`, 26p's mechanism) whenever the registry asking holds
/// them. The scope is the same toolset under the same switch, so a registry
/// without it -- `tools.disabled: [graph]` -- comes back as it was. One
/// traversal core: the scoped tools are `register_graph_tools`' own, handed
/// the store.
[[nodiscard]] agent::ToolRegistry with_graph_scope(const agent::ToolRegistry& registry,
                                                   const GraphToolsOptions& options);

/// The paragraph a scoped set adds to the environment note: for a question
/// about the attached code's structure, the graph before an answer, and no
/// path neither it nor the excerpts show.
[[nodiscard]] std::string_view scoped_graph_policy() noexcept;

}  // namespace apogee::tools
