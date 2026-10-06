#pragma once

#include <string_view>

#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"

/// The read twins of `apogee graph path|explain|neighbors|query` (27l): the
/// pattern the graph slice's `stats` and `entity` reads set, `{name}`
/// resolved graphs-first exactly as `--graph` resolves it, each answering
/// with the document the CLI prints under `--output-format json` and the
/// `graph` tools return -- one core (`graph/navigate`), byte for byte.
///
/// Reads only: nothing here writes, and nothing mutating joins them. A node
/// that resolves to nothing is a `404`, one several nodes answer to a `400`
/// carrying `candidates`, a cap out of range a `400` naming it -- every
/// message the CLI's own.
namespace apogee::httpserver {

/// `GET /v1/admin/graph/{name}/path?from=&to=&max_hops=&directed=&relations=a,b`
[[nodiscard]] HttpResponse admin_graph_path(const AdminConfigContext& context,
                                            std::string_view name, const HttpRequest& request);

/// `GET /v1/admin/graph/{name}/explain?node=&max_neighbors=`
[[nodiscard]] HttpResponse admin_graph_explain(const AdminConfigContext& context,
                                               std::string_view name, const HttpRequest& request);

/// `GET /v1/admin/graph/{name}/neighbors?node=&relation=&direction=&max_neighbors=`
[[nodiscard]] HttpResponse admin_graph_neighbors(const AdminConfigContext& context,
                                                 std::string_view name, const HttpRequest& request);

/// `GET /v1/admin/graph/{name}/query?q=&hops=&max_entities=`
[[nodiscard]] HttpResponse admin_graph_query(const AdminConfigContext& context,
                                             std::string_view name, const HttpRequest& request);

}  // namespace apogee::httpserver
