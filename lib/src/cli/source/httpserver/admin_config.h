#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string_view>

#include "harness/config.h"
#include "httpserver/http_types.h"

/// The config-editing slice of the control plane: backends and the three role
/// pointers, each the twin of an `apogee config` subcommand.
///
/// **Every write is the CLI's transform.** `append_backend`, `delete_backend`
/// and `set_models_role` from `harness/config_edit.h`, under `edit_config_file`
/// -- the handler formats no YAML of its own, so an HTTP edit is byte-identical
/// to the CLI's on the same starting file, comments and key order intact. The
/// byte-identity test is the proof, and the reason these are free functions
/// over a config path rather than methods: the test calls exactly what the
/// route calls.
///
/// **The config is read from disk on every request.** The server's own
/// providers were built from the file at startup; the plane reports the file
/// as it is now, and says when the two have drifted apart.
///
/// **No `api_key` ever leaves.** The view type these routes serialize carries
/// `api_key_set` and nothing else about the key -- the secrets rule made
/// structural, the same way the credential store's metadata will be.
namespace apogee::httpserver {

struct AdminConfigContext {
    std::filesystem::path config_path;
    /// The config this server started from, for `restart_required`.
    const harness::Config* startup = nullptr;
};

/// A backend entry as the plane serializes it: every field but the key.
[[nodiscard]] nlohmann::json backend_view(std::string_view name,
                                          const harness::BackendConfig& backend);

/// Whether the file now differs from what the server started with, in what
/// it serves: backend membership or the role pointers. Stateless on purpose,
/// so drift a CLI edit made while the server ran is reported too.
[[nodiscard]] bool config_drifted(const harness::Config& startup, const harness::Config& now);

/// Whether `api_key` is a literal secret rather than a `${ENV}` reference.
[[nodiscard]] bool is_literal_api_key(std::string_view api_key) noexcept;

/// `GET /v1/admin/backends`.
[[nodiscard]] HttpResponse admin_list_backends(const AdminConfigContext& context);

/// `GET /v1/admin/mcp-servers`: every entry as a view -- `command`, `args`,
/// `enabled`, and whether `env` is set, never its values.
[[nodiscard]] HttpResponse admin_list_mcp_servers(const AdminConfigContext& context);

/// `POST /v1/admin/mcp-servers` -- the `mcp create` twin: `{name, command?,
/// args?, force?}`; without `command` a Python server is scaffolded under the
/// data directory, exactly as the CLI would.
[[nodiscard]] HttpResponse admin_create_mcp_server(const AdminConfigContext& context,
                                                   const HttpRequest& request);

[[nodiscard]] HttpResponse admin_get_mcp_server(const AdminConfigContext& context,
                                                std::string_view name);

/// `DELETE /v1/admin/mcp-servers/{id}` -- the `config delete-mcp-server` twin.
[[nodiscard]] HttpResponse admin_delete_mcp_server(const AdminConfigContext& context,
                                                   std::string_view name);

/// `PUT /v1/admin/mcp-servers/{id}` -- the `mcp enable`/`disable` twin:
/// `{"enabled": true|false}`.
[[nodiscard]] HttpResponse admin_set_mcp_server_enabled(const AdminConfigContext& context,
                                                        std::string_view name,
                                                        const HttpRequest& request);

/// `GET /v1/admin/permissions`: every destructive tool with its effective
/// level, plus any other key the config carries.
[[nodiscard]] HttpResponse admin_list_permissions(const AdminConfigContext& context);

/// `PUT /v1/admin/permissions/{id}` -- the `config set-permission` twin.
/// Body: `{"level": "ask" | "allow" | "deny"}`.
[[nodiscard]] HttpResponse admin_put_permission(const AdminConfigContext& context,
                                                std::string_view tool, const HttpRequest& request);

/// `POST /v1/admin/backends` -- the `config add-backend` twin. `force` in the
/// body is `--force`. A literal `api_key` is accepted from a loopback peer
/// only (`403` otherwise): the `${ENV}` reference the CLI recommends is not a
/// secret and passes from anywhere.
[[nodiscard]] HttpResponse admin_create_backend(const AdminConfigContext& context,
                                                const HttpRequest& request);

/// `GET /v1/admin/backends/{name}`.
[[nodiscard]] HttpResponse admin_get_backend(const AdminConfigContext& context,
                                             std::string_view name);

/// `DELETE /v1/admin/backends/{name}` -- the `config delete-backend` twin.
[[nodiscard]] HttpResponse admin_delete_backend(const AdminConfigContext& context,
                                                std::string_view name);

/// `POST /v1/admin/config/format` -- the `config format` twin: whitespace
/// tidied, content untouched.
[[nodiscard]] HttpResponse admin_format_config(const AdminConfigContext& context);

/// `POST /v1/admin/backends/{default|default-embedding|default-extraction}` --
/// the `config set-default*` twins. `field` is the `models:` key.
[[nodiscard]] HttpResponse admin_set_role(const AdminConfigContext& context, std::string_view field,
                                          const HttpRequest& request);

}  // namespace apogee::httpserver
