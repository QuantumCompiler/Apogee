#include "httpserver/mux.h"

#include <array>
#include <string>
#include <utility>

#include "httpserver/admin_auth.h"

namespace apogee::httpserver {
namespace {

using RouteFn = HttpResponse (*)(Handler&, AdminHandler*, const HttpRequest&, const std::string&);

struct Route {
    const char* method;
    const char* pattern;
    bool admin;
    RouteFn call;
};

constexpr std::string_view kAdminPrefix = "/v1/admin";

/// The table. `tests/http_api_conformance.cmake` reads the method/pattern
/// pairs off these lines and requires each to be documented in
/// `documentation/reference/http-api.md`, and each documented route to be
/// here -- so the reference a client author trusts cannot drift from the
/// routes that exist. Admin rows are only reachable through the gate.
constexpr std::array<Route, 67> kRoutes{{
    {"POST", "/v1/chat/completions", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.chat_completions(r);
     }},
    {"POST", "/v1/completions", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.completions(r);
     }},
    {"GET", "/v1/models", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.list_models(r);
     }},
    {"GET", "/v1/model/status", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.model_status(r);
     }},
    {"GET", "/health", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.health(r);
     }},
    {"GET", "/v1/sessions", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.list_sessions(r);
     }},
    {"GET", "/v1/sessions/{id}", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string& id) {
         return h.get_session(r, id);
     }},
    {"DELETE", "/v1/sessions/{id}", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string& id) {
         return h.delete_session(r, id);
     }},
    // --- the control plane ------------------------------------------------
    {"GET", "/v1/admin/backends", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_backends(r);
     }},
    {"POST", "/v1/admin/backends", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_backend(r);
     }},
    {"POST", "/v1/admin/backends/default", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default(r);
     }},
    {"POST", "/v1/admin/backends/default-embedding", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default_embedding(r);
     }},
    {"POST", "/v1/admin/backends/default-extraction", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default_extraction(r);
     }},
    {"GET", "/v1/admin/backends/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_backend(r, id);
     }},
    {"DELETE", "/v1/admin/backends/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_backend(r, id);
     }},
    {"POST", "/v1/admin/config/format", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->format_config(r);
     }},
    {"GET", "/v1/admin/mcp-servers", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_mcp_servers(r);
     }},
    {"POST", "/v1/admin/mcp-servers", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_mcp_server(r);
     }},
    {"GET", "/v1/admin/mcp-servers/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_mcp_server(r, id);
     }},
    {"DELETE", "/v1/admin/mcp-servers/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_mcp_server(r, id);
     }},
    {"PUT", "/v1/admin/mcp-servers/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->set_mcp_server_enabled(r, id);
     }},
    {"GET", "/v1/admin/agents", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_agents(r);
     }},
    {"POST", "/v1/admin/agents", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_agent(r);
     }},
    {"GET", "/v1/admin/agents/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_agent(r, id);
     }},
    {"PUT", "/v1/admin/agents/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_agent(r, id);
     }},
    {"DELETE", "/v1/admin/agents/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_agent(r, id);
     }},
    {"POST", "/v1/admin/knowledge", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_knowledge(h, r);
     }},
    {"POST", "/v1/admin/knowledge/capture", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->capture_knowledge(h, r);
     }},
    {"POST", "/v1/admin/knowledge/refine", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->refine_knowledge(h, r);
     }},
    {"POST", "/v1/admin/knowledge/reindex", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->reindex_knowledge(h, r);
     }},
    {"GET", "/v1/admin/knowledge", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_knowledge(h, r);
     }},
    // The literal knowledge paths above are matched before the `{id}` rows,
    // so `capture`, `refine` and `reindex` are never read as record ids.
    {"GET", "/v1/admin/knowledge/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_knowledge(r, id);
     }},
    {"PATCH", "/v1/admin/knowledge/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->patch_knowledge(r, id);
     }},
    {"DELETE", "/v1/admin/knowledge/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_knowledge(r, id);
     }},
    // The knowledge graph over a collection or a named graph -- `{id}` is
    // resolved graphs-first. It sits mid-path, which the matcher handles
    // segment by segment; the fixed tails keep `build`, `stats`, `entity`,
    // `communities` and `dedupe` distinct from the bare `DELETE`.
    {"POST", "/v1/admin/graph/{id}/build", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->build_graph(h, r, id);
     }},
    {"GET", "/v1/admin/graph/{id}/stats", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->graph_stats(r, id);
     }},
    {"GET", "/v1/admin/graph/{id}/entity", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->graph_entity(r, id);
     }},
    {"POST", "/v1/admin/graph/{id}/communities", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->build_communities(h, r, id);
     }},
    {"GET", "/v1/admin/graph/{id}/communities", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->list_communities(r, id);
     }},
    {"POST", "/v1/admin/graph/{id}/dedupe", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->dedupe_graph(r, id);
     }},
    {"DELETE", "/v1/admin/graph/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_graph(r, id);
     }},
    {"PUT", "/v1/admin/embeddings/{id}/graph", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->set_graph_enabled(r, id);
     }},
    // The graphs: config slice -- named multi-collection graphs' entries;
    // the data lives behind /v1/admin/graph/{id}/* above.
    {"GET", "/v1/admin/graphs", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_graphs(r);
     }},
    {"POST", "/v1/admin/graphs", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_graph(r);
     }},
    {"GET", "/v1/admin/graphs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_graph(r, id);
     }},
    {"PUT", "/v1/admin/graphs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_graph(r, id);
     }},
    {"DELETE", "/v1/admin/graphs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_graph_config(r, id);
     }},
    // The datasets slice: the literal paths first, so `synth` and `kits`
    // are never read as dataset names.
    {"GET", "/v1/admin/datasets", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_datasets(r);
     }},
    {"POST", "/v1/admin/datasets", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_dataset(r);
     }},
    {"POST", "/v1/admin/datasets/synth", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->synth_dataset(h, r);
     }},
    {"GET", "/v1/admin/datasets/kits", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_kits(r);
     }},
    {"GET", "/v1/admin/datasets/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_dataset(r, id);
     }},
    {"DELETE", "/v1/admin/datasets/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_dataset(r, id);
     }},
    // The training slice: reads only. `train run|eval|promote|rollback|setup`,
    // `pipeline run|resume`, `regime run` and `cycle run|halt|resume` have
    // no route by the track's constraint -- control is CLI-only.
    {"GET", "/v1/admin/training/status", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->training_status(r);
     }},
    {"GET", "/v1/admin/training/runs", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_training_runs(r);
     }},
    {"GET", "/v1/admin/training/runs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_training_run(r, id);
     }},
    {"GET", "/v1/admin/training/versions", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_training_versions(r);
     }},
    {"GET", "/v1/admin/training/cycle", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->training_cycle(r);
     }},
    {"GET", "/v1/admin/permissions", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_permissions(r);
     }},
    {"PUT", "/v1/admin/permissions/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_permission(r, id);
     }},
    {"GET", "/v1/admin/auth", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_credentials(r);
     }},
    {"PUT", "/v1/admin/auth/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_credential(r, id);
     }},
    {"DELETE", "/v1/admin/auth/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->clear_credential(r, id);
     }},
    {"GET", "/v1/admin/events", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->events_stream(r);
     }},
    {"GET", "/v1/admin/jobs", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_jobs(r);
     }},
    {"GET", "/v1/admin/jobs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_job(r, id);
     }},
    {"DELETE", "/v1/admin/jobs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->cancel_job(r, id);
     }},
}};

}  // namespace

