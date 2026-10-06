#include "graph/report.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "embedstore/graph.h"

namespace apogee::graph {
namespace {

using embedstore::Store;

[[nodiscard]] std::string count_of(std::int64_t count, std::string_view one,
                                   std::string_view many) {
    return std::to_string(count) + " " + std::string{count == 1 ? one : many};
}

[[nodiscard]] std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

/// Whether two sorted lists share an element.
[[nodiscard]] bool overlap(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            return true;
        }
        if (a[i] < b[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    return false;
}

void fill_hubs(const Store& store, std::string_view graph, const std::vector<RankedNode>& ranked,
               GraphReport& out) {
    out.ranked = ranked.size();
    for (std::size_t i = 0; i < ranked.size() && i < kReportHubs; ++i) {
        const Neighborhood around =
            neighborhood(store, graph, ranked[i].node,
                         NeighborsRequest{.node = {},
                                          .relation = {},
                                          .direction = embedstore::EdgeDirection::Both,
                                          .max_per_relation = kReportNeighbors});
        out.hubs.push_back(ReportHub{.node = around.node,
                                     .mentions = ranked[i].node.mention_count,
                                     .degree = around.degree,
                                     .relations = around.groups});
    }
}

void fill_communities(const Store& store, GraphReport& out) {
    const std::vector<embedstore::GraphCommunity> stored = store.graph_communities();
    out.communities_total = static_cast<std::int64_t>(stored.size());
    for (const embedstore::GraphCommunity& community : stored) {
        if (community.summary.empty()) {
            ++out.communities_unsummarised;
        }
    }
    for (std::size_t i = 0; i < stored.size() && i < kReportCommunities; ++i) {
        ReportCommunity entry{.id = stored[i].id,
                              .size = stored[i].size,
                              .summary = stored[i].summary,
                              .members = {}};
        const std::vector<embedstore::GraphNode> nodes = store.community_members(stored[i].id);
        for (std::size_t m = 0; m < nodes.size() && m < kReportCommunityMembers; ++m) {
            entry.members.push_back(node_ref(nodes.at(m)));
        }
        out.communities.push_back(std::move(entry));
    }
}

/// What joins a named graph's members: relations whose endpoints share no
/// member, and entities stated in more than one.
void fill_links(const Store& store, const std::vector<RankedNode>& ranked,
                const std::map<std::int64_t, std::vector<std::string>>& stated_in,
                GraphReport& out) {
    std::map<std::int64_t, const RankedNode*> by_id;
    for (const RankedNode& entry : ranked) {
        by_id.emplace(entry.node.id, &entry);
    }
    std::map<std::pair<std::string, std::string>, MemberLink> pairs;
    const auto pair_of = [&pairs](const std::string& a, const std::string& b) -> MemberLink& {
        const std::pair<std::string, std::string> key = a < b ? std::pair{a, b} : std::pair{b, a};
        MemberLink& link = pairs[key];
        link.from = key.first;
        link.to = key.second;
        return link;
    };

    // `all_edges` leaves the unresolved names out: a name is not structure.
    for (const embedstore::GraphEdge& edge : store.all_edges()) {
        const auto source = stated_in.find(edge.source_id);
        const auto target = stated_in.find(edge.target_id);
        const auto from = by_id.find(edge.source_id);
        const auto to = by_id.find(edge.target_id);
        if (source == stated_in.end() || target == stated_in.end() || from == by_id.end() ||
            to == by_id.end() || overlap(source->second, target->second)) {
            continue;
        }
        for (const std::string& a : source->second) {
            for (const std::string& b : target->second) {
                ++pair_of(a, b).relations;
            }
        }
        out.crossings.push_back(CrossingRelation{.from = node_ref(from->second->node),
                                                 .from_members = source->second,
                                                 .relation = edge.relation,
                                                 .origin = edge.origin,
                                                 .weight = edge.weight,
                                                 .to = node_ref(to->second->node),
                                                 .to_members = target->second});
    }
    out.crossings_total = out.crossings.size();
    std::ranges::stable_sort(out.crossings,
                             [](const CrossingRelation& a, const CrossingRelation& b) {
                                 if (a.weight != b.weight) {
                                     return a.weight > b.weight;
                                 }
                                 return std::tie(a.from.name, a.relation, a.to.name) <
                                        std::tie(b.from.name, b.relation, b.to.name);
                             });
    if (out.crossings.size() > kReportLinks) {
        out.crossings.resize(kReportLinks);
    }

    for (const auto& [id, labels] : stated_in) {
        const auto entry = by_id.find(id);
        if (labels.size() < 2 || entry == by_id.end()) {
            continue;
        }
        for (std::size_t i = 0; i < labels.size(); ++i) {
            for (std::size_t j = i + 1; j < labels.size(); ++j) {
                ++pair_of(labels[i], labels[j]).shared;
            }
        }
        out.shared.push_back(SharedEntity{.node = node_ref(entry->second->node),
                                          .members = labels,
                                          .degree = entry->second->degree});
    }
    out.shared_total = out.shared.size();
    std::ranges::stable_sort(out.shared, [](const SharedEntity& a, const SharedEntity& b) {
        if (a.members.size() != b.members.size()) {
            return a.members.size() > b.members.size();
        }
        if (a.degree.total != b.degree.total) {
            return a.degree.total > b.degree.total;
        }
        return std::tie(a.node.name, a.node.type) < std::tie(b.node.name, b.node.type);
    });
    if (out.shared.size() > kReportLinks) {
        out.shared.resize(kReportLinks);
    }

    for (auto& [unused, link] : pairs) {
        out.links.push_back(std::move(link));
    }
    out.links_total = out.links.size();
    std::ranges::stable_sort(out.links, [](const MemberLink& a, const MemberLink& b) {
        return a.relations + a.shared > b.relations + b.shared;
    });
    if (out.links.size() > kReportLinks) {
        out.links.resize(kReportLinks);
    }
}

void fill_decisions(const Store& store, std::string_view graph,
                    const std::vector<RankedNode>& ranked, GraphReport& out) {
    for (const RankedNode& entry : ranked) {
        if (entry.node.type != embedstore::kNodeTypeDecision) {
            continue;
        }
        ++out.decisions_total;
        if (out.decisions.size() >= kReportDecisions) {
            continue;
        }
        const Neighborhood around =
            neighborhood(store, graph, entry.node,
                         NeighborsRequest{.node = {},
                                          .relation = {},
                                          .direction = embedstore::EdgeDirection::Out,
                                          .max_per_relation = kReportNeighbors});
        out.decisions.push_back(
            ReportDecision{.node = around.node,
                           .decision = clip_text(entry.node.description, kDescriptionClip),
                           .relations = around.groups});
    }
}

void fill_orphans(const std::vector<RankedNode>& ranked, GraphReport& out) {
    // The ranking puts every orphan last, the most mentioned first.
    for (const RankedNode& entry : ranked) {
        if (entry.degree.total != 0) {
            continue;
        }
        ++out.orphans_total;
        if (out.orphans.size() < kReportOrphans) {
            out.orphans.push_back(
                ReportOrphan{.node = node_ref(entry.node),
                             .mentions = entry.node.mention_count,
                             .description = clip_text(entry.node.description, kReportOrphanClip)});
        }
    }
}

/// "none has a summary", "1 has a summary", "N have a summary".
[[nodiscard]] std::string summary_count(std::int64_t summarised) {
    if (summarised == 0) {
        return "none has a summary";
    }
    return summarised == 1 ? std::string{"1 has a summary"}
                           : std::to_string(summarised) + " have a summary";
}

[[nodiscard]] std::string summarize(const GraphReport& report) {
    std::string out = "Graph \"" + report.graph + "\" holds " +
                      count_of(report.entities, "entity", "entities") + " and " +
                      count_of(report.relations, "relation", "relations");
    if (report.cross_member) {
        out += " across " + std::to_string(report.members.size()) + " members (" +
               joined(report.members) + ")";
    }
    out += ".";
    if (report.relations > 0) {
        if (report.inferred == 0) {
            out += " Every relation was parsed from source.";
        } else if (report.extracted == 0) {
            out += " Every relation was asserted by a model.";
        } else {
            out += " " + std::to_string(report.extracted) + " were parsed from source and " +
                   std::to_string(report.inferred) + " asserted by a model.";
        }
    }
    if (!report.hubs.empty() && report.hubs.front().degree.total > 0) {
        const ReportHub& top = report.hubs.front();
        out += " Its busiest entity is " + top.node.name + " (" + top.node.type + "), with " +
               count_of(top.degree.total, "relation", "relations") + ".";
    }
    if (report.communities_total == 0) {
        out += " No communities are stored.";
    } else {
        const std::int64_t summarised = report.communities_total - report.communities_unsummarised;
        out += " " +
               count_of(report.communities_total, "community is stored", "communities are stored") +
               "; " + summary_count(summarised) + ".";
    }
    if (report.cross_member) {
        out += " " +
               count_of(static_cast<std::int64_t>(report.crossings_total), "relation crosses",
                        "relations cross") +
               " between members.";
    }
    if (report.decisions_total > 0) {
        out += " " +
               count_of(static_cast<std::int64_t>(report.decisions_total),
                        "decision record is attached", "decision records are attached") +
               ".";
    }
    if (report.orphans_total > 0) {
        out += " " +
               count_of(static_cast<std::int64_t>(report.orphans_total), "entity stands alone",
                        "entities stand alone") +
               ", with no relation.";
    }
    return out;
}

[[nodiscard]] nlohmann::json refs_json(const std::vector<NodeRef>& nodes) {
    nlohmann::json out = nlohmann::json::array();
    for (const NodeRef& node : nodes) {
        out.push_back(to_json(node));
    }
    return out;
}

[[nodiscard]] nlohmann::json groups_json(const std::vector<NeighborGroup>& groups) {
    nlohmann::json out = nlohmann::json::array();
    for (const NeighborGroup& group : groups) {
        out.push_back(to_json(group));
    }
    return out;
}

[[nodiscard]] nlohmann::json links_json(const GraphReport& report) {
    nlohmann::json pairs = nlohmann::json::array();
    for (const MemberLink& link : report.links) {
        pairs.push_back(nlohmann::json{{"from", link.from},
                                       {"to", link.to},
                                       {"relations", link.relations},
                                       {"shared", link.shared}});
    }
    nlohmann::json crossings = nlohmann::json::array();
    for (const CrossingRelation& crossing : report.crossings) {
        nlohmann::json from = to_json(crossing.from);
        from["members"] = crossing.from_members;
        nlohmann::json to = to_json(crossing.to);
        to["members"] = crossing.to_members;
        crossings.push_back(nlohmann::json{{"from", std::move(from)},
                                           {"relation", crossing.relation},
                                           {"origin", crossing.origin},
                                           {"weight", crossing.weight},
                                           {"to", std::move(to)}});
    }
    nlohmann::json shared = nlohmann::json::array();
    for (const SharedEntity& entity : report.shared) {
        shared.push_back(nlohmann::json{{"node", to_json(entity.node)},
                                        {"members", entity.members},
                                        {"degree", to_json(entity.degree)}});
    }
    return nlohmann::json{
        {"pairs", nlohmann::json{{"total", report.links_total}, {"shown", std::move(pairs)}}},
        {"crossings",
         nlohmann::json{{"total", report.crossings_total}, {"shown", std::move(crossings)}}},
        {"shared", nlohmann::json{{"total", report.shared_total}, {"shown", std::move(shared)}}}};
}

}  // namespace

GraphReport build_report(const Store& store, std::string_view graph) {
    GraphReport out;
    out.graph = std::string{graph};
    const embedstore::GraphStats stats = store.graph_stats();
    out.entities = stats.nodes;
    out.relations = stats.edges;
    out.by_type = stats.nodes_by_type;
    out.code_files = stats.code_files_by_language;
    out.unresolved_names = stats.unresolved_names;
    out.extracted = stats.edges_extracted;
    out.inferred = stats.edges_inferred;

    const std::map<std::int64_t, std::vector<std::string>> stated_in = store.node_members();
    for (const auto& [unused, labels] : stated_in) {
        for (const std::string& label : labels) {
            if (std::ranges::find(out.members, label) == out.members.end()) {
                out.members.push_back(label);
            }
        }
    }
    std::ranges::sort(out.members);
    // A collection's own graph is the one `""` member; a named graph labels
    // every row with its member.
    out.cross_member = out.members.size() > 1;

    const std::vector<RankedNode> ranked = rank_by_degree(store);
    fill_hubs(store, out.graph, ranked, out);
    fill_communities(store, out);
    if (out.cross_member) {
        fill_links(store, ranked, stated_in, out);
    }
    fill_decisions(store, out.graph, ranked, out);
    fill_orphans(ranked, out);
    out.human_summary = summarize(out);
    return out;
}

GraphReport build_report(const OpenGraph& open) {
    return build_report(open.store(), open.target().name);
}

nlohmann::json to_json(const GraphReport& report) {
    nlohmann::json overview{{"entities", report.entities},
                            {"relations", report.relations},
                            {"by_type", report.by_type},
                            {"members", report.members},
                            {"unresolved_names", report.unresolved_names}};
    if (!report.code_files.empty()) {
        overview["code_files"] = report.code_files;
    }

    nlohmann::json hubs = nlohmann::json::array();
    for (const ReportHub& hub : report.hubs) {
        hubs.push_back(nlohmann::json{{"node", to_json(hub.node)},
                                      {"mentions", hub.mentions},
                                      {"degree", to_json(hub.degree)},
                                      {"relations", groups_json(hub.relations)}});
    }

    nlohmann::json communities = nlohmann::json::array();
    for (const ReportCommunity& community : report.communities) {
        nlohmann::json row{{"id", community.id}, {"size", community.size}};
        if (!community.summary.empty()) {
            row["summary"] = community.summary;
        }
        row["members"] = refs_json(community.members);
        communities.push_back(std::move(row));
    }

    nlohmann::json decisions = nlohmann::json::array();
    for (const ReportDecision& decision : report.decisions) {
        nlohmann::json row{{"node", to_json(decision.node)}};
        if (!decision.decision.empty()) {
            row["decision"] = decision.decision;
        }
        row["relations"] = groups_json(decision.relations);
        decisions.push_back(std::move(row));
    }

    nlohmann::json orphans = nlohmann::json::array();
    for (const ReportOrphan& orphan : report.orphans) {
        nlohmann::json row{{"node", to_json(orphan.node)}, {"mentions", orphan.mentions}};
        if (!orphan.description.empty()) {
            row["description"] = orphan.description;
        }
        orphans.push_back(std::move(row));
    }

    nlohmann::json out{
        {"object", "graph.report"},
        {"graph", report.graph},
        {"overview", std::move(overview)},
        {"origin", nlohmann::json{{"relations", report.relations},
                                  {"extracted", report.extracted},
                                  {"inferred", report.inferred}}},
        {"hubs",
         nlohmann::json{
             {"metric", "degree"}, {"ranked", report.ranked}, {"shown", std::move(hubs)}}},
        {"communities", nlohmann::json{{"total", report.communities_total},
                                       {"unsummarised", report.communities_unsummarised},
                                       {"shown", std::move(communities)}}},
        {"decisions",
         nlohmann::json{{"total", report.decisions_total}, {"shown", std::move(decisions)}}},
        {"orphans", nlohmann::json{{"total", report.orphans_total}, {"shown", std::move(orphans)}}},
        {"human_summary", report.human_summary}};
    if (report.cross_member) {
        out["links"] = links_json(report);
    }
    return out;
}

}  // namespace apogee::graph
