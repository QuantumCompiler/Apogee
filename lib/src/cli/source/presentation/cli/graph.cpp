#include "cli/graph.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "agentloop/graph_context.h"
#include "backends/factory.h"
#include "cli/embed.h"
#include "cli/graph_artifacts.h"
#include "cli/graph_navigate.h"
#include "cli/helpers.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/errors.h"
#include "contracts/paths.h"
#include "embedstore/store.h"
#include "graph/build.h"
#include "graph/code_build.h"
#include "graph/code_languages.h"
#include "graph/communities.h"
#include "graph/extract.h"
#include "harness/harness.h"
#include "harness/roles.h"
#include "operations/graph_sources.h"
#include "operations/knowledge_core.h"
#include "views/status_line.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee graph: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

[[noreturn]] void fail_backend(const std::string& message) {
    std::cerr << "apogee graph: " << message << "\n";
    throw CLI::RuntimeError(kBackendError);
}

void require_plain_name(std::string_view name) {
    if (name.empty() || name.find("..") != std::string_view::npos ||
        name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos) {
        fail_user("'" + std::string{name} + "' is not a plain graph or collection name");
    }
}

[[nodiscard]] harness::Config load_config_strict(const RootContext& context,
                                                 std::filesystem::path& config_path) {
    try {
        config_path = harness::resolve_config_path(context.config_path);
        return harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        fail_user(e.what());
    }
}

[[nodiscard]] bool file_exists(const std::filesystem::path& path) {
    std::error_code code;
    return std::filesystem::exists(path, code);
}

/// A collection that must already exist: a read never creates an empty
/// database out of a typo.
[[nodiscard]] embedstore::Store open_existing(const std::string& name) {
    require_plain_name(name);
    const std::filesystem::path path = collection_path(name);
    if (!file_exists(path)) {
        fail_user("no graph or collection named '" + name +
                  "' -- ingest documents first with 'apogee embed ingest', or add a graph with "
                  "'apogee config add-graph'");
    }
    return embedstore::Store{path};
}

/// Every configured provider, built so metered-ness and the embedder can be
/// asked of the real objects.
struct Providers {
    harness::Harness harness;

    Providers(const harness::Config& config, const std::filesystem::path& config_path)
        : harness{config} {
        backends::BuildOptions options;
        options.config_path = config_path;
        (void)backends::build_providers(harness, options);
    }
};

/// What a `graph` subcommand's `<name>` resolved to, graphs-first: a
/// `graphs:` entry (its own database under `graphs/`, which the first build
/// creates) or a collection (the graph inside its database).
struct Target {
    std::string name;
    /// Set exactly when the name resolved to a named graph.
    const harness::NamedGraphConfig* named = nullptr;
    /// The collection's entry, for a collection target; may be null.
    const harness::EmbeddingConfig* entry = nullptr;
    std::filesystem::path db_path;

    [[nodiscard]] std::string entry_backend() const {
        if (named != nullptr) {
            return named->extract_backend;
        }
        return entry != nullptr ? entry->graph.extract_backend : std::string{};
    }

    /// Where the user sets a standing extractor for this target.
    [[nodiscard]] std::string extractor_hint() const {
        return named != nullptr ? "set extract_backend on graph '" + name + "'"
                                : "set graph.extract_backend on '" + name + "'";
    }
};

[[nodiscard]] Target resolve_target(const harness::Config& config, const std::string& name) {
    require_plain_name(name);
    Target target;
    target.name = name;
    if (const harness::NamedGraphConfig* named = config.find_graph(name); named != nullptr) {
        target.named = named;
        target.db_path = agentloop::graph_db_path(name);
        return target;
    }
    target.entry = config.find_embedding(name);
    target.db_path = collection_path(name);
    return target;
}

/// Opens the graph store for a read-side command: a named graph must have
/// been built, a collection must exist.
[[nodiscard]] embedstore::Store open_target(const Target& target) {
    if (target.named != nullptr) {
        if (!file_exists(target.db_path)) {
            fail_user("graph '" + target.name +
                      "' has not been built yet -- run: apogee graph build " + target.name);
        }
        return embedstore::Store{target.db_path};
    }
    return open_existing(target.name);
}

/// The generation backend a build or a communities run uses: `-m` > the
/// entry's `extract_backend` > the extraction role > the default, through
/// the ONE role resolver -- a vendor CLI refused by type, and a metered
/// default refused naming the three ways to say so. `what` names the run in
/// the refusal ("graph build", "community summaries").
struct Generation {
    std::string key;
    /// The model name recorded as the staleness key: the entry's model when
    /// it names one, else the key.
    std::string kg_model;
};

[[nodiscard]] Generation resolve_generation(const harness::Config& config,
                                            const harness::Harness& harness,
                                            std::string_view override, const Target& target,
                                            const std::string& what,
                                            std::string_view alternative = {}) {
    const harness::Resolution resolved = harness::resolve_backend(
        config, harness::RoleRequest{.role = harness::ModelRole::Extraction,
                                     .override = override,
                                     .entry_backend = target.entry_backend()});
    if (resolved.key.empty()) {
        fail_user("no extraction backend: pass -m <backend>, " + target.extractor_hint() +
                  ", or set the extraction role");
    }
    const harness::BackendConfig* backend = config.find_backend(resolved.key);
    if (backend == nullptr) {
        fail_user("backend '" + resolved.key + "' is not configured");
    }
    if (harness::is_vendor_cli(backend->type)) {
        fail_user("'" + resolved.key +
                  "' is a vendor-CLI backend -- the clerk runs inside Apogee's own loop; pick an "
                  "API or local backend");
    }
    if (resolved.from == harness::ResolvedFrom::Default &&
        harness.generation_is_metered(resolved.key)) {
        fail_user("a full " + what +
                  " never runs on a metered backend on Apogee's initiative -- '" + resolved.key +
                  "' is the default backend and is billed per call. Name it explicitly with -m " +
                  resolved.key + ", " + target.extractor_hint() + ", or set the extraction role" +
                  (alternative.empty() ? std::string{} : " -- or " + std::string{alternative}));
    }
    return Generation{.key = resolved.key,
                      .kg_model = backend->model.empty() ? resolved.key : backend->model};
}

/// Entity vectors under the embedding spend rule: a collection through its
/// own chain and pin; a named graph through the default chain (it has no
/// per-collection override -- its vectors are its own).
[[nodiscard]] graph::EntityEmbedder resolve_embedder(const harness::Harness& harness,
                                                     const harness::Config& config,
                                                     const Target& target) {
    if (target.named != nullptr || target.entry == nullptr) {
        return graph::resolve_entity_embedder(harness, config, "", "");
    }
    return graph::resolve_entity_embedder(harness, config, target.entry->backend,
                                          target.entry->retriever);
}

[[nodiscard]] std::string indent_lines(const std::string& text, const std::string& prefix) {
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string line =
            text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
        out += prefix + line;
        if (newline == std::string::npos) {
            break;
        }
        out += '\n';
        start = newline + 1;
    }
    return out;
}

[[nodiscard]] std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

void print_dry_run_extraction(const embedstore::Chunk& chunk, const graph::ExtractResult& result) {
    std::cout << "\n" << chunk.source << " [chunk " << chunk.ordinal << "]\n";
    if (result.entities.empty()) {
        std::cout << "  (no entities)\n";
        return;
    }
    for (const graph::Entity& entity : result.entities) {
        std::cout << "  " << entity.name << " (" << entity.type << ")";
        if (!entity.description.empty()) {
            std::cout << " -- " << preview_text(entity.description, 120);
        }
        std::cout << "\n";
    }
    for (const graph::Relation& relation : result.relations) {
        std::cout << "  " << relation.source << " -[" << relation.relation << "]-> "
                  << relation.target << "\n";
    }
}

