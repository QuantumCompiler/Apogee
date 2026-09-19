#include "httpserver/admin_graph.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "commands/embed.h"
#include "embedstore/store.h"
#include "graph/build.h"
#include "graph/extract.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/roles.h"
#include "httpserver/handler.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kJobKind = "graph-build";
constexpr std::string_view kConfigError = "config_error";
constexpr int kNotImplemented = 501;

[[nodiscard]] bool plain_name(std::string_view name) {
    return !name.empty() && name.find("..") == std::string_view::npos &&
           name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos;
}

[[nodiscard]] std::optional<harness::Config> load_now(const AdminConfigContext& context,
                                                      HttpResponse& failure) {
    try {
        return harness::load_config(context.config_path);
    } catch (const harness::ConfigError& e) {
        failure = error_response(500, std::string{"the config cannot be read: "} + e.what(),
                                 kConfigError);
        return std::nullopt;
    }
}

[[nodiscard]] bool collection_exists(std::string_view collection) {
    std::error_code code;
    return std::filesystem::exists(commands::collection_path(collection), code);
}

[[nodiscard]] nlohmann::json stats_json(const embedstore::GraphStats& stats) {
    nlohmann::json out{{"nodes", stats.nodes},
                       {"edges", stats.edges},
                       {"mentions", stats.mentions},
                       {"nodes_by_type", nlohmann::json::object()},
                       {"nodes_with_vectors", stats.nodes_with_vectors},
                       {"total_chunks", stats.total_chunks},
                       {"chunks_with_mentions", stats.chunks_with_mentions},
                       {"stale_files", stats.stale_files},
                       {"failed_chunks", stats.failed_chunks}};
    for (const auto& [type, count] : stats.nodes_by_type) {
        out["nodes_by_type"][type] = count;
    }
    if (!stats.extract_model.empty()) {
        out["extract_model"] = stats.extract_model;
    }
    return out;
}

}  // namespace

