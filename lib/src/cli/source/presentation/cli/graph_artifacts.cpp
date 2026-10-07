#include "cli/graph_artifacts.h"

#include <CLI/CLI.hpp>

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "cli/helpers.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/paths.h"
#include "graph/export_graphml.h"
#include "graph/export_html.h"
#include "graph/export_mermaid.h"
#include "graph/navigate.h"
#include "graph/report.h"
#include "machine/json_reporter.h"
#include "render/graph_report.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee graph: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

/// Which graph, as every graph read takes it (27l's convention).
struct Selection {
    std::string graph;
    std::string collection;
};

void add_selection(CLI::App* command, const std::shared_ptr<Selection>& selection) {
    command
        ->add_option("--graph", selection->graph,
                     "The graph: a named graph or a collection's own graph (default: the one "
                     "graph built)")
        ->type_name(kGraphValue);
    command
        ->add_option("--collection", selection->collection,
                     "The graph covering this collection: a built named graph listing it, else "
                     "its own")
        ->type_name(kCollectionValue);
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

/// Writes an artifact: to stdout for `-`, else to the file, atomically.
/// Returns whether it went to a file.
bool write_artifact(const std::string& out, std::string_view content) {
    if (out == "-") {
        std::cout << content;
        std::cout.flush();
        return false;
    }
    try {
        harness::write_file_atomically(harness::expand_env_and_home(out), content);
    } catch (const std::exception& e) {
        fail_user("could not write " + out + ": " + e.what());
    }
    return true;
}

[[nodiscard]] std::string size_text(std::size_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " bytes";
    }
    return std::to_string((bytes + 512) / 1024) + " KB";
}

[[nodiscard]] std::string count_of(std::size_t count, std::string_view one, std::string_view many) {
    return std::to_string(count) + " " + std::string{count == 1 ? one : many};
}

struct ReportFlags {
    std::string out;
    ReadFormat format = ReadFormat::Text;
};

struct ExportFlags {
    std::string out;
    std::size_t max_nodes = 0;
};

void bind_report(CLI::App& graph_command, const RootContext& context) {
    auto selection = std::make_shared<Selection>();
    auto flags = std::make_shared<ReportFlags>();
    CLI::App* report = graph_command.add_subcommand(
        "report",
        "A Markdown architecture summary -- communities, hubs, the origin mix, cross-collection "
        "links, decisions, orphans -- from the store, with no model call");
    add_selection(report, selection);
    report->add_option("-o,--out", flags->out, "Write the report to this file instead of stdout")
        ->type_name(kPathValue);
    report
        ->add_option_function<std::string>(
            "--output-format",
            [flags](const std::string& value) {
                const std::optional<ReadFormat> parsed = read_format_from_string(value);
                if (!parsed.has_value()) {
                    throw CLI::ValidationError("--output-format", "expected 'text' or 'json'");
                }
                flags->format = *parsed;
            },
            "Output format: text (default, Markdown) or json -- the report's one document")
        ->type_name(words_value(read_format_names()));
    report->callback([&context, selection, flags]() {
        with_graph(context, *selection, [&](const graph::OpenGraph& open) {
            const nlohmann::json document = graph::to_json(graph::build_report(open));
            std::string text;
            if (flags->format == ReadFormat::Json) {
                // The one-line document 27j's reads print; text a store holds
                // that is not UTF-8 is replaced rather than thrown on.
                text =
                    document.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
            } else {
                text = render::render_graph_report(document);
            }
            if (flags->out.empty()) {
                std::cout << text;
                std::cout.flush();
                return;
            }
            if (write_artifact(flags->out, text)) {
                std::cout << "Wrote the report on graph \"" << open.target().name << "\" to "
                          << flags->out << " (" << size_text(text.size()) << ").\n";
            }
        });
    });
}

}  // namespace

std::string default_artifact_name(std::string_view graph, std::string_view kind) {
    if (kind == "html") {
        return std::string{graph} + ".html";
    }
    return std::string{graph} + (kind == "graphml" ? ".graphml" : ".mmd");
}

