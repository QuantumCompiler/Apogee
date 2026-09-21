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
/// clears a collection's graph rows or removes a named graph's database;
/// the `enabled` write is the twin of the build's own auto-enable, through
/// the one config editor.
///
/// Every `/v1/admin/graph/{name}/*` route resolves `{name}` **graphs-first**,
/// exactly as the CLI does: a `graphs:` entry is a named multi-collection
/// graph in its own database (a build over its members, stats per member,
/// an entity's chunks named by the member they live in, delete removing the
/// file); anything else is a collection.
///
/// `communities` is the plane's second async job (one summariser call per
/// new or changed cluster); `dedupe` is synchronous -- storage and cosine,
/// no generation. The extractor and the summariser run on the inference
/// plane's harness, resolved exactly as the CLI resolves them -- request
/// `model` > the entry's `extract_backend` > the extraction role > the
/// default, with a metered default refused -- and must be served backends.
namespace apogee::httpserver {

class Handler;

[[nodiscard]] HttpResponse admin_build_graph(const AdminConfigContext& context, Handler& plane,
                                             JobRegistry& jobs, JobWorkers& workers,
                                             std::string_view name, const HttpRequest& request);

[[nodiscard]] HttpResponse admin_graph_stats(const AdminConfigContext& context,
                                             std::string_view name, const HttpRequest& request);

[[nodiscard]] HttpResponse admin_graph_entity(const AdminConfigContext& context,
                                              std::string_view name, const HttpRequest& request);

[[nodiscard]] HttpResponse admin_delete_graph(const AdminConfigContext& context,
                                              std::string_view name, const HttpRequest& request);

[[nodiscard]] HttpResponse admin_set_graph_enabled(const AdminConfigContext& context,
                                                   std::string_view collection,
                                                   const HttpRequest& request);

/// `POST …/communities`: detect and summarise as a job (`graph-communities`).
[[nodiscard]] HttpResponse admin_build_communities(const AdminConfigContext& context,
                                                   Handler& plane, JobRegistry& jobs,
                                                   JobWorkers& workers, std::string_view name,
                                                   const HttpRequest& request);

/// `GET …/communities`: the stored communities; an unbuilt graph lists as
/// empty, never an error.
[[nodiscard]] HttpResponse admin_list_communities(const AdminConfigContext& context,
                                                  std::string_view name,
                                                  const HttpRequest& request);

/// `POST …/dedupe`: merge by vector similarity, synchronously; the report.
[[nodiscard]] HttpResponse admin_dedupe_graph(const AdminConfigContext& context,
                                              std::string_view name, const HttpRequest& request);

}  // namespace apogee::httpserver
