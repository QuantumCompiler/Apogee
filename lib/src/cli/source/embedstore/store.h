#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/graph_communities.h"
#include "embedstore/graph_dedupe.h"
#include "embedstore/graph_search.h"

/// The chunk store: SQLite with an FTS5 index, one database per collection.
///
/// **The permanent retrieval floor.** Lexical BM25 needs no embedding model, no
/// API key, and no network — which is why it is built first and why it stays
/// even though Apogee has cloud embedders. Ommi's recorded history is the
/// argument: its RAG survived an embedding-model freeze *only* because the
/// lexical path was model-free. A retrieval story whose floor depends on a
/// model is a story that stops working when the model does.
///
/// ## What a caller can rely on
///
/// - **Open is idempotent and migrations are atomic.** Every schema step runs
///   inside one transaction, so an interrupted first open leaves a database
///   that either has the whole schema or none of it — never half.
/// - **The FTS index is maintained on every ingest**, regardless of which
///   retriever a query later uses. An index built lazily is an index that is
///   missing exactly when someone first needs it.
/// - **Re-ingesting a source replaces its chunks** rather than duplicating
///   them, so running ingest twice is not a slow way to double your corpus.
///
/// ## The connection is owned here and never escapes
///
/// SQLite hands out a raw `sqlite3*`. It is wrapped in a `unique_ptr` with a
/// custom deleter at this boundary and no accessor returns it — the Code Style
/// rule for C APIs, and what makes a throw from any query safe.
namespace apogee::embedstore {

/// One stored chunk.
struct Chunk {
    std::int64_t id = 0;
    /// The source it came from — a file path, usually.
    std::string source;
    /// Position within that source, so chunks can be re-assembled in order.
    std::int64_t ordinal = 0;
    std::string text;
    /// Structured data riding beside the text -- a knowledge record's full
    /// JSON beside its thin index text. Empty for an ordinary ingested chunk
    /// (stored NULL). Never searched: the text is what the index sees, and
    /// this is what a reader decodes afterwards. Schema v3.
    std::string metadata;
};

/// A retrieval hit.
struct SearchHit {
    Chunk chunk;

    /// **Normalised to (0, 1] as `s/(1+s)`**, where `s` is the raw BM25
    /// relevance. Carried from Ommi, and the reason matters: raw BM25 is
    /// unbounded and corpus-dependent, so a raw score means nothing to a user
    /// comparing two collections and nothing to a threshold in a config file.
    double score = 0.0;

    /// Which retriever produced `score`.
    ///
    /// **Always reported, never assumed.** Lexical and vector scores are on
    /// incomparable scales, and a number shown without its retriever invites
    /// exactly the comparison that cannot be made.
    std::string retriever = "lexical";
};

/// A collection's on-disk database.
class Store {
public:
    /// Opens (creating if needed) the store at `path`.
    ///
    /// Throws `std::runtime_error` with a message naming the file when the
    /// database cannot be opened or migrated.
    explicit Store(const std::filesystem::path& path);
    ~Store();

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;

    /// Replaces every chunk belonging to `source` with `chunks`.
    ///
    /// One transaction: a source is entirely replaced or entirely untouched.
    /// Half-replacing it would leave a corpus that answers with a mixture of
    /// two document versions, which is worse than either.
    void replace_source(std::string_view source, const std::vector<std::string>& chunks);

    /// Replaces a source with chunks AND their vectors, one per chunk (an
    /// empty vector stores that chunk lexical-only, `dim = 0`). The vectors'
    /// width is recorded per row, so a store can report whether it holds one
    /// vector space or several.
    void replace_source(std::string_view source, const std::vector<std::string>& chunks,
                        const std::vector<std::vector<float>>& vectors);

    /// Replaces a source with chunks, their vectors, AND per-chunk metadata
    /// (one string per chunk; an empty string stores NULL). `vectors` may be
    /// empty for a lexical-only source; `metadata` may not be shorter than
    /// `chunks` when given at all.
    void replace_source(std::string_view source, const std::vector<std::string>& chunks,
                        const std::vector<std::vector<float>>& vectors,
                        const std::vector<std::string>& metadata);

