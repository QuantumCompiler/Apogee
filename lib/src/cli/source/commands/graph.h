#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "graph/build.h"
#include "harness/config.h"

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
};

void run_graph_build(const RootContext& context, const GraphBuildRequest& request);

/// A named graph's member collections, opened read-only for a build, stats
/// or show; a member whose database is missing maps to null and contributes
/// nothing (`apogee check` reports the config-level problem). Shared by the
/// CLI and the admin twins so a member is opened the same way on both.
struct GraphMembers {
    std::map<std::string, std::unique_ptr<embedstore::Store>> stores;

    /// The non-owning views the store's multi-member calls take.
    [[nodiscard]] embedstore::MemberStores views() const;
    /// In the entry's order -- the order the build plans in.
    [[nodiscard]] std::vector<graph::Member> members(const harness::NamedGraphConfig& named) const;
    /// Whether any member holds a chunk at all.
    [[nodiscard]] bool any_data() const;
};

[[nodiscard]] GraphMembers open_graph_members(const harness::NamedGraphConfig& named);

/// The write-time rules a `graphs:` entry must satisfy, decided ONCE for
/// `config add-graph` and its admin twin: a name, at least one member, no
/// collision with a collection name (the resolution rule depends on it),
/// a configured `extract_backend` when one is named, sane knobs. Unknown
/// members are warnings, not errors -- ingest registers collections on
/// first use. A non-empty `error` means the entry is refused.
struct NamedGraphValidation {
    std::string error;
    std::vector<std::string> warnings;
};

[[nodiscard]] NamedGraphValidation validate_named_graph(const harness::Config& config,
                                                        std::string_view name,
                                                        const harness::NamedGraphConfig& graph);

}  // namespace apogee::commands
