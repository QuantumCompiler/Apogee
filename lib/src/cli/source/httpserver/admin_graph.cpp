#include "httpserver/admin_graph.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "agentloop/graph_context.h"
#include "commands/embed.h"
#include "commands/graph.h"
#include "embedstore/store.h"
#include "graph/build.h"
#include "graph/communities.h"
#include "graph/extract.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/roles.h"
#include "httpserver/handler.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kBuildJobKind = "graph-build";
constexpr std::string_view kCommunitiesJobKind = "graph-communities";
constexpr std::string_view kConfigError = "config_error";
constexpr int kNotImplemented = 501;

[[nodiscard]] bool plain_name(std::string_view name) {
    return !name.empty() && name.find("..") == std::string_view::npos &&
           name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos;
}

[[nodiscard]] bool file_exists(const std::filesystem::path& path) {
    std::error_code code;
    return std::filesystem::exists(path, code);
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

/// What `{name}` resolved to, graphs-first -- the CLI's rule.
struct Target {
    std::string name;
    /// A copy, so a worker thread can outlive the request's config.
    std::optional<harness::NamedGraphConfig> named;
    const harness::EmbeddingConfig* entry = nullptr;
    std::filesystem::path db_path;

    [[nodiscard]] bool is_named() const noexcept {
        return named.has_value();
    }

    [[nodiscard]] std::string entry_backend() const {
        if (named.has_value()) {
            return named->extract_backend;
        }
        return entry != nullptr ? entry->graph.extract_backend : std::string{};
    }

    [[nodiscard]] std::string extractor_hint() const {
        return named.has_value() ? "set extract_backend on graph '" + name + "'"
                                 : "set graph.extract_backend on '" + name + "'";
    }

    [[nodiscard]] nlohmann::json job_fields(std::string_view backend) const {
        nlohmann::json out{{"model", std::string{backend}}};
        out[named.has_value() ? "graph" : "collection"] = name;
        return out;
    }
};

[[nodiscard]] Target resolve_target(const harness::Config& config, std::string_view name) {
    Target target;
    target.name = std::string{name};
    if (const harness::NamedGraphConfig* named = config.find_graph(name); named != nullptr) {
        target.named = *named;
        target.db_path = agentloop::graph_db_path(name);
        return target;
    }
    target.entry = config.find_embedding(name);
    target.db_path = commands::collection_path(name);
    return target;
}

/// The "no data" answer for a target whose database is absent: an unbuilt
/// named graph, or a collection never ingested.
[[nodiscard]] HttpResponse missing_target(const Target& target) {
    if (target.is_named()) {
        return error_response(404, "graph '" + target.name + "' has not been built yet",
                              kNotFoundError);
    }
    return error_response(404, "no graph or collection named '" + target.name + "'",
                          kNotFoundError);
}

/// The generation backend for a build or a summariser run: the same chain
/// and the same refusals as the CLI, through the one role resolver -- then
/// the served set, since this plane dispatches only to what it was started
/// with. `what` names the run in the refusal.
struct Generation {
    std::string key;
    std::string kg_model;
};

[[nodiscard]] std::optional<Generation> resolve_generation(
    const harness::Config& config, Handler& plane, std::string_view override, const Target& target,
    const std::string& what, HttpResponse& failure) {
    const harness::Resolution resolved = harness::resolve_backend(
        config, harness::RoleRequest{.role = harness::ModelRole::Extraction,
                                     .override = override,
                                     .entry_backend = target.entry_backend()});
    if (resolved.key.empty()) {
        failure = error_response(400, "no extraction backend: pass model, " +
                                          target.extractor_hint() + ", or set the extraction role");
        return std::nullopt;
    }
    std::string backend;
    try {
        backend = plane.resolve_served(resolved.key);
    } catch (const HttpError& e) {
        failure = to_response(e);
        return std::nullopt;
    }
    if (resolved.from == harness::ResolvedFrom::Default &&
        plane.harness().generation_is_metered(backend)) {
        failure = error_response(
            400, "a full " + what + " never runs on a metered backend on Apogee's initiative -- '" +
                     backend +
                     "' is the default backend and is billed per call; name it explicitly as "
                     "model, " +
                     target.extractor_hint() + ", or set the extraction role");
        return std::nullopt;
    }
    const harness::BackendConfig* entry = config.find_backend(backend);
    return Generation{
        .key = backend,
        .kg_model = entry == nullptr || entry->model.empty() ? backend : entry->model};
}

/// Entity and summary vectors under the embedding spend rule: a collection
/// through its own chain and pin, a named graph through the default chain.
[[nodiscard]] graph::EntityEmbedder resolve_embedder(Handler& plane, const harness::Config& config,
                                                     const Target& target) {
    if (target.is_named() || target.entry == nullptr) {
        return graph::resolve_entity_embedder(plane.harness(), config, "", "");
    }
    return graph::resolve_entity_embedder(plane.harness(), config, target.entry->backend,
                                          target.entry->retriever);
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
                       {"failed_chunks", stats.failed_chunks},
                       {"communities", stats.communities}};
    for (const auto& [type, count] : stats.nodes_by_type) {
        out["nodes_by_type"][type] = count;
    }
    if (!stats.extract_model.empty()) {
        out["extract_model"] = stats.extract_model;
    }
    return out;
}

