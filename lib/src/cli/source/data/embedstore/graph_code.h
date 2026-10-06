#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Storage for the code layer of a knowledge graph (27k): the rows the
/// deterministic code build writes into the same `kg_*` tables the prose
/// layer uses, and the provenance only code has.
///
/// - **Code mentions** (`kg_code_mentions`): where a code node is defined,
///   declared or -- a `name` node -- referenced, as `file:line` under the
///   source member it came from. A prose mention is a chunk; a code mention
///   is a line. A node's salience counts both.
/// - **Edge sites** (`kg_edge_sites`): every `file:line` that states a code
///   edge. A code edge's weight is how many there are.
/// - **Code file state** (`kg_state` rows with a `content_hash`): one per
///   parsed file -- its content hash, the extractor that read it, and the
///   facts it yielded, cached as JSON so an update re-links the whole tree
///   without re-parsing a file that did not change.
///
/// The layer is written whole by `Store::sync_code_graph` -- one transaction
/// that converges every code row on the graph it is handed and leaves the
/// prose layer exactly as it was -- so an incremental update and a fresh
/// build of the same tree store the same rows. Linking is `graph/`'s; this
/// is storage only.
namespace apogee::embedstore {

/// One code node to store: identity (type, name), what it says about
/// itself, and every place it is mentioned.
struct CodeMentionRow {
    /// The source member's label.
    std::string collection;
    std::string file;
    std::int64_t line = 0;
    std::int64_t end_line = 0;
    /// `definition`, `declaration` or `reference`.
    std::string role;
};

struct CodeNodeRow {
    std::string type;
    std::string name;
    std::string description;
    std::string metadata;
    std::vector<CodeMentionRow> mentions;
};

struct CodeSiteRow {
    std::string collection;
    std::string file;
    std::int64_t line = 0;
};

/// One code edge to store, by its endpoints' identities. Stored `extracted`
/// with confidence 1.0, its weight the number of sites.
struct CodeEdgeRow {
    std::string source_type;
    std::string source_name;
    std::string target_type;
    std::string target_name;
    std::string relation;
    std::vector<CodeSiteRow> sites;
};

/// What a sync changed.
struct CodeSyncResult {
    std::int64_t nodes_added = 0;
    /// An existing node whose description or metadata changed.
    std::int64_t nodes_changed = 0;
    std::int64_t nodes_removed = 0;
    std::int64_t edges_added = 0;
    std::int64_t edges_removed = 0;
    std::int64_t mentions = 0;
    std::int64_t sites = 0;
};

/// One parsed file's recorded state.
struct CodeFileState {
    std::string collection;
    std::string file;
    std::string content_hash;
    /// The extractor that read it (`code_extractor_id`): a different one
    /// means re-parse.
    std::string extractor;
    /// The cached facts, JSON.
    std::string facts;
    /// RFC 3339, UTC.
    std::string parsed_at;
};

/// A code mention as read back: where a node is stated.
struct CodeMention {
    std::string collection;
    std::string file;
    std::int64_t line = 0;
    std::int64_t end_line = 0;
    std::string role;
};

/// A site as read back: where an edge is stated.
struct EdgeSite {
    std::string collection;
    std::string file;
    std::int64_t line = 0;
};

/// An excerpt of a parsed file, named by its content and its lines (27o) --
/// what a chat's retrieved attachment excerpt is to the code graph. A chat
/// keys a file's chunks by the same SHA-256 of its bytes a code file's state
/// records, so the code an excerpt states is found with no path at all, in
/// every file of that content. Lines are 1-based and inclusive.
struct CodeExcerptRef {
    std::string content_hash;
    std::int64_t first_line = 0;
    std::int64_t last_line = 0;
};

/// How much of the code layer one source member states (27n): its parsed
/// files, the code nodes it mentions and the edges it is a site of. With one
/// member, every code row there is.
struct CodeMemberCounts {
    std::int64_t files = 0;
    std::int64_t nodes = 0;
    std::int64_t edges = 0;
};

}  // namespace apogee::embedstore
