#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "graph/extract.h"
#include "harness/cancellation.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "knowledge/record.h"

/// The resumable build loop that turns a collection's chunks into its
/// knowledge graph, and the deterministic pass that makes every knowledge
/// record in the collection a first-class `decision` node.
///
/// ## Incremental and resumable by construction
///
/// A source is re-extracted only when it is **stale**: no state row, its
/// chunk count changed, its highest chunk id moved (a same-count re-ingest
/// is still caught -- the fingerprint Ommi added after one silently lost
/// every decision node), or the extraction model changed. A file's state
/// row is written only after every one of its chunks finished, so an
/// interrupted build resumes without re-extracting finished files. Per-chunk
/// failures are soft: one retry, then counted, and the file left unstamped
/// so the next build retries it. Every build starts by reconciling rows
/// whose chunks no longer exist.
///
/// ## Records as nodes
///
/// Every build materialises each knowledge record in the collection --
/// deterministically, with no model call, before extraction -- as one node
/// of the reserved type `decision`, named by the record id, described by
/// decision + intent, carrying status and discipline as metadata replaced
/// on every run, mentioned by the record's own chunk so the ordinary
/// reconcile retires it with the record. Extraction still runs over the
/// record's text, and every entity it pulls gets a `concerns` edge from the
/// decision node -- the link that lets a documents chunk about the same thing
/// reach the *decision* in one hop. A `supersedes` edge mirrors the record's
/// lineage when the target is in the graph too. Deterministic edges are
/// facts (weight 1); extracted relations gain corroboration weight.
namespace apogee::graph {

/// The decision -> entity edge: the record's reasoning concerns that entity.
inline constexpr std::string_view kRelationConcerns = "concerns";
/// The decision -> decision edge mirroring `Record::supersedes`.
inline constexpr std::string_view kRelationSupersedes = "supersedes";

/// Caps a decision node's description (codepoints), so one long record
/// cannot dominate a graph-context section -- the full record is always one
/// `apogee knowledge info <id>` away.
inline constexpr std::size_t kMaxDecisionNodeDescriptionLen = 400;

/// The text a record's decision node carries: the decision first, then the
/// intent, joined by " — " and clipped. What expansion renders next to the
/// record id and what the entity embedder vectorises -- built from the same
/// immutable fields `index_text` uses.
[[nodiscard]] std::string decision_node_description(const knowledge::Record& record);

/// The embedder a build vectorises entities with, under the embedding spend
/// rule already in force for ingest: the collection's chain, used when it is
/// unmetered or the collection pins `retriever: vector`; otherwise no
/// function and a `note` saying why, and the graph stays full-text
/// searchable. ONE decision, called by the CLI and the admin plane alike.
struct EntityEmbedder {
    /// Null when no vectors will be built.
    EmbedFn embed;
    /// The model recorded as the graph's entity-vector space; empty when none.
    std::string model;
    /// Why entities stay lexical-only, when they do.
    std::string note;
};

[[nodiscard]] EntityEmbedder resolve_entity_embedder(const harness::Harness& harness,
                                                     const harness::Config& config,
                                                     std::string_view collection_backend,
                                                     std::string_view retriever_pin);

/// Whether a source needs (re)extraction given its recorded state and live
/// fingerprint. A legacy row (`max_chunk_id` 0) compares the count only.
[[nodiscard]] bool source_stale(const embedstore::SourceState& state,
                                const embedstore::ChunkSpan& span, std::string_view model);

/// A build heartbeat -- the status-line / admin-job seam.
struct Progress {
    enum class Stage : std::uint8_t { Extract, Embed };
    Stage stage = Stage::Extract;
    std::string file;
    /// 1-based, over the planned files.
    int file_index = 0;
    int file_count = 0;
    /// Across the whole build.
    int chunks_done = 0;
    int chunks_total = 0;
    int failed = 0;
    /// The embed stage: how many entities will be embedded.
    int entities = 0;
};

struct BuildOptions {
    /// The extraction model's name, recorded per source in `kg_state` (the
    /// staleness key) and in `graph_meta`.
    std::string model;
    /// Recorded as the graph's entity-vector model after the embed phase
    /// completes without error; empty when no embedder resolves.
    std::string embed_model;
    /// Marks every source stale regardless of its state row.
    bool force = false;
    /// Stops after N chunks (0 = no limit). A file interrupted by the limit
    /// gets no state row and re-extracts on the next build.
    int limit = 0;
    /// Runs extraction and reports what would be stored, committing nothing:
    /// no reconcile, no upserts, no state rows, no embeddings.
    bool dry_run = false;
    /// Receives each chunk's normalised extraction -- `--dry-run` prints these.
    std::function<void(const embedstore::Chunk&, const ExtractResult&)> on_extract;
    /// Receives each chunk's final extraction error, after the retry.
    std::function<void(const embedstore::Chunk&, std::string_view error)> on_chunk_failed;
    std::function<void(const Progress&)> on_progress;
    harness::CancellationToken cancellation;
};

struct BuildResult {
    embedstore::ReconcileResult reconcile;
    int files_planned = 0;
    /// Files fully extracted (and, outside a dry run, stamped).
    int files_extracted = 0;
    int chunks_done = 0;
    /// Chunks whose extraction failed after the retry.
    int chunks_failed = 0;
    /// Nodes created or description-changed, decision nodes included.
    int nodes_upserted = 0;
    /// Edges created or re-corroborated; deterministic edges only when inserted.
    int edges_upserted = 0;
    int mentions_added = 0;
    int nodes_embedded = 0;
    /// Knowledge records found and materialised as decision nodes (in a dry
    /// run: that would be).
    int record_nodes = 0;
    /// `supersedes` edges ensured; and records whose target is not in the
    /// graph -- no dangling node is created for those.
    int supersedes_edges = 0;
    int supersedes_skipped = 0;
    bool limit_hit = false;
    bool dry_run = false;
    /// Cancellation was requested; what was committed stays committed and
    /// the next build resumes.
    bool cancelled = false;
    /// An embed-phase failure. It does not fail the build: extraction is
    /// already committed, affected nodes stay lexical-only until the next
    /// build re-embeds them.
    std::string embed_error;
};

/// (Re)builds a collection's graph: reconcile → materialise records → plan by
/// fingerprint → extract stale sources chunk by chunk with one retry → state
/// row after a complete file only → embed mutated nodes last, so a failed
/// embed never loses extraction work. Storage errors throw; a cancellation
/// returns with `cancelled` set.
[[nodiscard]] BuildResult build(embedstore::Store& store, const ExtractFn& extract,
                                const EmbedFn& embed, const BuildOptions& options);

}  // namespace apogee::graph
