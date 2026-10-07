#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "embedstore/graph.h"
#include "embedstore/store.h"

/// Graph navigation (27l): walking a built graph -- how two entities
/// connect, everything around one and why it is there, and the bounded
/// neighbourhood a question names.
///
/// **One traversal core.** `apogee graph path|explain|neighbors|query`, the
/// read-only `graph` toolset (`tools/graph_nav`) and the admin plane's read
/// twins all call the functions here and serialise the payloads here, so the
/// JSON a person, a model and a remote client get for the same question is
/// the same bytes. Retrieval-time expansion keeps its own budgeted path
/// (`agentloop/graph_context`) and shares the store queries.
///
/// **Read-only.** Nothing here writes -- not a cache row, not a stat -- which
/// is what lets the toolset register ungated and be served to an external
/// MCP client by `apogee __mcp-tools`.
///
/// **Honest resolution.** A node is addressed by `name`, `kind:name` or, for
/// code, `path:line`; a code node also by its unqualified name when exactly
/// one qualified name ends in it. A name matching several nodes is never
/// silently picked -- the candidates are listed, each by the address that
/// names it alone -- and one matching nothing says so with its near matches.
///
/// **Bounded by construction.** `path` caps its hops (8, at most 32), a card
/// and a neighbour list cap each relation (12, at most 100), and `query` keeps
/// the expansion's budget: 1..2 hops, its entity cap (at most 50), and the
/// 1,500-codepoint section a turn injects. Every cap is an argument, none is
/// unbounded, and a list cut by one says how many it had.
///
/// **Model-free.** No closure, no embedder: `query` matches the question
/// exactly, then by the entity index over names -- the lexical path, on
/// every install.
namespace apogee::graph {

// ---- The caps -------------------------------------------------------------------

inline constexpr int kDefaultPathHops = 8;
inline constexpr int kMaxPathHops = 32;
inline constexpr int kDefaultNeighborsPerRelation = 12;
inline constexpr int kMaxNeighborsPerRelation = 100;
inline constexpr int kMaxQueryEntities = 50;
/// A card's provenance: the first `file:line`s or chunks, then a count.
inline constexpr std::size_t kCardMentions = 12;
/// A card's attached decision records, at most.
inline constexpr std::size_t kCardDecisions = 12;
/// Candidates an ambiguous name lists before "and N more".
inline constexpr std::size_t kCandidatesShown = 10;
/// Near matches a name that matches nothing suggests.
inline constexpr int kNearMatches = 5;
/// A node's description in a card, and a community summary, in codepoints.
inline constexpr std::size_t kDescriptionClip = 400;
inline constexpr std::size_t kSummaryClip = 300;
/// An inferred relation's description beside a neighbour or a step.
inline constexpr std::size_t kRelationDescriptionClip = 200;

/// `text` cut to `limit` codepoints, an ellipsis marking the cut -- never
/// inside a codepoint. How every payload clips a description.
[[nodiscard]] std::string clip_text(std::string_view text, std::size_t limit);

// ---- How a payload names a node -------------------------------------------------

/// A node as every payload names it: identity, and where it lives -- its
/// definition's `file:line` for code, its branch marker for a decision.
struct NodeRef {
    std::string name;
    std::string type;
    /// Code (27k): the member, file and lines of its definition, else its
    /// first declaration. Empty for prose and for an unresolved name.
    std::string member;
    std::string file;
    std::int64_t line = 0;
    std::int64_t end_line = 0;
    /// An unresolved reference's `name` node: something outside the tree.
    bool unresolved = false;
    /// A decision node's record status and discipline.
    std::string status;
    std::string discipline;
};

[[nodiscard]] NodeRef node_ref(const embedstore::GraphNode& node);

/// `type:name` -- the address that names this node alone.
[[nodiscard]] std::string node_address(const NodeRef& node);

/// `name (type, file:line)` -- how a person reads a node on one line.
[[nodiscard]] std::string node_label(const NodeRef& node);

[[nodiscard]] nlohmann::json to_json(const NodeRef& node);

// ---- Failures --------------------------------------------------------------------

/// Why a navigation could not answer. The message is complete -- every
/// surface shows it as it is (the CLI on stderr, a tool as its error result,
/// the admin plane in its error envelope) -- and an ambiguous name carries
/// its candidates as data too.
class NavigationError : public std::runtime_error {
public:
    enum class Kind : std::uint8_t {
        /// An argument out of range, or a selection that names nothing.
        InvalidArgument,
        /// No graph, collection or node by that name.
        NotFound,
        /// The graph exists in the config but holds nothing yet.
        NotBuilt,
        /// A name several nodes answer to.
        Ambiguous,
    };

