#include "graph/navigate.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <system_error>
#include <utility>

#include "agentloop/graph_context.h"
#include "contracts/layout.h"
#include "embedstore/graph_communities.h"
#include "graph/extract.h"

namespace apogee::graph {
namespace {

using embedstore::EdgeDirection;
using embedstore::GraphNode;
using embedstore::Store;
using Kind = NavigationError::Kind;

[[nodiscard]] bool is_continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U;
}

[[nodiscard]] std::size_t codepoints(std::string_view text) {
    return static_cast<std::size_t>(
        std::count_if(text.begin(), text.end(), [](char c) { return !is_continuation(c); }));
}

/// `text` cut to `limit` codepoints, an ellipsis marking the cut -- never
/// inside a codepoint.
[[nodiscard]] std::string clip(std::string_view text, std::size_t limit) {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (is_continuation(text[i])) {
            continue;
        }
        if (seen == limit) {
            return std::string{text.substr(0, i)} + "…";
        }
        ++seen;
    }
    return std::string{text};
}

[[nodiscard]] std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

[[nodiscard]] std::string lower(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

[[nodiscard]] bool file_exists(const std::filesystem::path& path) {
    std::error_code code;
    return std::filesystem::exists(path, code);
}

[[nodiscard]] bool plain_name(std::string_view name) {
    return !name.empty() && name.find("..") == std::string_view::npos &&
           name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos;
}

[[nodiscard]] std::filesystem::path collection_db(std::string_view name) {
    return harness::embeddings_dir() / (std::string{name} + ".db");
}

[[nodiscard]] std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

/// The node types an address's `kind:` may name: the code kinds, the prose
/// extractor's closed set, and the reserved decision type.
[[nodiscard]] bool known_kind(std::string_view kind) {
    if (embedstore::is_code_node_type(kind) || kind == embedstore::kNodeTypeDecision) {
        return true;
    }
    const auto types = valid_entity_types();
    return std::ranges::find(types, kind) != types.end();
}

/// One candidate as an ambiguity lists it: the address that names it alone,
/// and where it is when it is code.
[[nodiscard]] std::string candidate_text(const NodeRef& node) {
    std::string out = node_address(node);
    if (!node.file.empty()) {
        out += " (" + node.file + ":" + std::to_string(node.line) + ")";
    }
    return out;
}

[[noreturn]] void throw_ambiguous(std::string_view graph, std::string_view address,
                                  const std::vector<GraphNode>& nodes) {
    std::vector<NodeRef> refs;
    refs.reserve(nodes.size());
    for (const GraphNode& node : nodes) {
        refs.push_back(node_ref(node));
    }
    std::string message = "'" + std::string{address} + "' names " + std::to_string(refs.size()) +
                          " nodes in graph '" + std::string{graph} + "' -- name one: ";
    for (std::size_t i = 0; i < refs.size() && i < kCandidatesShown; ++i) {
        message += (i == 0 ? "" : ", ") + candidate_text(refs[i]);
    }
    if (refs.size() > kCandidatesShown) {
        message += ", and " + std::to_string(refs.size() - kCandidatesShown) + " more";
    }
    throw NavigationError(Kind::Ambiguous, message, std::move(refs));
}

/// The nodes whose (type) matches, in order.
[[nodiscard]] std::vector<GraphNode> of_kind(std::vector<GraphNode> nodes, std::string_view kind) {
    std::erase_if(nodes, [&](const GraphNode& node) { return node.type != kind; });
    return nodes;
}

/// `path:line` -- the line as a number when `address` ends in `:<digits>`
/// after a single colon (never the second of a `::`).
struct PathLine {
    std::string path;
    std::int64_t line = 0;
};

[[nodiscard]] std::optional<PathLine> split_path_line(std::string_view address) {
    const std::size_t colon = address.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= address.size() ||
        address[colon - 1] == ':') {
        return std::nullopt;
    }
    const std::string_view digits = address.substr(colon + 1);
    if (digits.size() > 9 || !std::ranges::all_of(digits, [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        })) {
        return std::nullopt;
    }
    return PathLine{.path = std::string{address.substr(0, colon)},
                    .line = std::stoll(std::string{digits})};
}

/// The files `path` names: as recorded, prefixed by the member's label, or
/// -- when neither -- every one it is the tail of at a `/`.
[[nodiscard]] std::vector<embedstore::CodeFile> files_named(const Store& store,
                                                            std::string_view path) {
    const std::vector<embedstore::CodeFile> files = store.code_files();
    std::vector<embedstore::CodeFile> exact;
    std::vector<embedstore::CodeFile> tails;
    const std::string tail = "/" + std::string{path};
    for (const embedstore::CodeFile& file : files) {
        const std::string labelled = file.collection + "/" + file.file;
        if (file.file == path || labelled == path) {
            exact.push_back(file);
        } else if (file.file.ends_with(tail) || labelled.ends_with(tail)) {
            tails.push_back(file);
        }
    }
    return exact.empty() ? tails : exact;
}

