#pragma once

#include <string_view>

#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"

/// The symphonies slice of the control plane (27q): the twins of `apogee
/// symphonies create|edit|delete`, over the SAME scaffold core the CLI calls
/// (`scaffold/symphony.h`), so a symphony written here is a byte-identical
/// config entry; and the reads `symphonies list|show --output-format json`
/// print, served byte for byte (`symphony/view.h`). `play` is not here: it
/// runs the host's models on a user's act, CLI-only like training control.
///
/// The body of `POST` and `PUT` is the definition as the view shows it --
/// `name` (POST; PUT's is the path's), `description`, `input: {description,
/// image}`, `stages: [{name, role, prompt, schema, image, brief_tokens,
/// answer_tokens}]` -- and `force` (POST). `PUT` is create with `force`, as
/// the agents' is. No `restart_required`: a play reads the config each run.
namespace apogee::httpserver {

/// `GET /v1/admin/symphonies`.
[[nodiscard]] HttpResponse admin_list_symphonies(const AdminConfigContext& context);

/// `POST /v1/admin/symphonies` -- the `symphonies create` twin.
[[nodiscard]] HttpResponse admin_create_symphony(const AdminConfigContext& context,
                                                 const HttpRequest& request);

/// `GET /v1/admin/symphonies/{id}`.
[[nodiscard]] HttpResponse admin_get_symphony(const AdminConfigContext& context,
                                              std::string_view name);

/// `PUT /v1/admin/symphonies/{id}` -- the `symphonies edit` twin.
[[nodiscard]] HttpResponse admin_put_symphony(const AdminConfigContext& context,
                                              std::string_view name, const HttpRequest& request);

/// `DELETE /v1/admin/symphonies/{id}` -- the `symphonies delete` twin.
[[nodiscard]] HttpResponse admin_delete_symphony(const AdminConfigContext& context,
                                                 std::string_view name);

}  // namespace apogee::httpserver
