#pragma once

#include <filesystem>
#include <string_view>

#include "events/bus.h"
#include "harness/config.h"
#include "httpserver/admin_auth_routes.h"
#include "httpserver/admin_config.h"
#include "httpserver/admin_events.h"
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

    [[nodiscard]] HttpResponse list_permissions(const HttpRequest& request);
    [[nodiscard]] HttpResponse put_permission(const HttpRequest& request, std::string_view tool);

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

    [[nodiscard]] const AdminOptions& options() const noexcept {
        return options_;
    }

private:
    [[nodiscard]] AdminConfigContext config_context() const;

    AdminOptions options_;
    JobRegistry* jobs_;
    events::Bus* bus_;
};

}  // namespace apogee::httpserver