/// The node `path:line` names, if `path` names a file the graph holds.
[[nodiscard]] std::optional<ResolvedNode> resolve_path_line(const Store& store,
                                                            std::string_view graph,
                                                            std::string_view address,
                                                            const PathLine& at) {
    const std::vector<embedstore::CodeFile> files = files_named(store, at.path);
    if (files.empty()) {
        return std::nullopt;  // not a path this graph knows: perhaps a name
    }
    if (files.size() > 1) {
        std::vector<std::string> listed;
        for (std::size_t i = 0; i < files.size() && i < kCandidatesShown; ++i) {
            listed.push_back(files[i].collection + "/" + files[i].file);
        }
        throw NavigationError(Kind::Ambiguous, "'" + at.path + "' names " +
                                                   std::to_string(files.size()) +
                                                   " files in graph '" + std::string{graph} +
                                                   "' -- give more of the path: " + join(listed));
    }
    const std::vector<embedstore::CodeSpan> spans =
        store.code_spans_at(files.front().collection, files.front().file, at.line);
    if (spans.empty()) {
        throw NavigationError(Kind::NotFound, "nothing in graph '" + std::string{graph} +
                                                  "' is defined or declared at " +
                                                  files.front().file + ":" +
                                                  std::to_string(at.line));
    }
    // The innermost span; several nodes sharing exactly it are ambiguous.
    std::vector<std::int64_t> ids;
    for (const embedstore::CodeSpan& span : spans) {
        if (span.line == spans.front().line && span.end_line == spans.front().end_line &&
            std::ranges::find(ids, span.node_id) == ids.end()) {
            ids.push_back(span.node_id);
        }
    }
    const std::vector<GraphNode> nodes = store.nodes_by_ids(ids);
    if (nodes.size() != 1) {
        throw_ambiguous(graph, address, nodes);
    }
    return ResolvedNode{.node = nodes.front(), .matched = "path"};
}

/// Where `kind:` ends in `address`: the first colon that is not half of a
/// `::`, or npos.
[[nodiscard]] std::size_t kind_colon(std::string_view address) {
    for (std::size_t i = 0; i < address.size(); ++i) {
        if (address[i] != ':') {
            continue;
        }
        if (i + 1 < address.size() && address[i + 1] == ':') {
            ++i;  // a `::` separator, both halves skipped
            continue;
        }
        return i;
    }
    return std::string_view::npos;
}

/// The one node `name` names among `nodes`, or the throw that says it is
/// several; nullopt for none.
[[nodiscard]] std::optional<ResolvedNode> one_of(const std::vector<GraphNode>& nodes,
                                                 std::string_view graph, std::string_view address,
                                                 std::string_view matched) {
    if (nodes.empty()) {
        return std::nullopt;
    }
    if (nodes.size() > 1) {
        throw_ambiguous(graph, address, nodes);
    }
    return ResolvedNode{.node = nodes.front(), .matched = std::string{matched}};
}

[[noreturn]] void throw_not_found(const Store& store, std::string_view graph,
                                  std::string_view name) {
    std::vector<NodeRef> near;
    for (const embedstore::NodeResult& hit :
         store.search_node_names(name, kNearMatches, /*include_unresolved=*/true)) {
        near.push_back(node_ref(hit.node));
    }
    std::string message =
        "no node named '" + std::string{name} + "' in graph '" + std::string{graph} + "'";
    if (near.empty()) {
        message += " -- nothing has a name near it";
    } else {
        message += " -- near matches: ";
        for (std::size_t i = 0; i < near.size(); ++i) {
            message += (i == 0 ? "" : ", ") + candidate_text(near[i]);
        }
    }
    throw NavigationError(Kind::NotFound, message, std::move(near));
}

[[nodiscard]] std::string site_text(const Store& store, const embedstore::Neighbor& edge) {
    if (edge.origin != embedstore::kOriginExtracted) {
        return {};
    }
    const std::vector<embedstore::EdgeSite> sites = store.edge_sites(edge.edge_id, 1);
    if (sites.empty()) {
        return {};
    }
    return sites.front().file + ":" + std::to_string(sites.front().line);
}

/// Node refs for `ids`, by id.
[[nodiscard]] std::map<std::int64_t, GraphNode> nodes_for(const Store& store,
                                                          std::vector<std::int64_t> ids) {
    std::ranges::sort(ids);
    const auto [first, last] = std::ranges::unique(ids);
    ids.erase(first, last);
    std::map<std::int64_t, GraphNode> out;
    for (GraphNode& node : store.nodes_by_ids(ids)) {
        const std::int64_t id = node.id;
        out.emplace(id, std::move(node));
    }
    return out;
}

void check_hop_cap(int hops) {
    if (hops < 1 || hops > kMaxPathHops) {
        throw NavigationError(Kind::InvalidArgument, "the hop cap must be between 1 and " +
                                                         std::to_string(kMaxPathHops) + " (got " +
                                                         std::to_string(hops) + ")");
    }
}

void check_neighbor_cap(int cap) {
    if (cap < 1 || cap > kMaxNeighborsPerRelation) {
        throw NavigationError(Kind::InvalidArgument, "the neighbour cap must be between 1 and " +
                                                         std::to_string(kMaxNeighborsPerRelation) +
                                                         " per relation (got " +
                                                         std::to_string(cap) + ")");
    }
}

/// The degree `counts` add up to -- the one summation.
[[nodiscard]] Degree degree_of(const std::vector<embedstore::RelationCount>& counts) {
    Degree degree;
    for (const embedstore::RelationCount& count : counts) {
        degree.total += count.count;
        (count.outgoing ? degree.out : degree.in) += count.count;
    }
    return degree;
}