    /// Removes every chunk belonging to `source`. Returns how many went.
    [[nodiscard]] std::int64_t delete_source(std::string_view source);

    /// Rewrites the metadata of every chunk under `source`, touching nothing
    /// else -- not the text, not the vector, not the FTS index. Returns how
    /// many rows changed. **This is what lets a record's mutable fields be
    /// edited without a re-embed**: the index is built from what does not
    /// change, and what changes lives here.
    [[nodiscard]] std::int64_t update_metadata(std::string_view source, std::string_view metadata);

    /// Every chunk that carries metadata, in id order. The knowledge store's
    /// listing walks this rather than every chunk, so a shared collection
    /// holding plain documents beside records costs nothing to list.
    [[nodiscard]] std::vector<Chunk> chunks_with_metadata() const;

    /// One chunk by its id, or nullopt.
    [[nodiscard]] std::optional<Chunk> chunk_by_id(std::int64_t id) const;

    /// The stored vector of chunk `id`, or empty when it has none. Exposed
    /// so a caller -- a test, above all -- can assert that an edit which must
    /// not re-embed left the bytes alone.
    [[nodiscard]] std::vector<float> chunk_vector(std::int64_t id) const;

    /// Lexical search. `limit` caps the hits returned; 0 or less returns every
    /// match, for a caller that filters before it cuts.
    ///
    /// `query` is **natural language**, not FTS5 syntax: see
    /// `fts_match_query`. A user typing `AND` or a quote gets results, never a
    /// syntax error.
    [[nodiscard]] std::vector<SearchHit> search(std::string_view query, int limit) const;

    /// Cosine over every vector in the store, best first, labelled `vector`.
    /// Lexical-only chunks (`dim = 0`) are skipped: they have no position in
    /// any vector space. Reaching them is `search`'s job, and a turn that
    /// needs every chunk searchable is resolved to lexical before it gets here.
    [[nodiscard]] std::vector<SearchHit> search_vector(const std::vector<float>& query_vector,
                                                       int limit) const;

    /// Both halves, each fetched `kHybridFetchDepth` deep, fused by RRF and cut
    /// to `limit`, labelled `hybrid`. Requires a query vector: whether the
    /// vector half is available is the resolver's decision, made before this
    /// is called, so a hybrid turn without one never reaches the store.
    [[nodiscard]] std::vector<SearchHit> search_hybrid(const std::vector<float>& query_vector,
                                                       std::string_view query, int limit) const;

    /// What the store holds, for retriever resolution.
    struct Stats {
        std::int64_t chunk_count = 0;
        /// The vector width, or 0 when the store holds no vectors.
        std::int64_t dimension = 0;
        /// Chunks stored without a vector. Any non-zero count means vector
        /// search would silently skip part of the corpus.
        std::int64_t lexical_only = 0;
        /// Distinct non-zero widths. More than one means mixed vector spaces.
        std::int64_t vector_dims = 0;
    };

    [[nodiscard]] Stats stats() const;

    /// Which model vectorised this store, recorded at vector ingest.
    ///
    /// Kept in `store_meta` beside the schema version. Query-time resolution
    /// compares it with the model that would embed the question; a mismatch
    /// falls to lexical rather than scoring across two vector spaces.
    struct EmbeddingBinding {
        std::string model;
        std::int64_t dimension = 0;

        [[nodiscard]] bool recorded() const noexcept {
            return !model.empty();
        }
    };

    void set_embedding_model(std::string_view model, std::int64_t dimension);
    /// Returns the store to the unrecorded state, so a stale record cannot
    /// make auto pick vector over a store that no longer holds any.
    void clear_embedding_model();
    [[nodiscard]] EmbeddingBinding embedding_model() const;

    /// Total chunks held.
    [[nodiscard]] std::int64_t chunk_count() const;

    /// Distinct sources, sorted.
    [[nodiscard]] std::vector<std::string> sources() const;

    /// Runs SQLite's own content-comparing integrity check on the FTS index.
    ///
    /// The `integrity-check 1` form specifically: the cheaper form validates
    /// only the index's internal structure, and an external-content index can
    /// be perfectly well-formed while disagreeing with the table it mirrors.
    /// Returns empty on success, or the reason.
    [[nodiscard]] std::string verify_index() const;

