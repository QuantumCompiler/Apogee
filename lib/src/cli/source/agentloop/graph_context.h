#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "harness/config.h"

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
/// what the store throws; the caller decides what a failure costs. The
/// collection's own graph: every seed is labelled `''`.
[[nodiscard]] GraphSection build_graph_section(const embedstore::Store& store,
                                               std::string_view collection,
                                               const std::vector<std::int64_t>& seed_chunks,
                                               std::string_view lexical_query, int hops,
                                               int max_entities);

/// The same over any graph store -- a named graph's own database included
/// -- with the seeds named by (collection, chunk) and `header` the name the
/// section carries.
[[nodiscard]] GraphSection build_graph_section_labelled(
    const embedstore::Store& graph_store, std::string_view header,
    const std::vector<embedstore::ChunkRef>& seed_chunks, std::string_view lexical_query, int hops,
    int max_entities);

// --- Which graph covers a collection -----------------------------------------
//
// A per-collection graph cannot see across collection boundaries; a **named
// graph** (`graphs:` in the config) spans several. The rule, decided once
// here for every surface: a BUILT named graph takes retrieval precedence for
// its members -- the member's own `graph:` block is untouched and simply not
// consulted while the named graph covers it, and takes over again the moment
// the collection leaves. Named graphs are read in config order, so the first
// entry wins a double-listing; an unbuilt entry covers nothing.

/// The database file for a named graph: `<embeddings_dir>/graphs/<name>.db`.
/// The `graphs/` directory is derived data, created lazily by the first
/// build -- never by an installer, and not a layout row.
[[nodiscard]] std::filesystem::path graph_db_path(std::string_view name);

/// A `graphs:` entry that lists `collection`, and its name.
struct CoveringGraph {
    std::string name;
    const harness::NamedGraphConfig* config = nullptr;
};

/// The first `graphs:` entry listing `collection` as a member. With
/// `built_only`, only an entry whose database exists counts -- the retrieval
/// precedence rule; without it, config membership alone -- the `embed ingest
/// --graph` chaining target, since the first chained build is what creates
/// the database.
[[nodiscard]] std::optional<CoveringGraph> named_graph_covering(const harness::Config& config,
                                                                std::string_view collection,
                                                                bool built_only);

/// What one turn's expansion walks for `collection`: the covering named
/// graph when one is built, else the collection's own graph when its block
/// says `enabled`, else nothing.
struct TurnGraph {
    bool enabled = false;
    /// The store to walk; empty means the collection's own store.
    std::filesystem::path store_path;
    /// The name the section's header carries.
    std::string name;
    /// The label the seed chunks carry: the collection's name under a named
    /// graph, `''` under its own.
    std::string seed_collection;
    int hops = 1;
    int max_entities = 8;
};

/// ONE decision for every surface -- the conversational turns, `knowledge
/// query --graph`, the HTTP twins -- so the precedence rule cannot drift.
[[nodiscard]] TurnGraph resolve_turn_graph(const harness::Config& config,
                                           std::string_view collection);

}  // namespace apogee::agentloop
