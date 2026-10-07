#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

/// The knowledge-graph layer's storage types: entities and relations
/// extracted from chunks, stored in `kg_*` tables **inside the collection's
/// own database** beside the chunks they came from (schema v4).
///
/// Storage only. Extraction, validation and the resumable build loop live in
/// `graph/`; the retrieval-time expansion in `agentloop/graph_context`. The
/// tables are created by an Open-time migration and stay empty until
/// `apogee graph build` populates them.
///
/// **Node identity is (normalised name, type).** The first-extracted display
/// casing is kept; descriptions merge first-non-empty-wins; a description
/// change clears the stored vector so a stale embedding can never outlive its
/// text. An edge's weight increments each time another chunk re-states it --
/// corroboration -- except for the deterministic edges (`ensure_edge`), which
/// are facts and stay at weight 1.
///
/// `kg_mentions.collection` and `kg_state.collection` carry the provenance
/// column a **named multi-collection graph** keys on: its database lives
/// under the embeddings directory's `graphs/` and every mention and state row
/// names the member collection it came from. A collection's own graph writes
/// `''` there -- the self reference -- so the two shapes are one schema and
/// one set of queries, the label being the only difference.
namespace apogee::embedstore {

class Store;

/// The reserved node type for an organizational knowledge record
/// materialised as a first-class node. **Storage-only**: it is not in the
/// extractor's closed type set, so an extraction that emits it is dropped
/// like any unknown type -- only the build's deterministic record pass
/// creates nodes of this type. The record id is the node's name, so a
/// decision node can never collide with an extracted entity.
inline constexpr std::string_view kNodeTypeDecision = "decision";

/// The `kind` a decision node's metadata carries, so a later reserved type
/// can be told apart.
inline constexpr std::string_view kDecisionNodeKind = "knowledge_record";

// ---- Origin (schema v6, 27k) --------------------------------------------------
//
// Every edge says where it came from: `extracted` -- stated in source and
// parsed (the code graph), confidence 1.0 -- or `inferred` -- asserted by a
// model (the prose extractor, and the record pass built on its entities),
// confidence not recorded. Schema, not convention: a reader can always tell
// the two apart, and an `extracted` edge never merges down to `inferred`.
inline constexpr std::string_view kOriginExtracted = "extracted";
inline constexpr std::string_view kOriginInferred = "inferred";
inline constexpr double kExtractedConfidence = 1.0;

// ---- The code layer's kinds (27k) ---------------------------------------------
//
// The closed type set grows by the code kinds, still enforced in host code:
// only the deterministic code build writes them (the prose extractor's set
// is disjoint), so a model can never forge a code node and the code layer
// never merges with a prose entity of the same name.
inline constexpr std::string_view kCodeKindFile = "file";
inline constexpr std::string_view kCodeKindModule = "module";
inline constexpr std::string_view kCodeKindClass = "class";
inline constexpr std::string_view kCodeKindFunction = "function";
/// An unresolved reference's target: the text as written, standing for
/// something outside the parsed tree. Navigable, never ranked as context nor
/// clustered as structure (expansion and community detection leave it out).
inline constexpr std::string_view kCodeKindName = "name";

inline constexpr std::string_view kCodeRelationDefinedIn = "defined_in";
inline constexpr std::string_view kCodeRelationImports = "imports";
inline constexpr std::string_view kCodeRelationCalls = "calls";
inline constexpr std::string_view kCodeRelationInherits = "inherits";
inline constexpr std::string_view kCodeRelationReferences = "references";

/// Whether `type` is one of the code kinds.
[[nodiscard]] bool is_code_node_type(std::string_view type) noexcept;

/// A code node's identity key -- what its `name_norm` holds: the qualified
/// name **exactly as written**. Code is case-sensitive (Go's `Parse` and
/// `parse` are two functions), so the prose layer's case-folding would merge
/// two definitions into one node -- an invented link. Prose entities keep
/// `normalize_entity_name`; the two layers' types are disjoint, so the two
/// keys never meet.
[[nodiscard]] std::string code_identity(std::string_view name);

/// What a code node's metadata JSON carries: its primary location (the
/// definition, else the first declaration) and language, or the unresolved
/// marker on a `name` node.
struct CodeNodeMetadata {
    /// The metadata is a code node's at all.
    bool code = false;
    bool unresolved = false;
    std::string language;
    std::string member;
    std::string file;
    std::int64_t line = 0;
    std::int64_t end_line = 0;
};

[[nodiscard]] std::string code_node_metadata_json(const CodeNodeMetadata& metadata);
[[nodiscard]] CodeNodeMetadata parse_code_node_metadata(std::string_view json);

/// Canonical form for dedup: lowercased, trimmed, interior whitespace runs
/// collapsed to one space.
[[nodiscard]] std::string normalize_entity_name(std::string_view name);

/// One entity in a collection's knowledge graph.
struct GraphNode {
    std::int64_t id = 0;
    /// Display form -- the first-extracted casing wins.
    std::string name;
    std::string name_norm;
    std::string type;
    std::string description;
    /// The entity vector's width; 0 means lexical-only (no vector).
    std::int64_t dim = 0;
    /// How many chunks mention this entity -- the salience counter.
    std::int64_t mention_count = 0;
    /// JSON a decision node carries (status, discipline); empty otherwise.
    std::string metadata;
};

/// The record fields a decision node's metadata carries: the branch marker
/// and the discipline. `status` is empty when the metadata is not a record's.
struct DecisionNodeMetadata {
    std::string status;
    std::string discipline;
};

[[nodiscard]] std::string decision_node_metadata_json(std::string_view status,
                                                      std::string_view discipline);
[[nodiscard]] DecisionNodeMetadata parse_decision_node_metadata(std::string_view json);

/// What an upsert did: the node's id, and whether it is new or its
/// description text changed -- the signal the build's embed phase acts on.
struct UpsertResult {
    std::int64_t id = 0;
    bool mutated = false;
};

/// One source file's extraction bookkeeping -- the staleness fingerprint.
///
/// A source is up to date iff its row exists, its live chunk count AND its
/// highest chunk id match, and the extraction model matches. Chunk ids are
/// never reused, so a re-ingest -- which deletes and re-creates a source's
/// chunks -- always raises the highest id even when the count is unchanged.
/// Without that half of the fingerprint the reconcile pass would prune the
/// source's dead mentions while the planner still read it as up to date, and
/// its entities would not return until a forced rebuild. `max_chunk_id` is 0
/// on a row stamped before it was recorded; the planner then compares the
/// count only, so an upgrade never forces a rebuild.
struct SourceState {
    std::string source;
    std::int64_t chunk_count = 0;
    std::int64_t max_chunk_id = 0;
    /// RFC 3339, UTC.
    std::string extracted_at;
    std::string model;
};

/// One source file's live chunk fingerprint: its count and highest chunk id.
struct ChunkSpan {
    std::int64_t count = 0;
    std::int64_t max_id = 0;
};

/// A chunk in a member collection -- the provenance a mention row carries.
/// `collection` is `''` for a collection's own graph.
struct ChunkRef {
    std::string collection;
    std::int64_t chunk_id = 0;