void print_record_summary(const graph::BuildResult& result) {
    if (result.record_nodes == 0) {
        return;
    }
    std::cout << "  Records as nodes:  " << result.record_nodes << " decision node(s)";
    if (result.supersedes_edges > 0 || result.supersedes_skipped > 0) {
        std::cout << ", " << result.supersedes_edges << " supersedes edge(s)";
        if (result.supersedes_skipped > 0) {
            std::cout << " (" << result.supersedes_skipped
                      << " skipped -- target record not in the graph)";
        }
    }
    std::cout << "\n";
}

void print_reconcile(const embedstore::ReconcileResult& reconcile) {
    if (reconcile.total() == 0) {
        return;
    }
    std::cout << "Reconciled stale rows: " << reconcile.mentions_pruned << " mention(s), "
              << reconcile.nodes_pruned << " node(s), " << reconcile.edges_pruned << " edge(s), "
              << reconcile.states_pruned << " source state(s).\n";
}

void print_build_summary(const std::string& name, const graph::BuildResult& result) {
    if (result.dry_run) {
        std::cout << "\nDry run over \"" << name << "\": " << result.chunks_done
                  << " chunk(s) extracted across " << result.files_planned << " file(s), "
                  << result.chunks_failed << " failed -- nothing was stored.\n";
        if (result.record_nodes > 0) {
            std::cout << result.record_nodes
                      << " knowledge record(s) would be materialised as decision nodes.\n";
        }
        return;
    }
    if (result.cancelled) {
        std::cout << "Graph build for \"" << name << "\" interrupted after " << result.chunks_done
                  << " chunk(s); what finished is stored and the next build resumes.\n";
        return;
    }
    if (result.files_planned == 0) {
        std::cout << "Nothing to extract -- every source in \"" << name
                  << "\" is up to date. (--force re-extracts everything.)\n";
        // The record pass still ran, and a reconcile can still have done
        // real work with nothing planned.
        print_record_summary(result);
        print_reconcile(result.reconcile);
        return;
    }
    std::cout << "Graph build complete for \"" << name << "\":\n";
    std::cout << "  Files extracted:   " << result.files_extracted << " of " << result.files_planned
              << " planned\n";
    std::cout << "  Chunks extracted:  " << (result.chunks_done - result.chunks_failed);
    if (result.chunks_failed > 0) {
        std::cout << " (" << result.chunks_failed << " failed after retry -- re-run to retry them)";
    }
    std::cout << "\n";
    std::cout << "  Nodes upserted:    " << result.nodes_upserted << "\n";
    std::cout << "  Edges upserted:    " << result.edges_upserted << "\n";
    std::cout << "  Mentions linked:   " << result.mentions_added << "\n";
    print_record_summary(result);
    if (result.nodes_embedded > 0) {
        std::cout << "  Entities embedded: " << result.nodes_embedded << "\n";
    }
    if (!result.embed_error.empty()) {
        std::cout << "Warning: entity embedding stopped early (" << result.embed_error
                  << ") -- affected entities stay full-text searchable; re-run to embed them.\n";
    }
    print_reconcile(result.reconcile);
    if (result.limit_hit) {
        std::cout << "Chunk limit reached -- the interrupted file will re-extract on the next "
                     "build.\n";
    }
    std::cout << "Run `apogee graph stats " << name << "` for the graph's shape.\n";
}

