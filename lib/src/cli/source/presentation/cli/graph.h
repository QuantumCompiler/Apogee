#pragma once

#include <nlohmann/json_fwd.hpp>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "contracts/config.h"
#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "graph/build.h"
#include "operations/graph_members.h"

/// `apogee graph` -- build and inspect the knowledge graph over a collection
/// or a named multi-collection graph.
///
/// `build` runs the extraction clerk once per stale chunk -- one structured
/// call each, validated in host code -- into `kg_*` tables inside the
/// collection's own database, incrementally and resumably; `stats`, `show`
/// and `delete` inspect and clear it. Retrieval-time expansion consumes what
/// build produces, on every RAG surface, once the collection's `graph.enabled`
/// is set -- which the first successful build does through the one config
/// editor.
///
/// **Every subcommand resolves its `<name>` graphs-first**: a `graphs:` entry
/// names a named graph over several collections -- built into its own
/// database under `embeddings/graphs/`, one node per entity across every
/// member, taking retrieval precedence for them, with nothing to enable --
/// and anything else is a collection. `apogee check` keeps the two name
/// spaces apart.
///
/// **Code, with no model at all** (27k): a named graph's `sources:` are
/// source trees parsed with tree-sitter into the same tables -- files,
/// modules, classes, functions; calls, imports, bases, type references and
/// containment, each stated at a `file:line` and stored `extracted` -- built
/// by `build` beside its collections and refreshed by `update`, which
/// re-parses only the files whose content changed and never makes a model
/// call. `build --source <dir> --graph <name>` adds a tree (and the entry,
/// when it is new).
///
/// `communities` is the global layer: deterministic label-propagation
/// clusters, each summarised by one generation call and stored as a
/// retrievable pseudo-chunk, so "what are the main themes?" is answerable by
/// plain retrieval. `dedupe` merges same-type entities whose vectors say
/// they are the same thing -- never on its own, and never a decision node.
///
/// **Local extractor by default.** A full build, and a summariser run, never
/// runs on a metered backend on Apogee's initiative: it does so only when
/// that backend was named explicitly (`-m`, the entry's `extract_backend`,
/// or the extraction role). Whether a backend is metered is a fact the
/// provider states, discovered through the Harness.
namespace apogee::commands {

class GraphCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

/// What `graph build <name>` runs -- shared with `embed ingest --graph`, so
/// the chained build is the same build. Refusals throw the CLI's user error.
struct GraphBuildRequest {
    std::string name;
    std::string model;
    bool dry_run = false;
    bool force = false;
    int limit = 0;
    /// No progress at all: no busy line on a terminal, no per-chunk line on
    /// a pipe (M1). The results and every failure still print.
    bool quiet = false;
    /// `--source` (27k): source trees to add to the named graph `name` --
    /// the entry made when it is new -- before it builds. A collection's
    /// name is refused: a tree builds into a named graph.
    std::vector<std::string> sources;
    /// `--lang`: the grammars the graph's trees are parsed with, recorded on
    /// the entry (empty: leave the entry's as it is).
    std::vector<std::string> languages;
    /// Name every skipped file, however many.
    bool show_skipped = false;
};

void run_graph_build(const RootContext& context, const GraphBuildRequest& request);

/// A refusal a carved graph read throws, in the command's words: the command
/// prints it as its user error, a view shows it.
class GraphRefusal : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// `graph stats NAME` as a person reads it, and as one document (37c) --
/// `{"object": "graph.stats", "graph", "kind": "named"|"collection",
/// "built", "nodes", "edges", ...}`, the counts the text prints, plus a named
/// graph's `collections`, `sources`, `members` and `embed_model`. A graph not
/// built yet is a read (`"built": false`), not a refusal; each throws
/// `GraphRefusal` for a name that is not plain or names nothing.
[[nodiscard]] std::string graph_stats_text(const RootContext& context, const std::string& name);
[[nodiscard]] nlohmann::json graph_stats_document(const RootContext& context,
                                                  const std::string& name);

/// Whether `graph update` refreshes `graph`: it has source trees to re-parse
/// (27k). The verb's refusal and its completion both ask this.
[[nodiscard]] bool graph_updatable(const harness::NamedGraphConfig& graph) noexcept;

}  // namespace apogee::commands
