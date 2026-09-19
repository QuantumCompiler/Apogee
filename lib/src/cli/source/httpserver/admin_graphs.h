#pragma once

#include <string_view>

#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"

/// The `graphs:` config slice of the control plane -- the twins of `apogee
/// config add-graph` / `delete-graph`, giving a remote client CRUD over
/// named multi-collection knowledge graphs. Writes go through the same
/// comment-preserving transforms the CLI uses, so an HTTP-made entry is
/// byte-identical to a CLI-made one, and the write-time rules are the CLI's
/// own (`commands::validate_named_graph`). Config only: the graph DATA routes
/// live at `/v1/admin/graph/{name}/*`, and deleting an entry here leaves the
/// graph's database on disk exactly as `config delete-graph` does.
namespace apogee::httpserver {

[[nodiscard]] HttpResponse admin_list_graphs(const AdminConfigContext& context);
/// `409` when the name exists -- `PUT` replaces.
[[nodiscard]] HttpResponse admin_create_graph(const AdminConfigContext& context,
                                              const HttpRequest& request);
[[nodiscard]] HttpResponse admin_get_graph(const AdminConfigContext& context,
                                           std::string_view name);
/// Replaces one entry; a body `name`, when present, must match the path.
[[nodiscard]] HttpResponse admin_put_graph(const AdminConfigContext& context, std::string_view name,
                                           const HttpRequest& request);
[[nodiscard]] HttpResponse admin_delete_graph_config(const AdminConfigContext& context,
                                                     std::string_view name);

}  // namespace apogee::httpserver
