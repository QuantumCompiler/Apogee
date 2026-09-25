#include "commands/graph.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "agentloop/graph_context.h"
#include "backends/factory.h"
#include "commands/embed.h"
#include "commands/helpers.h"
#include "commands/knowledge_core.h"
#include "embedstore/store.h"
#include "graph/build.h"
#include "graph/communities.h"
#include "graph/extract.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "harness/paths.h"
#include "harness/roles.h"

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
                                            const std::string& what) {
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
                  resolved.key + ", " + target.extractor_hint() + ", or set the extraction role");
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

/// The counts every graph form shares.
void print_stats_body(const std::string& name, const embedstore::GraphStats& st) {
    std::cout << "  Nodes:     " << st.nodes << " (" << type_summary(st) << ")\n";
    std::cout << "  Edges:     " << st.edges << "\n";
    std::cout << "  Mentions:  " << st.mentions << "\n";
    const std::int64_t coverage =
        st.total_chunks > 0 ? 100 * st.chunks_with_mentions / st.total_chunks : 0;
    std::cout << "  Coverage:  " << coverage << "% of " << st.total_chunks
              << " chunks carry at least one entity\n";
    std::cout << "  Vectors:   " << st.nodes_with_vectors << "/" << st.nodes
              << " entities embedded\n";
    std::cout << "  Extractor: " << (st.extract_model.empty() ? "(unrecorded)" : st.extract_model)
              << "\n";
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
        std::cout << "  Communities: " << st.communities
                  << " summarised (`apogee graph communities " << name << " --list`)\n";
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
            if (!neighbor.description.empty()) {
                std::cout << " -- " << preview_text(neighbor.description, 100);
            }
            std::cout << "\n";
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

void print_communities(const embedstore::Store& store, const std::string& name) {
    const std::vector<embedstore::GraphCommunity> communities = store.graph_communities();
    if (communities.empty()) {
        std::cout << "No communities stored for \"" << name << "\". Run: apogee graph communities "
                  << name << "\n";
        return;
    }
    for (const embedstore::GraphCommunity& community : communities) {
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
        }
        std::cout << "\n";
    }
}

struct BuildFlags {
    std::string name;
    std::string model;
    bool dry_run = false;
    bool force = false;
    int limit = 0;
};

struct CommunitiesFlags {
    std::string name;
    std::string model;
    bool force = false;
    int min_size = 0;
    bool list = false;
};

struct DedupeFlags {
    std::string name;
    double threshold = embedstore::kDefaultDedupeThreshold;
    bool dry_run = false;
};

