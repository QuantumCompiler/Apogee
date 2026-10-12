#include "cli/graph_navigate.h"

#include <CLI/CLI.hpp>

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cli/helpers.h"
#include "contracts/config.h"
#include "contracts/paths.h"
#include "graph/navigate.h"
#include "machine/json_reporter.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee graph: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

/// What every navigation verb takes besides its own arguments.
struct Selection {
    std::string graph;
    std::string collection;
    ReadFormat format = ReadFormat::Text;
};

void add_selection(CLI::App* command, const std::shared_ptr<Selection>& selection) {
    command
        ->add_option("--graph", selection->graph,
                     "The graph to read: a named graph or a collection's own graph (default: the "
                     "one graph built)")
        ->type_name(kGraphValue);
    command
        ->add_option("--collection", selection->collection,
                     "Read the graph covering this collection: a built named graph listing it, "
                     "else its own")
        ->type_name(kCollectionValue);
    command
        ->add_option_function<std::string>(
            "--output-format",
            [selection](const std::string& value) {
                const std::optional<ReadFormat> parsed = read_format_from_string(value);
                if (!parsed.has_value()) {
                    throw CLI::ValidationError("--output-format", "expected 'text' or 'json'");
                }
                selection->format = *parsed;
            },
            "Output format: text (default) or json -- one JSON document, the payload the graph "
            "tools return")
        ->type_name(words_value(read_format_names()));
}

/// Loads the config, resolves and opens the graph, and runs `body` -- a
/// navigation failure a user error with its whole message.
void with_graph(const RootContext& context, const Selection& selection,
                const std::function<void(const graph::OpenGraph&)>& body) {
    harness::Config config;
    try {
        config = harness::load_config(harness::resolve_config_path(context.config_path));
    } catch (const harness::ConfigError& e) {
        fail_user(e.what());
    }
    try {
        const graph::OpenGraph open{graph::resolve_graph_target(
            config,
            graph::GraphSelection{.graph = selection.graph, .collection = selection.collection})};
        body(open);
    } catch (const graph::NavigationError& e) {
        fail_user(e.what());
    }
}

[[nodiscard]] std::string matched_note(std::string_view address, std::string_view matched,
                                       const graph::NodeRef& node) {
    if (matched == "unqualified") {
        return "'" + std::string{address} + "' is " + node.name + ", by its unqualified name";
    }
    if (matched == "path") {
        return std::string{address} + " is " + node.name + ", defined there";
    }
    return {};
}

void print_entry(std::ostream& out, const graph::NeighborEntry& entry) {
    out << "    " << graph::node_label(entry.node);
    if (entry.weight > 1) {
        out << " x" << entry.weight;
    }
    if (!entry.at.empty()) {
        out << "  at " << entry.at;
    }
    if (!entry.description.empty()) {
        out << " -- " << entry.description;
    }
    out << "\n";
}

void print_groups(std::ostream& out, const std::vector<graph::NeighborGroup>& groups) {
    for (const graph::NeighborGroup& group : groups) {
        out << "  " << group.relation << (group.outgoing ? " ->" : " <-") << " (" << group.total
            << ")\n";
        for (const graph::NeighborEntry& entry : group.shown) {
            print_entry(out, entry);
        }
        if (std::cmp_greater(group.total, group.shown.size())) {
            out << "    ... and " << group.total - static_cast<std::int64_t>(group.shown.size())
                << " more\n";
        }
    }
}

[[nodiscard]] std::string degree_text(const graph::Degree& degree) {
    return std::to_string(degree.total) + " edge(s): " + std::to_string(degree.out) + " out, " +
           std::to_string(degree.in) + " in";
}

// ---- path ------------------------------------------------------------------------