[[nodiscard]] nlohmann::json build_fields(const Target& target, const graph::BuildResult& result) {
    nlohmann::json fields{{"files_planned", result.files_planned},
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
    if (target.is_named()) {
        fields["graph"] = target.name;
        fields["collections"] = target.named->collections;
    } else {
        fields["collection"] = target.name;
    }
    if (!result.embed_error.empty()) {
        fields["embed_error"] = result.embed_error;
    }
    return fields;
}

/// The progress and failure callbacks every build form shares.
void wire_progress(graph::BuildOptions& options, JobRegistry& jobs, const std::string& job_id) {
    options.on_progress = [&jobs, job_id](const graph::Progress& progress) {
        if (progress.stage == graph::Progress::Stage::Extract) {
            nlohmann::json fields{{"file", progress.file_index},
                                  {"files", progress.file_count},
                                  {"chunks_done", progress.chunks_done},
                                  {"chunks_total", progress.chunks_total},
                                  {"failed", progress.failed}};
            std::string message = "extracting ";
            if (!progress.collection.empty()) {
                fields["collection"] = progress.collection;
                message += progress.collection + ": ";
            }
            jobs.progress(job_id, message + progress.file, std::move(fields));
        } else {
            jobs.progress(job_id, "embedding entities",
                          nlohmann::json{{"entities", progress.entities}});
        }
    };
    options.on_chunk_failed = [&jobs, job_id](const embedstore::Chunk& chunk,
                                              std::string_view error) {
        jobs.progress(
            job_id, "chunk extraction failed",
            nlohmann::json{
                {"source", chunk.source}, {"chunk", chunk.ordinal}, {"error", std::string{error}}});
    };
}

}  // namespace