/// The groups and degree every neighbour read shares.
void read_groups(const Store& store, const GraphNode& node, const std::string& relation,
                 EdgeDirection direction, int cap, Degree& degree,
                 std::vector<NeighborGroup>& groups) {
    const std::vector<embedstore::RelationCount> counts = store.relation_counts(node.id);
    degree = degree_of(counts);
    std::vector<std::pair<NeighborGroup, std::vector<embedstore::Neighbor>>> read;
    std::vector<std::int64_t> peers;
    for (const embedstore::RelationCount& count : counts) {
        if (!relation.empty() && count.relation != relation) {
            continue;
        }
        if ((direction == EdgeDirection::Out && !count.outgoing) ||
            (direction == EdgeDirection::In && count.outgoing)) {
            continue;
        }
        embedstore::NeighborFilter filter;
        filter.relations = {count.relation};
        filter.direction = count.outgoing ? EdgeDirection::Out : EdgeDirection::In;
        std::vector<embedstore::Neighbor> rows = store.node_neighbors(node.id, filter, cap);
        for (const embedstore::Neighbor& row : rows) {
            peers.push_back(row.peer_id);
        }
        read.emplace_back(NeighborGroup{.relation = count.relation,
                                        .outgoing = count.outgoing,
                                        .total = count.count,
                                        .shown = {}},
                          std::move(rows));
    }
    const std::map<std::int64_t, GraphNode> by_id = nodes_for(store, std::move(peers));
    for (auto& [group, rows] : read) {
        for (const embedstore::Neighbor& row : rows) {
            const auto it = by_id.find(row.peer_id);
            NeighborEntry entry;
            if (it != by_id.end()) {
                entry.node = node_ref(it->second);
            } else {
                entry.node.name = row.peer_name;
                entry.node.type = row.peer_type;
            }
            entry.weight = row.weight;
            entry.origin = row.origin;
            entry.at = site_text(store, row);
            entry.description = clip(row.description, kRelationDescriptionClip);
            group.shown.push_back(std::move(entry));
        }
        groups.push_back(std::move(group));
    }
}

[[nodiscard]] nlohmann::json groups_json(const std::vector<NeighborGroup>& groups) {
    nlohmann::json out = nlohmann::json::array();
    for (const NeighborGroup& group : groups) {
        out.push_back(to_json(group));
    }
    return out;
}

}  // namespace

std::string clip_text(std::string_view text, std::size_t limit) {
    return clip(text, limit);
}

// ---- Degree and the ranking ------------------------------------------------------

Degree node_degree(const Store& store, std::int64_t node_id) {
    return degree_of(store.relation_counts(node_id));
}

nlohmann::json to_json(const Degree& degree) {
    return nlohmann::json{{"total", degree.total}, {"out", degree.out}, {"in", degree.in}};
}

nlohmann::json to_json(const NeighborGroup& group) {
    nlohmann::json neighbors = nlohmann::json::array();
    for (const NeighborEntry& entry : group.shown) {
        nlohmann::json row = to_json(entry.node);
        row["weight"] = entry.weight;
        row["origin"] = entry.origin;
        if (!entry.at.empty()) {
            row["at"] = entry.at;
        }
        if (!entry.description.empty()) {
            row["description"] = entry.description;
        }
        neighbors.push_back(std::move(row));
    }
    return nlohmann::json{{"relation", group.relation},
                          {"direction", group.outgoing ? "out" : "in"},
                          {"total", group.total},
                          {"neighbors", std::move(neighbors)}};
}

std::vector<RankedNode> rank_by_degree(const Store& store) {
    std::vector<RankedNode> out;
    for (GraphNode& node : store.graph_nodes()) {
        if (node.type == embedstore::kCodeKindName) {
            continue;
        }
        const Degree degree = node_degree(store, node.id);
        out.push_back(RankedNode{.node = std::move(node), .degree = degree});
    }
    std::ranges::sort(out, [](const RankedNode& a, const RankedNode& b) {
        if (a.degree.total != b.degree.total) {
            return a.degree.total > b.degree.total;
        }
        if (a.node.mention_count != b.node.mention_count) {
            return a.node.mention_count > b.node.mention_count;
        }
        if (a.node.name != b.node.name) {
            return a.node.name < b.node.name;
        }
        if (a.node.type != b.node.type) {
            return a.node.type < b.node.type;
        }
        return a.node.id < b.node.id;
    });
    return out;
}

// ---- NodeRef ---------------------------------------------------------------------

NodeRef node_ref(const GraphNode& node) {
    NodeRef out;
    out.name = node.name;
    out.type = node.type;
    if (embedstore::is_code_node_type(node.type)) {
        const embedstore::CodeNodeMetadata meta =
            embedstore::parse_code_node_metadata(node.metadata);
        if (node.type == embedstore::kCodeKindName || meta.unresolved) {
            out.unresolved = true;
        } else if (meta.code) {
            out.member = meta.member;
            out.file = meta.file;
            out.line = meta.line;
            out.end_line = meta.end_line;
        }
    } else if (node.type == embedstore::kNodeTypeDecision) {
        const embedstore::DecisionNodeMetadata meta =
            embedstore::parse_decision_node_metadata(node.metadata);
        out.status = meta.status;
        out.discipline = meta.discipline;
    }
    return out;
}

std::string node_address(const NodeRef& node) {
    return node.type + ":" + node.name;
}

std::string node_label(const NodeRef& node) {
    std::string label = node.type;
    if (!node.file.empty()) {
        label += ", " + node.file + ":" + std::to_string(node.line);
    } else if (node.unresolved) {
        label += ", unresolved";
    } else if (!node.status.empty()) {
        label += ", " + node.status;
    }
    return node.name + " (" + label + ")";
}