/// The build options every build form shares: the progress line, the
/// dry-run printer, failures as they happen.
[[nodiscard]] graph::BuildOptions build_options(const Generation& generation,
                                                const std::string& embed_model,
                                                const GraphBuildRequest& request) {
    graph::BuildOptions options;
    options.model = generation.kg_model;
    options.embed_model = embed_model;
    options.force = request.force;
    options.limit = request.limit;
    options.dry_run = request.dry_run;
    options.on_progress = [](const graph::Progress& progress) {
        if (progress.stage == graph::Progress::Stage::Extract) {
            std::cerr << "[graph] extracting "
                      << (progress.collection.empty() ? "" : progress.collection + ": ")
                      << progress.file << " (file " << progress.file_index << "/"
                      << progress.file_count << ", chunk " << progress.chunks_done + 1 << "/"
                      << progress.chunks_total
                      << (progress.failed > 0 ? ", " + std::to_string(progress.failed) + " failed"
                                              : "")
                      << ")\n";
        } else {
            std::cerr << "[graph] embedding " << progress.entities << " entities\n";
        }
    };
    if (request.dry_run) {
        options.on_extract = print_dry_run_extraction;
    }
    // Failed chunks print as they happen -- a count alone cannot tell a
    // flaky backend from a model that cannot produce the JSON. A dry run
    // prints the whole error; a real build keeps it to one line.
    options.on_chunk_failed = [dry_run = request.dry_run](const embedstore::Chunk& chunk,
                                                          std::string_view error) {
        std::cout << "\n"
                  << chunk.source << " [chunk " << chunk.ordinal
                  << "] FAILED: " << (dry_run ? std::string{error} : preview_text(error, 200))
                  << "\n";
    };
    return options;
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
    try {
        result = graph::build(
            store, graph::make_structured_extractor(providers.harness, generation.key),
            entity_embedder.embed, build_options(generation, entity_embedder.model, request));
    } catch (const std::exception& e) {
        fail_backend(std::string{"graph build failed: "} + e.what());
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

/// A named graph: every member's chunks into the graph's own database,
/// created by this build; nothing to enable -- the entry is the enablement.
void build_named(const harness::Config& config, const std::filesystem::path& config_path,
                 const Target& target, const GraphBuildRequest& request) {
    const harness::NamedGraphConfig& named = *target.named;
    if (named.collections.empty()) {
        fail_user("graph '" + target.name +
                  "' has no member collections -- add some with 'apogee config add-graph " +
                  target.name + " --collections <a,b> --force'");
    }
    const GraphMembers members = open_graph_members(named);
    for (const auto& [collection, store] : members.stores) {
        if (store == nullptr) {
            std::cout << "[graph] member collection \"" << collection
                      << "\" has no database -- it contributes nothing this build.\n";
        }
    }
    if (!members.any_data()) {
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
    graph::BuildOptions options = build_options(generation, entity_embedder.model, request);
    options.graph_name = target.name;
    std::cout << "Building knowledge graph \"" << target.name << "\" over ["
              << join(named.collections) << "] with " << generation.key << "...\n";
    graph::BuildResult result;
    try {
        result =
            graph::build_multi(store, members.members(named),
                               graph::make_structured_extractor(providers.harness, generation.key),
                               entity_embedder.embed, options);
    } catch (const std::exception& e) {
        fail_backend(std::string{"graph build failed: "} + e.what());
    }
    print_build_summary(target.name, result);
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
    std::cout << "Knowledge graph \"" << target.name << "\" over [" << join(named.collections)
              << "]:\n";
    print_stats_body(target.name, stats.totals);
    if (const std::string embed_model = store.graph_meta(embedstore::kGraphMetaEmbedModel);
        !embed_model.empty()) {
        std::cout << "  Embedder:  " << embed_model << " (the graph's own entity-vector model)\n";
    }
    std::cout << "  Members:\n";
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
        !as_built.empty() && as_built != current) {
        std::cout << "  Note: membership changed since the last build (was [" << join(as_built)
                  << "]) -- run `apogee graph build " << target.name << "` to converge.\n";
    }
}

}  // namespace

embedstore::MemberStores GraphMembers::views() const {
    embedstore::MemberStores out;
    for (const auto& [collection, store] : stores) {
        out[collection] = store.get();
    }
    return out;
}

std::vector<graph::Member> GraphMembers::members(const harness::NamedGraphConfig& named) const {
    std::vector<graph::Member> out;
    for (const std::string& collection : named.collections) {
        const auto it = stores.find(collection);
        out.push_back(graph::Member{.collection = collection,
                                    .store = it == stores.end() ? nullptr : it->second.get()});
    }
    return out;
}

bool GraphMembers::any_data() const {
    return std::ranges::any_of(stores, [](const auto& entry) {
        return entry.second != nullptr && entry.second->chunk_count() > 0;
    });
}

GraphMembers open_graph_members(const harness::NamedGraphConfig& named) {
    GraphMembers out;
    for (const std::string& collection : named.collections) {
        const std::filesystem::path path = collection_path(collection);
        out.stores[collection] =
            file_exists(path) ? std::make_unique<embedstore::Store>(path) : nullptr;
    }
    return out;
}

NamedGraphValidation validate_named_graph(const harness::Config& config, std::string_view name,
                                          const harness::NamedGraphConfig& graph) {
    NamedGraphValidation out;
    if (name.empty()) {
        out.error = "a graph needs a name";
        return out;
    }
    if (name.find("..") != std::string_view::npos || name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        out.error = "'" + std::string{name} + "' is not a plain graph name";
        return out;
    }
    if (graph.collections.empty()) {
        out.error = "at least one member collection is required (--collections a,b)";
        return out;
    }
    for (const std::string& collection : graph.collections) {
        if (collection.empty() || collection.find("..") != std::string::npos ||
            collection.find('/') != std::string::npos ||
            collection.find('\\') != std::string::npos) {
            out.error = "'" + collection + "' is not a plain collection name";
            return out;
        }
    }
    // The collision ban: resolution is graphs-first, so a graph sharing a
    // collection's name would make the collection's own graph unreachable.
    const bool on_disk = std::ranges::any_of(collection_names(), [&](const std::string& existing) {
        return harness::CaseInsensitiveLess{}(existing, name) ==
                   harness::CaseInsensitiveLess{}(name, existing) &&
               !harness::CaseInsensitiveLess{}(existing, name);
    });
    if (config.find_embedding(name) != nullptr || on_disk) {
        out.error = "'" + std::string{name} +
                    "' is already a collection name -- graph names must not collide with "
                    "collection names";
        return out;
    }
    if (!graph.extract_backend.empty() && config.find_backend(graph.extract_backend) == nullptr) {
        out.error = "extraction backend '" + graph.extract_backend +
                    "' is not configured (add it with 'apogee config add-backend')";
        return out;
    }
    if (graph.hops < 1 || graph.hops > 2) {
        out.error = "hops must be 1 or 2";
        return out;
    }
    if (graph.max_entities < 1) {
        out.error = "max_entities must be at least 1";
        return out;
    }
    for (const std::string& collection : graph.collections) {
        if (config.find_embedding(collection) == nullptr &&
            !file_exists(collection_path(collection))) {
            out.warnings.push_back("member collection '" + collection +
                                   "' is not configured yet (ingest registers it on first use)");
        }
    }
    return out;
}

void run_graph_build(const RootContext& context, const GraphBuildRequest& request) {
    std::filesystem::path config_path;
    const harness::Config config = load_config_strict(context, config_path);
    const Target target = resolve_target(config, request.name);
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
        "Extract entities and relations from a collection's or a named graph's stale chunks");
    build->add_option("NAME", b->name, "A named graph (a graphs: entry) or a collection")
        ->type_name(kGraphValue)
        ->required();
    build
        ->add_option("-m,--model", b->model,
                     "The extraction backend (default: the entry's extract_backend, then the "
                     "extraction role, then the default -- a metered default is refused)")
        ->type_name(kBackendValue);
    build->add_flag("--dry-run", b->dry_run,
                    "Print every chunk's extraction and a summary; store nothing");
    build->add_flag("--force", b->force, "Re-extract every source, stale or not");
    build->add_option("--limit", b->limit, "Stop after N chunks (0 = no limit)");
    build->callback([&context, b]() {
        run_graph_build(context, GraphBuildRequest{.name = b->name,
                                                   .model = b->model,
                                                   .dry_run = b->dry_run,
                                                   .force = b->force,
                                                   .limit = b->limit});
    });

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
        const Providers providers{config, config_path};
        const Generation generation = resolve_generation(config, providers.harness, c->model,
                                                         target, "community summary run");
        const graph::EntityEmbedder embedder = resolve_embedder(providers.harness, config, target);
        if (!embedder.embed) {
            std::cout << "[graph] summary vectors skipped for \"" << target.name << "\" -- "
                      << embedder.note << "; summaries will be full-text searchable only.\n";
        }
        graph::CommunitiesOptions options;
        options.model = generation.kg_model;
        options.force = c->force;
        options.min_size = c->min_size;
        options.on_progress = [](int done, int total) {
            std::cerr << "[graph] summarising community " << done + 1 << "/" << total << "\n";
        };
        std::cout << "Detecting and summarising communities for \"" << target.name << "\" with "
                  << generation.key << "...\n";
        graph::CommunitiesResult result;
        try {
            result = graph::build_communities(
                store, graph::make_summarizer(providers.harness, generation.key), embedder.embed,
                options);
        } catch (const std::exception& e) {
            fail_backend(std::string{"community build failed: "} + e.what());
        }
        if (result.cancelled) {
            std::cout << "Community build for \"" << target.name
                      << "\" interrupted; what finished is stored.\n";
            return;
        }
        std::cout << "Communities for \"" << target.name << "\": " << result.detected
                  << " detected -- " << result.summarized << " summarised, " << result.unchanged
                  << " unchanged, " << result.pruned << " pruned";
        if (result.failed > 0) {
            std::cout << ", " << result.failed << " failed (re-run to retry)";
        }
        if (result.embedded > 0) {
            std::cout << ", " << result.embedded << " embedded";
        }
        std::cout << ".\n";
        if (!result.embed_error.empty()) {
            std::cout << "Warning: summary embedding stopped early (" << result.embed_error
                      << ") -- summaries stay full-text searchable; re-run to embed them.\n";
        }
        std::cout << "\n";
        print_communities(store, target.name);
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