    /// The schema version the file carries.
    [[nodiscard]] int schema_version() const;

    // --- The knowledge graph: mutation (embedstore/graph.cpp) ---------------
    //
    // Entities and relations live in `kg_*` tables beside the chunks (schema
    // v4). See graph.h for the identity and merge rules. Storage only: the
    // build loop that fills these is `graph/build`.

    /// Inserts an entity or merges it into the node with the same (normalised
    /// name, type): first casing kept, first non-empty description wins, a
    /// description change clears the stored vector. `mutated` reports a new
    /// node or a changed description.
    [[nodiscard]] UpsertResult upsert_node(std::string_view name, std::string_view type,
                                           std::string_view description);

    /// Inserts a directed relation, or -- when the (source, target, relation)
    /// triple exists -- increments its weight: each re-extraction from another
    /// chunk is corroboration. Descriptions merge first-non-empty-wins.
    void upsert_edge(std::int64_t source_id, std::int64_t target_id, std::string_view relation,
                     std::string_view description);

    /// Inserts or refreshes a knowledge record's decision node (type
    /// `kNodeTypeDecision`, name = the record id). Unlike `upsert_node`, the
    /// description and metadata are REPLACED on every call -- the record is
    /// the source of truth and its status legitimately changes after capture.
    /// A description change clears the vector; a metadata-only change does
    /// not, and is not `mutated`.
    [[nodiscard]] UpsertResult upsert_decision_node(std::string_view record_id,
                                                    std::string_view description,
                                                    std::string_view metadata);

    /// Inserts a directed relation if absent and leaves an existing one
    /// untouched -- weight stays 1. The upsert for DETERMINISTIC edges
    /// (`concerns`, `supersedes`), which the build re-derives on every run:
    /// facts, not corroborations. Returns whether a row was inserted.
    [[nodiscard]] bool ensure_edge(std::int64_t source_id, std::int64_t target_id,
                                   std::string_view relation, std::string_view description);

    /// Links a node to a chunk it was extracted from, maintaining
    /// `mention_count`. Re-linking an existing pair is a no-op, so a
    /// re-extraction cannot inflate the count. Returns whether it was new.
    /// The chunk is the collection's own (`collection = ''`).
    [[nodiscard]] bool add_mention(std::int64_t node_id, std::int64_t chunk_id);

    /// The same, for a chunk in the member collection `collection` -- the
    /// row a named graph writes. `''` is the collection's own graph.
    [[nodiscard]] bool add_mention(std::int64_t node_id, std::string_view collection,
                                   std::int64_t chunk_id);

    /// Stores the entity vector for a node; empty returns it to lexical-only.
    void update_node_embedding(std::int64_t node_id, const std::vector<float>& vector);

    /// The stored entity vector, or empty when the node has none.
    [[nodiscard]] std::vector<float> node_vector(std::int64_t node_id) const;

    /// Records that `source`'s chunks were fully extracted: the fingerprint
    /// (count, highest chunk id) and the model, stamped now. The build writes
    /// this only after every chunk of the file finished, so a crash mid-file
    /// leaves no row and the file re-extracts on resume.
    void set_source_state(std::string_view source, std::int64_t chunk_count,
                          std::int64_t max_chunk_id, std::string_view model);

    /// The same, keyed under the member collection `collection`.
    void set_source_state(std::string_view collection, std::string_view source,
                          std::int64_t chunk_count, std::int64_t max_chunk_id,
                          std::string_view model);

    /// Every source's extraction bookkeeping, keyed by source. Empty for an
    /// unbuilt graph. The collection's own rows (`''`).
    [[nodiscard]] std::map<std::string, SourceState> source_states() const;

    /// The rows recorded under the member collection `collection`.
    [[nodiscard]] std::map<std::string, SourceState> source_states(
        std::string_view collection) const;

    /// Every source's live fingerprint -- what the planner compares against
    /// `source_states`. Community pseudo-chunks (`graph://` sources) are
    /// left out: they are graph output, never extraction input.
    [[nodiscard]] std::map<std::string, ChunkSpan> source_chunk_spans() const;

