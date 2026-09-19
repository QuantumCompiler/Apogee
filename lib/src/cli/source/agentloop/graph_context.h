#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/store.h"

/// Retrieval-time expansion: the knowledge graph rendered as the section a
/// turn's transient prefix carries after the chunk list.
///
/// The entities mentioned by the turn's retrieved top-k seed a walk of the
/// collection's graph; on a lexical turn the query's own terms additionally
/// seed through the entity full-text index, so an entity-name hit expands
/// with no embedder at all. What comes back is rendered under a fixed
/// **budget with whole-line truncation** -- graph context augments the
/// chunks and must never crowd them out -- and is **best-effort**: the
/// caller turns a failure into a note, never into a turn losing its chunks.
namespace apogee::agentloop {

/// The section's cap, in codepoints. A constant until the number proves
/// wrong against real corpora.
inline constexpr std::size_t kGraphSectionBudget = 1500;

struct GraphSection {
    /// The rendered section, empty when nothing was found. No trailing
    /// newline.
    std::string text;
    /// How many entity lines it carries -- what every surface reports as
    /// `+N graph entities`.
    int entities = 0;

    [[nodiscard]] bool empty() const noexcept {
        return text.empty();
    }
};

/// One entity line: `Name (type): description`. A knowledge record's
/// decision node folds the record's branch marker into the parenthetical --
/// `kr-… (decision, shipped): <decision — intent>` -- so the model weighs a
/// superseded or rejected decision correctly and a reader can `knowledge
/// info` the id.
[[nodiscard]] std::string entity_line(const embedstore::GraphNode& node);

/// Expands and renders: `[Knowledge graph: <collection>]`, the entity lines,
/// then the relation triples (`A —[relation]→ B: description`), cut whole
/// lines at the budget. `seed_chunks` are the turn's retrieved chunks in
/// retrieval order; `lexical_query` is the question on a lexical or hybrid
/// turn (empty on a vector turn) and seeds through the entity index. Throws
/// what the store throws; the caller decides what a failure costs.
[[nodiscard]] GraphSection build_graph_section(const embedstore::Store& store,
                                               std::string_view collection,
                                               const std::vector<std::int64_t>& seed_chunks,
                                               std::string_view lexical_query, int hops,
                                               int max_entities);

}  // namespace apogee::agentloop