nlohmann::json to_json(const NodeRef& node) {
    nlohmann::json out{{"name", node.name}, {"type", node.type}};
    if (!node.file.empty()) {
        if (!node.member.empty()) {
            out["member"] = node.member;
        }
        out["file"] = node.file;
        out["line"] = node.line;
        if (node.end_line > node.line) {
            out["end_line"] = node.end_line;
        }
    }
    if (node.unresolved) {
        out["unresolved"] = true;
    }
    if (!node.status.empty()) {
        out["status"] = node.status;
    }
    if (!node.discipline.empty()) {
        out["discipline"] = node.discipline;
    }
    return out;
}

NavigationError::NavigationError(Kind kind, const std::string& message,
                                 std::vector<NodeRef> candidates)
    : std::runtime_error(message), kind_{kind}, candidates_{std::move(candidates)} {}

std::string_view to_string(EdgeDirection direction) noexcept {
    switch (direction) {
        case EdgeDirection::Out:
            return "out";
        case EdgeDirection::In:
            return "in";
        case EdgeDirection::Both:
            break;
    }
    return "both";
}

bool direction_from_string(std::string_view word, EdgeDirection& direction) noexcept {
    if (word == "both" || word.empty()) {
        direction = EdgeDirection::Both;
    } else if (word == "out") {
        direction = EdgeDirection::Out;
    } else if (word == "in") {
        direction = EdgeDirection::In;
    } else {
        return false;
    }
    return true;
}

// ---- Which graph -----------------------------------------------------------------

namespace {

/// Whether the database at `path` holds a graph. An unreadable one does
/// not: it is not a graph a selection can name.
[[nodiscard]] bool holds_graph(const std::filesystem::path& path) {
    try {
        const Store store{path};
        return store.has_graph();
    } catch (const std::exception&) {
        return false;
    }
}

[[nodiscard]] std::string available_note(const harness::Config& config) {
    const std::vector<std::string> built = built_graph_names(config);
    return built.empty() ? " -- no graph is built yet" : " -- built: " + join(built);
}

/// `name`, graphs-first: a `graphs:` entry, else a collection's own graph.
[[nodiscard]] GraphTarget target_named(const harness::Config& config, const std::string& name) {
    if (!plain_name(name)) {
        throw NavigationError(Kind::InvalidArgument,
                              "'" + name + "' is not a plain graph or collection name");
    }
    GraphTarget target;
    target.name = name;
    target.build_hint = "run: apogee graph build " + name;
    if (const harness::NamedGraphConfig* named = config.find_graph(name); named != nullptr) {
        target.store_path = agentloop::graph_db_path(name);
        if (!file_exists(target.store_path)) {
            throw NavigationError(
                Kind::NotBuilt,
                "graph '" + name + "' has not been built yet -- " + target.build_hint);
        }
        for (const std::string& collection : named->collections) {
            target.databases[collection] = collection_db(collection);
        }
        target.hops = named->hops;
        target.max_entities = named->max_entities;
        return target;
    }
    target.store_path = collection_db(name);
    if (!file_exists(target.store_path)) {
        throw NavigationError(
            Kind::NotFound, "no graph or collection named '" + name + "'" + available_note(config));
    }
    target.databases[""] = target.store_path;
    if (const harness::EmbeddingConfig* entry = config.find_embedding(name); entry != nullptr) {
        target.hops = entry->graph.hops;
        target.max_entities = entry->graph.max_entities;
    }
    return target;
}

}  // namespace

std::vector<std::string> built_graph_names(const harness::Config& config) {
    std::vector<std::string> out;
    for (const auto& [name, unused] : config.graphs) {
        if (file_exists(agentloop::graph_db_path(name))) {
            out.push_back(name);
        }
    }
    // Collections on disk -- a database file directly under embeddings/ --
    // holding a graph of their own.
    std::vector<std::string> collections;
    std::error_code code;
    const std::filesystem::path dir = harness::embeddings_dir();
    if (std::filesystem::is_directory(dir, code)) {
        for (const auto& entry : std::filesystem::directory_iterator{dir, code}) {
            if (entry.is_regular_file(code) && entry.path().extension() == ".db") {
                collections.push_back(entry.path().stem().string());
            }
        }
    }
    std::ranges::sort(collections);
    for (const std::string& collection : collections) {
        if (config.find_graph(collection) != nullptr) {
            continue;
        }
        if (holds_graph(collection_db(collection))) {
            out.push_back(collection);
        }
    }
    return out;
}

GraphTarget resolve_graph_target(const harness::Config& config, const GraphSelection& selection) {
    const std::string graph = trim(selection.graph);
    const std::string collection = trim(selection.collection);
    if (!graph.empty() && !collection.empty()) {
        throw NavigationError(Kind::InvalidArgument, "name a graph or a collection, not both ('" +
                                                         graph + "', '" + collection + "')");
    }
    if (!graph.empty()) {
        return target_named(config, graph);
    }
    if (!collection.empty()) {
        if (!plain_name(collection)) {
            throw NavigationError(Kind::InvalidArgument,
                                  "'" + collection + "' is not a plain collection name");
        }
        // The retrieval precedence rule, decided once: a built named graph
        // covers its members.
        if (const std::optional<agentloop::CoveringGraph> covering =
                agentloop::named_graph_covering(config, collection, /*built_only=*/true);
            covering.has_value()) {
            return target_named(config, covering->name);
        }
        if (!file_exists(collection_db(collection))) {
            throw NavigationError(Kind::NotFound, "no collection named '" + collection + "'" +
                                                      available_note(config));
        }
        return target_named(config, collection);
    }
    const std::vector<std::string> built = built_graph_names(config);
    if (built.empty()) {
        throw NavigationError(
            Kind::NotBuilt,
            "no graph is built yet -- build one with 'apogee graph build "
            "<collection>' or 'apogee graph build --source <dir> --graph <name>'");
    }
    if (built.size() > 1) {
        throw NavigationError(
            Kind::InvalidArgument,
            std::to_string(built.size()) + " graphs are built -- name one: " + join(built));
    }
    return target_named(config, built.front());
}