    /// Which of `ids` exist in this store's chunks table -- the
    /// cross-database half of a named graph's reconcile, batched under the
    /// bound-parameter limit.
    [[nodiscard]] std::set<std::int64_t> chunk_ids_existing(
        const std::vector<std::int64_t>& ids) const;

    /// A source's chunks in ordinal order -- the extraction unit.
    [[nodiscard]] std::vector<Chunk> chunks_by_source(std::string_view source) const;

    /// Prunes what chunk churn orphaned: mentions whose chunk no longer
    /// exists, state rows for vanished sources, then -- after recomputing
    /// every `mention_count` from the surviving mentions -- zero-mention nodes
    /// and their edges. One transaction. Runs first on every build; safe at
    /// any time. The single-member form of `reconcile_graph_multi`.
    [[nodiscard]] ReconcileResult reconcile_graph();

    /// The reconcile pass for a graph whose provenance spans member
    /// collections: rows whose collection is no longer a member die (so a
    /// member dropped from the config converges on the next build with no
    /// special case), mentions whose chunk no longer exists in its member's
    /// store and state rows for vanished sources are pruned per member, then
    /// the usual recount and sweep. A null member reads as empty. Reads
    /// across databases happen before the one write transaction.
    [[nodiscard]] ReconcileResult reconcile_graph_multi(const MemberStores& members);

    /// Clears every graph row -- nodes, edges, mentions, state, communities
    /// and their pseudo-chunks, meta. The tables stay; real chunks are
    /// untouched.
    void delete_graph();

    /// One `graph_meta` key. `graph_meta` returns empty when unset.
    void set_graph_meta(std::string_view key, std::string_view value);
    [[nodiscard]] std::string graph_meta(std::string_view key) const;

    /// The member set a named graph was last built over, recorded sorted so
    /// `stats` can compare it with the config's current list. Empty when
    /// never built as a named graph.
    void set_graph_members(const std::vector<std::string>& members);
    [[nodiscard]] std::vector<std::string> graph_members() const;

    // --- The knowledge graph: communities (embedstore/graph_communities.cpp)

    /// Every stored community, largest first.
    [[nodiscard]] std::vector<GraphCommunity> graph_communities() const;

    /// A community's member nodes, most-mentioned first.
    [[nodiscard]] std::vector<GraphNode> community_members(std::int64_t community_id) const;

    /// Stores a freshly summarised community: an existing row with the same
    /// key (a forced regeneration) is removed first, then the row, its
    /// membership and its lexical pseudo-chunk are written in one
    /// transaction. The summary is searchable at once through the chunk
    /// index; its vector arrives later through `update_community_embedding`,
    /// mirroring the build's batched embed phase. Returns the community id.
    [[nodiscard]] std::int64_t replace_community(std::string_view member_key,
                                                 const std::vector<std::int64_t>& members,
                                                 std::string_view summary, std::string_view model);

    /// Vectorises a community's pseudo-chunk so the summary is reachable by
    /// vector search too. The chunk is re-written rather than updated in
    /// place: `replace_source` is the one chunk write path.
    void update_community_embedding(std::int64_t community_id, const std::vector<float>& vector);

    /// Removes every stored community whose key is not in `keep` -- the
    /// exact-staleness sweep -- with its membership and pseudo-chunk.
    /// Returns how many went.
    [[nodiscard]] std::int64_t prune_communities(const std::set<std::string>& keep);

    /// Communities whose pseudo-chunk holds no vector, so a run with an
    /// embedder can heal an earlier embed failure instead of needing a
    /// forced regeneration.
    [[nodiscard]] std::vector<std::int64_t> communities_without_vectors() const;

    // --- The knowledge graph: dedupe (embedstore/graph_dedupe.cpp) ---------

    /// Merges same-type nodes whose entity vectors exceed `threshold` cosine
    /// similarity, union-find per type. Within each cluster the earliest node
    /// (lowest id, the first extracted) survives: edges are repointed to it
    /// (weights summed when the repoint collides with an existing edge,
    /// would-be self-loops dropped), mentions are unioned and recounted, the
    /// description merges first-non-empty (the survivor's vector cleared on
    /// a text change), and the merged nodes' community memberships are
    /// removed (derived; the next communities run recomputes). Nodes without
    /// a vector and `decision` nodes are never considered. `dry_run` computes
    /// the groups and writes nothing. One transaction.
    [[nodiscard]] std::vector<MergeGroup> dedupe_nodes(double threshold, bool dry_run);