    NavigationError(Kind kind, const std::string& message, std::vector<NodeRef> candidates = {});

    [[nodiscard]] Kind kind() const noexcept {
        return kind_;
    }

    [[nodiscard]] const std::vector<NodeRef>& candidates() const noexcept {
        return candidates_;
    }

private:
    Kind kind_;
    std::vector<NodeRef> candidates_;
};

// ---- Which graph -----------------------------------------------------------------

/// What a navigation is asked to read, as `--graph`/`--collection` and the
/// tools' `graph`/`collection` say it. Both empty: the one graph built, when
/// there is exactly one.
struct GraphSelection {
    /// A named graph or a collection's own graph, graphs-first -- as every
    /// `graph` subcommand resolves its name.
    std::string graph;
    /// The graph that covers a collection: a built named graph listing it,
    /// else its own -- the retrieval precedence rule, decided once in
    /// `agentloop::named_graph_covering`.
    std::string collection;
};

/// One graph's database and its members, resolved and not yet opened. A
/// surface may also make one directly over a store it owns -- a chat's
/// attachment database (27o) -- and the core reads it the same way.
struct GraphTarget {
    /// What every payload calls the graph.
    std::string name;
    std::filesystem::path store_path;
    /// Member collections' databases by label: a named graph's chunk
    /// mentions resolve through these. A collection's own graph is the one
    /// `""` member, the graph's own store.
    std::map<std::string, std::filesystem::path> databases;
    /// The graph's expansion knobs -- `query`'s defaults.
    int hops = 1;
    int max_entities = 8;
    /// Said when the store holds nothing yet; empty says nothing more.
    std::string build_hint;
};

/// Resolves a selection against the config and the disk. Throws
/// `NavigationError` naming what is missing; never creates a database.
[[nodiscard]] GraphTarget resolve_graph_target(const harness::Config& config,
                                               const GraphSelection& selection);

/// Every graph a selection could name: built named graphs in config order,
/// then collections holding their own graph, sorted.
[[nodiscard]] std::vector<std::string> built_graph_names(const harness::Config& config);

/// A graph opened for reading: its store and its member stores. A member
/// whose database is missing is null, and its mentions read as missing.
class OpenGraph {
public:
    /// Throws `NavigationError` when the store is absent or holds no graph.
    explicit OpenGraph(GraphTarget target);

    [[nodiscard]] const GraphTarget& target() const noexcept {
        return target_;
    }

    [[nodiscard]] const embedstore::Store& store() const noexcept {
        return *store_;
    }

