#pragma once

#include <string_view>

#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"

/// The `suites:` config slice of the control plane (27d) -- the twins of
/// `apogee config add-suite`, `set-suite`, `delete-suite` and
/// `set-default-suite`. Writes go through the same comment-preserving
/// transforms the CLI uses, so an entry made over HTTP is byte-identical to
/// one made on the command line, and the write-time rules are the CLI's own
/// (`commands::validate_suite`). A suite is resolution, not transport: the
/// running server resolves under the suite it started with, and every write
/// says `restart_required` when the file has moved on from it.
namespace apogee::httpserver {

[[nodiscard]] HttpResponse admin_list_suites(const AdminConfigContext& context);
/// `409` when the name exists -- `PUT` replaces.
[[nodiscard]] HttpResponse admin_create_suite(const AdminConfigContext& context,
                                              const HttpRequest& request);
[[nodiscard]] HttpResponse admin_get_suite(const AdminConfigContext& context,
                                           std::string_view name);
/// Replaces one entry; a body `name`, when present, must match the path.
[[nodiscard]] HttpResponse admin_put_suite(const AdminConfigContext& context, std::string_view name,
                                           const HttpRequest& request);
[[nodiscard]] HttpResponse admin_delete_suite(const AdminConfigContext& context,
                                              std::string_view name);
/// One member, in place -- the twin of `config set-suite`: `{"role", "member":
/// <backend or {"backend", "context_size"?, "toolset"?}> | null}`, null
/// removing it.
[[nodiscard]] HttpResponse admin_set_suite_member(const AdminConfigContext& context,
                                                  std::string_view name,
                                                  const HttpRequest& request);
/// The twin of `config set-default-suite`: `{"name"}`, `off` for none.
[[nodiscard]] HttpResponse admin_set_default_suite(const AdminConfigContext& context,
                                                   const HttpRequest& request);

}  // namespace apogee::httpserver
