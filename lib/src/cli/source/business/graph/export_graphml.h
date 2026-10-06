#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "embedstore/store.h"
#include "graph/navigate.h"

/// `apogee graph export graphml` (27m): the whole graph as GraphML, for real
/// graph tooling -- yEd, Gephi, Cytoscape, networkx -- and the **uncapped
/// escape hatch** the report and the HTML point to: every node, the
/// unresolved names included (marked), and every edge.
///
/// Each node carries its name, kind, description, mention count, 27l's
/// degree, its largest community, the members it is stated in and, for
/// code, the `file:line` of its definition; each edge its relation, origin
/// (`extracted`/`inferred`), weight, confidence when recorded, and
/// description. Ids are the store's (`n<id>`, `e<id>`), so two exports of
/// one graph are the same bytes. Nothing from a chunk's text, nothing from a
/// private layout row.
///
/// Well-formed by construction: every text escaped, characters XML 1.0
/// cannot carry (and invalid UTF-8) written as U+FFFD -- held by a
/// round trip through an independent parser.
namespace apogee::graph {

struct GraphmlExport {
    std::string xml;
    std::size_t nodes = 0;
    std::size_t edges = 0;
};

/// The document over `store`, called `graph`. Reads only; never a model.
[[nodiscard]] GraphmlExport export_graphml(const embedstore::Store& store, std::string_view graph);

[[nodiscard]] GraphmlExport export_graphml(const OpenGraph& open);

/// `text` as XML 1.0 character data or an attribute value: `&`, `<`, `>`,
/// `"` and `'` as entities, a character XML cannot carry -- a control
/// character but tab, newline and return, U+FFFE, U+FFFF -- and any byte
/// that is not valid UTF-8 as U+FFFD.
[[nodiscard]] std::string xml_escape(std::string_view text);

}  // namespace apogee::graph