void bind_graph_artifacts(CLI::App& graph_command, const RootContext& context) {
    bind_report(graph_command, context);

    CLI::App* export_command = graph_command.add_subcommand(
        "export",
        "Write a graph as a file: one self-contained interactive HTML page, GraphML, or a "
        "Mermaid diagram -- never a server, never a model call");
    export_command->require_subcommand(1);

    // ---- html ----------------------------------------------------------------
    {
        auto selection = std::make_shared<Selection>();
        auto flags = std::make_shared<ExportFlags>();
        flags->max_nodes = graph::kHtmlMaxNodes;
        CLI::App* html = export_command->add_subcommand(
            "html",
            "One self-contained file that opens offline: a force layout coloured by community, "
            "search, and each entity's card on a click");
        add_selection(html, selection);
        html->add_option("-o,--out", flags->out,
                         "The file to write (default: <graph>.html here; - for stdout)")
            ->type_name(kPathValue);
        html->add_option("--max-nodes", flags->max_nodes,
                         "Entities drawn, the highest-degree first (default and most: 2000)");
        html->callback([&context, selection, flags]() {
            with_graph(context, *selection, [&](const graph::OpenGraph& open) {
                const graph::HtmlExport page =
                    graph::export_html(open, graph::HtmlOptions{.max_nodes = flags->max_nodes});
                const std::string out = flags->out.empty()
                                            ? default_artifact_name(open.target().name, "html")
                                            : flags->out;
                if (write_artifact(out, page.html)) {
                    std::cout << "Exported graph \"" << open.target().name << "\" to " << out
                              << ": " << count_of(page.shown, "entity", "entities") << " and "
                              << count_of(page.relations, "relation", "relations") << " drawn, "
                              << size_text(page.html.size()) << ".\n"
                              << page.cap_note << "\n";
                }
            });
        });
    }

    // ---- graphml -------------------------------------------------------------
    {
        auto selection = std::make_shared<Selection>();
        auto flags = std::make_shared<ExportFlags>();
        CLI::App* graphml = export_command->add_subcommand(
            "graphml", "Every entity and relation as GraphML, uncapped, for graph tooling");
        add_selection(graphml, selection);
        graphml
            ->add_option("-o,--out", flags->out,
                         "The file to write (default: <graph>.graphml here; - for stdout)")
            ->type_name(kPathValue);
        graphml->callback([&context, selection, flags]() {
            with_graph(context, *selection, [&](const graph::OpenGraph& open) {
                const graph::GraphmlExport document = graph::export_graphml(open);
                const std::string out = flags->out.empty()
                                            ? default_artifact_name(open.target().name, "graphml")
                                            : flags->out;
                if (write_artifact(out, document.xml)) {
                    std::cout << "Exported graph \"" << open.target().name << "\" to " << out
                              << ": every entity (" << document.nodes << ") and relation ("
                              << document.edges << "), uncapped, " << size_text(document.xml.size())
                              << ".\n";
                }
            });
        });
    }

    // ---- mermaid -------------------------------------------------------------
    {
        auto selection = std::make_shared<Selection>();
        auto flags = std::make_shared<ExportFlags>();
        flags->max_nodes = graph::kMermaidDefaultNodes;
        CLI::App* mermaid = export_command->add_subcommand(
            "mermaid",
            "A Mermaid flowchart for docs: a code graph's call flow, else its busiest entities "
            "and their relations");
        add_selection(mermaid, selection);
        mermaid
            ->add_option("-o,--out", flags->out,
                         "The file to write (default: <graph>.mmd here; - for stdout)")
            ->type_name(kPathValue);
        mermaid->add_option("--max-nodes", flags->max_nodes,
                            "Entities drawn, the highest-degree first (default 40, at most 150)");
        mermaid->callback([&context, selection, flags]() {
            with_graph(context, *selection, [&](const graph::OpenGraph& open) {
                const graph::MermaidExport diagram = graph::export_mermaid(
                    open, graph::MermaidOptions{.max_nodes = flags->max_nodes});
                const std::string out = flags->out.empty()
                                            ? default_artifact_name(open.target().name, "mermaid")
                                            : flags->out;
                if (write_artifact(out, diagram.text)) {
                    std::cout << "Exported graph \"" << open.target().name << "\" to " << out
                              << ": "
                              << (diagram.call_flow
                                      ? "the call flow of " +
                                            count_of(diagram.shown, "function", "functions") +
                                            " and " + count_of(diagram.edges, "call", "calls")
                                      : count_of(diagram.shown, "entity", "entities") + " and " +
                                            count_of(diagram.edges, "relation", "relations"));
                    if (diagram.shown < diagram.candidates) {
                        std::cout << ", the highest-degree " << diagram.shown << " of "
                                  << diagram.candidates;
                    }
                    if (diagram.edges < diagram.edges_total) {
                        std::cout << "; " << diagram.edges_total - diagram.edges
                                  << " more edges left out at Mermaid's limit of "
                                  << graph::kMermaidMaxEdges;
                    }
                    std::cout << ".\n";
                }
            });
        });
    }
}

}  // namespace apogee::commands
