#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "embedstore/graph.h"

/// Read-side types over the knowledge-graph tables: entity lookup, per-node
/// neighbourhoods and supporting chunks, the aggregate `graph stats`, and
/// **expansion** -- the retrieval-time walk that turns a turn's retrieved
/// chunks into the connected context top-k search cannot surface.
///
/// Expansion is pure SQL over `kg_mentions` and `kg_edges`: exact, and
/// independent of vectors, which is why its quality does not degrade with
/// the embedder's.
namespace apogee::embedstore {

/// Aggregate information about a collection's graph. Chunk totals and
/// coverage count the collection's real chunks -- community pseudo-chunks
/// (`graph://` sources) are graph output, not corpus input.
struct GraphStats {
    std::int64_t nodes = 0;
    std::int64_t edges = 0;
    std::int64_t mentions = 0;
    std::map<std::string, std::int64_t> nodes_by_type;
    /// Nodes with an entity vector (`dim > 0`).
    std::int64_t nodes_with_vectors = 0;
    std::int64_t total_chunks = 0;
    /// Chunks with at least one extracted entity.
    std::int64_t chunks_with_mentions = 0;
    /// Sources the next build would (re)extract -- no state row, a moved
    /// fingerprint, or a different model than the last build's.
    std::int64_t stale_files = 0;
    /// Chunks the last build failed to extract.
    std::int64_t failed_chunks = 0;
    std::string extract_model;
    /// Stored label-propagation communities with summaries.
    std::int64_t communities = 0;

    [[nodiscard]] bool built() const noexcept {
        return nodes > 0;
    }
};

/// One member collection's slice of a named graph's stats.
struct MemberStats {
    std::string collection;
    std::int64_t mentions = 0;
    /// Live chunks in the member store.
    std::int64_t total_chunks = 0;
    std::int64_t chunks_with_mentions = 0;
    std::int64_t stale_files = 0;
    /// The member's database is absent.
    bool missing = false;
};

/// A named graph's stats: the totals over every member, and each member's
/// share.
struct GraphStatsMulti {
    GraphStats totals;
    std::vector<MemberStats> members;
};

/// A node with its BM25 relevance, normalised to (0, 1) exactly as a lexical
/// chunk hit is, so the two read the same.
struct NodeResult {
    GraphNode node;
    double score = 0.0;
};

/// One edge incident to a node, joined with the peer it connects to.
/// Direction is from the queried node's perspective.
struct Neighbor {
    std::string relation;
    std::string description;
    std::int64_t weight = 1;
    /// True: queried node -> peer; false: peer -> queried node.
    bool outgoing = true;
    std::int64_t peer_id = 0;
    std::string peer_name;
    std::string peer_type;
};

/// The expansion defaults a collection's `graph:` block falls back to.
inline constexpr int kDefaultGraphHops = 1;
inline constexpr int kMaxGraphHops = 2;
inline constexpr int kDefaultGraphMaxEntities = 8;

/// One neighbour entity an expansion surfaced.
struct ExpandEntity {
    GraphNode node;
    /// Graph distance from the seeds: 0 for a query-term entity hit passed in
    /// as a seed node, 1 or 2 for one reached by that many edges.
    int hop = 0;
    /// The ranking signal: the sum of connecting edge weights times the
    /// node's mention count. Corroborated, well-evidenced neighbours first.
    std::int64_t score = 0;
    /// One chunk evidencing the entity, 0 when none -- the pointer back to
    /// source text -- and the member collection it lives in (`''` for the
    /// collection's own graph).
    std::int64_t support_chunk = 0;
    std::string support_collection;
};

/// One relation on the traversed neighbourhood, carrying display names so a
/// caller renders triples without re-querying.
struct ExpandEdge {
    std::int64_t source_id = 0;
    std::int64_t target_id = 0;
    std::string source_name;
    std::string target_name;
    std::string relation;
    std::string description;
    std::int64_t weight = 1;
};

/// An expansion: neighbour entities (ranked, capped) and every relation among
/// the traversed neighbourhood, seeds included, so triples can name the
/// entities the retrieved chunks already cover.
struct Expansion {
    std::vector<ExpandEntity> entities;
    std::vector<ExpandEdge> edges;

    [[nodiscard]] bool empty() const noexcept {
        return entities.empty();
    }
};

}  // namespace apogee::embedstore
