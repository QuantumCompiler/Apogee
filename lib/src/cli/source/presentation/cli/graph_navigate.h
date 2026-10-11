#pragma once

#include <string>

#include "cli/command.h"

namespace CLI {
class App;
}

/// `apogee graph path|explain|neighbors|query` (27l): walking a built graph
/// from the command line -- how two entities connect, everything around one,
/// its neighbours by relation, and the bounded neighbourhood a question
/// names.
///
/// Thin over `graph/navigate`, the one traversal core the `graph` toolset and
/// the admin plane's read twins call too: each verb resolves its graph from
/// `--graph`/`--collection` (graphs-first; a collection through the
/// retrieval precedence rule; neither, the one graph built), runs the core,
/// and renders its payload for a person -- or, with `--output-format json`,
/// prints the payload as the one document a tool returns and the admin
/// plane serves, byte for byte. Nothing here writes; a node that cannot be
/// resolved, or several that could, is a user error naming the candidates.
namespace apogee::commands {

/// `graph explain NODE --graph GRAPH`'s card as the command prints it (37c):
/// the Graph view's card, the command's own lines. A config, resolution or
/// node failure throws its std::runtime_error with the command's message.
[[nodiscard]] std::string graph_explain_text(const RootContext& context, const std::string& graph,
                                             const std::string& node);

/// Adds the four verbs under `graph` (the `GraphCommand` binds them).
void bind_graph_navigation(CLI::App& graph, const RootContext& context);

}  // namespace apogee::commands