OpenGraph::OpenGraph(GraphTarget target) : target_{std::move(target)} {
    const std::string hint = target_.build_hint.empty() ? "" : " -- " + target_.build_hint;
    if (!file_exists(target_.store_path)) {
        throw NavigationError(Kind::NotBuilt,
                              "graph '" + target_.name + "' has not been built yet" + hint);
    }
    store_ = std::make_unique<Store>(target_.store_path);
    if (!store_->has_graph()) {
        throw NavigationError(Kind::NotBuilt, "no graph built for '" + target_.name + "'" + hint);
    }
    for (const auto& [label, path] : target_.databases) {
        if (path == target_.store_path) {
            members_[label] = nullptr;  // the graph's own store, viewed below
            continue;
        }
        members_[label] = file_exists(path) ? std::make_unique<Store>(path) : nullptr;
    }
}

embedstore::MemberStores OpenGraph::members() const {
    embedstore::MemberStores out;
    for (const auto& [label, store] : members_) {
        const auto it = target_.databases.find(label);
        const bool own = it != target_.databases.end() && it->second == target_.store_path;
        out[label] = own ? store_.get() : store.get();
    }
    return out;
}

// ---- Addressing ------------------------------------------------------------------

ResolvedNode resolve_node(const Store& store, std::string_view graph, std::string_view address_in) {
    const std::string address = trim(address_in);
    if (address.empty()) {
        throw NavigationError(Kind::InvalidArgument,
                              "a node is required -- a name, kind:name, or path:line");
    }
    if (const std::optional<PathLine> at = split_path_line(address); at.has_value()) {
        if (std::optional<ResolvedNode> found = resolve_path_line(store, graph, address, *at);
            found.has_value()) {
            return std::move(*found);
        }
    }
    std::string bare = address;
    if (const std::size_t colon = kind_colon(address); colon != std::string::npos) {
        const std::string kind = lower(trim(address.substr(0, colon)));
        const std::string name = trim(address.substr(colon + 1));
        if (known_kind(kind) && !name.empty()) {
            if (std::optional<ResolvedNode> found =
                    one_of(of_kind(store.find_nodes(name), kind), graph, address, "kind");
                found.has_value()) {
                return std::move(*found);
            }
            if (embedstore::is_code_node_type(kind)) {
                if (std::optional<ResolvedNode> found =
                        one_of(of_kind(store.code_nodes_ending(name), kind), graph, address,
                               "unqualified");
                    found.has_value()) {
                    return std::move(*found);
                }
            }
            // Perhaps a name with a colon in it; else the part after the
            // kind is what the near matches are for.
            bare = name;
            if (std::optional<ResolvedNode> found =
                    one_of(store.find_nodes(address), graph, address, "exact");
                found.has_value()) {
                return std::move(*found);
            }
            throw_not_found(store, graph, bare);
        }
    }
    if (std::optional<ResolvedNode> found =
            one_of(store.find_nodes(address), graph, address, "exact");
        found.has_value()) {
        return std::move(*found);
    }
    if (std::optional<ResolvedNode> found =
            one_of(store.code_nodes_ending(address), graph, address, "unqualified");
        found.has_value()) {
        return std::move(*found);
    }
    throw_not_found(store, graph, bare);
}

// ---- path ------------------------------------------------------------------------

namespace {

/// How a walk reached a node: the node it came from, and the edge.
struct Reached {
    std::int64_t previous = 0;
    embedstore::Neighbor via;
};

/// Breadth first from `from` until `to` is reached or the hop cap is spent.
/// Each reached node remembers the node it was reached from and the edge, so
/// the first time `to` is reached the path is a shortest one -- and, the
/// edges at each node read in the store's fixed order, always the same one.
[[nodiscard]] std::map<std::int64_t, Reached> breadth_first(
    const Store& store, std::int64_t from, std::int64_t to,
    const std::vector<std::string>& relations, const PathRequest& request) {
    embedstore::NeighborFilter filter;
    filter.relations = relations;
    filter.direction = request.directed ? EdgeDirection::Out : EdgeDirection::Both;
    std::map<std::int64_t, Reached> reached;
    reached.emplace(from, Reached{});
    std::vector<std::int64_t> frontier{from};
    for (int hop = 1; hop <= request.max_hops && !frontier.empty(); ++hop) {
        std::vector<std::int64_t> next;
        for (const std::int64_t at : frontier) {
            for (embedstore::Neighbor& edge : store.node_neighbors(at, filter, 0)) {
                const std::int64_t peer = edge.peer_id;
                if (reached.contains(peer)) {
                    continue;
                }
                // An unresolved name may end a path, never carry one: walked
                // through, `.push_back` would connect every caller of it.
                const bool name = edge.peer_type == embedstore::kCodeKindName;
                reached.emplace(peer, Reached{.previous = at, .via = std::move(edge)});
                if (peer == to) {
                    return reached;
                }
                if (!name) {
                    next.push_back(peer);
                }
            }
        }
        frontier = std::move(next);
    }
    return reached;
}

/// The path `reached` holds from `from` to `to`, as nodes and steps.
void fill_path(const Store& store, const std::map<std::int64_t, Reached>& reached,
               std::int64_t from, std::int64_t to, PathResult& result) {
    std::vector<std::int64_t> ids{to};
    std::vector<const Reached*> hops;
    for (std::int64_t at = to; at != from;) {
        const Reached& step = reached.at(at);
        hops.push_back(&step);
        at = step.previous;
        ids.push_back(at);
    }
    std::ranges::reverse(ids);
    std::ranges::reverse(hops);
    const std::map<std::int64_t, GraphNode> by_id = nodes_for(store, ids);
    for (const std::int64_t id : ids) {
        result.nodes.push_back(node_ref(by_id.at(id)));
    }
    for (std::size_t i = 0; i < hops.size(); ++i) {
        const embedstore::Neighbor& edge = hops[i]->via;
        result.steps.push_back(
            PathStep{.from = result.nodes[i].name,
                     .to = result.nodes[i + 1].name,
                     .relation = edge.relation,
                     .origin = edge.origin,
                     .forward = edge.outgoing,
                     .weight = edge.weight,
                     .at = site_text(store, edge),
                     .description = clip(edge.description, kRelationDescriptionClip)});
    }
}

}  // namespace