[[nodiscard]] std::string type_summary(const embedstore::GraphStats& stats) {
    std::vector<std::pair<std::string, std::int64_t>> types(stats.nodes_by_type.begin(),
                                                            stats.nodes_by_type.end());
    std::ranges::sort(types, [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    std::string out;
    for (const auto& [type, count] : types) {
        out += (out.empty() ? "" : ", ") + type + " " + std::to_string(count);
    }
    return out;
}

/// How many of a graph's nodes are the code layer's (27k).
[[nodiscard]] std::int64_t code_node_count(const embedstore::GraphStats& st) {
    std::int64_t out = 0;
    for (const auto& [type, count] : st.nodes_by_type) {
        if (embedstore::is_code_node_type(type)) {
            out += count;
        }
    }
    return out;
}

/// The counts every graph form shares.
void print_stats_body(const std::string& name, const embedstore::GraphStats& st) {
    const std::int64_t code_nodes = code_node_count(st);
    const bool code_only = code_nodes == st.nodes && st.total_chunks == 0;
    std::cout << "  Nodes:     " << st.nodes << " (" << type_summary(st) << ")\n";
    std::cout << "  Edges:     " << st.edges << "\n";
    // Origin is schema (27k): what a parse stated, and what a model asserted.
    std::cout << "  Origin:    " << st.edges_extracted << " extracted (parsed from source), "
              << st.edges_inferred << " inferred (asserted by a model)\n";
    std::cout << "  Mentions:  " << st.mentions;
    if (st.code_mentions > 0) {
        std::cout << " in chunks, " << st.code_mentions << " at a file:line";
    }
    std::cout << "\n";
    if (!code_only) {
        const std::int64_t coverage =
            st.total_chunks > 0 ? 100 * st.chunks_with_mentions / st.total_chunks : 0;
        std::cout << "  Coverage:  " << coverage << "% of " << st.total_chunks
                  << " chunks carry at least one entity\n";
    }
    if (code_nodes > 0) {
        std::cout << "  Vectors:   " << st.nodes_with_vectors << "/" << st.nodes - code_nodes
                  << " prose entities embedded (code entities never are)\n";
    } else {
        std::cout << "  Vectors:   " << st.nodes_with_vectors << "/" << st.nodes
                  << " entities embedded\n";
    }
    if (st.code_files > 0) {
        std::string languages;
        for (const auto& [language, count] : st.code_files_by_language) {
            languages += (languages.empty() ? "" : ", ") + language + " " + std::to_string(count);
        }
        std::cout << "  Code:      " << st.code_files << " file(s) parsed (" << languages << "), "
                  << st.unresolved_names << " unresolved name(s) -- `apogee graph update " << name
                  << "` re-parses what changed\n";
    }
    if (!code_only || !st.extract_model.empty()) {
        std::cout << "  Extractor: "
                  << (st.extract_model.empty() ? "(unrecorded)" : st.extract_model) << "\n";
    }
    if (st.stale_files > 0) {
        std::cout << "  Stale:     " << st.stale_files
                  << " source file(s) need re-extraction -- run `apogee graph build " << name
                  << "`\n";
    }
    if (st.failed_chunks > 0) {
        std::cout << "  Failed:    " << st.failed_chunks
                  << " chunk(s) failed extraction in the last build\n";
    }
    if (st.communities > 0) {
        std::cout << "  Communities: " << st.communities - st.communities_unsummarised
                  << " summarised";
        if (st.communities_unsummarised > 0) {
            std::cout << ", " << st.communities_unsummarised << " clustered without a summary";
        }
        std::cout << " (`apogee graph communities " << name << " --list`)\n";
    }
}

/// The lines every `show` form shares: the node, its markers, its relations.
void print_node_core(const embedstore::Store& store, const embedstore::GraphNode& node) {
    std::string label = node.type;
    if (node.type == embedstore::kNodeTypeDecision) {
        const embedstore::DecisionNodeMetadata meta =
            embedstore::parse_decision_node_metadata(node.metadata);
        if (!meta.status.empty()) {
            label += ", " + meta.status;
            if (!meta.discipline.empty()) {
                label += ", " + meta.discipline;
            }
        }
    }
    std::cout << "\n"
              << node.name << " (" << label << ") -- " << node.mention_count << " mention(s), "
              << (node.dim > 0 ? std::to_string(node.dim) + "d vector" : std::string{"no vector"})
              << "\n";
    if (!node.description.empty()) {
        std::cout << indent_lines(preview_text(node.description, 300), "  ") << "\n";
    }
    if (node.type == embedstore::kNodeTypeDecision) {
        std::cout << "  (knowledge record -- `apogee knowledge info " << node.name
                  << "` for the full record)\n";
    }
    // A code node's provenance (27k): where it is defined and declared --
    // or, for an unresolved name, referenced -- each a file:line.
    if (embedstore::is_code_node_type(node.type)) {
        const std::vector<embedstore::CodeMention> mentions = store.node_code_mentions(node.id, 0);
        if (!mentions.empty()) {
            std::cout << "\n"
                      << (node.type == embedstore::kCodeKindName ? "Referenced at:" : "Stated at:")
                      << "\n";
            constexpr std::size_t kShown = 12;
            for (std::size_t i = 0; i < mentions.size() && i < kShown; ++i) {
                const embedstore::CodeMention& at = mentions[i];
                std::cout << "  " << at.role << "  " << at.collection << ": " << at.file << ":"
                          << at.line;
                if (at.end_line > at.line) {
                    std::cout << "-" << at.end_line;
                }
                std::cout << "\n";
            }
            if (mentions.size() > kShown) {
                std::cout << "  ... and " << mentions.size() - kShown << " more\n";
            }
        }
    }
    const std::vector<embedstore::Neighbor> neighbors = store.node_neighbors(node.id);
    if (!neighbors.empty()) {
        std::cout << "\nRelations:\n";
        std::string last;
        for (const embedstore::Neighbor& neighbor : neighbors) {
            if (neighbor.relation != last) {
                std::cout << "  [" << neighbor.relation << "]\n";
                last = neighbor.relation;
            }
            std::cout << "    " << (neighbor.outgoing ? "->" : "<-") << " " << neighbor.peer_name
                      << " (" << neighbor.peer_type << ")";
            if (neighbor.weight > 1) {
                std::cout << " x" << neighbor.weight;
            }
            if (neighbor.origin == embedstore::kOriginExtracted) {
                std::cout << " -- extracted";
            }
            if (!neighbor.description.empty()) {
                std::cout << " -- " << preview_text(neighbor.description, 100);
            }
            std::cout << "\n";
            // Where a parsed edge is stated: the call site, the import, the
            // declaration -- a few, then a count.
            if (neighbor.origin == embedstore::kOriginExtracted) {
                constexpr int kSites = 3;
                const std::vector<embedstore::EdgeSite> sites =
                    store.edge_sites(neighbor.edge_id, kSites + 1);
                for (std::size_t i = 0; i < sites.size() && i < static_cast<std::size_t>(kSites);
                     ++i) {
                    std::cout << "         at " << sites[i].file << ":" << sites[i].line << "\n";
                }
                if (neighbor.weight > kSites) {
                    std::cout << "         (+" << neighbor.weight - kSites << " more)\n";
                }
            }
        }
    }
}

void print_node(const embedstore::Store& store, const embedstore::GraphNode& node, int chunks) {
    print_node_core(store, node);
    const std::vector<embedstore::Chunk> supporting = store.node_chunks(node.id, chunks);
    if (!supporting.empty()) {
        std::cout << "\nSupporting chunks:\n";
        for (const embedstore::Chunk& chunk : supporting) {
            std::cout << "  " << chunk.source << " [chunk " << chunk.ordinal << "]\n";
            std::cout << indent_lines(preview_text(chunk.text, 240), "    ") << "\n";
        }
    }
}

/// A named graph's supporting chunks are (collection, chunk) refs resolved
/// through the member stores, each line naming the member the evidence
/// lives in -- the cross-collection identity made visible.
void print_named_node(const embedstore::Store& store, const embedstore::GraphNode& node,
                      const GraphMembers& members, int chunks) {
    print_node_core(store, node);
    const std::vector<embedstore::ChunkRef> refs = store.node_mention_refs(node.id, chunks);
    if (refs.empty()) {
        return;
    }
    std::cout << "\nSupporting chunks:\n";
    for (const embedstore::ChunkRef& ref : refs) {
        const auto it = members.stores.find(ref.collection);
        if (it == members.stores.end() || it->second == nullptr) {
            std::cout << "  " << ref.collection << ": chunk id " << ref.chunk_id
                      << " (collection database missing)\n";
            continue;
        }
        const std::optional<embedstore::Chunk> chunk = it->second->chunk_by_id(ref.chunk_id);
        if (!chunk.has_value()) {
            std::cout << "  " << ref.collection << ": chunk id " << ref.chunk_id
                      << " (no longer present -- run `apogee graph build` to reconcile)\n";
            continue;
        }
        std::cout << "  " << ref.collection << ": " << chunk->source << " [chunk " << chunk->ordinal
                  << "]\n";
        std::cout << indent_lines(preview_text(chunk->text, 240), "    ") << "\n";
    }
}

/// How many communities a run lists after it; `--list` shows every one.
constexpr std::size_t kCommunitiesAfterRun = 20;

/// The stored communities, largest first; `limit` 0 shows every one. One
/// clustered with no model reads `(no summary)`, the way to write them said
/// once at the end -- a code graph clusters into hundreds.
void print_communities(const embedstore::Store& store, const std::string& name,
                       std::size_t limit = 0) {
    const std::vector<embedstore::GraphCommunity> communities = store.graph_communities();
    if (communities.empty()) {
        std::cout << "No communities stored for \"" << name << "\". Run: apogee graph communities "
                  << name << "\n";
        return;
    }
    std::size_t shown = 0;
    std::size_t unsummarised = 0;
    for (const embedstore::GraphCommunity& community : communities) {
        if (community.summary.empty()) {
            ++unsummarised;
        }
        if (limit > 0 && shown == limit) {
            continue;
        }
        ++shown;
        std::vector<std::string> top;
        for (const embedstore::GraphNode& member : store.community_members(community.id)) {
            if (top.size() == 3) {
                break;
            }
            top.push_back(member.name);
        }
        std::cout << "Community #" << community.id << " -- " << community.size
                  << " entities (top: " << join(top) << ")\n";
        if (!community.summary.empty()) {
            std::cout << indent_lines(preview_text(community.summary, 400), "  ") << "\n";
        } else {
            std::cout << "  (no summary)\n";
        }
        std::cout << "\n";
    }
    if (shown < communities.size()) {
        std::cout << "... and " << communities.size() - shown
                  << " more -- `apogee graph communities " << name << " --list` lists every one.\n";
    }
    if (unsummarised > 0) {
        std::cout << unsummarised << " communit" << (unsummarised == 1 ? "y" : "ies")
                  << " clustered with no model have no summary -- `apogee graph communities "
                  << name << " -m <backend>` writes them.\n";
    }
}

struct BuildFlags {
    std::string name;
    std::string graph;
    std::string model;
    std::vector<std::string> sources;
    std::vector<std::string> languages;
    bool dry_run = false;
    bool force = false;
    int limit = 0;
    bool quiet = false;
    bool show_skipped = false;
};

struct UpdateFlags {
    std::string name;
    bool force = false;
    bool quiet = false;
    bool show_skipped = false;
};

struct CommunitiesFlags {
    std::string name;
    std::string model;
    bool force = false;
    int min_size = 0;
    bool list = false;
    bool quiet = false;
    bool no_summaries = false;
};

struct DedupeFlags {
    std::string name;
    double threshold = embedstore::kDefaultDedupeThreshold;
    bool dry_run = false;
};

/// The build options every build form shares: the progress, the dry-run
/// printer, failures as they happen. `busy` must outlive the build.
///
/// On a terminal the progress is the busy line, one line repainted per chunk
/// (M1); on a pipe it stays a line per chunk a log can read, as before; with
/// `--quiet`, neither. What stays -- a failed chunk, a dry run's extraction --
/// is printed above the busy line, never through it.
[[nodiscard]] graph::BuildOptions build_options(const Generation& generation,
                                                const std::string& embed_model,
                                                const GraphBuildRequest& request, BusyLine& busy) {
    graph::BuildOptions options;
    options.model = generation.kg_model;
    options.embed_model = embed_model;
    options.force = request.force;
    options.limit = request.limit;
    options.dry_run = request.dry_run;
    options.on_progress = [&busy, quiet = request.quiet](const graph::Progress& progress) {
        if (progress.stage == graph::Progress::Stage::Extract) {
            const std::string failed =
                progress.failed > 0 ? ", " + std::to_string(progress.failed) + " failed" : "";
            const std::string where =
                (progress.collection.empty() ? "" : progress.collection + ": ") + progress.file;
            if (busy.active()) {
                busy.report("extracting " + where + ", file " +
                                std::to_string(progress.file_index) + " of " +
                                std::to_string(progress.file_count) + failed,
                            static_cast<std::size_t>(progress.chunks_done) + 1,
                            static_cast<std::size_t>(progress.chunks_total));
            } else if (!quiet) {
                std::cerr << "[graph] extracting " << where << " (file " << progress.file_index
                          << "/" << progress.file_count << ", chunk " << progress.chunks_done + 1
                          << "/" << progress.chunks_total << failed << ")\n";
            }
        } else if (busy.active()) {
            busy.set("embedding " + std::to_string(progress.entities) + " entities");
        } else if (!quiet) {
            std::cerr << "[graph] embedding " << progress.entities << " entities\n";
        }
    };
    if (request.dry_run) {
        options.on_extract = [&busy](const embedstore::Chunk& chunk,
                                     const graph::ExtractResult& result) {
            busy.above([&] { print_dry_run_extraction(chunk, result); });
        };
    }
    // Failed chunks print as they happen -- a count alone cannot tell a
    // flaky backend from a model that cannot produce the JSON. A dry run
    // prints the whole error; a real build keeps it to one line.
    options.on_chunk_failed = [&busy, dry_run = request.dry_run](const embedstore::Chunk& chunk,
                                                                 std::string_view error) {
        busy.above([&] {
            std::cout << "\n"
                      << chunk.source << " [chunk " << chunk.ordinal
                      << "] FAILED: " << (dry_run ? std::string{error} : preview_text(error, 200))
                      << "\n";
        });
    };
    return options;
}

// ---- The code layer (27k) ----------------------------------------------------

/// The skipped files, grouped by why, each named -- up to ten per reason
/// unless `all`, the rest counted with the flag that names them.
void print_skipped(const graph::SourceBuildResult& result, bool all) {
    if (result.skipped.empty()) {
        return;
    }
    std::map<std::string, std::vector<std::string>> by_reason;
    for (const graph::SkippedFile& file : result.skipped) {
        by_reason[file.reason].push_back(file.member + "/" + file.file);
    }
    std::cout << "  Skipped:          " << result.skipped.size()
              << " file(s) -- named, never parsed:\n";
    constexpr std::size_t kNamed = 10;
    for (const auto& [reason, files] : by_reason) {
        std::cout << "    " << reason << ": " << files.size() << "\n";
        for (std::size_t i = 0; i < files.size(); ++i) {
            if (!all && i == kNamed) {
                std::cout << "      ... and " << files.size() - kNamed
                          << " more (--show-skipped names every one)\n";
                break;
            }
            std::cout << "      " << files[i] << "\n";
        }
    }
}

void print_code_summary(const std::string& name, const GraphSourceBuild& build, bool show_skipped) {
    const graph::SourceBuildResult& result = build.result;
    for (const std::string& missing : build.missing) {
        std::cout << "[graph] source tree " << missing
                  << " is not a directory -- it contributes nothing, and what it contributed "
                     "is forgotten.\n";
    }
    for (const std::string& empty : build.empty) {
        std::cout << "[graph] source tree " << empty
                  << " offers no file to read -- inside a git repository, only what git tracks "
                     "or does not ignore is read.\n";
    }
    if (result.cancelled) {
        std::cout << "Code graph for \"" << name << "\" interrupted after " << result.files_parsed
                  << " parsed file(s); they are cached, and the next build or update links "
                     "them.\n";
        return;
    }
    std::string languages;
    for (const auto& [language, count] : result.files_by_language) {
        languages += (languages.empty() ? "" : ", ") + language + " " + std::to_string(count);
    }
    std::cout << (result.dry_run ? "Dry run -- code graph for \"" : "Code graph for \"") << name
              << "\" (no model):\n";
    std::cout << "  Files used:       " << result.files_used;
    if (!languages.empty()) {
        std::cout << " (" << languages << ")";
    }
    std::cout << " -- " << result.files_parsed << " parsed, " << result.files_unchanged
              << " unchanged since the last build\n";
    if (result.files_removed > 0) {
        std::cout << "  Files forgotten:  " << result.files_removed << "\n";
    }
    for (const auto& [directory, count] : result.excluded) {
        std::cout << "  Excluded:         " << directory << " (" << count
                  << " file(s); vendored or build output)\n";
    }
    if (!result.partial.empty()) {
        std::cout << "  Partial:          " << result.partial.size()
                  << " file(s) parsed around a syntax error -- what parsed is kept:\n";
        for (const std::string& file : result.partial) {
            std::cout << "      " << file << "\n";
        }
    }
    const graph::ResolveCounts& counts = result.counts;
    std::cout << "  References:       " << counts.resolved() << " resolved, " << counts.unresolved()
              << " unresolved (calls " << counts.calls_resolved << "/" << counts.calls << ", types "
              << counts.types_resolved << "/" << counts.types << ", imports "
              << counts.imports_resolved << "/" << counts.imports << ", bases "
              << counts.inherits_resolved << "/" << counts.inherits
              << ") -- an unresolved one is a name node, never a guess\n";
    if (result.dry_run) {
        std::cout << "  Would store:      " << result.nodes << " code node(s), " << result.edges
                  << " edge(s) -- nothing was stored.\n";
    } else {
        std::cout << "  Code graph:       " << result.nodes << " node(s), " << result.edges
                  << " edge(s) (nodes +" << result.sync.nodes_added << " ~"
                  << result.sync.nodes_changed << " -" << result.sync.nodes_removed << "; edges +"
                  << result.sync.edges_added << " -" << result.sync.edges_removed << ")\n";
    }
    print_skipped(result, show_skipped);
}

/// Parses a named graph's source trees into `store` -- the model-free half
/// of a build, and all of an update. Nothing here builds a provider.
void build_code(embedstore::Store& store, const Target& target,
                const harness::NamedGraphConfig& named, bool force, bool dry_run, bool quiet,
                bool show_skipped, bool update) {
    std::string labels;
    for (const std::string& source : named.sources) {
        labels += (labels.empty() ? "" : ", ") + source_member_label(source);
    }
    std::cout << (update ? "Updating" : "Parsing") << " the code of \"" << target.name
              << "\" from [" << labels << "] -- tree-sitter, no model...\n";
    GraphSourceBuild build;
    {
        BusyLine busy{std::cerr, "parsing", busy_options(quiet)};
        graph::SourceBuildOptions options;
        options.force = force;
        options.dry_run = dry_run;
        options.on_progress = [&busy, quiet](const graph::SourceProgress& progress) {
            if (!progress.parsing) {
                return;  // a file read from the cache is not news
            }
            if (busy.active()) {
                busy.report("parsing " + progress.member + "/" + progress.file,
                            static_cast<std::size_t>(progress.index),
                            static_cast<std::size_t>(progress.count));
            } else if (!quiet) {
                std::cerr << "[graph] parsing " << progress.member << "/" << progress.file << " ("
                          << progress.index << "/" << progress.count << ")\n";
            }
        };
        try {
            build = build_graph_sources(store, named, options);
        } catch (const std::exception& e) {
            busy.finish();
            fail_backend(std::string{"code graph build failed: "} + e.what());
        }
    }
    if (!dry_run && !build.result.cancelled) {
        store.set_graph_meta(embedstore::kGraphMetaGraphName, target.name);
    }
    print_code_summary(target.name, build, show_skipped);
}

/// `--source` / `--lang`: the named graph `request.name`, with the trees and
/// the grammars added -- made when it is new, written through the one config
/// editor (a dry run writes nothing and builds what would be written).
[[nodiscard]] harness::NamedGraphConfig register_sources(const harness::Config& config,
                                                         const std::filesystem::path& config_path,
                                                         const GraphBuildRequest& request) {
    const std::string& name = request.name;
    require_plain_name(name);
    const harness::NamedGraphConfig* existing = config.find_graph(name);
    if (existing == nullptr &&
        (config.find_embedding(name) != nullptr || file_exists(collection_path(name)))) {
        fail_user("'" + name +
                  "' is a collection -- a source tree builds into a named graph. Name one with "
                  "--graph <name>; it may list '" +
                  name + "' as a member: apogee config add-graph <name> --collections " + name +
                  " --sources <dir>");
    }
    harness::NamedGraphConfig entry = existing != nullptr ? *existing : harness::NamedGraphConfig{};
    bool changed = existing == nullptr;
    std::vector<std::string> added;
    for (const std::string& source : request.sources) {
        std::error_code code;
        if (!std::filesystem::is_directory(source, code)) {
            fail_user("--source " + source + ": not a directory");
        }
        const std::string absolute = absolute_source_path(source);
        if (std::ranges::find(entry.sources, absolute) == entry.sources.end()) {
            entry.sources.push_back(absolute);
            added.push_back(absolute);
            changed = true;
        }
    }
    if (!request.languages.empty()) {
        std::vector<std::string> names;
        for (const std::string& language : request.languages) {
            const graph::CodeLanguage* known = graph::code_language_by_name(language);
            if (known == nullptr) {
                fail_user("--lang " + language +
                          ": no vendored grammar -- one of: " + graph::code_language_names());
            }
            if (std::ranges::find(names, std::string{known->name}) == names.end()) {
                names.emplace_back(known->name);
            }
        }
        if (names != entry.languages) {
            entry.languages = std::move(names);
            changed = true;
        }
    }
    if (entry.sources.empty()) {
        fail_user("graph '" + name +
                  "' has no source trees -- --lang applies to them; add one "
                  "with --source <dir>");
    }
    const NamedGraphValidation validation = validate_named_graph(config, name, entry);
    if (!validation.error.empty()) {
        fail_user(validation.error);
    }
    for (const std::string& warning : validation.warnings) {
        std::cerr << "apogee graph: warning: " << warning << "\n";
    }
    if (changed && !request.dry_run) {
        try {
            harness::edit_config_file(config_path, [&](std::string_view content) {
                return harness::append_graph(content, name, entry, existing != nullptr);
            });
        } catch (const std::exception& e) {
            fail_backend("could not record graph '" + name + "' in " + config_path.string() + ": " +
                         e.what());
        }
        if (existing == nullptr) {
            std::cout << "added graph '" << name << "' to " << config_path.string() << "\n";
        }
        for (const std::string& source : added) {
            std::cout << "graph '" << name << "' parses source tree '"
                      << source_member_label(source) << "' (" << source << ")\n";
        }
    }
    return entry;
}

/// A collection's own graph: extract into its database, then record that
/// the graph exists through the one config editor.
void build_collection(const harness::Config& config, const std::filesystem::path& config_path,
                      const Target& target, const GraphBuildRequest& request) {
    embedstore::Store store = open_existing(target.name);
    if (store.chunk_count() == 0) {
        fail_user("collection '" + target.name +
                  "' has no chunks to extract from -- ingest documents first with 'apogee embed "
                  "ingest'");
    }
    const Providers providers{config, config_path};
    const Generation generation =
        resolve_generation(config, providers.harness, request.model, target, "graph build");
    const graph::EntityEmbedder entity_embedder =
        resolve_embedder(providers.harness, config, target);
    if (!entity_embedder.embed && !request.dry_run) {
        std::cout << "[graph] entity vectors skipped for \"" << target.name << "\" -- "
                  << entity_embedder.note << "; entity search will be full-text only.\n";
    }
    std::cout << "Building the knowledge graph for \"" << target.name << "\" with "
              << generation.key << "...\n";
    graph::BuildResult result;
    {
        BusyLine busy{std::cerr, "extracting", busy_options(request.quiet)};
        try {
            result = graph::build(
                store, graph::make_structured_extractor(providers.harness, generation.key),
                entity_embedder.embed,
                build_options(generation, entity_embedder.model, request, busy));
        } catch (const std::exception& e) {
            busy.finish();
            fail_backend(std::string{"graph build failed: "} + e.what());
        }
    }
    print_build_summary(target.name, result);
    if (request.dry_run || result.cancelled) {
        return;
    }
    // The first successful build records that the graph exists, through the
    // one path that writes a config file -- the same edit the admin twin
    // makes. Reported and never fatal: the graph is already built.
    if (target.entry == nullptr) {
        try {
            harness::edit_config_file(config_path, [&](std::string_view content) {
                return harness::append_embedding(content, target.name, harness::EmbeddingConfig{},
                                                 false);
            });
            std::cout << "registered '" << target.name << "' in " << config_path.string() << "\n";
        } catch (const std::exception& e) {
            std::cerr << "apogee graph: could not register '" << target.name << "' in config -- "
                      << e.what() << "\n";
        }
    }
    if (target.entry == nullptr || !target.entry->graph.enabled) {
        try {
            harness::edit_config_file(config_path, [&](std::string_view content) {
                return harness::set_embedding_graph_enabled(content, target.name, true);
            });
            std::cout << "graph.enabled set on '" << target.name
                      << "' -- retrieval expands through it from now on\n";
        } catch (const std::exception& e) {
            std::cerr << "apogee graph: could not set graph.enabled on '" << target.name << "' -- "
                      << e.what() << "\n";
        }
    }
}

/// A named graph: its source trees parsed (no model), then every member's
/// chunks extracted, into the graph's own database, created by this build;
/// nothing to enable -- the entry is the enablement.
void build_named(const harness::Config& config, const std::filesystem::path& config_path,
                 const Target& target, const GraphBuildRequest& request) {
    const harness::NamedGraphConfig& named = *target.named;
    if (named.collections.empty() && named.sources.empty()) {
        fail_user("graph '" + target.name +
                  "' has no member collections or source trees -- add some with 'apogee config "
                  "add-graph " +
                  target.name +
                  " --collections <a,b> --force', or 'apogee graph build --source "
                  "<dir> --graph " +
                  target.name + "'");
    }
    // The graph database is created by the first real build. A dry run over
    // a graph that does not exist yet plans against an in-memory store, so
    // its footprint is zero -- not even the file.
    std::error_code code;
    if (!request.dry_run) {
        std::filesystem::create_directories(target.db_path.parent_path(), code);
    }
    const bool built = file_exists(target.db_path);
    embedstore::Store store{request.dry_run && !built ? std::filesystem::path{":memory:"}
                                                      : target.db_path};

    // The code first: free, local, and the half that needs no backend.
    if (!named.sources.empty()) {
        build_code(store, target, named, request.force, request.dry_run, request.quiet,
                   request.show_skipped, false);
        if (named.collections.empty()) {
            return;
        }
        std::cout << "\n";
    }

    const GraphMembers members = open_graph_members(named);
    for (const auto& [collection, member_store] : members.stores) {
        if (member_store == nullptr) {
            std::cout << "[graph] member collection \"" << collection
                      << "\" has no database -- it contributes nothing this build.\n";
        }
    }
    if (!members.any_data()) {
        if (!named.sources.empty()) {
            std::cout << "[graph] no member collection of \"" << target.name
                      << "\" has any data yet -- only its code was built.\n";
            return;
        }
        fail_user("no member collection of graph '" + target.name +
                  "' has any data -- ingest documents first with 'apogee embed ingest'");
    }
    const Providers providers{config, config_path};
    const Generation generation =
        resolve_generation(config, providers.harness, request.model, target, "graph build");
    const graph::EntityEmbedder entity_embedder =
        resolve_embedder(providers.harness, config, target);
    if (!entity_embedder.embed && !request.dry_run) {
        std::cout << "[graph] entity vectors skipped for \"" << target.name << "\" -- "
                  << entity_embedder.note << "; entity search will be full-text only.\n";
    }
    std::cout << "Building knowledge graph \"" << target.name << "\" over ["
              << join(named.collections) << "] with " << generation.key << "...\n";
    graph::BuildResult result;
    {
        BusyLine busy{std::cerr, "extracting", busy_options(request.quiet)};
        graph::BuildOptions options =
            build_options(generation, entity_embedder.model, request, busy);
        options.graph_name = target.name;
        try {
            result = graph::build_multi(
                store, members.members(named),
                graph::make_structured_extractor(providers.harness, generation.key),
                entity_embedder.embed, options);
        } catch (const std::exception& e) {
            busy.finish();
            fail_backend(std::string{"graph build failed: "} + e.what());
        }
    }
    print_build_summary(target.name, result);
}

/// `graph update`: a named graph's source trees, re-parsed where their
/// content changed and re-linked whole -- never a model call, never a
/// provider built.
void run_graph_update(const RootContext& context, const UpdateFlags& flags) {
    std::filesystem::path config_path;
    const harness::Config config = load_config_strict(context, config_path);
    const Target target = resolve_target(config, flags.name);
    if (target.named == nullptr) {
        fail_user("'" + target.name +
                  "' is not a named graph -- `graph update` refreshes a named graph's source "
                  "trees; a collection's graph is rebuilt with `apogee graph build " +
                  target.name + "`");
    }
    const harness::NamedGraphConfig& named = *target.named;
    if (!graph_updatable(named)) {
        fail_user("graph '" + target.name +
                  "' has no source trees -- add one with `apogee graph build --source <dir> "
                  "--graph " +
                  target.name + "`; its collections are extracted by `apogee graph build " +
                  target.name + "`");
    }
    std::error_code code;
    std::filesystem::create_directories(target.db_path.parent_path(), code);
    embedstore::Store store{target.db_path};
    build_code(store, target, named, flags.force, false, flags.quiet, flags.show_skipped, true);
    if (!named.collections.empty()) {
        std::cout << "Its collections [" << join(named.collections)
                  << "] are left as they are: `apogee graph build " << target.name
                  << "` extracts them, a model call per stale chunk -- `update` never makes "
                     "one.\n";
    }
}

void print_named_stats(const Target& target) {
    const harness::NamedGraphConfig& named = *target.named;
    if (!file_exists(target.db_path)) {
        std::cout << "No graph built for \"" << target.name << "\". Run: apogee graph build "
                  << target.name << "\n";
        return;
    }
    const embedstore::Store store{target.db_path};
    const GraphMembers members = open_graph_members(named);
    const embedstore::GraphStatsMulti stats = store.graph_stats_multi(members.views());
    if (!stats.totals.built()) {
        std::cout << "No graph built for \"" << target.name << "\". Run: apogee graph build "
                  << target.name << "\n";
        return;
    }
    std::cout << "Knowledge graph \"" << target.name << "\"";
    if (!named.collections.empty()) {
        std::cout << " over [" << join(named.collections) << "]";
    }
    if (!named.sources.empty()) {
        std::vector<std::string> labels;
        for (const std::string& source : named.sources) {
            labels.push_back(source_member_label(source));
        }
        std::cout << (named.collections.empty() ? " from" : " and") << " source tree(s) ["
                  << join(labels) << "]";
    }
    std::cout << ":\n";
    print_stats_body(target.name, stats.totals);
    if (const std::string embed_model = store.graph_meta(embedstore::kGraphMetaEmbedModel);
        !embed_model.empty()) {
        std::cout << "  Embedder:  " << embed_model << " (the graph's own entity-vector model)\n";
    }
    if (!stats.members.empty()) {
        std::cout << "  Members:\n";
    }
    for (const embedstore::MemberStats& member : stats.members) {
        std::cout << "    " << member.collection << ": " << member.mentions << " mention(s), "
                  << member.chunks_with_mentions << "/" << member.total_chunks
                  << " chunk(s) covered";
        if (member.stale_files > 0) {
            std::cout << ", " << member.stale_files << " stale file(s)";
        }
        if (member.missing) {
            std::cout << " -- database missing";
        }
        std::cout << "\n";
    }
    std::vector<std::string> current = named.collections;
    std::ranges::sort(current);
    if (const std::vector<std::string> as_built = store.graph_members();
        !as_built.empty() && !current.empty() && as_built != current) {
        std::cout << "  Note: membership changed since the last build (was [" << join(as_built)
                  << "]) -- run `apogee graph build " << target.name << "` to converge.\n";
    }
}

}  // namespace

bool graph_updatable(const harness::NamedGraphConfig& graph) noexcept {
    return !graph.sources.empty();
}

void run_graph_build(const RootContext& context, const GraphBuildRequest& request) {
    std::filesystem::path config_path;
    harness::Config config = load_config_strict(context, config_path);
    // `--source`/`--lang` (27k) shape the named graph before it builds: the
    // entry written (and re-read), or -- in a dry run -- held in memory.
    std::optional<harness::NamedGraphConfig> pending;
    if (!request.sources.empty() || !request.languages.empty()) {
        pending = register_sources(config, config_path, request);
        if (!request.dry_run) {
            config = load_config_strict(context, config_path);
            pending.reset();
        }
    }
    Target target = resolve_target(config, request.name);
    if (pending.has_value()) {
        target.named = &*pending;
        target.entry = nullptr;
        target.db_path = agentloop::graph_db_path(request.name);
    }
    if (target.named != nullptr) {
        build_named(config, config_path, target, request);
    } else {
        build_collection(config, config_path, target, request);
    }
}

std::string_view GraphCommand::name() const noexcept {
    return "graph";
}

std::string_view GraphCommand::summary() const noexcept {
    return "Build and inspect the knowledge graph over a collection or a named graph";
}

void GraphCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    // ---- build ---------------------------------------------------------------
    auto b = std::make_shared<BuildFlags>();
    CLI::App* build = cmd->add_subcommand(
        "build",
        "Extract entities and relations from a collection's or a named graph's stale chunks, "
        "and parse its source trees (no model)");
    build->add_option("NAME", b->name, "A named graph (a graphs: entry) or a collection")
        ->type_name(kGraphValue);
    build
        ->add_option("--graph", b->graph,
                     "The named graph to build -- the same as NAME; with --source, made when new")
        ->type_name(kNamedGraphValue);
    build
        ->add_option("--source", b->sources,
                     "A source tree to parse into the named graph with tree-sitter -- no model, no "
                     "key, no network (repeatable; recorded on the graph's entry)")
        ->type_name(kPathValue);
    {
        std::vector<std::string> names;
        for (const graph::CodeLanguage& language : graph::code_languages()) {
            names.emplace_back(language.name);
        }
        build
            ->add_option("--lang", b->languages,
                         "Parse only these languages (repeatable; recorded on the graph's entry)")
            ->check(CLI::IsMember(names));
    }
    build->add_flag("--show-skipped", b->show_skipped,
                    "Name every file the code build skipped, however many");
    build
        ->add_option("-m,--model", b->model,
                     "The extraction backend (default: the entry's extract_backend, then the "
                     "extraction role, then the default -- a metered default is refused)")
        ->type_name(kBackendValue);
    build->add_flag("--dry-run", b->dry_run,
                    "Print every chunk's extraction and a summary; store nothing");
    build->add_flag("--force", b->force, "Re-extract every source, stale or not");
    build->add_option("--limit", b->limit, "Stop after N chunks (0 = no limit)");
    build->add_flag("-q,--quiet", b->quiet, "No progress: no busy line, no per-chunk lines");
    build->callback([&context, b]() {
        if (!b->name.empty() && !b->graph.empty() && b->name != b->graph) {
            fail_user("NAME '" + b->name + "' and --graph '" + b->graph +
                      "' name two graphs -- give one");
        }
        const std::string name = b->graph.empty() ? b->name : b->graph;
        if (name.empty()) {
            fail_user("which graph? -- give NAME, or --graph <name> with --source <dir>");
        }
        run_graph_build(context, GraphBuildRequest{.name = name,
                                                   .model = b->model,
                                                   .dry_run = b->dry_run,
                                                   .force = b->force,
                                                   .limit = b->limit,
                                                   .quiet = b->quiet,
                                                   .sources = b->sources,
                                                   .languages = b->languages,
                                                   .show_skipped = b->show_skipped});
    });

    // ---- update --------------------------------------------------------------
    auto u = std::make_shared<UpdateFlags>();
    CLI::App* update = cmd->add_subcommand(
        "update",
        "Re-parse a named graph's source trees where files changed -- never a model call");
    update->add_option("NAME", u->name, "A named graph with source trees")
        ->type_name(kSourcedGraphValue)
        ->required();
    update->add_flag("--force", u->force, "Re-parse every file, changed or not");
    update->add_flag("-q,--quiet", u->quiet, "No progress: no busy line, no per-file lines");
    update->add_flag("--show-skipped", u->show_skipped,
                     "Name every file the build skipped, however many");
    update->callback([&context, u]() { run_graph_update(context, *u); });

    // ---- stats ---------------------------------------------------------------
    auto s_name = std::make_shared<std::string>();
    CLI::App* stats =
        cmd->add_subcommand("stats", "Show a graph's counts, coverage, and build state");
    stats->add_option("NAME", *s_name, "A named graph or a collection")
        ->type_name(kGraphValue)
        ->required();
    stats->callback([&context, s_name]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const Target target = resolve_target(config, *s_name);
        if (target.named != nullptr) {
            print_named_stats(target);
            return;
        }
        const embedstore::Store store = open_existing(target.name);
        const embedstore::GraphStats st = store.graph_stats();
        if (!st.built()) {
            std::cout << "No graph built for \"" << target.name << "\". Run: apogee graph build "
                      << target.name << "\n";
            return;
        }
        std::cout << "Knowledge graph for \"" << target.name << "\":\n";
        print_stats_body(target.name, st);
    });

    // ---- show ----------------------------------------------------------------
    auto sh_name = std::make_shared<std::string>();
    auto sh_entity = std::make_shared<std::string>();
    auto sh_chunks = std::make_shared<int>(3);
    CLI::App* show =
        cmd->add_subcommand("show", "One entity: its relations and the chunks that support it");
    show->add_option("NAME", *sh_name, "A named graph or a collection")
        ->type_name(kGraphValue)
        ->required();
    show->add_option("ENTITY", *sh_entity, "The entity's name (case-insensitive), or a record id")
        ->required();
    show->add_option("--chunks", *sh_chunks, "Supporting chunks to print (default 3; 0 = all)");
    show->callback([&context, sh_name, sh_entity, sh_chunks]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const Target target = resolve_target(config, *sh_name);
        const embedstore::Store store = open_target(target);
        if (!store.graph_stats().built()) {
            fail_user("no graph built for '" + target.name + "' -- run: apogee graph build " +
                      target.name);
        }
        std::vector<embedstore::GraphNode> nodes = store.find_nodes(*sh_entity);
        if (nodes.empty()) {
            // Exact by normalised name, then the entity index for a partial.
            const std::vector<embedstore::NodeResult> hits = store.search_nodes(*sh_entity, 5);
            if (hits.empty()) {
                fail_user("no entity matching '" + *sh_entity + "' in '" + target.name + "'");
            }
            std::cout << "No exact match for \"" << *sh_entity
                      << "\"; closest: " << hits.front().node.name << "\n";
            if (hits.size() > 1) {
                std::cout << "Also matched:";
                for (std::size_t i = 1; i < hits.size(); ++i) {
                    std::cout << (i == 1 ? " " : ", ") << hits[i].node.name;
                }
                std::cout << "\n";
            }
            nodes.push_back(hits.front().node);
        }
        if (target.named != nullptr) {
            const GraphMembers members = open_graph_members(*target.named);
            for (const embedstore::GraphNode& node : nodes) {
                print_named_node(store, node, members, *sh_chunks);
            }
            return;
        }
        for (const embedstore::GraphNode& node : nodes) {
            print_node(store, node, *sh_chunks);
        }
    });

    // ---- path, explain, neighbors, query (27l) ---------------------------------
    bind_graph_navigation(*cmd, context);

    // ---- report, export html|graphml|mermaid (27m) ------------------------------
    bind_graph_artifacts(*cmd, context);

    // ---- communities -----------------------------------------------------------
    auto c = std::make_shared<CommunitiesFlags>();
    CLI::App* communities =
        cmd->add_subcommand("communities",
                            "Detect and summarise thematic communities; each summary becomes a "
                            "retrievable chunk");
    communities->add_option("NAME", c->name, "A named graph or a collection")
        ->type_name(kGraphValue)
        ->required();
    communities
        ->add_option("-m,--model", c->model,
                     "The summariser backend (default: as graph build resolves it)")
        ->type_name(kBackendValue);
    communities->add_flag("--force", c->force,
                          "Re-summarise every community, membership unchanged or not");
    communities->add_option("--min-size", c->min_size,
                            "The smallest community to summarise (default 3)");
    communities->add_flag("--list", c->list, "List the stored communities; summarise nothing");
    communities->add_flag("-q,--quiet", c->quiet,
                          "No progress: no busy line, no per-community lines");
    communities->add_flag("--no-summaries", c->no_summaries,
                          "Cluster only -- no model call; the summaries are reported absent");
    communities->callback([&context, c]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const Target target = resolve_target(config, c->name);
        embedstore::Store store = open_target(target);
        if (c->list) {
            print_communities(store, target.name);
            return;
        }
        if (store.graph_stats().edges == 0) {
            fail_user("graph '" + target.name +
                      "' has no relations to cluster -- build it first: apogee graph build " +
                      target.name);
        }
        // Clustering is deterministic and needs no model; a summary is one
        // generation call (27k). With nothing to summarise with -- or told
        // not to -- the clusters are stored and the summaries said absent.
        std::optional<Providers> providers;
        std::optional<Generation> generation;
        graph::EntityEmbedder embedder;
        std::string absent;
        if (c->no_summaries) {
            absent = "--no-summaries";
        } else if (c->model.empty() &&
                   harness::resolve_backend(
                       config, harness::RoleRequest{.role = harness::ModelRole::Extraction,
                                                    .override = "",
                                                    .entry_backend = target.entry_backend()})
                       .key.empty()) {
            absent = "no generation backend is configured";
        } else {
            providers.emplace(config, config_path);
            generation = resolve_generation(
                config, providers->harness, c->model, target, "community summary run",
                "--no-summaries to cluster with no model, the summaries reported absent");
            embedder = resolve_embedder(providers->harness, config, target);
            if (!embedder.embed) {
                std::cout << "[graph] summary vectors skipped for \"" << target.name << "\" -- "
                          << embedder.note << "; summaries will be full-text searchable only.\n";
            }
        }
        graph::CommunitiesOptions options;
        options.model = generation.has_value() ? generation->kg_model : std::string{};
        options.force = c->force;
        options.min_size = c->min_size;
        if (generation.has_value()) {
            std::cout << "Detecting and summarising communities for \"" << target.name << "\" with "
                      << generation->key << "...\n";
        } else {
            std::cout << "Detecting communities for \"" << target.name
                      << "\" -- no model; summaries absent (" << absent << ")...\n";
        }
        graph::CommunitiesResult result;
        {
            BusyLine busy{std::cerr, "detecting communities", busy_options(c->quiet)};
            options.on_progress = [&busy, quiet = c->quiet](int done, int total) {
                if (busy.active()) {
                    busy.report("summarising communities", static_cast<std::size_t>(done) + 1,
                                static_cast<std::size_t>(total));
                } else if (!quiet) {
                    std::cerr << "[graph] summarising community " << done + 1 << "/" << total
                              << "\n";
                }
            };
            try {
                result = graph::build_communities(
                    store,
                    generation.has_value()
                        ? graph::make_summarizer(providers->harness, generation->key)
                        : graph::SummarizeFn{},
                    embedder.embed, options);
            } catch (const std::exception& e) {
                busy.finish();
                fail_backend(std::string{"community build failed: "} + e.what());
            }
        }
        if (result.cancelled) {
            std::cout << "Community build for \"" << target.name
                      << "\" interrupted; what finished is stored.\n";
            return;
        }
        std::cout << "Communities for \"" << target.name << "\": " << result.detected
                  << " detected -- " << result.summarized << " summarised, " << result.unchanged
                  << " unchanged, " << result.pruned << " pruned";
        if (result.clustered > 0) {
            std::cout << ", " << result.clustered << " clustered without a summary";
        }
        if (result.failed > 0) {
            std::cout << ", " << result.failed << " failed (re-run to retry)";
        }
        if (result.embedded > 0) {
            std::cout << ", " << result.embedded << " embedded";
        }
        std::cout << ".\n";
        if (result.summaries_absent > 0) {
            std::cout << "Summaries absent: " << result.summaries_absent << " communit"
                      << (result.summaries_absent == 1 ? "y" : "ies") << " stored without one"
                      << (absent.empty() ? std::string{} : " (" + absent + ")")
                      << " -- clustering needs no model; `apogee graph communities " << target.name
                      << " -m <backend>` summarises them.\n";
        }
        if (!result.embed_error.empty()) {
            std::cout << "Warning: summary embedding stopped early (" << result.embed_error
                      << ") -- summaries stay full-text searchable; re-run to embed them.\n";
        }
        std::cout << "\n";
        print_communities(store, target.name, kCommunitiesAfterRun);
    });

    // ---- dedupe --------------------------------------------------------------
    auto d = std::make_shared<DedupeFlags>();
    CLI::App* dedupe = cmd->add_subcommand(
        "dedupe", "Merge same-type entities whose vectors say they are the same thing");
    dedupe->add_option("NAME", d->name, "A named graph or a collection")
        ->type_name(kGraphValue)
        ->required();
    dedupe->add_option("--threshold", d->threshold,
                       "Cosine similarity at or above which two entities merge (default 0.92)");
    dedupe->add_flag("--dry-run", d->dry_run, "Print the merge groups; change nothing");
    dedupe->callback([&context, d]() {
        if (d->threshold <= 0.0 || d->threshold > 1.0) {
            fail_user("--threshold must be in (0, 1]");
        }
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const Target target = resolve_target(config, d->name);
        embedstore::Store store = open_target(target);
        // What each layer's dedupe is (27k), read once beside the admin
        // twin: a code entity's identity is its exact qualified name, so its
        // merges are the ones the build made; a prose entity merges by vector
        // agreement, and with no vector to compare the pass is skipped --
        // said, never silently.
        const embedstore::DedupeScope scope = store.dedupe_scope();
        if (scope.code_entities > 0) {
            std::cout << "Code entities: " << scope.code_entities
                      << " -- merged by exact qualified name as they were built ("
                      << scope.code_identity_merges
                      << " stated at more than one place, one node each); never by "
                         "resemblance.\n";
        }
        if (const std::string skip = scope.prose_skip_reason(); !skip.empty()) {
            std::cout << "Prose entities: vector dedupe skipped -- " << skip
                      << "; nothing to merge.\n";
            return;
        }
        std::vector<embedstore::MergeGroup> groups;
        try {
            groups = store.dedupe_nodes(d->threshold, d->dry_run);
        } catch (const std::exception& e) {
            fail_backend(std::string{"dedupe failed: "} + e.what());
        }
        if (groups.empty()) {
            std::cout << "No entities in \"" << target.name << "\" exceed similarity "
                      << d->threshold << " -- nothing to merge.\n";
            return;
        }
        const std::string verb = d->dry_run ? "Would merge" : "Merged";
        std::size_t merged = 0;
        for (const embedstore::MergeGroup& group : groups) {
            std::vector<std::string> names;
            for (const embedstore::GraphNode& node : group.merged) {
                names.push_back(node.name);
            }
            merged += names.size();
            std::cout << verb << " into " << group.kept.name << " (" << group.kept.type
                      << "): " << join(names) << "\n";
        }
        std::cout << groups.size() << " group(s), " << merged << " entity(ies) "
                  << (d->dry_run ? "would merge" : "merged") << " at threshold " << d->threshold
                  << ".\n";
        if (d->dry_run) {
            std::cout << "Dry run -- nothing was changed.\n";
        } else {
            std::cout << "Note: community memberships changed -- re-run `apogee graph communities "
                      << target.name << "` to refresh the summaries.\n";
        }
    });

    // ---- delete --------------------------------------------------------------
    auto d_name = std::make_shared<std::string>();
    CLI::App* del = cmd->add_subcommand(
        "delete",
        "Clear a collection's graph, or remove a named graph's database (config "
        "entries and chunks stay)");
    del->add_option("NAME", *d_name, "A named graph or a collection")
        ->type_name(kGraphValue)
        ->required();
    del->callback([&context, d_name]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const Target target = resolve_target(config, *d_name);
        if (target.named != nullptr) {
            if (!file_exists(target.db_path)) {
                std::cout << "No graph to delete for \"" << target.name << "\".\n";
                return;
            }
            embedstore::GraphStats st;
            {
                const embedstore::Store store{target.db_path};
                st = store.graph_stats();
            }
            std::error_code code;
            if (!std::filesystem::remove(target.db_path, code) || code) {
                fail_backend("could not delete the graph database " + target.db_path.string() +
                             ": " + code.message());
            }
            // Absent unless SQLite left one behind.
            for (const char* sidecar : {"-wal", "-shm"}) {
                std::filesystem::remove(target.db_path.string() + sidecar, code);
            }
            std::cout << "Deleted graph \"" << target.name << "\": " << st.nodes << " node(s), "
                      << st.edges << " edge(s), " << st.mentions
                      << " mention(s). The graphs: config entry is untouched (`apogee config "
                         "delete-graph "
                      << target.name << "` removes it).\n";
            return;
        }
        embedstore::Store store = open_existing(target.name);
        const embedstore::GraphStats st = store.graph_stats();
        if (!st.built()) {
            std::cout << "No graph to delete for \"" << target.name << "\".\n";
            return;
        }
        store.delete_graph();
        std::cout << "Deleted the graph for \"" << target.name << "\": " << st.nodes << " node(s), "
                  << st.edges << " edge(s), " << st.mentions
                  << " mention(s). The chunks are untouched; the next build starts from scratch.\n";
    });
}

}  // namespace apogee::commands
