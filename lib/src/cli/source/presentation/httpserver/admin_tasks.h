#pragma once

#include <string_view>

#include "httpserver/http_types.h"

/// The tasks slice of the control plane (27j) -- **reads only**, the training
/// track's split verbatim: `task run`, `resume`, `halt` and `cancel` are
/// CLI-only, an unattended run of the host's own session and tools under its
/// lock not being a control surface a remote client should hold. Each is a
/// documented parity carve-out with no route; what a remote client may do is
/// see the tasks, read straight off the ledgers the CLI writes, through the
/// one view `task status` and `task list` render (`tasks/view.h`) -- so the
/// body a route serves is the document `--output-format json` prints, byte
/// for byte, and never carries a declared answer's text or a path into the
/// private layout.
namespace apogee::httpserver {

/// `GET /v1/admin/tasks[?all=true]`: `{"object": "list", "data": [{id,
/// status, rounds_used, rounds_budget, goal}], "total": N}`, newest first --
/// the newest 50 unless `all`, as `task list`. A ledger that cannot be read
/// is left out, as `task list` leaves it out.
[[nodiscard]] HttpResponse admin_list_tasks(const HttpRequest& request);

/// `GET /v1/admin/tasks/{id}`: the task's view, as `task status --output-format
/// json` prints it; `400` for an id that is not one, `404` when there is no
/// such task, `500` when its ledger cannot be read -- the reason never naming
/// the ledger's path.
[[nodiscard]] HttpResponse admin_get_task(std::string_view id);

}  // namespace apogee::httpserver
