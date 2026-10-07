#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/store.h"
#include "graph/navigate.h"

/// The architecture report (27m): the one-page read of a built graph a
/// newcomer starts from -- its communities with the summaries they already
/// have, its hubs, the extracted/inferred mix, what links its members, the
/// decisions it carries with what they concern, and the entities nothing
/// connects.
///
/// **Assembled from the store, never generated.** No closure, no model:
/// community summaries were written and paid for at `graph communities`
/// time and are read here; a community clustered with no model says so,
/// one by one. `apogee graph report` succeeds with zero backends.
///
/// **One source of truth.** A hub is what `rank_by_degree` says -- 27l's
/// `Degree`, every relation touching an entity either way -- and every
/// entity, degree and relation group here is 27l's payload (`NodeRef`,
/// `Degree`, `neighborhood`'s groups), serialised by its own `to_json`. The
/// HTML export ranks and carries cards through the same functions.
///
/// **Top-N with counts, never the whole enumeration.** Each list is capped
/// and says how many it had; GraphML is the uncapped escape hatch.
///
/// **What it carries:** names, kinds, relations, descriptions, summaries and
/// `file:line`s -- never a chunk's text, and nothing from a private layout
/// row: a report is made to leave the machine.
///
/// Pure over a store: the Markdown is `render/graph_report`'s, from
/// `to_json` (the `human_summary`-last discipline).
namespace apogee::graph {

/// The hubs listed: the highest-degree entities.
inline constexpr std::size_t kReportHubs = 10;
/// The orphans listed: the most-mentioned entities with no relation.
inline constexpr std::size_t kReportOrphans = 10;
/// The communities listed, largest first, and the members each names.
inline constexpr std::size_t kReportCommunities = 10;
inline constexpr std::size_t kReportCommunityMembers = 5;
/// The decision records listed, most connected first.
inline constexpr std::size_t kReportDecisions = 10;
/// Member pairs, relations across members and shared entities listed.
inline constexpr std::size_t kReportLinks = 10;
/// Neighbours a hub or a decision names per relation.
inline constexpr int kReportNeighbors = 3;
/// An orphan's description, in codepoints.
inline constexpr std::size_t kReportOrphanClip = 200;

/// One of the highest-degree entities and its relations, counted, each
/// naming its first few neighbours.
struct ReportHub {
    NodeRef node;
    std::int64_t mentions = 0;
    Degree degree;
    std::vector<NeighborGroup> relations;
};

/// One stored community: its summary as `graph communities` wrote it (empty
/// for one clustered with no model) and its most-mentioned members.
struct ReportCommunity {
    std::int64_t id = 0;
    std::int64_t size = 0;
    std::string summary;
    std::vector<NodeRef> members;
};

/// Two members and what joins them: relations whose endpoints share no
/// member, one stated in each, and entities stated in both.
struct MemberLink {
    std::string from;
    std::string to;
    std::int64_t relations = 0;
    std::int64_t shared = 0;
};

/// A relation between entities stated in different members.
struct CrossingRelation {
    NodeRef from;
    std::vector<std::string> from_members;
    std::string relation;
    std::string origin;
    std::int64_t weight = 1;
    NodeRef to;
    std::vector<std::string> to_members;
};

/// An entity stated in more than one member.
struct SharedEntity {
    NodeRef node;
    std::vector<std::string> members;
    Degree degree;
};

/// A decision record's node and what it concerns -- its outgoing relations.
struct ReportDecision {
    NodeRef node;
    std::string decision;
    std::vector<NeighborGroup> relations;
};

/// An entity nothing relates to: worth asking about.
struct ReportOrphan {
    NodeRef node;
    std::int64_t mentions = 0;
    std::string description;
};

struct GraphReport {
    std::string graph;

    // The overview.
    /// The members the store states, sorted: collections' labels and source
    /// trees' -- `""` alone for a collection's own graph.
    std::vector<std::string> members;
    std::int64_t entities = 0;
    std::int64_t relations = 0;
    std::map<std::string, std::int64_t> by_type;
    std::map<std::string, std::int64_t> code_files;
    std::int64_t unresolved_names = 0;

    // The origin mix.
    std::int64_t extracted = 0;
    std::int64_t inferred = 0;

    // The hubs: of `ranked` entities, the first `kReportHubs`.
    std::size_t ranked = 0;
    std::vector<ReportHub> hubs;

    // The communities.
    std::int64_t communities_total = 0;
    std::int64_t communities_unsummarised = 0;
    std::vector<ReportCommunity> communities;

    // The links between members -- a named graph of two or more.
    bool cross_member = false;
    std::vector<MemberLink> links;
    std::size_t links_total = 0;
    std::vector<CrossingRelation> crossings;
    std::size_t crossings_total = 0;
    std::vector<SharedEntity> shared;
    std::size_t shared_total = 0;

    // The decisions.
    std::vector<ReportDecision> decisions;
    std::size_t decisions_total = 0;

    // The orphans.
    std::vector<ReportOrphan> orphans;
    std::size_t orphans_total = 0;

    /// One paragraph of the facts above, the section rendered last.
    std::string human_summary;
};

/// Assembles the report over `store`, called `graph` in every payload.
/// Reads only; never a model.
[[nodiscard]] GraphReport build_report(const embedstore::Store& store, std::string_view graph);

/// The same over an opened graph.
[[nodiscard]] GraphReport build_report(const OpenGraph& open);

/// The report's document, `{"object":"graph.report",…}` -- what
/// `graph report --output-format json` prints and the Markdown renders.
[[nodiscard]] nlohmann::json to_json(const GraphReport& report);

}  // namespace apogee::graph
