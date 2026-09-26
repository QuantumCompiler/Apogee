#pragma once

#include <filesystem>
#include <string_view>

#include "events/bus.h"
#include "harness/config.h"
#include "httpserver/admin_agents.h"
#include "httpserver/admin_auth_routes.h"
#include "httpserver/admin_config.h"
#include "httpserver/admin_datasets.h"
#include "httpserver/admin_events.h"
#include "httpserver/admin_graph.h"
#include "httpserver/admin_graphs.h"
#include "httpserver/admin_knowledge.h"
#include "httpserver/admin_training.h"
#include "httpserver/http_types.h"
#include "httpserver/jobs.h"

/// The control plane: one method per `/v1/admin/*` route.
///
/// Mounted behind the bearer gate the mux applies to the whole `/v1/admin/`
/// prefix, so nothing here checks a token. What it does check, on the one
/// route that can carry a secret, is the socket's peer address.
///
/// The public inference plane and this one are deliberately two handlers: the
/// first is open for OpenAI-client compatibility, the second is gated and
/// mutating, and a route can only be in one of them.
namespace apogee::httpserver {

class Handler;

struct AdminOptions {
    std::filesystem::path config_path;
    /// The config this server started from -- for `restart_required`.
    harness::Config startup;
    EventStreamOptions events;
    /// The environment the credential listing reports against; null means
    /// the process-wide snapshot.
    const secrets::EnvSnapshot* env = nullptr;
};

class AdminHandler {
public:
    /// `jobs` and `bus` outlive the handler.
    AdminHandler(AdminOptions options, JobRegistry& jobs, events::Bus& bus);

