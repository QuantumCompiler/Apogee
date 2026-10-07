#include "graph/export_mermaid.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "embedstore/graph.h"

namespace apogee::graph {
namespace {

using Kind = NavigationError::Kind;

/// One edge as drawn: the entities' places in the ranking.
struct Drawn {
    std::size_t from = 0;
    std::size_t to = 0;
    std::string relation;
    bool inferred = false;
    std::int64_t weight = 1;
};

/// The last part of a code name: after its last `::` or `.`, or a file's
/// last `/`.
[[nodiscard]] std::string short_name(const embedstore::GraphNode& node) {
    const std::string& name = node.name;
    std::size_t cut = std::string::npos;
    if (node.type == embedstore::kCodeKindFile) {
        cut = name.rfind('/');
    } else {
        const std::size_t scope = name.rfind("::");
        const std::size_t dot = name.rfind('.');
        if (scope != std::string::npos) {
            cut = scope + 1;
        }
        if (dot != std::string::npos && (cut == std::string::npos || dot > cut)) {
            cut = dot;
        }
    }
    if (cut == std::string::npos || cut + 1 >= name.size()) {
        return name;
    }
    return name.substr(cut + 1);
}

/// A comment's text: one line, and never a directive's brace.
[[nodiscard]] std::string comment_text(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\n' || c == '\r') {
            out += ' ';
        } else if (c == '{') {
            out += '(';
        } else if (c == '}') {
            out += ')';
        } else {
            out += c;
        }
    }
    return out;
}

[[nodiscard]] std::string count_of(std::size_t count, std::string_view one, std::string_view many) {
    return std::to_string(count) + " " + std::string{count == 1 ? one : many};
}

}  // namespace