void print_path(const graph::PathResult& result, const graph::PathRequest& request) {
    for (const auto& [address, matched, node] :
         {std::tuple{request.from, result.from_matched, result.from},
          std::tuple{request.to, result.to_matched, result.to}}) {
        if (const std::string note = matched_note(address, matched, node); !note.empty()) {
            std::cout << "(" << note << ")\n";
        }
    }
    std::string how = result.directed ? "directed" : "undirected";
    if (!result.relations.empty()) {
        std::string relations;
        for (const std::string& relation : result.relations) {
            relations += (relations.empty() ? "" : ", ") + relation;
        }
        how += ", relations: " + relations;
    }
    if (!result.found) {
        std::cout << "No path within " << result.max_hops << " hops between "
                  << graph::node_label(result.from) << " and " << graph::node_label(result.to)
                  << " in graph \"" << result.graph << "\" (" << how << ").\n";
        return;
    }
    std::cout << "Path from " << graph::node_label(result.from) << " to "
              << graph::node_label(result.to) << " in graph \"" << result.graph << "\" -- "
              << result.steps.size() << " hop(s), " << how << ":\n";
    if (result.steps.empty()) {
        std::cout << "  (the same node)\n";
        return;
    }
    for (const graph::PathStep& step : result.steps) {
        const std::string label = step.relation + "·" + step.origin;
        std::cout << "  " << step.from
                  << (step.forward ? " -[" + label + "]-> " : " <-[" + label + "]- ") << step.to;
        if (!step.at.empty()) {
            std::cout << "  at " << step.at;
        }
        if (!step.description.empty()) {
            std::cout << " -- " << step.description;
        }
        std::cout << "\n";
    }
    std::cout << "Nodes:\n";
    for (const graph::NodeRef& node : result.nodes) {
        std::cout << "  " << graph::node_label(node) << "\n";
    }
}

// ---- explain ---------------------------------------------------------------------

/// Where a card's node is stated: its lines for code, its chunks for prose,
/// the first few, then how many more.
void print_provenance(std::ostream& out, const graph::NodeCard& card) {
    if (!card.code_mentions.empty()) {
        out << (card.node.unresolved ? "Referenced at:" : "Stated at:") << "\n";
        for (const embedstore::CodeMention& at : card.code_mentions) {
            out << "  " << at.role << "  " << at.collection << ": " << at.file << ":" << at.line;
            if (at.end_line > at.line) {
                out << "-" << at.end_line;
            }
            out << "\n";
        }
    }
    if (!card.chunk_mentions.empty()) {
        out << "Mentioned in:\n";
        for (const graph::ChunkMention& mention : card.chunk_mentions) {
            out << "  " << (mention.collection.empty() ? "" : mention.collection + ": ");
            if (mention.missing) {
                out << "chunk id " << mention.chunk_id
                    << " (no longer present -- `apogee graph build` reconciles it)\n";
            } else {
                out << mention.source << " [chunk " << mention.chunk << "]\n";
            }
        }
    }
    const std::size_t listed = card.code_mentions.size() + card.chunk_mentions.size();
    if (std::cmp_greater(card.mentions, listed) && listed > 0) {
        out << "  ... and " << card.mentions - static_cast<std::int64_t>(listed) << " more\n";
    }
}

void print_card(std::ostream& out, const graph::NodeCard& card, std::string_view address) {
    if (const std::string note = matched_note(address, card.matched, card.node); !note.empty()) {
        out << "(" << note << ")\n";
    }
    out << graph::node_label(card.node) << " in graph \"" << card.graph << "\"\n";
    if (!card.description.empty()) {
        out << "  " << card.description << "\n";
    }
    if (!card.node.discipline.empty()) {
        out << "  Discipline: " << card.node.discipline << "\n";
    }
    out << "  Degree: " << degree_text(card.degree) << "; " << card.mentions << " mention(s)\n";
    print_provenance(out, card);
    if (!card.relations.empty()) {
        out << "Relations:\n";
        print_groups(out, card.relations);
    }
    for (const graph::CommunityRef& community : card.communities) {
        out << "Community #" << community.id << " (" << community.size << " members): "
            << (community.summary.empty() ? "(no summary -- clustered with no model)"
                                          : community.summary)
            << "\n";
    }
    if (!card.decisions.empty()) {
        out << "Decisions:\n";
        for (const graph::DecisionRef& decision : card.decisions) {
            out << "  " << graph::node_label(decision.node);
            if (!decision.decision.empty()) {
                out << ": " << decision.decision;
            }
            out << "\n";
        }
    }
}