bool is_admin_path(std::string_view path) noexcept {
    return path == kAdminPrefix || path.starts_with("/v1/admin/");
}

std::optional<std::string> match_route(std::string_view pattern, std::string_view path) {
    std::string captured;
    while (true) {
        const std::size_t pattern_slash = pattern.find('/');
        const std::size_t path_slash = path.find('/');
        const std::string_view pattern_segment = pattern.substr(0, pattern_slash);
        const std::string_view path_segment = path.substr(0, path_slash);

        if (pattern_segment == "{id}") {
            if (path_segment.empty()) {
                return std::nullopt;
            }
            captured = std::string{path_segment};
        } else if (pattern_segment != path_segment) {
            return std::nullopt;
        }

        const bool pattern_done = pattern_slash == std::string_view::npos;
        const bool path_done = path_slash == std::string_view::npos;
        if (pattern_done || path_done) {
            return pattern_done && path_done ? std::optional<std::string>{captured} : std::nullopt;
        }
        pattern.remove_prefix(pattern_slash + 1);
        path.remove_prefix(path_slash + 1);
    }
}

Mux::Mux(Handler& handler) : handler_{&handler} {}

Mux::Mux(Handler& handler, AdminHandler& admin, std::string admin_token)
    : handler_{&handler}, admin_{&admin}, admin_token_{std::move(admin_token)} {}

std::vector<RouteSpec> Mux::routes() {
    std::vector<RouteSpec> out;
    out.reserve(kRoutes.size());
    for (const Route& route : kRoutes) {
        out.push_back(RouteSpec{route.method, route.pattern, route.admin});
    }
    return out;
}

HttpResponse Mux::dispatch(const HttpRequest& request) const {
    if (is_admin_path(request.path)) {
        if (admin_ == nullptr) {
            return error_response(404, "the admin plane is not mounted on this server",
                                  kNotFoundError);
        }
        // The gate runs BEFORE routing: an unauthenticated probe learns
        // nothing about which routes exist.
        if (!bearer_valid(request, admin_token_)) {
            return unauthorized_response();
        }
    }

    std::string allowed;
    for (const Route& route : kRoutes) {
        const std::optional<std::string> captured = match_route(route.pattern, request.path);
        if (!captured.has_value()) {
            continue;
        }
        if (request.method != route.method) {
            allowed += allowed.empty() ? "" : ", ";
            allowed += route.method;
            continue;
        }
        return route.call(*handler_, admin_, request, *captured);
    }
    if (!allowed.empty()) {
        HttpResponse out = error_response(405, request.method + " is not allowed on " +
                                                   request.path + " (allowed: " + allowed + ")");
        out.headers["Allow"] = allowed;
        return out;
    }
    return error_response(404, "no route for " + request.method + " " + request.path,
                          kNotFoundError);
}

}  // namespace apogee::httpserver