std::string mermaid_label(std::string_view text) {
    std::string out;
    bool space = false;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0 ||
            static_cast<unsigned char>(c) < 0x20U) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        switch (c) {
            case '"':
                out += "#34;";
                break;
            case '#':
                out += "#35;";
                break;
            case '&':
                out += "#38;";
                break;
            case '<':
                out += "#60;";
                break;
            case '>':
                out += "#62;";
                break;
            case '`':
                out += "#96;";
                break;
            case '|':
                out += "#124;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

MermaidExport export_mermaid(const embedstore::Store& store, std::string_view graph,
                             const MermaidOptions& options) {
    if (options.max_nodes < 1 || options.max_nodes > kMermaidMaxNodes) {
        throw NavigationError(Kind::InvalidArgument, "the node cap must be between 1 and " +
                                                         std::to_string(kMermaidMaxNodes) +
                                                         " (got " +
                                                         std::to_string(options.max_nodes) + ")");
    }
    MermaidExport out;
    const std::vector<RankedNode> ranked = rank_by_degree(store);
    const std::vector<embedstore::GraphEdge> edges = store.all_edges();
    std::map<std::int64_t, std::size_t> rank_of;
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        rank_of.emplace(ranked[i].node.id, i);
    }
    const auto is_function = [&](std::int64_t id) {
        const auto at = rank_of.find(id);
        return at != rank_of.end() && ranked[at->second].node.type == embedstore::kCodeKindFunction;
    };

    // A code graph's call flow when it has one: functions calling functions.
    std::set<std::int64_t> calling;
    for (const embedstore::GraphEdge& edge : edges) {
        if (edge.relation == embedstore::kCodeRelationCalls && edge.source_id != edge.target_id &&
            is_function(edge.source_id) && is_function(edge.target_id)) {
            calling.insert(edge.source_id);
            calling.insert(edge.target_id);
        }
    }
    out.call_flow = !calling.empty();

    // The selection: the top of the one ranking.
    std::map<std::int64_t, std::size_t> drawn_at;
    std::vector<const RankedNode*> drawn;
    for (const RankedNode& entry : ranked) {
        const bool eligible =
            out.call_flow ? calling.contains(entry.node.id) : entry.degree.total > 0;
        if (!eligible) {
            continue;
        }
        ++out.candidates;
        if (drawn.size() < options.max_nodes) {
            drawn_at.emplace(entry.node.id, drawn.size());
            drawn.push_back(&entry);
        }
    }
    out.shown = drawn.size();

    std::vector<Drawn> lines;
    for (const embedstore::GraphEdge& edge : edges) {
        if (out.call_flow && edge.relation != embedstore::kCodeRelationCalls) {
            continue;
        }
        const auto from = drawn_at.find(edge.source_id);
        const auto to = drawn_at.find(edge.target_id);
        if (from == drawn_at.end() || to == drawn_at.end() || from->second == to->second) {
            continue;
        }
        lines.push_back(Drawn{.from = from->second,
                              .to = to->second,
                              .relation = edge.relation,
                              .inferred = edge.origin != embedstore::kOriginExtracted,
                              .weight = edge.weight});
    }
    out.edges_total = lines.size();
    std::ranges::stable_sort(lines, [](const Drawn& a, const Drawn& b) {
        if (a.weight != b.weight) {
            return a.weight > b.weight;
        }
        return std::tie(a.from, a.to) < std::tie(b.from, b.to);
    });
    if (lines.size() > kMermaidMaxEdges) {
        lines.resize(kMermaidMaxEdges);
    }
    std::ranges::stable_sort(lines, [](const Drawn& a, const Drawn& b) {
        return std::tie(a.from, a.to, a.relation) < std::tie(b.from, b.to, b.relation);
    });
    out.edges = lines.size();

    // The opening comment says what was drawn of what.
    std::string& text = out.text;
    text += "flowchart LR\n";
    const std::string drawn_edges =
        std::to_string(out.edges) + " of " +
        count_of(out.edges_total, out.call_flow ? "call" : "relation",
                 out.call_flow ? "calls" : "relations") +
        " among them drawn" +
        (out.edges < out.edges_total
             ? " (capped at " + std::to_string(kMermaidMaxEdges) + ", Mermaid's limit)"
             : "");
    if (out.call_flow) {
        text +=
            comment_text("%% apogee graph export mermaid -- graph \"" + std::string{graph} +
                         "\": the call flow of the " + std::to_string(out.shown) +
                         " highest-degree of " + count_of(out.candidates, "function", "functions") +
                         " that call or are called, grouped by file; " + drawn_edges +
                         ". Unresolved names are left out.") +
            "\n";
    } else {
        text += comment_text("%% apogee graph export mermaid -- graph \"" + std::string{graph} +
                             "\": the " + std::to_string(out.shown) + " highest-degree of " +
                             count_of(out.candidates, "related entity", "related entities") + "; " +
                             drawn_edges + ", a model's dotted.") +
                "\n";
    }

    if (out.call_flow) {
        // Grouped by the file that defines each function, files in order; a
        // short name two functions of one file share is written in full.
        std::map<std::string, std::vector<std::size_t>> by_file;
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            by_file[node_ref(drawn[i]->node).file].push_back(i);
        }
        std::size_t group = 0;
        for (const auto& [file, members] : by_file) {
            std::map<std::string, int> uses;
            for (const std::size_t i : members) {
                ++uses[short_name(drawn[i]->node)];
            }
            const bool grouped = !file.empty();
            const std::string indent = grouped ? "    " : "  ";
            if (grouped) {
                text += "  subgraph f" + std::to_string(group++) + "[\"" + mermaid_label(file) +
                        "\"]\n";
            }
            for (const std::size_t i : members) {
                const std::string name = short_name(drawn[i]->node);
                text += indent + "n" + std::to_string(i) + "[\"" +
                        mermaid_label(uses[name] > 1 ? drawn[i]->node.name : name) + "\"]\n";
            }
            if (grouped) {
                text += "  end\n";
            }
        }
        for (const Drawn& line : lines) {
            text += "  n" + std::to_string(line.from) + (line.inferred ? " -.-> n" : " --> n") +
                    std::to_string(line.to) + "\n";
        }
        return out;
    }

    for (std::size_t i = 0; i < drawn.size(); ++i) {
        text += "  n" + std::to_string(i) + "[\"" +
                mermaid_label(drawn[i]->node.name + " · " + drawn[i]->node.type) + "\"]\n";
    }
    for (const Drawn& line : lines) {
        text += "  n" + std::to_string(line.from) + (line.inferred ? " -.->|\"" : " -->|\"") +
                mermaid_label(line.relation) + "\"| n" + std::to_string(line.to) + "\n";
    }
    return out;
}

MermaidExport export_mermaid(const OpenGraph& open, const MermaidOptions& options) {
    return export_mermaid(open.store(), open.target().name, options);
}

}  // namespace apogee::graph