// ---- neighbors -------------------------------------------------------------------

void print_neighbors(const graph::Neighborhood& result, std::string_view address) {
    if (const std::string note = matched_note(address, result.matched, result.node);
        !note.empty()) {
        std::cout << "(" << note << ")\n";
    }
    std::cout << "Neighbours of " << graph::node_label(result.node) << " in graph \""
              << result.graph << "\" -- " << degree_text(result.degree) << "\n";
    if (result.groups.empty()) {
        std::cout << "  none";
        if (!result.relation.empty()) {
            std::cout << " over '" << result.relation << "'";
        }
        if (result.direction != embedstore::EdgeDirection::Both) {
            std::cout << " (" << graph::to_string(result.direction) << ")";
        }
        std::cout << "\n";
        return;
    }
    print_groups(std::cout, result.groups);
}

// ---- query -----------------------------------------------------------------------

void print_query(const graph::QueryResult& result) {
    if (result.match == "none") {
        std::cout << "Nothing in graph \"" << result.graph << "\" is named by \"" << result.question
                  << "\".\n";
        return;
    }
    std::string seeds;
    for (const graph::NodeRef& seed : result.seeds) {
        seeds += (seeds.empty() ? "" : ", ") + seed.name;
    }
    std::cout << "Matched " << result.seeds.size() << " entit"
              << (result.seeds.size() == 1 ? "y" : "ies")
              << (result.match == "exact" ? " by exact name" : " by name") << ": " << seeds
              << " -- " << result.hops << " hop(s), at most " << result.max_entities
              << " entities\n";
    std::cout << "[Knowledge graph: " << result.graph << "]\n";
    for (const graph::QueryEntity& entity : result.entities) {
        std::cout << graph::node_label(entity.node);
        if (!entity.description.empty()) {
            std::cout << ": " << entity.description;
        }
        std::cout << "\n";
    }
    for (const graph::QueryRelation& relation : result.relations) {
        const std::string label = relation.origin == embedstore::kOriginExtracted
                                      ? relation.relation + "·" + relation.origin
                                      : relation.relation;
        std::cout << relation.from << " —[" << label << "]→ " << relation.to;
        if (!relation.description.empty()) {
            std::cout << ": " << relation.description;
        }
        std::cout << "\n";
    }
    if (result.truncated) {
        std::cout << "(cut at the " << result.budget
                  << "-codepoint budget a turn's graph section keeps)\n";
    }
}

}  // namespace

std::string graph_explain_text(const RootContext& context, const std::string& graph,
                               const std::string& node) {
    const harness::Config config =
        harness::load_config(harness::resolve_config_path(context.config_path));
    const graph::OpenGraph open{
        graph::resolve_graph_target(config, graph::GraphSelection{.graph = graph})};
    graph::CardRequest request;
    request.node = node;
    std::ostringstream out;
    print_card(out, graph::explain_node(open, request), request.node);
    return out.str();
}

