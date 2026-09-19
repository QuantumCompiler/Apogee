#include "commands/graph.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "backends/factory.h"
#include "commands/embed.h"
#include "commands/helpers.h"
#include "commands/knowledge_core.h"
#include "embedstore/store.h"
#include "graph/build.h"
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
    if (name.find("..") != std::string_view::npos || name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        fail_user("'" + std::string{name} + "' is not a plain collection name");
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

/// A collection that must already exist: a read never creates an empty
/// database out of a typo.
[[nodiscard]] embedstore::Store open_existing(const std::string& name) {
    require_plain_name(name);
    const std::filesystem::path path = collection_path(name);
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        fail_user("no collection named '" + name +
                  "' -- ingest documents first with 'apogee embed ingest'");
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

void print_node(const embedstore::Store& store, const embedstore::GraphNode& node, int chunks) {
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
    const std::vector<embedstore::Chunk> supporting = store.node_chunks(node.id, chunks);
    if (!supporting.empty()) {
        std::cout << "\nSupporting chunks:\n";
        for (const embedstore::Chunk& chunk : supporting) {
            std::cout << "  " << chunk.source << " [chunk " << chunk.ordinal << "]\n";
            std::cout << indent_lines(preview_text(chunk.text, 240), "    ") << "\n";
        }
    }
}

struct BuildFlags {
    std::string collection;
    std::string model;
    bool dry_run = false;
    bool force = false;
    int limit = 0;
};

}  // namespace

std::string_view GraphCommand::name() const noexcept {
    return "graph";
}

std::string_view GraphCommand::summary() const noexcept {
    return "Build and inspect the knowledge graph over a collection";
}

void GraphCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    // ---- build ---------------------------------------------------------------
    auto b = std::make_shared<BuildFlags>();
    CLI::App* build = cmd->add_subcommand(
        "build", "Extract entities and relations from a collection's stale chunks");
    build->add_option("COLLECTION", b->collection, "The collection to build the graph over")
        ->required();
    build->add_option("-m,--model", b->model,
                      "The extraction backend (default: the collection's graph.extract_backend, "
                      "then the extraction role, then the default -- a metered default is "
                      "refused)");
    build->add_flag("--dry-run", b->dry_run,
                    "Print every chunk's extraction and a summary; store nothing");
    build->add_flag("--force", b->force, "Re-extract every source, stale or not");
    build->add_option("--limit", b->limit, "Stop after N chunks (0 = no limit)");
    build->callback([&context, b]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        embedstore::Store store = open_existing(b->collection);
        if (store.chunk_count() == 0) {
            fail_user("collection '" + b->collection +
                      "' has no chunks to extract from -- ingest documents first with 'apogee "
                      "embed ingest'");
        }
        const harness::EmbeddingConfig* entry = config.find_embedding(b->collection);
        const std::string entry_backend =
            entry != nullptr ? entry->graph.extract_backend : std::string{};

        // The extractor: -m > the collection's graph.extract_backend > the
        // extraction role > the default, through the ONE role resolver --
        // and a metered default refused, naming the three ways to say so.
        const harness::Resolution resolved = harness::resolve_backend(
            config, harness::RoleRequest{.role = harness::ModelRole::Extraction,
                                         .override = b->model,
                                         .entry_backend = entry_backend});
        if (resolved.key.empty()) {
            fail_user("no extraction backend: pass -m <backend>, set graph.extract_backend on '" +
                      b->collection + "', or set the extraction role");
        }
        const harness::BackendConfig* backend = config.find_backend(resolved.key);
        if (backend == nullptr) {
            fail_user("backend '" + resolved.key + "' is not configured");
        }
        if (harness::is_vendor_cli(backend->type)) {
            fail_user("'" + resolved.key +
                      "' is a vendor-CLI backend -- the extractor runs inside Apogee's own loop; "
                      "pick an API or local backend");
        }
        const Providers providers{config, config_path};
        if (resolved.from == harness::ResolvedFrom::Default &&
            providers.harness.generation_is_metered(resolved.key)) {
            fail_user(
                "a full graph build never runs on a metered backend on Apogee's "
                "initiative -- '" +
                resolved.key +
                "' is the default backend and is billed per call. Name it explicitly "
                "with -m " +
                resolved.key + ", set graph.extract_backend on '" + b->collection +
                "', or set the extraction role");
        }
        // The model name recorded as the staleness key: the entry's model
        // when it names one, else the key.
        const std::string kg_model = backend->model.empty() ? resolved.key : backend->model;

        // Entity vectors under the embedding spend rule, decided in ONE
        // place for this surface and the admin twin.
        const graph::EntityEmbedder entity_embedder = graph::resolve_entity_embedder(
            providers.harness, config, entry != nullptr ? entry->backend : std::string{},
            entry != nullptr ? entry->retriever : std::string{});
        const graph::EmbedFn& embed = entity_embedder.embed;
        const std::string& embed_model = entity_embedder.model;
        const std::string& embed_note = entity_embedder.note;
        if (!embed && !b->dry_run) {
            std::cout << "[graph] entity vectors skipped for \"" << b->collection << "\" -- "
                      << embed_note << "; entity search will be full-text only.\n";
        }

        graph::BuildOptions options;
        options.model = kg_model;
        options.embed_model = embed_model;
        options.force = b->force;
        options.limit = b->limit;
        options.dry_run = b->dry_run;
        options.on_progress = [](const graph::Progress& progress) {
            if (progress.stage == graph::Progress::Stage::Extract) {
                std::cerr << "[graph] extracting " << progress.file << " (file "
                          << progress.file_index << "/" << progress.file_count << ", chunk "
                          << progress.chunks_done + 1 << "/" << progress.chunks_total
                          << (progress.failed > 0
                                  ? ", " + std::to_string(progress.failed) + " failed"
                                  : "")
                          << ")\n";
            } else {
                std::cerr << "[graph] embedding " << progress.entities << " entities\n";
            }
        };
        if (b->dry_run) {
            options.on_extract = print_dry_run_extraction;
        }
        // Failed chunks print as they happen -- a count alone cannot tell a
        // flaky backend from a model that cannot produce the JSON. A dry run
        // prints the whole error; a real build keeps it to one line.
        options.on_chunk_failed = [dry_run = b->dry_run](const embedstore::Chunk& chunk,
                                                         std::string_view error) {
            std::cout << "\n"
                      << chunk.source << " [chunk " << chunk.ordinal
                      << "] FAILED: " << (dry_run ? std::string{error} : preview_text(error, 200))
                      << "\n";
        };

        std::cout << "Building the knowledge graph for \"" << b->collection << "\" with "
                  << resolved.key << "...\n";
        graph::BuildResult result;
        try {
            result = graph::build(store,
                                  graph::make_structured_extractor(providers.harness, resolved.key),
                                  embed, options);
        } catch (const std::exception& e) {
            fail_backend(std::string{"graph build failed: "} + e.what());
        }
        print_build_summary(b->collection, result);
        if (b->dry_run || result.cancelled) {
            return;
        }

        // The first successful build records that the graph exists, through
        // the one path that writes a config file -- the same edit the admin
        // twin makes. Reported and never fatal: the graph is already built.
        if (entry == nullptr) {
            harness::EmbeddingConfig fresh;
            fresh.description = {};
            try {
                harness::edit_config_file(config_path, [&](std::string_view content) {
                    return harness::append_embedding(content, b->collection, fresh, false);
                });
                std::cout << "registered '" << b->collection << "' in " << config_path.string()
                          << "\n";
            } catch (const std::exception& e) {
                std::cerr << "apogee graph: could not register '" << b->collection
                          << "' in config -- " << e.what() << "\n";
            }
        }
        if (entry == nullptr || !entry->graph.enabled) {
            try {
                harness::edit_config_file(config_path, [&](std::string_view content) {
                    return harness::set_embedding_graph_enabled(content, b->collection, true);
                });
                std::cout << "graph.enabled set on '" << b->collection
                          << "' -- retrieval expands through it from now on\n";
            } catch (const std::exception& e) {
                std::cerr << "apogee graph: could not set graph.enabled on '" << b->collection
                          << "' -- " << e.what() << "\n";
            }
        }
    });

    // ---- stats ---------------------------------------------------------------
    auto s_collection = std::make_shared<std::string>();
    CLI::App* stats =
        cmd->add_subcommand("stats", "Show a graph's counts, coverage, and build state");
    stats->add_option("COLLECTION", *s_collection, "The collection")->required();
    stats->callback([s_collection]() {
        const embedstore::Store store = open_existing(*s_collection);
        const embedstore::GraphStats st = store.graph_stats();
        if (!st.built()) {
            std::cout << "No graph built for \"" << *s_collection << "\". Run: apogee graph build "
                      << *s_collection << "\n";
            return;
        }
        std::cout << "Knowledge graph for \"" << *s_collection << "\":\n";
        std::cout << "  Nodes:     " << st.nodes << " (" << type_summary(st) << ")\n";
        std::cout << "  Edges:     " << st.edges << "\n";
        std::cout << "  Mentions:  " << st.mentions << "\n";
        const std::int64_t coverage =
            st.total_chunks > 0 ? 100 * st.chunks_with_mentions / st.total_chunks : 0;
        std::cout << "  Coverage:  " << coverage << "% of " << st.total_chunks
                  << " chunks carry at least one entity\n";
        std::cout << "  Vectors:   " << st.nodes_with_vectors << "/" << st.nodes
                  << " entities embedded\n";
        std::cout << "  Extractor: "
                  << (st.extract_model.empty() ? "(unrecorded)" : st.extract_model) << "\n";
        if (st.stale_files > 0) {
            std::cout << "  Stale:     " << st.stale_files
                      << " source file(s) need re-extraction -- run `apogee graph build "
                      << *s_collection << "`\n";
        }
        if (st.failed_chunks > 0) {
            std::cout << "  Failed:    " << st.failed_chunks
                      << " chunk(s) failed extraction in the last build\n";
        }
    });

    // ---- show ----------------------------------------------------------------
    auto sh_collection = std::make_shared<std::string>();
    auto sh_entity = std::make_shared<std::string>();
    auto sh_chunks = std::make_shared<int>(3);
    CLI::App* show =
        cmd->add_subcommand("show", "One entity: its relations and the chunks that support it");
    show->add_option("COLLECTION", *sh_collection, "The collection")->required();
    show->add_option("ENTITY", *sh_entity, "The entity's name (case-insensitive), or a record id")
        ->required();
    show->add_option("--chunks", *sh_chunks, "Supporting chunks to print (default 3; 0 = all)");
    show->callback([sh_collection, sh_entity, sh_chunks]() {
        const embedstore::Store store = open_existing(*sh_collection);
        if (!store.graph_stats().built()) {
            fail_user("no graph built for '" + *sh_collection + "' -- run: apogee graph build " +
                      *sh_collection);
        }
        std::vector<embedstore::GraphNode> nodes = store.find_nodes(*sh_entity);
        if (nodes.empty()) {
            // Exact by normalised name, then the entity index for a partial.
            const std::vector<embedstore::NodeResult> hits = store.search_nodes(*sh_entity, 5);
            if (hits.empty()) {
                fail_user("no entity matching '" + *sh_entity + "' in '" + *sh_collection + "'");
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
        for (const embedstore::GraphNode& node : nodes) {
            print_node(store, node, *sh_chunks);
        }
    });

    // ---- delete --------------------------------------------------------------
    auto d_collection = std::make_shared<std::string>();
    CLI::App* del = cmd->add_subcommand(
        "delete", "Clear a collection's graph (its chunks and config entry stay)");
    del->add_option("COLLECTION", *d_collection, "The collection")->required();
    del->callback([d_collection]() {
        embedstore::Store store = open_existing(*d_collection);
        const embedstore::GraphStats st = store.graph_stats();
        if (!st.built()) {
            std::cout << "No graph to delete for \"" << *d_collection << "\".\n";
            return;
        }
        store.delete_graph();
        std::cout << "Deleted the graph for \"" << *d_collection << "\": " << st.nodes
                  << " node(s), " << st.edges << " edge(s), " << st.mentions
                  << " mention(s). The chunks are untouched; the next build starts from scratch.\n";
    });
}

}  // namespace apogee::commands