HttpResponse admin_build_graph(const AdminConfigContext& context, Handler& plane, JobRegistry& jobs,
                               JobWorkers& workers, std::string_view name,
                               const HttpRequest& request) {
    if (!plain_name(name)) {
        return error_response(400, "'" + std::string{name} + "' is not a plain name");
    }
    // Every body field is optional, so an empty body means "build with the
    // entry's defaults" rather than a 400.
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
    const Target target = resolve_target(*config, name);
    // What must hold data: a collection's own store, or at least one of a
    // named graph's members (the graph's database is what the build makes).
    std::shared_ptr<commands::GraphMembers> members;
    if (target.is_named()) {
        if (target.named->collections.empty()) {
            return error_response(400, "graph '" + target.name + "' has no member collections");
        }
        members =
            std::make_shared<commands::GraphMembers>(commands::open_graph_members(*target.named));
        if (!members->any_data()) {
            return error_response(
                404, "no member collection of graph '" + target.name + "' has any data",
                kNotFoundError);
        }
    } else if (!file_exists(target.db_path)) {
        return missing_target(target);
    }
    const std::optional<Generation> generation =
        resolve_generation(*config, plane, model, target, "graph build", failure);
    if (!generation.has_value()) {
        return failure;
    }
    // Entity vectors under the embedding spend rule, decided before the job
    // starts, in the one place the CLI decides it too.
    const graph::EntityEmbedder entity_embedder = resolve_embedder(plane, *config, target);
    const graph::EmbedFn embed = entity_embedder.embed;
    const std::string embed_model = entity_embedder.model;
    const bool already_enabled = target.entry != nullptr && target.entry->graph.enabled;
    const bool registered = target.entry != nullptr;

    const JobRegistry::Started started =
        jobs.start(std::string{kBuildJobKind}, target.job_fields(generation->key));
    const std::string job_id = started.id;
    const std::filesystem::path config_path = context.config_path;
    const std::string backend = generation->key;
    const std::string kg_model = generation->kg_model;
    workers.run(started.cancellation, [&plane, &jobs, job_id, target, members, backend, kg_model,
                                       embed, embed_model, force, limit, already_enabled,
                                       registered, config_path,
                                       cancellation = started.cancellation] {
        try {
            graph::BuildOptions options;
            options.model = kg_model;
            options.embed_model = embed_model;
            options.force = force;
            options.limit = limit;
            options.cancellation = cancellation;
            wire_progress(options, jobs, job_id);
            const graph::ExtractFn extract =
                graph::make_structured_extractor(plane.harness(), backend);
            graph::BuildResult result;
            if (target.is_named()) {
                std::error_code code;
                std::filesystem::create_directories(target.db_path.parent_path(), code);
                embedstore::Store store{target.db_path};
                options.graph_name = target.name;
                result = graph::build_multi(store, members->members(*target.named), extract, embed,
                                            options);
            } else {
                embedstore::Store store{target.db_path};
                result = graph::build(store, extract, embed, options);
            }
            if (result.cancelled) {
                return;  // the registry already marked it
            }
            nlohmann::json fields = build_fields(target, result);
            // A collection's first successful build records that the graph
            // exists, through the same editor the CLI uses -- byte-identical.
            // A failure is a warning on the result: the graph is already
            // built. A named graph has nothing to enable: its entry is the
            // enablement.
            if (!target.is_named()) {
                try {
                    if (!registered) {
                        harness::edit_config_file(config_path, [&](std::string_view content) {
                            return harness::append_embedding(content, target.name,
                                                             harness::EmbeddingConfig{}, false);
                        });
                    }
                    if (!already_enabled) {
                        harness::edit_config_file(config_path, [&](std::string_view content) {
                            return harness::set_embedding_graph_enabled(content, target.name, true);
                        });
                        fields["enabled"] = true;
                    }
                } catch (const std::exception& e) {
                    fields["config_warning"] =
                        std::string{"could not set graph.enabled: "} + e.what();
                }
            }
            jobs.finish(job_id, std::move(fields));
        } catch (const std::exception& e) {
            jobs.fail(job_id, e.what());
        }
    });
    return json_response(202, nlohmann::json{{"job_id", job_id}});
}