HttpResponse admin_build_graph(const AdminConfigContext& context, Handler& plane, JobRegistry& jobs,
                               JobWorkers& workers, std::string_view collection,
                               const HttpRequest& request) {
    if (!plain_name(collection)) {
        return error_response(400,
                              "'" + std::string{collection} + "' is not a plain collection name");
    }
    // Every body field is optional, so an empty body means "build with the
    // collection's defaults" rather than a 400.
    nlohmann::json body = nlohmann::json::object();
    if (!request.body.empty()) {
        body = nlohmann::json::parse(request.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            return error_response(400, "the request body must be a JSON object");
        }
    }
    std::string model;
    bool force = false;
    int limit = 0;
    if (const auto it = body.find("model"); it != body.end() && !it->is_null()) {
        if (!it->is_string()) {
            return error_response(400, "model must be a string");
        }
        model = it->get<std::string>();
    }
    if (const auto it = body.find("force"); it != body.end() && !it->is_null()) {
        if (!it->is_boolean()) {
            return error_response(400, "force must be a boolean");
        }
        force = it->get<bool>();
    }
    if (const auto it = body.find("limit"); it != body.end() && !it->is_null()) {
        if (!it->is_number_integer() || it->get<int>() < 0) {
            return error_response(400, "limit must be a non-negative integer");
        }
        limit = it->get<int>();
    }
    if (plane.options().served.empty()) {
        return error_response(kNotImplemented,
                              "this server has no generation backend to run the extractor on",
                              kBackendUnavailable);
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    if (!collection_exists(collection)) {
        return error_response(404, "no collection named '" + std::string{collection} + "'",
                              kNotFoundError);
    }
    const harness::EmbeddingConfig* entry = config->find_embedding(collection);
    const std::string entry_backend =
        entry != nullptr ? entry->graph.extract_backend : std::string{};

    // The same chain and the same refusals as the CLI, through the one role
    // resolver -- then the served set, since this plane dispatches only to
    // what it was started with.
    const harness::Resolution resolved = harness::resolve_backend(
        *config, harness::RoleRequest{.role = harness::ModelRole::Extraction,
                                      .override = model,
                                      .entry_backend = entry_backend});
    if (resolved.key.empty()) {
        return error_response(400,
                              "no extraction backend: pass model, set graph.extract_backend on "
                              "the collection, or set the extraction role");
    }
    std::string backend;
    try {
        backend = plane.resolve_served(resolved.key);
    } catch (const HttpError& e) {
        return to_response(e);
    }
    if (resolved.from == harness::ResolvedFrom::Default &&
        plane.harness().generation_is_metered(backend)) {
        return error_response(400,
                              "a full graph build never runs on a metered backend on Apogee's "
                              "initiative -- '" +
                                  backend +
                                  "' is the default backend and is billed per call; name it "
                                  "explicitly as model, set graph.extract_backend on the "
                                  "collection, or set the extraction role");
    }
    const harness::BackendConfig* backend_entry = config->find_backend(backend);
    const std::string kg_model =
        backend_entry == nullptr || backend_entry->model.empty() ? backend : backend_entry->model;

    // Entity vectors under the embedding spend rule, decided before the job
    // starts, in the one place the CLI decides it too.
    const graph::EntityEmbedder entity_embedder = graph::resolve_entity_embedder(
        plane.harness(), *config, entry != nullptr ? entry->backend : std::string{},
        entry != nullptr ? entry->retriever : std::string{});
    const graph::EmbedFn embed = entity_embedder.embed;
    const std::string embed_model = entity_embedder.model;
    const bool already_enabled = entry != nullptr && entry->graph.enabled;
    const bool registered = entry != nullptr;

    const JobRegistry::Started started =
        jobs.start(std::string{kJobKind},
                   nlohmann::json{{"collection", std::string{collection}}, {"model", backend}});
    const std::string job_id = started.id;
    const std::filesystem::path config_path = context.config_path;
    const std::string name{collection};
    workers.run(started.cancellation, [&plane, &jobs, job_id, name, backend, kg_model, embed,
                                       embed_model, force, limit, already_enabled, registered,
                                       config_path, cancellation = started.cancellation] {
        try {
            embedstore::Store store{commands::collection_path(name)};
            graph::BuildOptions options;
            options.model = kg_model;
            options.embed_model = embed_model;
            options.force = force;
            options.limit = limit;
            options.cancellation = cancellation;
            options.on_progress = [&](const graph::Progress& progress) {
                if (progress.stage == graph::Progress::Stage::Extract) {
                    jobs.progress(job_id, "extracting " + progress.file,
                                  nlohmann::json{{"file", progress.file_index},
                                                 {"files", progress.file_count},
                                                 {"chunks_done", progress.chunks_done},
                                                 {"chunks_total", progress.chunks_total},
                                                 {"failed", progress.failed}});
                } else {
                    jobs.progress(job_id, "embedding entities",
                                  nlohmann::json{{"entities", progress.entities}});
                }
            };
            options.on_chunk_failed = [&](const embedstore::Chunk& chunk, std::string_view error) {
                jobs.progress(job_id, "chunk extraction failed",
                              nlohmann::json{{"source", chunk.source},
                                             {"chunk", chunk.ordinal},
                                             {"error", std::string{error}}});
            };
            const graph::BuildResult result = graph::build(
                store, graph::make_structured_extractor(plane.harness(), backend), embed, options);
            if (result.cancelled) {
                return;  // the registry already marked it
            }
            nlohmann::json fields{{"collection", name},
                                  {"files_planned", result.files_planned},
                                  {"files_extracted", result.files_extracted},
                                  {"chunks_extracted", result.chunks_done - result.chunks_failed},
                                  {"chunks_failed", result.chunks_failed},
                                  {"nodes_upserted", result.nodes_upserted},
                                  {"edges_upserted", result.edges_upserted},
                                  {"mentions_added", result.mentions_added},
                                  {"entities_embedded", result.nodes_embedded},
                                  {"record_nodes", result.record_nodes},
                                  {"supersedes_edges", result.supersedes_edges},
                                  {"supersedes_skipped", result.supersedes_skipped},
                                  {"limit_hit", result.limit_hit}};
            if (!result.embed_error.empty()) {
                fields["embed_error"] = result.embed_error;
            }
            // The first successful build records that the graph exists,
            // through the same editor the CLI uses -- byte-identical. A
            // failure is a warning on the result: the graph is already built.
            try {
                if (!registered) {
                    harness::edit_config_file(config_path, [&](std::string_view content) {
                        return harness::append_embedding(content, name, harness::EmbeddingConfig{},
                                                         false);
                    });
                }
                if (!already_enabled) {
                    harness::edit_config_file(config_path, [&](std::string_view content) {
                        return harness::set_embedding_graph_enabled(content, name, true);
                    });
                    fields["enabled"] = true;
                }
            } catch (const std::exception& e) {
                fields["config_warning"] = std::string{"could not set graph.enabled: "} + e.what();
            }
            jobs.finish(job_id, std::move(fields));
        } catch (const std::exception& e) {
            jobs.fail(job_id, e.what());
        }
    });
    return json_response(202, nlohmann::json{{"job_id", job_id}});
}

HttpResponse admin_graph_stats(const AdminConfigContext& /*context*/, std::string_view collection,
                               const HttpRequest& /*request*/) {
    if (!plain_name(collection)) {
        return error_response(400,
                              "'" + std::string{collection} + "' is not a plain collection name");
    }
    // An unbuilt graph -- or a collection with no data yet -- reports zeros
    // rather than an error: a GUI polling an empty layer must not see one.
    if (!collection_exists(collection)) {
        return json_response(200, stats_json(embedstore::GraphStats{}));
    }
    try {
        const embedstore::Store store{commands::collection_path(collection)};
        return json_response(200, stats_json(store.graph_stats()));
    } catch (const std::exception& e) {
        return error_response(500, std::string{"could not read the collection: "} + e.what());
    }
}

HttpResponse admin_graph_entity(const AdminConfigContext& /*context*/, std::string_view collection,
                                const HttpRequest& request) {
    if (!plain_name(collection)) {
        return error_response(400,
                              "'" + std::string{collection} + "' is not a plain collection name");
    }
    const std::string name = request.query_value("name");
    if (name.empty()) {
        return error_response(400, "the name query parameter is required");
    }
    if (!collection_exists(collection)) {
        return error_response(404, "no collection named '" + std::string{collection} + "'",
                              kNotFoundError);
    }
    try {
        const embedstore::Store store{commands::collection_path(collection)};
        std::vector<embedstore::GraphNode> nodes = store.find_nodes(name);
        bool fuzzy = false;
        nlohmann::json also = nlohmann::json::array();
        if (nodes.empty()) {
            const std::vector<embedstore::NodeResult> hits = store.search_nodes(name, 5);
            if (hits.empty()) {
                return error_response(404, "no entity matching '" + name + "'", kNotFoundError);
            }
            fuzzy = true;
            nodes.push_back(hits.front().node);
            for (std::size_t i = 1; i < hits.size(); ++i) {
                also.push_back(hits[i].node.name);
            }
        }
        nlohmann::json data = nlohmann::json::array();
        for (const embedstore::GraphNode& node : nodes) {
            nlohmann::json detail{{"name", node.name},
                                  {"type", node.type},
                                  {"mentions", node.mention_count},
                                  {"dim", node.dim},
                                  {"relations", nlohmann::json::array()},
                                  {"chunks", nlohmann::json::array()}};
            if (!node.description.empty()) {
                detail["description"] = node.description;
            }
            if (node.type == embedstore::kNodeTypeDecision) {
                const embedstore::DecisionNodeMetadata meta =
                    embedstore::parse_decision_node_metadata(node.metadata);
                detail["status"] = meta.status;
                if (!meta.discipline.empty()) {
                    detail["discipline"] = meta.discipline;
                }
            }
            for (const embedstore::Neighbor& neighbor : store.node_neighbors(node.id)) {
                nlohmann::json relation{{"relation", neighbor.relation},
                                        {"direction", neighbor.outgoing ? "out" : "in"},
                                        {"peer", neighbor.peer_name},
                                        {"peer_type", neighbor.peer_type},
                                        {"weight", neighbor.weight}};
                if (!neighbor.description.empty()) {
                    relation["description"] = neighbor.description;
                }
                detail["relations"].push_back(std::move(relation));
            }
            for (const embedstore::Chunk& chunk : store.node_chunks(node.id, 3)) {
                detail["chunks"].push_back(nlohmann::json{
                    {"source", chunk.source}, {"chunk", chunk.ordinal}, {"text", chunk.text}});
            }
            data.push_back(std::move(detail));
        }
        nlohmann::json out{{"data", std::move(data)}, {"fuzzy", fuzzy}};
        if (!also.empty()) {
            out["also_matched"] = std::move(also);
        }
        return json_response(200, out);
    } catch (const std::exception& e) {
        return error_response(500, std::string{"could not read the collection: "} + e.what());
    }
}

HttpResponse admin_delete_graph(const AdminConfigContext& /*context*/, std::string_view collection,
                                const HttpRequest& /*request*/) {
    if (!plain_name(collection)) {
        return error_response(400,
                              "'" + std::string{collection} + "' is not a plain collection name");
    }
    if (!collection_exists(collection)) {
        return error_response(404, "no collection named '" + std::string{collection} + "'",
                              kNotFoundError);
    }
    try {
        embedstore::Store store{commands::collection_path(collection)};
        const embedstore::GraphStats stats = store.graph_stats();
        store.delete_graph();
        return json_response(
            200, nlohmann::json{{"deleted", nlohmann::json{{"nodes", stats.nodes},
                                                           {"edges", stats.edges},
                                                           {"mentions", stats.mentions}}}});
    } catch (const std::exception& e) {
        return error_response(500, std::string{"could not clear the graph: "} + e.what());
    }
}

HttpResponse admin_set_graph_enabled(const AdminConfigContext& context, std::string_view collection,
                                     const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    const auto it = body.find("enabled");
    if (it == body.end() || !it->is_boolean()) {
        return error_response(400, "enabled (a boolean) is required");
    }
    const bool enabled = it->get<bool>();
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::set_embedding_graph_enabled(content, collection, enabled);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(404, e.what(), kNotFoundError);
    } catch (const harness::ConfigError& e) {
        return error_response(500, std::string{"the config cannot be read: "} + e.what(),
                              kConfigError);
    } catch (const std::exception& e) {
        return error_response(500, e.what());
    }
    return json_response(
        200, nlohmann::json{{"collection", std::string{collection}}, {"enabled", enabled}});
}

}  // namespace apogee::httpserver