void bind_graph_navigation(CLI::App& graph_command, const RootContext& context) {
    // ---- path ----------------------------------------------------------------
    {
        auto selection = std::make_shared<Selection>();
        auto request = std::make_shared<graph::PathRequest>();
        CLI::App* path = graph_command.add_subcommand(
            "path",
            "How two entities connect: the shortest path, each hop with its relation and origin");
        path->add_option("FROM", request->from, "Where it starts: a name, kind:name or path:line")
            ->required();
        path->add_option("TO", request->to, "Where it ends")->required();
        path->add_option("--max-hops", request->max_hops,
                         "The longest path looked for (default 8, at most 32)");
        path->add_flag("--directed", request->directed,
                       "Follow edges only the way they point (A calls B: A to B)");
        path->add_option("--relation", request->relations,
                         "Walk only this relation (repeatable), such as calls");
        add_selection(path, selection);
        path->callback([&context, selection, request]() {
            with_graph(context, *selection, [&](const graph::OpenGraph& open) {
                const graph::PathResult result = graph::find_path(open, *request);
                if (selection->format == ReadFormat::Json) {
                    write_document(std::cout, graph::to_json(result));
                    return;
                }
                print_path(result, *request);
            });
        });
    }

    // ---- explain -------------------------------------------------------------
    {
        auto selection = std::make_shared<Selection>();
        auto request = std::make_shared<graph::CardRequest>();
        CLI::App* explain = graph_command.add_subcommand(
            "explain",
            "Everything around one entity: kind, degree, neighbours by relation, where it is "
            "stated, its community and attached decisions");
        explain->add_option("NODE", request->node, "A name, kind:name or path:line")->required();
        explain->add_option("--max-neighbors", request->max_per_relation,
                            "Neighbours listed per relation (default 12, at most 100)");
        add_selection(explain, selection);
        explain->callback([&context, selection, request]() {
            with_graph(context, *selection, [&](const graph::OpenGraph& open) {
                const graph::NodeCard card = graph::explain_node(open, *request);
                if (selection->format == ReadFormat::Json) {
                    write_document(std::cout, graph::to_json(card));
                    return;
                }
                print_card(std::cout, card, request->node);
            });
        });
    }

    // ---- neighbors -----------------------------------------------------------
    {
        auto selection = std::make_shared<Selection>();
        auto request = std::make_shared<graph::NeighborsRequest>();
        auto direction = std::make_shared<std::string>("both");
        CLI::App* neighbors = graph_command.add_subcommand(
            "neighbors", "An entity's neighbours by relation and direction, capped per relation");
        neighbors->add_option("NODE", request->node, "A name, kind:name or path:line")->required();
        neighbors->add_option("--relation", request->relation, "Only this relation, such as calls");
        neighbors
            ->add_option("--direction", *direction,
                         "out: edges from the node; in: edges to it; both (default)")
            ->check(CLI::IsMember(std::vector<std::string>{"both", "out", "in"}));
        neighbors->add_option("--max-neighbors", request->max_per_relation,
                              "Neighbours listed per relation (default 12, at most 100)");
        add_selection(neighbors, selection);
        neighbors->callback([&context, selection, request, direction]() {
            (void)graph::direction_from_string(*direction, request->direction);
            with_graph(context, *selection, [&](const graph::OpenGraph& open) {
                const graph::Neighborhood result = graph::find_neighbors(open, *request);
                if (selection->format == ReadFormat::Json) {
                    write_document(std::cout, graph::to_json(result));
                    return;
                }
                print_neighbors(result, request->node);
            });
        });
    }

    // ---- query ---------------------------------------------------------------
    {
        auto selection = std::make_shared<Selection>();
        auto request = std::make_shared<graph::QueryRequest>();
        CLI::App* query = graph_command.add_subcommand(
            "query",
            "The entities a question names and the bounded neighbourhood around them -- a "
            "turn's graph expansion, at the command line, with no model");
        query->add_option("QUESTION", request->question, "An entity's name, or words naming some")
            ->required();
        query->add_option("--hops", request->hops,
                          "How far to walk: 1 or 2 (default: the graph's hops)");
        query->add_option("--max-entities", request->max_entities,
                          "Entities at most (1-50; default: the graph's max_entities)");
        add_selection(query, selection);
        query->callback([&context, selection, request]() {
            with_graph(context, *selection, [&](const graph::OpenGraph& open) {
                const graph::QueryResult result = graph::run_query(open, *request);
                if (selection->format == ReadFormat::Json) {
                    write_document(std::cout, graph::to_json(result));
                    return;
                }
                print_query(result);
            });
        });
    }
}

}  // namespace apogee::commands