    // --- The knowledge graph: reads (embedstore/graph_search.cpp) -----------

    [[nodiscard]] GraphStats graph_stats() const;

    /// `graph_stats` for a named graph, whose chunk coverage lives in its
    /// member stores rather than its own chunks table: the totals summed over
    /// members, with a per-member breakdown. A null member reports zero
    /// chunks and is flagged missing.
    [[nodiscard]] GraphStatsMulti graph_stats_multi(const MemberStores& members) const;

    /// Every node whose normalised name equals `name` -- one per type sharing
    /// it -- most-mentioned first. Empty for an unknown name, never an error.
    [[nodiscard]] std::vector<GraphNode> find_nodes(std::string_view name) const;

    /// The top `limit` entities by BM25 over names and descriptions -- the
    /// fallback when an exact lookup misses, and the model-free seed for a
    /// lexical turn's expansion. Natural language in, through the same guard
    /// as `search`; 0 or less returns every match.
    [[nodiscard]] std::vector<NodeResult> search_nodes(std::string_view query, int limit) const;

    /// Nodes by id, in ascending id order; unknown ids skipped.
    [[nodiscard]] std::vector<GraphNode> nodes_by_ids(const std::vector<std::int64_t>& ids) const;

    /// Every edge incident to a node, both directions, ordered by relation
    /// then descending weight -- ready to group by relation.
    [[nodiscard]] std::vector<Neighbor> node_neighbors(std::int64_t node_id) const;

    /// The chunks a node was extracted from, in source and ordinal order --
    /// the collection's own (`''`). `limit` 0 or less returns all.
    [[nodiscard]] std::vector<Chunk> node_chunks(std::int64_t node_id, int limit) const;

    /// The (collection, chunk) pairs a node was extracted from, by collection
    /// then chunk id -- the named-graph counterpart of `node_chunks`, resolved
    /// to text through the member stores by the caller. `limit` 0 or less
    /// returns all.
    [[nodiscard]] std::vector<ChunkRef> node_mention_refs(std::int64_t node_id, int limit) const;

    /// The whole edge set -- the community detector's input.
    [[nodiscard]] std::vector<GraphEdge> all_edges() const;

    /// Expands a retrieval turn through the graph. The seed set is the union
    /// of the entities mentioned by `seed_chunks` (the turn's retrieved
    /// chunks) and `seed_nodes` (query-term entity hits from `search_nodes`).
    /// It walks `hops` (clamped to 1..2) edge steps outward and returns up to
    /// `max_entities` neighbour entities ordered by hop then score, plus
    /// every relation among the traversed neighbourhood. Entities reached
    /// only through the seed chunks are not re-listed (their text is already
    /// injected); hop-0 seed nodes are, since nothing else carries their
    /// descriptions. Empty seeds give an empty expansion, never an error.
    [[nodiscard]] Expansion graph_expand(const std::vector<std::int64_t>& seed_chunks,
                                         const std::vector<std::int64_t>& seed_nodes, int hops,
                                         int max_entities) const;

    /// The same, with the seed chunks named by (collection, chunk) -- how a
    /// named graph is seeded from a member's retrieval. The plain form is
    /// this with every seed labelled `''`.
    [[nodiscard]] Expansion graph_expand_labelled(const std::vector<ChunkRef>& seed_chunks,
                                                  const std::vector<std::int64_t>& seed_nodes,
                                                  int hops, int max_entities) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// The current schema version. Bumped when a migration is added.
/// v2 added `chunks.embedding` and `chunks.dim`, and the `embed_model` /
/// `embed_dim` keys in `store_meta`. v3 added the nullable `chunks.metadata`
/// column (knowledge records). v4 added the knowledge-graph tables (`kg_*`,
/// `graph_meta`, the entity FTS index) and made chunk ids AUTOINCREMENT. v5
/// added the community tables (`kg_communities`, `kg_community_members`). An
/// older store gains them on open.
inline constexpr int kSchemaVersion = 5;

}  // namespace apogee::embedstore