PathResult shortest_path(const Store& store, std::string_view graph, const GraphNode& from,
                         const GraphNode& to, const PathRequest& request) {
    check_hop_cap(request.max_hops);
    PathResult result;
    result.graph = std::string{graph};
    result.from = node_ref(from);
    result.to = node_ref(to);
    result.directed = request.directed;
    result.max_hops = request.max_hops;
    for (const std::string& relation : request.relations) {
        const std::string word = trim(relation);
        if (!word.empty() && std::ranges::find(result.relations, word) == result.relations.end()) {
            result.relations.push_back(word);
        }
    }
    if (from.id == to.id) {
        result.found = true;
        result.nodes.push_back(result.from);
        return result;
    }

    const std::map<std::int64_t, Reached> reached =
        breadth_first(store, from.id, to.id, result.relations, request);
    if (!reached.contains(to.id)) {
        return result;
    }
    fill_path(store, reached, from.id, to.id, result);
    result.found = true;
    return result;
}

PathResult find_path(const OpenGraph& open, const PathRequest& request) {
    check_hop_cap(request.max_hops);
    const std::string& graph = open.target().name;
    const ResolvedNode from = resolve_node(open.store(), graph, request.from);
    const ResolvedNode to = resolve_node(open.store(), graph, request.to);
    PathResult result = shortest_path(open.store(), graph, from.node, to.node, request);
    result.from_matched = from.matched;
    result.to_matched = to.matched;
    return result;
}

nlohmann::json to_json(const PathResult& result) {
    nlohmann::json out{{"object", "graph.path"},
                       {"graph", result.graph},
                       {"from", to_json(result.from)},
                       {"to", to_json(result.to)},
                       {"matched", {{"from", result.from_matched}, {"to", result.to_matched}}},
                       {"directed", result.directed},
                       {"max_hops", result.max_hops}};
    if (!result.relations.empty()) {
        out["relations"] = result.relations;
    }
    out["found"] = result.found;
    out["hops"] = result.steps.size();
    nlohmann::json nodes = nlohmann::json::array();
    for (const NodeRef& node : result.nodes) {
        nodes.push_back(to_json(node));
    }
    out["nodes"] = std::move(nodes);
    nlohmann::json steps = nlohmann::json::array();
    for (const PathStep& step : result.steps) {
        nlohmann::json row{
            {"from", step.from},     {"relation", step.relation},
            {"origin", step.origin}, {"direction", step.forward ? "forward" : "backward"},
            {"to", step.to},         {"weight", step.weight}};
        if (!step.at.empty()) {
            row["at"] = step.at;
        }
        if (!step.description.empty()) {
            row["description"] = step.description;
        }
        steps.push_back(std::move(row));
    }
    out["steps"] = std::move(steps);
    if (!result.found) {
        out["note"] = "no path within " + std::to_string(result.max_hops) + " hops";
    }
    return out;
}

// ---- neighbors -------------------------------------------------------------------

Neighborhood neighborhood(const Store& store, std::string_view graph, const GraphNode& node,
                          const NeighborsRequest& request) {
    check_neighbor_cap(request.max_per_relation);
    Neighborhood out;
    out.graph = std::string{graph};
    out.node = node_ref(node);
    out.relation = trim(request.relation);
    out.direction = request.direction;
    out.max_per_relation = request.max_per_relation;
    read_groups(store, node, out.relation, out.direction, out.max_per_relation, out.degree,
                out.groups);
    return out;
}

Neighborhood find_neighbors(const OpenGraph& open, const NeighborsRequest& request) {
    check_neighbor_cap(request.max_per_relation);
    const ResolvedNode resolved = resolve_node(open.store(), open.target().name, request.node);
    Neighborhood out = neighborhood(open.store(), open.target().name, resolved.node, request);
    out.matched = resolved.matched;
    return out;
}

nlohmann::json to_json(const Neighborhood& result) {
    nlohmann::json out{{"object", "graph.neighbors"},
                       {"graph", result.graph},
                       {"node", to_json(result.node)},
                       {"matched", result.matched},
                       {"degree", to_json(result.degree)}};
    if (!result.relation.empty()) {
        out["relation"] = result.relation;
    }
    out["direction"] = std::string{to_string(result.direction)};
    out["max_per_relation"] = result.max_per_relation;
    out["relations"] = groups_json(result.groups);
    return out;
}

