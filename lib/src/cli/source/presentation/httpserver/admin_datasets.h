#pragma once

#include <string_view>

#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"
#include "httpserver/jobs.h"

/// The datasets slice of the control plane -- the twins of `apogee
/// datasets`. `create` and `delete` are synchronous over the same store the
/// CLI writes, so a dataset made here is byte-identical to one made there.
/// `synth` is an **async job** (`datasets-synth`): teacher inference, not
/// training -- the distinction the training track's carve-out rests on, and
/// exactly how Ommi exposed it -- with the teacher named explicitly, served
/// by this plane, and never a vendor CLI. `prepare` and `pull` are backfills
/// of the datasets plane (a server-side conversion and download), not here.
namespace apogee::httpserver {

class Handler;

/// `GET /v1/admin/datasets`.
[[nodiscard]] HttpResponse admin_list_datasets(const AdminConfigContext& context);

/// `POST /v1/admin/datasets` -- the `datasets create` twin: `{name, from?,
/// lines?, backend?, since?, until?, force?}`.
[[nodiscard]] HttpResponse admin_create_dataset(const AdminConfigContext& context,
                                                const HttpRequest& request);

/// `GET /v1/admin/datasets/{id}`.
[[nodiscard]] HttpResponse admin_get_dataset(const AdminConfigContext& context,
                                             std::string_view name);

/// `DELETE /v1/admin/datasets/{id}` -- the `datasets delete` twin.
[[nodiscard]] HttpResponse admin_delete_dataset(const AdminConfigContext& context,
                                                std::string_view name);

/// `POST /v1/admin/datasets/synth` -- the `datasets synth` twin, as a job.
[[nodiscard]] HttpResponse admin_synth_dataset(const AdminConfigContext& context, Handler& plane,
                                               JobRegistry& jobs, JobWorkers& workers,
                                               const HttpRequest& request);

/// `GET /v1/admin/datasets/kits` -- the `datasets kits` listing.
[[nodiscard]] HttpResponse admin_list_kits(const AdminConfigContext& context);

}  // namespace apogee::httpserver