    [[nodiscard]] bool operator==(const ChunkRef&) const noexcept = default;
};

/// One relation row as stored -- the community detector's input, and the
/// edge list an export carries (27m).
struct GraphEdge {
    std::int64_t id = 0;
    std::int64_t source_id = 0;
    std::int64_t target_id = 0;
    std::string relation;
    std::string description;
    std::int64_t weight = 1;
    /// `extracted` or `inferred` (schema v6).
    std::string origin;
    /// Set for an extracted edge (1.0); a negative value means not recorded.
    double confidence = -1.0;
};

/// A graph's member collections by label, each a read-only view of that
/// collection's store. A **null** store is a member whose database is
/// missing: it reads as empty, so everything it contributed reconciles away
/// rather than wedging the graph. A collection's own graph is the
/// single-member `{"": self}` case. Non-owning: the caller keeps the stores
/// open for the duration of the call.
using MemberStores = std::map<std::string, const Store*>;

/// What a reconcile pass removed.
struct ReconcileResult {
    /// Mention rows whose chunk no longer exists.
    std::int64_t mentions_pruned = 0;
    /// State rows for sources that vanished.
    std::int64_t states_pruned = 0;
    /// Edges touching a node left with zero mentions.
    std::int64_t edges_pruned = 0;
    /// Nodes left with zero mentions.
    std::int64_t nodes_pruned = 0;

    [[nodiscard]] std::int64_t total() const noexcept {
        return mentions_pruned + states_pruned + edges_pruned + nodes_pruned;
    }
};

/// The `graph_meta` keys the build writes. A named graph's database also
/// records its own identity: the `graphs:` entry it was built for and the
/// member set as built (a sorted JSON array), so `stats` can say when the
/// config's membership has drifted since.
inline constexpr std::string_view kGraphMetaExtractModel = "extract_model";
inline constexpr std::string_view kGraphMetaFailedChunks = "failed_chunks";
inline constexpr std::string_view kGraphMetaEmbedModel = "embed_model";
inline constexpr std::string_view kGraphMetaGraphName = "graph_name";
inline constexpr std::string_view kGraphMetaMembers = "members";

}  // namespace apogee::embedstore
