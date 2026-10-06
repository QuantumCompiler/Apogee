#pragma once

#include <string>
#include <string_view>

#include "cli/command.h"

namespace CLI {
class App;
}

/// `apogee graph report` and `apogee graph export html|graphml|mermaid`
/// (27m): a built graph's artifacts -- the Markdown architecture report, one
/// self-contained interactive HTML file, and the interop pair.
///
/// Thin over `graph/report` and `graph/export_*`: each resolves its graph
/// from `--graph`/`--collection` exactly as the navigation verbs do (27l),
/// assembles the artifact from the store with **no model** -- every one
/// succeeds with zero backends -- and writes it. `report` prints its
/// Markdown (or, with `--output-format json`, its one document) and `--out`
/// writes the same bytes to a file; an export lands in the working directory
/// under its default name, or at `--out` (`-` for stdout). Nothing is ever
/// written into the data directory, and nothing an artifact holds comes from
/// a chunk's text or a private layout row.
namespace apogee::commands {

/// Adds `report` and `export` under `graph` (the `GraphCommand` binds them).
void bind_graph_artifacts(CLI::App& graph, const RootContext& context);

/// Where an export lands without `--out`: `<graph>.html`, `<graph>.graphml`
/// or `<graph>.mmd` in the working directory, for `kind` `html`, `graphml`
/// or `mermaid`.
[[nodiscard]] std::string default_artifact_name(std::string_view graph, std::string_view kind);

}  // namespace apogee::commands