    /// The non-owning views a card resolves chunk mentions through.
    [[nodiscard]] embedstore::MemberStores members() const;

private:
    GraphTarget target_;
    std::unique_ptr<embedstore::Store> store_;
    std::map<std::string, std::unique_ptr<embedstore::Store>> members_;
};

// ---- Addressing ------------------------------------------------------------------

/// A node an address resolved to, and how: `exact`, `kind` (`kind:name`),
/// `path` (`path:line`) or `unqualified` (a code node's name's last part).
struct ResolvedNode {
    embedstore::GraphNode node;
    std::string matched;
};

/// Resolves `address` in `store`, in this order: `path:line` (a code
/// definition or declaration whose lines hold it, innermost first; the path
/// as recorded, prefixed by its member, or a unique suffix), `kind:name`,
/// the exact name, then a code node's unqualified name. Throws
/// `NavigationError`: `Ambiguous` with the candidates, `NotFound` with the
/// near matches. `graph` names the graph in the messages.
[[nodiscard]] ResolvedNode resolve_node(const embedstore::Store& store, std::string_view graph,
                                        std::string_view address);

// ---- Shared payload pieces -------------------------------------------------------

/// A node's edges, counted.
struct Degree {
    std::int64_t total = 0;
    std::int64_t out = 0;
    std::int64_t in = 0;
};

/// A node's degree: every edge touching it, either way, counted in SQL
/// (`relation_counts`) -- the one definition a card, a neighbour list and
/// 27m's hub ranking all state.
[[nodiscard]] Degree node_degree(const embedstore::Store& store, std::int64_t node_id);

[[nodiscard]] nlohmann::json to_json(const Degree& degree);

/// A node and its degree.
struct RankedNode {
    embedstore::GraphNode node;
    Degree degree;
};

/// Every node but the unresolved `name` nodes, ranked by degree -- the
/// highest first, then the most mentioned, then by name and type, so the
/// order is the store's to decide. What "a hub" means everywhere (27m): the
/// report's hubs and orphans, the HTML's cap and the Mermaid selection all
/// read this one ranking. A name is left out because it is not structure
/// (27k): ranked, `.push_back` would be the busiest entity of any C++ tree.
[[nodiscard]] std::vector<RankedNode> rank_by_degree(const embedstore::Store& store);

/// One neighbour: the peer, and the edge that reaches it.
struct NeighborEntry {
    NodeRef node;
    std::int64_t weight = 1;
    std::string origin;
    /// A parsed edge's first site, `file:line`; empty otherwise.
    std::string at;
    /// An asserted relation's description, clipped.
    std::string description;
};

/// A node's neighbours over one relation, one way: how many there are, and
/// the first `max_per_relation` of them, heaviest first.
struct NeighborGroup {
    std::string relation;
    /// True: the node is the edge's source (it calls, imports, is defined
    /// in); false: the peer is.
    bool outgoing = true;
    std::int64_t total = 0;
    std::vector<NeighborEntry> shown;
};

/// One group as every payload carries it: `relation`, `direction`, `total`
/// and the `neighbors` shown.
[[nodiscard]] nlohmann::json to_json(const NeighborGroup& group);

// ---- path ------------------------------------------------------------------------

struct PathRequest {
    std::string from;
    std::string to;
    int max_hops = kDefaultPathHops;
    /// Follow edges source to target only; undirected by default, each
    /// hop's direction shown.
    bool directed = false;
    /// Walk only these relations; empty walks every one.
    std::vector<std::string> relations;
};

/// One hop: an edge, read from the path's side.
struct PathStep {
    std::string from;
    std::string to;
    std::string relation;
    std::string origin;
    /// True: the edge runs `from` -> `to`; false: it runs `to` -> `from`.
    bool forward = true;
    std::int64_t weight = 1;
    /// A parsed edge's first site, `file:line`; empty otherwise.
    std::string at;
    std::string description;
};

struct PathResult {
    std::string graph;
    NodeRef from;
    NodeRef to;
    /// How each endpoint's address resolved.
    std::string from_matched;
    std::string to_matched;
    bool directed = false;
    int max_hops = kDefaultPathHops;
    std::vector<std::string> relations;
    bool found = false;
    /// The path's nodes, `from` first and `to` last; one node for a node to
    /// itself; empty when there is none within the cap.
    std::vector<NodeRef> nodes;
    std::vector<PathStep> steps;
};

/// The shortest path between two resolved nodes: breadth first, the edges
/// at each node taken heaviest first, so the path is one the store
/// determines. An unresolved `name` node may be an endpoint and is never a
/// hop between two others -- a call to `.push_back` connects nothing.
/// Throws `NavigationError` for a hop cap outside 1..kMaxPathHops.
[[nodiscard]] PathResult shortest_path(const embedstore::Store& store, std::string_view graph,
                                       const embedstore::GraphNode& from,
                                       const embedstore::GraphNode& to, const PathRequest& request);

/// Resolves both endpoints in `open`, then `shortest_path`.
[[nodiscard]] PathResult find_path(const OpenGraph& open, const PathRequest& request);

[[nodiscard]] nlohmann::json to_json(const PathResult& result);

// ---- neighbors -------------------------------------------------------------------

struct NeighborsRequest {
    std::string node;
    /// Only this relation; empty for every one.
    std::string relation;
    embedstore::EdgeDirection direction = embedstore::EdgeDirection::Both;
    int max_per_relation = kDefaultNeighborsPerRelation;
};

struct Neighborhood {
    std::string graph;
    NodeRef node;
    std::string matched;
    Degree degree;
    std::string relation;
    embedstore::EdgeDirection direction = embedstore::EdgeDirection::Both;
    int max_per_relation = kDefaultNeighborsPerRelation;
    std::vector<NeighborGroup> groups;
};

/// A node's neighbours grouped by (relation, direction), each group capped,
/// counted in SQL first -- so a hub never loads its whole edge list. Throws
/// `NavigationError` for a cap outside 1..kMaxNeighborsPerRelation.
[[nodiscard]] Neighborhood neighborhood(const embedstore::Store& store, std::string_view graph,
                                        const embedstore::GraphNode& node,
                                        const NeighborsRequest& request);

[[nodiscard]] Neighborhood find_neighbors(const OpenGraph& open, const NeighborsRequest& request);

[[nodiscard]] nlohmann::json to_json(const Neighborhood& result);

// ---- explain ---------------------------------------------------------------------

struct CardRequest {
    std::string node;
    int max_per_relation = kDefaultNeighborsPerRelation;
};

/// Where a prose node was extracted from: a chunk of a member's source.
struct ChunkMention {
    std::string collection;
    std::string source;
    std::int64_t chunk = 0;
    /// The chunk's row id, for one whose member or row is gone.
    std::int64_t chunk_id = 0;
    bool missing = false;
};

struct CommunityRef {
    std::int64_t id = 0;
    std::int64_t size = 0;
    /// Clipped; empty for a community clustered with no model.
    std::string summary;
};

struct DecisionRef {
    NodeRef node;
    /// The record's decision -- intent, as its node describes it.
    std::string decision;
};

/// The explain payload: everything around one node and why it is there.
/// What 27m's report and HTML cards render.
struct NodeCard {
    std::string graph;
    NodeRef node;
    std::string matched;
    std::string description;
    /// Its salience: chunks and `file:line`s mentioning it.
    std::int64_t mentions = 0;
    Degree degree;
    int max_per_relation = kDefaultNeighborsPerRelation;
    std::vector<NeighborGroup> relations;
    /// Provenance: a code node's definitions, declarations or references,
    /// a prose node's chunks -- each list the first `kCardMentions`.
    std::vector<embedstore::CodeMention> code_mentions;
    std::vector<ChunkMention> chunk_mentions;
    std::vector<CommunityRef> communities;
    std::vector<DecisionRef> decisions;
};

/// The card for a resolved node. `stores` -- the graph's members by label --
/// resolves its chunk mentions.
/// Throws `NavigationError` for a cap outside 1..kMaxNeighborsPerRelation.
[[nodiscard]] NodeCard node_card(const embedstore::Store& store,
                                 const embedstore::MemberStores& stores, std::string_view graph,
                                 const embedstore::GraphNode& node, int max_per_relation);

[[nodiscard]] NodeCard explain_node(const OpenGraph& open, const CardRequest& request);

[[nodiscard]] nlohmann::json to_json(const NodeCard& card);

// ---- query -----------------------------------------------------------------------

struct QueryRequest {
    std::string question;
    /// 0: the graph's own knob (its `hops:` / `max_entities:`).
    int hops = 0;
    int max_entities = 0;
};

struct QueryEntity {
    NodeRef node;
    /// 0 for an entity the question named, else how many edges away.
    int hop = 0;
    std::string description;
};

struct QueryRelation {
    std::string from;
    std::string to;
    std::string relation;
    std::string origin;
    std::string description;
};

struct QueryResult {
    std::string graph;
    std::string question;
    /// `exact` (the question is an entity's name), `names` (its words matched
    /// entity names) or `none`.
    std::string match;
    std::vector<NodeRef> seeds;
    std::vector<QueryEntity> entities;
    std::vector<QueryRelation> relations;
    int hops = 1;
    int max_entities = 8;
    std::size_t budget = 0;
    /// The budget cut an entity or a relation.
    bool truncated = false;
};

/// Entity match, then the bounded neighbourhood -- retrieval-time
/// expansion's walk, seeded by the question alone: its exact name, else its
/// words against entity names (at most half the entity cap, so the
/// neighbourhood has room), walked `hops` out, ranked and capped as a turn's
/// expansion is, and cut whole lines at the section budget. Throws
/// `NavigationError` for hops outside 1..2 or entities outside
/// 1..kMaxQueryEntities.
[[nodiscard]] QueryResult query_graph(const embedstore::Store& store, std::string_view graph,
                                      std::string_view question, int hops, int max_entities);

[[nodiscard]] QueryResult run_query(const OpenGraph& open, const QueryRequest& request);

[[nodiscard]] nlohmann::json to_json(const QueryResult& result);

/// The words a direction is spelled with: `both`, `out`, `in`.
[[nodiscard]] std::string_view to_string(embedstore::EdgeDirection direction) noexcept;
/// Parses one; false for anything else.
[[nodiscard]] bool direction_from_string(std::string_view word,
                                         embedstore::EdgeDirection& direction) noexcept;

}  // namespace apogee::graph
