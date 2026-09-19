#pragma once

#include <string_view>

#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"
#include "httpserver/jobs.h"

/// The knowledge-graph slice of the control plane -- the twins of `apogee
/// graph`. A build runs one generation call per chunk, so it is an **async
/// job**: the route answers `202 {job_id}` at once, progress streams as
/// `admin.job.*` events, and the finished counts are pollable at
/// `GET /v1/admin/jobs/{id}`. Stats and entity lookup are pure reads; delete
/// clears a collection's graph rows; the `enabled` write is the twin of the
/// build's own auto-enable, through the one config editor.
///
/// The extractor runs on the inference plane's harness, resolved exactly as
/// the CLI resolves it -- request `model` > the collection's
/// `graph.extract_backend` > the extraction role > the default, with a
/// metered default refused -- and must be a served backend.
namespace apogee::httpserver {

class Handler;

[[nodiscard]] HttpResponse admin_build_graph(const AdminConfigContext& context, Handler& plane,
                                             JobRegistry& jobs, JobWorkers& workers,
                                             std::string_view collection,
                                             const HttpRequest& request);

[[nodiscard]] HttpResponse admin_graph_stats(const AdminConfigContext& context,
                                             std::string_view collection,
                                             const HttpRequest& request);

[[nodiscard]] HttpResponse admin_graph_entity(const AdminConfigContext& context,
                                              std::string_view collection,
                                              const HttpRequest& request);

[[nodiscard]] HttpResponse admin_delete_graph(const AdminConfigContext& context,
                                              std::string_view collection,
                                              const HttpRequest& request);

[[nodiscard]] HttpResponse admin_set_graph_enabled(const AdminConfigContext& context,
                                                   std::string_view collection,
                                                   const HttpRequest& request);

}  // namespace apogee::httpserver
