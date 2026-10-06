#include "graph/export_html.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include "embedstore/graph.h"
#include "graph/html_template.h"

namespace apogee::graph {
namespace {

using Kind = NavigationError::Kind;

[[nodiscard]] std::string cap_sentence(std::size_t shown, std::size_t entities, bool capped,
                                       std::size_t limit, std::int64_t names) {
    std::string left_out;
    if (names == 1) {
        left_out = "; the 1 unresolved name is left out";
    } else if (names > 1) {
        left_out = "; the " + std::to_string(names) + " unresolved names are left out";
    }
    if (capped) {
        return "Showing the " + std::to_string(shown) + " highest-degree of " +
               std::to_string(entities) + " entities -- capped at " + std::to_string(limit) +
               " by degree rank" + left_out + ". apogee graph export graphml carries every one.";
    }
    return "Showing all " + std::to_string(shown) + (shown == 1 ? " entity" : " entities") +
           left_out + ".";
}

}  // namespace

std::string embeddable_json(const nlohmann::json& data) {
    const std::string dumped = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    std::string out;
    out.reserve(dumped.size() + (dumped.size() / 8));
    // Outside a string JSON has none of these characters, so every one
    // replaced sits inside a string, where each escape means the character.
    for (const char c : dumped) {
        switch (c) {
            case '<':
                out += "\\u003c";
                break;
            case '>':
                out += "\\u003e";
                break;
            case '&':
                out += "\\u0026";
                break;
            case '/':
                out += "\\/";
                break;
            default:
                out += c;
        }
    }
    return out;
}

std::string html_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

std::string fill_template(std::string_view page, const std::map<std::string, std::string>& values) {
    std::string out;
    std::size_t size = page.size();
    for (const auto& [unused, value] : values) {
        size += value.size();
    }
    out.reserve(size);
    std::size_t at = 0;
    while (true) {
        const std::size_t open = page.find("{{", at);
        if (open == std::string_view::npos) {
            out.append(page.substr(at));
            return out;
        }
        const std::size_t close = page.find("}}", open + 2);
        if (close == std::string_view::npos) {
            throw std::logic_error("the page has an unterminated placeholder at byte " +
                                   std::to_string(open));
        }
        const std::string name{page.substr(open + 2, close - open - 2)};
        const auto value = values.find(name);
        if (value == values.end()) {
            throw std::logic_error("the page names a placeholder nothing fills: {{" + name + "}}");
        }
        out.append(page.substr(at, open - at));
        out.append(value->second);
        at = close + 2;
    }
}

HtmlExport export_html(const embedstore::Store& store, const embedstore::MemberStores& members,
                       std::string_view graph, const HtmlOptions& options) {
    if (options.max_nodes < 1 || options.max_nodes > kHtmlMaxNodes) {
        throw NavigationError(Kind::InvalidArgument, "the node cap must be between 1 and " +
                                                         std::to_string(kHtmlMaxNodes) + " (got " +
                                                         std::to_string(options.max_nodes) + ")");
    }
    const embedstore::GraphStats stats = store.graph_stats();
    const std::vector<RankedNode> ranked = rank_by_degree(store);

    HtmlExport out;
    out.entities = ranked.size();
    out.shown = std::min(ranked.size(), options.max_nodes);
    out.capped = ranked.size() > options.max_nodes;
    out.unresolved_names = stats.unresolved_names;
    out.cap_note = cap_sentence(out.shown, out.entities, out.capped, options.max_nodes,
                                stats.unresolved_names);

    // The cards: 27l's payload for each entity drawn, in rank order.
    std::map<std::int64_t, std::size_t> index;
    nlohmann::json nodes = nlohmann::json::array();
    std::map<std::int64_t, CommunityRef> communities;
    for (std::size_t i = 0; i < out.shown; ++i) {
        const NodeCard card = node_card(store, members, graph, ranked[i].node, kHtmlCardNeighbors);
        for (const CommunityRef& community : card.communities) {
            communities.emplace(community.id, community);
        }
        index.emplace(ranked[i].node.id, i);
        nodes.push_back(to_json(card));
    }

    // The relations between them -- an unresolved name's never among them.
    std::map<std::string, std::size_t> relation_index;
    nlohmann::json relations = nlohmann::json::array();
    nlohmann::json edges = nlohmann::json::array();
    for (const embedstore::GraphEdge& edge : store.all_edges()) {
        const auto source = index.find(edge.source_id);
        const auto target = index.find(edge.target_id);
        if (source == index.end() || target == index.end() || source->second == target->second) {
            continue;
        }
        const auto [slot, added] = relation_index.emplace(edge.relation, relation_index.size());
        if (added) {
            relations.push_back(edge.relation);
        }
        edges.push_back(
            nlohmann::json::array({source->second, target->second, slot->second,
                                   edge.origin == embedstore::kOriginExtracted ? 1 : 0}));
    }
    out.relations = edges.size();

    std::vector<CommunityRef> by_size;
    by_size.reserve(communities.size());
    for (auto& [unused, community] : communities) {
        by_size.push_back(std::move(community));
    }
    std::ranges::stable_sort(
        by_size, [](const CommunityRef& a, const CommunityRef& b) { return a.size > b.size; });
    nlohmann::json legend = nlohmann::json::array();
    for (const CommunityRef& community : by_size) {
        nlohmann::json row{{"id", community.id}, {"size", community.size}};
        if (!community.summary.empty()) {
            row["summary"] = community.summary;
        }
        legend.push_back(std::move(row));
    }

    const nlohmann::json data{{"object", "graph.html"},
                              {"graph", graph},
                              {"cap", nlohmann::json{{"metric", "degree"},
                                                     {"limit", options.max_nodes},
                                                     {"entities", out.entities},
                                                     {"shown", out.shown},
                                                     {"capped", out.capped},
                                                     {"unresolved_names", out.unresolved_names},
                                                     {"card_neighbors", kHtmlCardNeighbors},
                                                     {"note", out.cap_note}}},
                              {"origin", nlohmann::json{{"extracted", stats.edges_extracted},
                                                        {"inferred", stats.edges_inferred}}},
                              {"communities", std::move(legend)},
                              {"relations", std::move(relations)},
                              {"edges", std::move(edges)},
                              {"nodes", std::move(nodes)}};

    out.html = fill_template(html_template(), {{"title", html_escape(graph)},
                                               {"cap_note", html_escape(out.cap_note)},
                                               {"data", embeddable_json(data)}});
    return out;
}

HtmlExport export_html(const OpenGraph& open, const HtmlOptions& options) {
    return export_html(open.store(), open.members(), open.target().name, options);
}

}  // namespace apogee::graph