// ---- explain ---------------------------------------------------------------------

NodeCard node_card(const Store& store, const embedstore::MemberStores& stores,
                   std::string_view graph, const GraphNode& node, int max_per_relation) {
    check_neighbor_cap(max_per_relation);
    NodeCard card;
    card.graph = std::string{graph};
    card.node = node_ref(node);
    card.description = clip(node.description, kDescriptionClip);
    card.mentions = node.mention_count;
    card.max_per_relation = max_per_relation;
    read_groups(store, node, {}, EdgeDirection::Both, max_per_relation, card.degree,
                card.relations);

    // Provenance: a line for code, a chunk for prose -- the first few of each.
    if (embedstore::is_code_node_type(node.type)) {
        card.code_mentions = store.node_code_mentions(node.id, static_cast<int>(kCardMentions));
    }
    for (const embedstore::ChunkRef& ref :
         store.node_mention_refs(node.id, static_cast<int>(kCardMentions))) {
        ChunkMention mention;
        mention.collection = ref.collection;
        mention.chunk_id = ref.chunk_id;
        const auto it = stores.find(ref.collection);
        const std::optional<embedstore::Chunk> chunk = it == stores.end() || it->second == nullptr
                                                           ? std::nullopt
                                                           : it->second->chunk_by_id(ref.chunk_id);
        if (chunk.has_value()) {
            mention.source = chunk->source;
            mention.chunk = chunk->ordinal;
        } else {
            mention.missing = true;
        }
        card.chunk_mentions.push_back(std::move(mention));
    }

    for (const embedstore::GraphCommunity& community : store.node_communities(node.id)) {
        card.communities.push_back(CommunityRef{.id = community.id,
                                                .size = community.size,
                                                .summary = clip(community.summary, kSummaryClip)});
    }

    // The knowledge records attached to it: decision nodes one edge away.
    embedstore::NeighborFilter decisions;
    decisions.peer_type = std::string{embedstore::kNodeTypeDecision};
    std::vector<std::int64_t> ids;
    for (const embedstore::Neighbor& edge :
         store.node_neighbors(node.id, decisions, static_cast<int>(kCardDecisions))) {
        if (std::ranges::find(ids, edge.peer_id) == ids.end()) {
            ids.push_back(edge.peer_id);
        }
    }
    const std::map<std::int64_t, GraphNode> by_id = nodes_for(store, ids);
    for (const std::int64_t id : ids) {
        const auto it = by_id.find(id);
        if (it != by_id.end()) {
            card.decisions.push_back(
                DecisionRef{.node = node_ref(it->second),
                            .decision = clip(it->second.description, kDescriptionClip)});
        }
    }
    return card;
}

NodeCard explain_node(const OpenGraph& open, const CardRequest& request) {
    check_neighbor_cap(request.max_per_relation);
    const ResolvedNode resolved = resolve_node(open.store(), open.target().name, request.node);
    NodeCard card = node_card(open.store(), open.members(), open.target().name, resolved.node,
                              request.max_per_relation);
    card.matched = resolved.matched;
    return card;
}

nlohmann::json to_json(const NodeCard& card) {
    nlohmann::json out{{"object", "graph.node"},
                       {"graph", card.graph},
                       {"node", to_json(card.node)},
                       {"matched", card.matched}};
    if (!card.description.empty()) {
        out["description"] = card.description;
    }
    out["mentions"] = card.mentions;
    out["degree"] = to_json(card.degree);
    out["max_per_relation"] = card.max_per_relation;
    out["relations"] = groups_json(card.relations);
    nlohmann::json code = nlohmann::json::array();
    for (const embedstore::CodeMention& at : card.code_mentions) {
        nlohmann::json row{
            {"role", at.role}, {"member", at.collection}, {"file", at.file}, {"line", at.line}};
        if (at.end_line > at.line) {
            row["end_line"] = at.end_line;
        }
        code.push_back(std::move(row));
    }
    nlohmann::json chunks = nlohmann::json::array();
    for (const ChunkMention& mention : card.chunk_mentions) {
        nlohmann::json row{{"collection", mention.collection}};
        if (mention.missing) {
            row["chunk_id"] = mention.chunk_id;
            row["missing"] = true;
        } else {
            row["source"] = mention.source;
            row["chunk"] = mention.chunk;
        }
        chunks.push_back(std::move(row));
    }
    out["provenance"] = nlohmann::json{{"code", std::move(code)}, {"chunks", std::move(chunks)}};
    nlohmann::json communities = nlohmann::json::array();
    for (const CommunityRef& community : card.communities) {
        nlohmann::json row{{"id", community.id}, {"size", community.size}};
        if (!community.summary.empty()) {
            row["summary"] = community.summary;
        }
        communities.push_back(std::move(row));
    }
    out["communities"] = std::move(communities);
    nlohmann::json decisions = nlohmann::json::array();
    for (const DecisionRef& decision : card.decisions) {
        nlohmann::json row = to_json(decision.node);
        if (!decision.decision.empty()) {
            row["decision"] = decision.decision;
        }
        decisions.push_back(std::move(row));
    }
    out["decisions"] = std::move(decisions);
    return out;
}

// ---- query -----------------------------------------------------------------------

