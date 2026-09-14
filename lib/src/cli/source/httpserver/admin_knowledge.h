#pragma once

#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"

/// The knowledge slice of the control plane: the twin of `apogee knowledge
/// capture`, and the finished-record store a review UI needs.
///
/// Both run through the same capture core the CLI and chat call
/// (`commands/knowledge_core.h`), on the inference plane's own harness -- one
/// set of providers per server -- with the clerk's backend resolved the way a
/// chat request's `model` is: served backends only, a vendor CLI refused by
/// type. A record captured here is the record the CLI would have produced.
namespace apogee::httpserver {

class Handler;

/// `POST /v1/admin/knowledge/capture` -- the `knowledge capture` twin.
/// Body: `raw` (required), and optionally `model`, `db`, `status`,
/// `discipline`, `source`, `link`, `supersedes`, `retriever`. `201` with
/// `{record, db, retriever, registered[, note][, notes]}`; `400` on a bad
/// body, an unknown model or a resolver refusal; `501` when the server has no
/// generation backend; `502` when the clerk failed.
[[nodiscard]] HttpResponse admin_capture_knowledge(const AdminConfigContext& context,
                                                   Handler& plane, const HttpRequest& request);

/// `POST /v1/admin/knowledge` -- stores a finished record without running the
/// clerk: the record's fields at the top level (`intent` required), plus
/// optionally `raw` (archived), `db` and `retriever`. `201` with the same
/// envelope; `400` when the record does not validate.
[[nodiscard]] HttpResponse admin_create_knowledge(const AdminConfigContext& context, Handler& plane,
                                                  const HttpRequest& request);

}  // namespace apogee::httpserver
