#pragma once

#include <string_view>

#include "httpserver/admin_config.h"
#include "httpserver/http_types.h"

/// The knowledge slice of the control plane: the twin of `apogee knowledge
/// capture`, the finished-record store a review UI needs, the read and edit
/// routes, and the stateless capture → review → store flow.
///
/// Every write runs through the same capture core the CLI and chat call
/// (`commands/knowledge_core.h`), on the inference plane's own harness -- one
/// set of providers per server -- with the clerk's backend resolved the way a
/// chat request's `model` is: served backends only, a vendor CLI refused by
/// type. A record captured here is the record the CLI would have produced.
///
/// **The refine loop is stateless.** `draft: true` returns a record with no
/// id, no timestamp and no `raw_ref`; `/refine` takes that draft back with
/// one instruction and returns a new one; the reviewed draft is stored
/// through the ordinary finished-record `POST`. The server holds nothing
/// between calls, and nothing touches the store, the archive or the config
/// until the store step -- which is what makes a draft → refine → store round
/// trip land the record a one-shot capture would.
namespace apogee::httpserver {

class Handler;

/// `POST /v1/admin/knowledge/capture` -- the `knowledge capture` twin.
/// Body: `raw` (required), and optionally `model`, `db`, `status`,
/// `discipline`, `source`, `link`, `supersedes`, `retriever`, and `draft`.
/// `201` with `{record, db, retriever, registered[, note][, notes]}`; with
/// `draft: true`, `200` with `{draft: true, record, db, retriever[, warning]
/// [, note]}` and nothing stored. `400` on a bad body, an unknown model or a
/// resolver refusal; `501` when the server has no generation backend; `502`
/// when the clerk failed.
[[nodiscard]] HttpResponse admin_capture_knowledge(const AdminConfigContext& context,
                                                   Handler& plane, const HttpRequest& request);

/// `POST /v1/admin/knowledge` -- stores a finished record without running the
/// clerk: the record's fields at the top level (`intent` required), plus
/// optionally `raw` (archived), `db` and `retriever`. `201` with the same
/// envelope; `400` when the record does not validate.
[[nodiscard]] HttpResponse admin_create_knowledge(const AdminConfigContext& context, Handler& plane,
                                                  const HttpRequest& request);

/// `POST /v1/admin/knowledge/refine` -- one bounded revision pass over a
/// client-held draft: `{record, instruction, raw?, model?}`. `200` with
/// `{draft: true, record}`, never stored. `400` for a missing record or an
/// instruction that is empty or over `kMaxRefineInstructionLen` -- checked
/// BEFORE any clerk call; `501` with no generation backend; `502` when the
/// revision is not a conforming record.
[[nodiscard]] HttpResponse admin_refine_knowledge(const AdminConfigContext& context, Handler& plane,
                                                  const HttpRequest& request);

/// `GET /v1/admin/knowledge[?db&q&status&discipline&retriever&rerank&limit&anonymize=true]`.
/// A missing collection is an empty list, never an error and never a created
/// file. With `q`: `{object: "list", data: [{record, score}…], retriever,
/// reranked[, note]}` through the one resolver; `501` for an explicit
/// `vector` with no embedding backend. Without `q`: `{object: "list", data:
/// [record…]}`, newest first.
[[nodiscard]] HttpResponse admin_list_knowledge(const AdminConfigContext& context, Handler& plane,
                                                const HttpRequest& request);

/// `GET /v1/admin/knowledge/{id}[?db]` -- the record, or `404`.
[[nodiscard]] HttpResponse admin_get_knowledge(const AdminConfigContext& context,
                                               std::string_view id, const HttpRequest& request);

/// `PATCH /v1/admin/knowledge/{id}` -- the `link` and `status` twins: a body
/// with exactly one of `link` or `status` (and optionally `db`). A metadata
/// edit, never a re-embed. `200` with the record; `400` for neither or both;
/// `404` when unknown.
[[nodiscard]] HttpResponse admin_patch_knowledge(const AdminConfigContext& context,
                                                 std::string_view id, const HttpRequest& request);

/// `DELETE /v1/admin/knowledge/{id}[?db]` -- the `delete` twin: the record
/// and its archived conversation. `200 {deleted}`; `404` when unknown.
[[nodiscard]] HttpResponse admin_delete_knowledge(const AdminConfigContext& context,
                                                  std::string_view id, const HttpRequest& request);

/// `POST /v1/admin/knowledge/reindex` -- the `reindex` twin: `{db?, id?}`.
/// `200 {reindexed[, note]}` -- zero with a note for a collection with no
/// records or no vectors; `404` for a collection or record that does not
/// exist; `501` for vectors with no embedding backend to rebuild them; `502`
/// when the embedder failed.
[[nodiscard]] HttpResponse admin_reindex_knowledge(const AdminConfigContext& context,
                                                   Handler& plane, const HttpRequest& request);

}  // namespace apogee::httpserver
