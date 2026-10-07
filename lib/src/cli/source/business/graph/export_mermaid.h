#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "embedstore/store.h"
#include "graph/navigate.h"

/// `apogee graph export mermaid` (27m): a diagram for documentation -- a
/// Mermaid `flowchart` a README or a wiki renders as it stands.
///
/// **A code graph's call flow.** When the graph holds parsed `calls`, the
/// diagram is the highest-degree functions that call or are called (27l's
/// ranking, the one every artifact reads), grouped by the file that defines
/// them, and the calls among them. Otherwise it is the highest-degree
/// entities and every relation among them, labelled, a model's dotted.
///
/// **Sized to render.** At most `kMermaidMaxNodes` entities (40 by default)
/// and `kMermaidMaxEdges` edges -- Mermaid's own default ceiling, past which
/// a diagram does not draw at all -- both said in the diagram's opening
/// comment. Unresolved names are never drawn. Every label is escaped with
/// Mermaid's entity codes, so the output parses whatever an entity is called.
namespace apogee::graph {

inline constexpr std::size_t kMermaidDefaultNodes = 40;
inline constexpr std::size_t kMermaidMaxNodes = 150;
/// Mermaid's default `maxEdges`.
inline constexpr std::size_t kMermaidMaxEdges = 500;

struct MermaidOptions {
    /// 1..kMermaidMaxNodes.
    std::size_t max_nodes = kMermaidDefaultNodes;
};

struct MermaidExport {
    std::string text;
    /// A code graph's call flow; false for the entities-and-relations view.
    bool call_flow = false;
    /// The entities eligible, and how many were drawn.
    std::size_t candidates = 0;
    std::size_t shown = 0;
    /// The edges drawn, and how many there were among the entities drawn.
    std::size_t edges = 0;
    std::size_t edges_total = 0;
};

/// The diagram over `store`, called `graph`. Throws `NavigationError` for a
/// cap outside 1..kMermaidMaxNodes. Reads only; never a model.
[[nodiscard]] MermaidExport export_mermaid(const embedstore::Store& store, std::string_view graph,
                                           const MermaidOptions& options);

[[nodiscard]] MermaidExport export_mermaid(const OpenGraph& open, const MermaidOptions& options);

/// `text` as a quoted Mermaid label's content: whitespace runs one space,
/// and `"`, `#`, `&`, `<`, `>`, `|` and the backtick as numeric entity
/// codes (`#34;`) -- never a character the grammar reads.
[[nodiscard]] std::string mermaid_label(std::string_view text);

}  // namespace apogee::graph