namespace {

/// The entities a question names: its exact name, else its words against
/// entity names -- at most half the cap, so the neighbourhood has room. An
/// unresolved name is never a seed (expansion's rule). Sets `out.match`.
[[nodiscard]] std::vector<GraphNode> query_seeds(const Store& store, QueryResult& out,
                                                 int max_entities) {
    const std::size_t seed_limit = static_cast<std::size_t>(std::max(1, max_entities / 2));
    std::vector<GraphNode> seeds;
    for (GraphNode& node : store.find_nodes(out.question)) {
        if (node.type != embedstore::kCodeKindName && seeds.size() < seed_limit) {
            seeds.push_back(std::move(node));
        }
    }
    if (!seeds.empty()) {
        out.match = "exact";
        return seeds;
    }
    for (embedstore::NodeResult& hit : store.search_node_names(
             out.question, static_cast<int>(seed_limit), /*include_unresolved=*/false)) {
        seeds.push_back(std::move(hit.node));
    }
    if (!seeds.empty()) {
        out.match = "names";
    }
    return seeds;
}

/// The expansion under its budget, as a turn's section is cut: whole lines,
/// the header first, a cut entity ending it before any relation.
void fit_expansion(const embedstore::Expansion& expansion, QueryResult& out) {
    std::size_t budget = out.budget;
    const auto fits = [&budget](const std::string& line) {
        const std::size_t cost = codepoints(line) + 1;
        if (cost > budget) {
            return false;
        }
        budget -= cost;
        return true;
    };
    if (!fits("[Knowledge graph: " + out.graph + "]")) {
        out.truncated = !expansion.entities.empty();
        return;
    }
    for (const embedstore::ExpandEntity& entity : expansion.entities) {
        if (!fits(agentloop::entity_line(entity.node))) {
            out.truncated = true;
            return;
        }
        out.entities.push_back(QueryEntity{.node = node_ref(entity.node),
                                           .hop = entity.hop,
                                           .description = entity.node.description});
    }
    for (const embedstore::ExpandEdge& edge : expansion.edges) {
        const std::string relation = edge.origin == embedstore::kOriginExtracted
                                         ? edge.relation + "·" + edge.origin
                                         : edge.relation;
        std::string line = edge.source_name + " —[" + relation + "]→ " + edge.target_name;
        if (!edge.description.empty()) {
            line += ": " + edge.description;
        }
        if (!fits(line)) {
            out.truncated = true;
            return;
        }
        out.relations.push_back(QueryRelation{.from = edge.source_name,
                                              .to = edge.target_name,
                                              .relation = edge.relation,
                                              .origin = edge.origin,
                                              .description = edge.description});
    }
}

}  // namespace

QueryResult query_graph(const Store& store, std::string_view graph, std::string_view question,
                        int hops, int max_entities) {
    if (hops < 1 || hops > embedstore::kMaxGraphHops) {
        throw NavigationError(Kind::InvalidArgument,
                              "a query walks 1 or " + std::to_string(embedstore::kMaxGraphHops) +
                                  " hops (got " + std::to_string(hops) + ")");
    }
    if (max_entities < 1 || max_entities > kMaxQueryEntities) {
        throw NavigationError(Kind::InvalidArgument, "the entity cap must be between 1 and " +
                                                         std::to_string(kMaxQueryEntities) +
                                                         " (got " + std::to_string(max_entities) +
                                                         ")");
    }
    QueryResult out;
    out.graph = std::string{graph};
    out.question = trim(question);
    out.hops = hops;
    out.max_entities = max_entities;
    out.budget = agentloop::kGraphSectionBudget;
    out.match = "none";
    if (out.question.empty()) {
        throw NavigationError(Kind::InvalidArgument, "a question is required");
    }

    const std::vector<GraphNode> seeds = query_seeds(store, out, max_entities);
    if (seeds.empty()) {
        return out;
    }
    std::vector<std::int64_t> seed_ids;
    for (const GraphNode& node : seeds) {
        out.seeds.push_back(node_ref(node));
        seed_ids.push_back(node.id);
    }
    fit_expansion(store.graph_expand({}, seed_ids, hops, max_entities), out);
    return out;
}

QueryResult run_query(const OpenGraph& open, const QueryRequest& request) {
    const GraphTarget& target = open.target();
    return query_graph(open.store(), target.name, request.question,
                       request.hops > 0 ? request.hops : target.hops,
                       request.max_entities > 0 ? request.max_entities : target.max_entities);
}

nlohmann::json to_json(const QueryResult& result) {
    nlohmann::json seeds = nlohmann::json::array();
    for (const NodeRef& node : result.seeds) {
        seeds.push_back(to_json(node));
    }
    nlohmann::json entities = nlohmann::json::array();
    for (const QueryEntity& entity : result.entities) {
        nlohmann::json row = to_json(entity.node);
        row["hop"] = entity.hop;
        if (!entity.description.empty()) {
            row["description"] = entity.description;
        }
        entities.push_back(std::move(row));
    }
    nlohmann::json relations = nlohmann::json::array();
    for (const QueryRelation& relation : result.relations) {
        nlohmann::json row{{"from", relation.from},
                           {"relation", relation.relation},
                           {"origin", relation.origin},
                           {"to", relation.to}};
        if (!relation.description.empty()) {
            row["description"] = relation.description;
        }
        relations.push_back(std::move(row));
    }
    return nlohmann::json{{"object", "graph.query"},
                          {"graph", result.graph},
                          {"question", result.question},
                          {"match", result.match},
                          {"hops", result.hops},
                          {"max_entities", result.max_entities},
                          {"budget", result.budget},
                          {"seeds", std::move(seeds)},
                          {"entities", std::move(entities)},
                          {"relations", std::move(relations)},
                          {"truncated", result.truncated}};
}

}  // namespace apogee::graph
