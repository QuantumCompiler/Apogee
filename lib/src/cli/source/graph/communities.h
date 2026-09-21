#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "graph/extract.h"
#include "harness/cancellation.h"

namespace apogee::harness {
class Harness;
}

/// The global layer over a knowledge graph: **communities** -- thematic
/// clusters found by deterministic weighted label propagation over the
/// extracted relations (no model involved in detection), each summarised by
/// ONE generation call under a compiled-in prompt and stored as an ordinary
/// retrievable pseudo-chunk. Top-k retrieval plus expansion answers *local*
/// questions; "what are the main themes in this corpus?" needs this.
///
/// Like the build loop, orchestration here is model-free: the summariser
/// and the embedder arrive as injected closures, so every test runs on
/// fakes and the CLI and the admin plane summarise identically.
namespace apogee::graph {

/// The summariser's system prompt, byte-matched to
/// `assets/clerks/community_prompt.txt` and compiled in like the
/// extractor's: a fixed system concern with no install-parity surface.
[[nodiscard]] std::string_view community_prompt() noexcept;

/// The prompt as sent: trimmed. Output is plain prose, so unlike extraction
/// there is no schema appendix.
[[nodiscard]] std::string community_system_prompt();

/// Runs the summariser over one cluster's rendered entity-and-relation text
/// and returns the summary prose. Throws on failure; the build treats a
/// failure as soft.
using SummarizeFn =
    std::function<std::string(std::string_view community_text, const harness::CancellationToken&)>;

/// The production summariser: one plain generation call (no schema, no
/// tools, a side request) at the extraction temperature and token cap.
[[nodiscard]] SummarizeFn make_summarizer(const harness::Harness& harness, std::string model);

/// The smallest cluster worth summarising -- singletons and pairs are
/// noise, and a generation call each.
inline constexpr int kDefaultMinCommunitySize = 3;

/// Bounds the detector; label propagation on real graphs converges in a
/// handful of rounds.
inline constexpr int kMaxLabelPropagationRounds = 20;

/// Caps the relation lines handed to the summariser so a dense cluster
/// cannot blow the prompt up.
inline constexpr std::size_t kMaxCommunityRelationLines = 20;

/// Partitions the graph's nodes into communities by weighted label
/// propagation over the edge set, returning each community of at least
/// `min_size` members as a sorted member-id list, largest first. Deterministic:
/// labels initialise to the node's own id, nodes update in ascending-id
/// order (asynchronously, so labels flow within a round), each adopts the
/// label with the greatest edge-weight support among its neighbours
/// (smallest label winning ties), and propagation stops at convergence or
/// after `kMaxLabelPropagationRounds`. Nodes with no edges never join a
/// community. `min_size` 0 or less means the default.
[[nodiscard]] std::vector<std::vector<std::int64_t>> detect_communities(
    const std::vector<embedstore::GraphEdge>& edges, int min_size);

/// A community's identity: its sorted member ids joined by commas. Any
/// membership change is a different key -- the exact-staleness rule that
/// decides what is re-summarised and what is pruned.
[[nodiscard]] std::string community_key(const std::vector<std::int64_t>& members);

/// Renders one cluster for the summariser: member entity lines
/// (most-mentioned first), then the intra-community relations by
/// descending weight, at most `kMaxCommunityRelationLines`.
[[nodiscard]] std::string community_text(const std::vector<embedstore::GraphNode>& nodes,
                                         const std::vector<embedstore::GraphEdge>& edges);

struct CommunitiesOptions {
    /// Recorded on each summarised community (informational).
    std::string model;
    /// Re-summarise every detected community, membership unchanged or not.
    bool force = false;
    /// The smallest community to keep (0 = the default).
    int min_size = 0;
    /// Receives (done, total) as communities are processed.
    std::function<void(int done, int total)> on_progress;
    harness::CancellationToken cancellation;
};

struct CommunitiesResult {
    /// Communities of at least `min_size` the detector found.
    int detected = 0;
    /// Kept as-is: same membership, already summarised.
    int unchanged = 0;
    /// Newly generated: a new membership, or forced.
    int summarized = 0;
    /// Summariser errors -- the community is skipped, never fatal.
    int failed = 0;
    /// Stored communities whose membership no longer exists.
    int pruned = 0;
    /// Pseudo-chunks vectorised in the embed phase.
    int embedded = 0;
    /// Cancellation was requested; what was committed stays committed.
    bool cancelled = false;
    /// An embed-phase failure. Soft, like the build's: summaries are stored
    /// and text-searchable, and the next run with an embedder vectorises
    /// what is still missing a vector.
    std::string embed_error;
};

/// (Re)derives the graph's global layer: detect communities over the
/// current edge set, keep the ones whose membership is unchanged, summarise
/// new or changed ones through the injected summariser, prune stored
/// communities that no longer exist, then vectorise every summary still
/// without a vector in a batched embed phase last (so a failed embed never
/// loses summarisation work). Summariser failures are soft: the community
/// is counted and skipped, and an existing summary row is left in place
/// rather than replaced with nothing. Throws `std::invalid_argument` with
/// no summariser.
[[nodiscard]] CommunitiesResult build_communities(embedstore::Store& store,
                                                  const SummarizeFn& summarize,
                                                  const EmbedFn& embed,
                                                  const CommunitiesOptions& options);

}  // namespace apogee::graph
