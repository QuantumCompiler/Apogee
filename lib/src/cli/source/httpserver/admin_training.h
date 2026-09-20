#pragma once

#include <string_view>

#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"

/// The training slice of the control plane -- **reads only**. `train run`,
/// `eval`, `promote`, `rollback` and `setup` are CLI-only by the track's
/// constraint: an expensive GPU job with live progress is not a control
/// surface a remote client should be able to start, and a promotion changes
/// what a server chats with. Each is a documented parity carve-out; what a
/// remote client may do is see the runs, the manifests and the ledgers,
/// read straight off the filesystem the CLI writes.
namespace apogee::httpserver {

/// `GET /v1/admin/training/status`: `{runs, running[], versions[],
/// pipelines, active_pipeline, cycle_active, cycle}` -- `active_pipeline`
/// the id of the newest pipeline whose manifest says `running` (else
/// `null`), `cycle_active` whether a cycle holds the lock now, and `cycle`
/// the history's headline fields (`null` with no history).
[[nodiscard]] HttpResponse admin_training_status(const AdminConfigContext& context);

/// `GET /v1/admin/training/runs[?kind=run|pipeline]`: every run's and every
/// pipeline run's summary, newest first, each tagged `kind`; the filter
/// keeps one kind. `data` is `[]` and never null.
[[nodiscard]] HttpResponse admin_list_training_runs(const AdminConfigContext& context,
                                                    const HttpRequest& request);

/// `GET /v1/admin/training/runs/{id}`: `{kind: "run", run: <manifest>}` or
/// `{kind: "pipeline", pipeline: <manifest>}`; `404` when neither.
[[nodiscard]] HttpResponse admin_get_training_run(const AdminConfigContext& context,
                                                  std::string_view id);

/// `GET /v1/admin/training/cycle`: the cycle history plus `active` (the
/// lock is held); `404` with no history yet. `cycle run|halt|resume` have
/// no route.
[[nodiscard]] HttpResponse admin_training_cycle(const AdminConfigContext& context);

/// `GET /v1/admin/training/versions[?backend=]`: one ledger (`404` when
/// there is none), or every ledger as a list.
[[nodiscard]] HttpResponse admin_list_training_versions(const AdminConfigContext& context,
                                                        const HttpRequest& request);

}  // namespace apogee::httpserver
