#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string_view>

#include "harness/assets.h"
#include "harness/config.h"
#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"

/// The agents slice of the control plane: the twins of `apogee agents
/// create|edit|delete`, over the SAME scaffold core the CLI calls
/// (`scaffold/agent.h`), so an agent made here leaves byte-identical files
/// and a byte-identical config entry. `GET` inlines the prompt and schema
/// bodies so a remote client can show and edit them; `PUT` is `create`
/// with `force`, bodies included; `DELETE ?purge=true` removes the files.
///
/// No `restart_required`: `analyze` reads the config on every run, so an
/// agent registered here is runnable at once.
namespace apogee::httpserver {

/// An agent as the plane serializes it: every field of the entry, plus
/// whether it is a bundled agent (no entry) or an entry overriding one.
[[nodiscard]] nlohmann::json agent_view(const harness::NamedAgent& agent);

/// `GET /v1/admin/agents`.
[[nodiscard]] HttpResponse admin_list_agents(const AdminConfigContext& context);

/// `POST /v1/admin/agents` -- the `agents create` twin.
[[nodiscard]] HttpResponse admin_create_agent(const AdminConfigContext& context,
                                              const HttpRequest& request);

/// `GET /v1/admin/agents/{id}`: the view plus `prompt_bodies` and
/// `schema_bodies`, each `{path, body}`.
[[nodiscard]] HttpResponse admin_get_agent(const AdminConfigContext& context,
                                           std::string_view name);

/// `PUT /v1/admin/agents/{id}` -- the `agents edit` twin: the same body as
/// `POST`, `force` implied, the name from the path.
[[nodiscard]] HttpResponse admin_put_agent(const AdminConfigContext& context, std::string_view name,
                                           const HttpRequest& request);

/// `DELETE /v1/admin/agents/{id}[?purge=true]` -- the `agents delete` twin.
[[nodiscard]] HttpResponse admin_delete_agent(const AdminConfigContext& context,
                                              std::string_view name, const HttpRequest& request);

}  // namespace apogee::httpserver