    [[nodiscard]] HttpResponse list_backends(const HttpRequest& request);
    [[nodiscard]] HttpResponse create_backend(const HttpRequest& request);
    [[nodiscard]] HttpResponse get_backend(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse delete_backend(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse set_default(const HttpRequest& request);
    [[nodiscard]] HttpResponse set_default_embedding(const HttpRequest& request);
    [[nodiscard]] HttpResponse set_default_extraction(const HttpRequest& request);
    [[nodiscard]] HttpResponse format_config(const HttpRequest& request);

    [[nodiscard]] HttpResponse list_mcp_servers(const HttpRequest& request);
    [[nodiscard]] HttpResponse create_mcp_server(const HttpRequest& request);
    [[nodiscard]] HttpResponse get_mcp_server(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse delete_mcp_server(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse set_mcp_server_enabled(const HttpRequest& request,
                                                      std::string_view name);

    [[nodiscard]] HttpResponse list_agents(const HttpRequest& request);
    [[nodiscard]] HttpResponse create_agent(const HttpRequest& request);
    [[nodiscard]] HttpResponse get_agent(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse put_agent(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse delete_agent(const HttpRequest& request, std::string_view name);

    /// The knowledge routes borrow the inference plane: its harness runs the
    /// clerk, its served set decides which backend may.
    [[nodiscard]] HttpResponse capture_knowledge(Handler& plane, const HttpRequest& request);
    [[nodiscard]] HttpResponse create_knowledge(Handler& plane, const HttpRequest& request);
    [[nodiscard]] HttpResponse refine_knowledge(Handler& plane, const HttpRequest& request);
    [[nodiscard]] HttpResponse list_knowledge(Handler& plane, const HttpRequest& request);
    [[nodiscard]] HttpResponse reindex_knowledge(Handler& plane, const HttpRequest& request);
    [[nodiscard]] HttpResponse get_knowledge(const HttpRequest& request, std::string_view id);
    [[nodiscard]] HttpResponse patch_knowledge(const HttpRequest& request, std::string_view id);
    [[nodiscard]] HttpResponse delete_knowledge(const HttpRequest& request, std::string_view id);

    /// The graph routes: `{name}` graphs-first; the build and the community
    /// summaries are async jobs on the plane's harness, dedupe synchronous.
    [[nodiscard]] HttpResponse build_graph(Handler& plane, const HttpRequest& request,
                                           std::string_view name);
    [[nodiscard]] HttpResponse graph_stats(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse graph_entity(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse delete_graph(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse set_graph_enabled(const HttpRequest& request,
                                                 std::string_view collection);
    [[nodiscard]] HttpResponse build_communities(Handler& plane, const HttpRequest& request,
                                                 std::string_view name);
    [[nodiscard]] HttpResponse list_communities(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse dedupe_graph(const HttpRequest& request, std::string_view name);

    /// The `graphs:` config slice -- the twins of `config add-graph` /
    /// `delete-graph`.
    [[nodiscard]] HttpResponse list_graphs(const HttpRequest& request);
    [[nodiscard]] HttpResponse create_graph(const HttpRequest& request);
    [[nodiscard]] HttpResponse get_graph(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse put_graph(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse delete_graph_config(const HttpRequest& request,
                                                   std::string_view name);

    /// The datasets slice -- the twins of `apogee datasets`; synth is the
    /// plane's third job kind (teacher inference, not training).
    [[nodiscard]] HttpResponse list_datasets(const HttpRequest& request);
    [[nodiscard]] HttpResponse create_dataset(const HttpRequest& request);
    [[nodiscard]] HttpResponse get_dataset(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse delete_dataset(const HttpRequest& request, std::string_view name);
    [[nodiscard]] HttpResponse synth_dataset(Handler& plane, const HttpRequest& request);
    [[nodiscard]] HttpResponse list_kits(const HttpRequest& request);

    /// The training slice -- reads only; every control action is a parity
    /// carve-out with no route.
    [[nodiscard]] HttpResponse training_status(const HttpRequest& request);
    [[nodiscard]] HttpResponse list_training_runs(const HttpRequest& request);
    [[nodiscard]] HttpResponse get_training_run(const HttpRequest& request, std::string_view id);
    [[nodiscard]] HttpResponse list_training_versions(const HttpRequest& request);
    [[nodiscard]] HttpResponse training_cycle(const HttpRequest& request);

    [[nodiscard]] HttpResponse list_permissions(const HttpRequest& request);
    [[nodiscard]] HttpResponse put_permission(const HttpRequest& request, std::string_view tool);
    [[nodiscard]] HttpResponse list_allowed_hosts(const HttpRequest& request);
    [[nodiscard]] HttpResponse put_allowed_host(const HttpRequest& request, std::string_view host);
    [[nodiscard]] HttpResponse delete_allowed_host(const HttpRequest& request,
                                                   std::string_view host);

    [[nodiscard]] HttpResponse list_credentials(const HttpRequest& request);
    [[nodiscard]] HttpResponse put_credential(const HttpRequest& request,
                                              std::string_view provider);
    [[nodiscard]] HttpResponse clear_credential(const HttpRequest& request,
                                                std::string_view provider);

    [[nodiscard]] HttpResponse events_stream(const HttpRequest& request);

    [[nodiscard]] HttpResponse list_jobs(const HttpRequest& request);
    [[nodiscard]] HttpResponse get_job(const HttpRequest& request, std::string_view id);
    [[nodiscard]] HttpResponse cancel_job(const HttpRequest& request, std::string_view id);

    [[nodiscard]] JobRegistry& jobs() noexcept {
        return *jobs_;
    }

    /// The threads async jobs run on; joined -- after cancelling -- when the
    /// handler goes, so no job outlives the plane it works on.
    [[nodiscard]] JobWorkers& workers() noexcept {
        return workers_;
    }

    [[nodiscard]] const AdminOptions& options() const noexcept {
        return options_;
    }

private:
    [[nodiscard]] AdminConfigContext config_context() const;

    AdminOptions options_;
    JobRegistry* jobs_;
    events::Bus* bus_;
    JobWorkers workers_;
};

}  // namespace apogee::httpserver