HttpResponse admin_graph_stats(const AdminConfigContext& context, std::string_view name,
                               const HttpRequest& /*request*/) {
    if (!plain_name(name)) {
        return error_response(400, "'" + std::string{name} + "' is not a plain name");
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    const Target target = resolve_target(*config, name);
    // An unbuilt graph -- or a collection with no data yet -- reports zeros
    // rather than an error: a GUI polling an empty layer must not see one.
    if (!file_exists(target.db_path)) {
        nlohmann::json out = stats_json(embedstore::GraphStats{});
        if (target.is_named()) {
            out["graph"] = target.name;
            out["collections"] = target.named->collections;
        }
        return json_response(200, out);
    }
    try {
        const embedstore::Store store{target.db_path};
        if (!target.is_named()) {
            return json_response(200, stats_json(store.graph_stats()));
        }
        const commands::GraphMembers members = commands::open_graph_members(*target.named);
        const embedstore::GraphStatsMulti stats = store.graph_stats_multi(members.views());
        nlohmann::json out = stats_json(stats.totals);
        out["graph"] = target.name;
        out["collections"] = target.named->collections;
        if (const std::string embed_model = store.graph_meta(embedstore::kGraphMetaEmbedModel);
            !embed_model.empty()) {
            out["embed_model"] = embed_model;
        }
        out["members"] = nlohmann::json::array();
        for (const embedstore::MemberStats& member : stats.members) {
            nlohmann::json row{{"collection", member.collection},
                               {"mentions", member.mentions},
                               {"total_chunks", member.total_chunks},
                               {"chunks_with_mentions", member.chunks_with_mentions},
                               {"stale_files", member.stale_files}};
            if (member.missing) {
                row["missing"] = true;
            }
            out["members"].push_back(std::move(row));
        }
        return json_response(200, out);
    } catch (const std::exception& e) {
        return error_response(500, std::string{"could not read the graph: "} + e.what());
    }
}

HttpResponse admin_graph_entity(const AdminConfigContext& context, std::string_view name,
                                const HttpRequest& request) {
    if (!plain_name(name)) {
        return error_response(400, "'" + std::string{name} + "' is not a plain name");
    }
    const std::string entity = request.query_value("name");
    if (entity.empty()) {
        return error_response(400, "the name query parameter is required");
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    const Target target = resolve_target(*config, name);
    if (!file_exists(target.db_path)) {
        return missing_target(target);
    }
    try {
        const embedstore::Store store{target.db_path};
        std::optional<commands::GraphMembers> members;
        if (target.is_named()) {
            members.emplace(commands::open_graph_members(*target.named));
        }
        std::vector<embedstore::GraphNode> nodes = store.find_nodes(entity);
        bool fuzzy = false;
        nlohmann::json also = nlohmann::json::array();
        if (nodes.empty()) {
            const std::vector<embedstore::NodeResult> hits = store.search_nodes(entity, 5);
            if (hits.empty()) {
                return error_response(404, "no entity matching '" + entity + "'", kNotFoundError);
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
            if (members.has_value()) {
                // A named graph's mentions point into member databases:
                // resolved through the member stores, each naming its
                // collection. A missing member or a chunk since reconciled
                // away has no text to show.
                for (const embedstore::ChunkRef& ref : store.node_mention_refs(node.id, 3)) {
                    const auto it = members->stores.find(ref.collection);
                    if (it == members->stores.end() || it->second == nullptr) {
                        continue;
                    }
                    const std::optional<embedstore::Chunk> chunk =
                        it->second->chunk_by_id(ref.chunk_id);
                    if (!chunk.has_value()) {
                        continue;
                    }
                    detail["chunks"].push_back(nlohmann::json{{"collection", ref.collection},
                                                              {"source", chunk->source},
                                                              {"chunk", chunk->ordinal},
                                                              {"text", chunk->text}});
                }
            } else {
                for (const embedstore::Chunk& chunk : store.node_chunks(node.id, 3)) {
                    detail["chunks"].push_back(nlohmann::json{
                        {"source", chunk.source}, {"chunk", chunk.ordinal}, {"text", chunk.text}});
                }
            }
            data.push_back(std::move(detail));
        }
        nlohmann::json out{{"data", std::move(data)}, {"fuzzy", fuzzy}};
        if (!also.empty()) {
            out["also_matched"] = std::move(also);
        }
        return json_response(200, out);
    } catch (const std::exception& e) {
        return error_response(500, std::string{"could not read the graph: "} + e.what());
    }
}

HttpResponse admin_delete_graph(const AdminConfigContext& context, std::string_view name,
                                const HttpRequest& /*request*/) {
    if (!plain_name(name)) {
        return error_response(400, "'" + std::string{name} + "' is not a plain name");
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    const Target target = resolve_target(*config, name);
    if (!file_exists(target.db_path)) {
        return missing_target(target);
    }
    try {
        embedstore::GraphStats stats;
        {
            embedstore::Store store{target.db_path};
            stats = store.graph_stats();
            if (!target.is_named()) {
                store.delete_graph();
            }
        }
        if (target.is_named()) {
            // Everything in a named graph's database is derived: the file
            // goes. The graphs: entry stays -- DELETE /v1/admin/graphs/{name}
            // removes that.
            std::error_code code;
            if (!std::filesystem::remove(target.db_path, code) || code) {
                return error_response(500,
                                      "could not delete the graph database: " + code.message());
            }
            for (const char* sidecar : {"-wal", "-shm"}) {
                std::filesystem::remove(target.db_path.string() + sidecar, code);
            }
        }
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

HttpResponse admin_build_communities(const AdminConfigContext& context, Handler& plane,
                                     JobRegistry& jobs, JobWorkers& workers, std::string_view name,
                                     const HttpRequest& request) {
    if (!plain_name(name)) {
        return error_response(400, "'" + std::string{name} + "' is not a plain name");
    }
    nlohmann::json body = nlohmann::json::object();
    if (!request.body.empty()) {
        body = nlohmann::json::parse(request.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            return error_response(400, "the request body must be a JSON object");
        }
    }
    std::string model;
    bool force = false;
    int min_size = 0;
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
    if (const auto it = body.find("min_size"); it != body.end() && !it->is_null()) {
        if (!it->is_number_integer() || it->get<int>() < 0) {
            return error_response(400, "min_size must be a non-negative integer");
        }
        min_size = it->get<int>();
    }
    if (plane.options().served.empty()) {
        return error_response(kNotImplemented,
                              "this server has no generation backend to run the summariser on",
                              kBackendUnavailable);
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    const Target target = resolve_target(*config, name);
    if (!file_exists(target.db_path)) {
        return missing_target(target);
    }
    const std::optional<Generation> generation =
        resolve_generation(*config, plane, model, target, "community summary run", failure);
    if (!generation.has_value()) {
        return failure;
    }
    const graph::EntityEmbedder embedder = resolve_embedder(plane, *config, target);
    const graph::EmbedFn embed = embedder.embed;

    const JobRegistry::Started started =
        jobs.start(std::string{kCommunitiesJobKind}, target.job_fields(generation->key));
    const std::string job_id = started.id;
    const std::string backend = generation->key;
    const std::string kg_model = generation->kg_model;
    const std::filesystem::path db_path = target.db_path;
    workers.run(started.cancellation, [&plane, &jobs, job_id, db_path, backend, kg_model, embed,
                                       force, min_size, cancellation = started.cancellation] {
        try {
            embedstore::Store store{db_path};
            graph::CommunitiesOptions options;
            options.model = kg_model;
            options.force = force;
            options.min_size = min_size;
            options.cancellation = cancellation;
            options.on_progress = [&jobs, job_id](int done, int total) {
                jobs.progress(job_id, "summarising communities",
                              nlohmann::json{{"done", done}, {"total", total}});
            };
            const graph::CommunitiesResult result = graph::build_communities(
                store, graph::make_summarizer(plane.harness(), backend), embed, options);
            if (result.cancelled) {
                return;
            }
            nlohmann::json fields{
                {"detected", result.detected},   {"summarized", result.summarized},
                {"unchanged", result.unchanged}, {"failed", result.failed},
                {"pruned", result.pruned},       {"embedded", result.embedded}};
            if (!result.embed_error.empty()) {
                fields["embed_error"] = result.embed_error;
            }
            jobs.finish(job_id, std::move(fields));
        } catch (const std::exception& e) {
            jobs.fail(job_id, e.what());
        }
    });
    return json_response(202, nlohmann::json{{"job_id", job_id}});
}

HttpResponse admin_list_communities(const AdminConfigContext& context, std::string_view name,
                                    const HttpRequest& /*request*/) {
    if (!plain_name(name)) {
        return error_response(400, "'" + std::string{name} + "' is not a plain name");
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    const Target target = resolve_target(*config, name);
    nlohmann::json data = nlohmann::json::array();
    if (file_exists(target.db_path)) {
        try {
            const embedstore::Store store{target.db_path};
            for (const embedstore::GraphCommunity& community : store.graph_communities()) {
                nlohmann::json top = nlohmann::json::array();
                for (const embedstore::GraphNode& member : store.community_members(community.id)) {
                    if (top.size() == 3) {
                        break;
                    }
                    top.push_back(member.name);
                }
                nlohmann::json row{{"id", community.id},
                                   {"size", community.size},
                                   {"summary", community.summary},
                                   {"top_members", std::move(top)}};
                if (!community.model.empty()) {
                    row["model"] = community.model;
                }
                data.push_back(std::move(row));
            }
        } catch (const std::exception& e) {
            return error_response(500, std::string{"could not read the graph: "} + e.what());
        }
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

HttpResponse admin_dedupe_graph(const AdminConfigContext& context, std::string_view name,
                                const HttpRequest& request) {
    if (!plain_name(name)) {
        return error_response(400, "'" + std::string{name} + "' is not a plain name");
    }
    nlohmann::json body = nlohmann::json::object();
    if (!request.body.empty()) {
        body = nlohmann::json::parse(request.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            return error_response(400, "the request body must be a JSON object");
        }
    }
    double threshold = embedstore::kDefaultDedupeThreshold;
    bool dry_run = false;
    if (const auto it = body.find("threshold"); it != body.end() && !it->is_null()) {
        if (!it->is_number()) {
            return error_response(400, "threshold must be a number in (0, 1]");
        }
        threshold = it->get<double>();
    }
    if (threshold <= 0.0 || threshold > 1.0) {
        return error_response(400, "threshold must be a number in (0, 1]");
    }
    if (const auto it = body.find("dry_run"); it != body.end() && !it->is_null()) {
        if (!it->is_boolean()) {
            return error_response(400, "dry_run must be a boolean");
        }
        dry_run = it->get<bool>();
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    const Target target = resolve_target(*config, name);
    if (!file_exists(target.db_path)) {
        return missing_target(target);
    }
    try {
        embedstore::Store store{target.db_path};
        const std::vector<embedstore::MergeGroup> groups = store.dedupe_nodes(threshold, dry_run);
        nlohmann::json data = nlohmann::json::array();
        std::size_t merged = 0;
        for (const embedstore::MergeGroup& group : groups) {
            nlohmann::json names = nlohmann::json::array();
            for (const embedstore::GraphNode& node : group.merged) {
                names.push_back(node.name);
            }
            merged += group.merged.size();
            data.push_back(nlohmann::json{{"kept", group.kept.name},
                                          {"kept_type", group.kept.type},
                                          {"merged", std::move(names)}});
        }
        return json_response(200, nlohmann::json{{"groups", std::move(data)},
                                                 {"merged_nodes", merged},
                                                 {"threshold", threshold},
                                                 {"dry_run", dry_run}});
    } catch (const std::exception& e) {
        return error_response(500, std::string{"dedupe failed: "} + e.what());
    }
}

}  // namespace apogee::httpserver
