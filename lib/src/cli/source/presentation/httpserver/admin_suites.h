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
/// The twin of `config set-suite --consultable/--consult-cap` (27f): `{
/// "consultable"?: [role, ...] | null, "consult_caps"?: {per_turn?,
/// brief_tokens?, answer_tokens?} | null}` -- a key left out keeps what the
/// entry has, `null` clears it, a cap set to `null` takes its default. The
/// two keys are replaced in place, every other line of the entry as it was;
/// a consultable member must be local and unmetered, as the CLI holds it.
[[nodiscard]] HttpResponse admin_set_suite_consult(const AdminConfigContext& context,
                                                   std::string_view name,
                                                   const HttpRequest& request);
/// The twin of `config set-suite --verifier/--validate` (27g): `{"verifier"?:
/// role | null, "tool_args"?: bool | null, "extraction"?: bool | null,
/// "answers"?: "request" | "always" | null}` -- a key left out keeps what the
/// entry has, `null` puts it back to its default, and nothing left set removes
/// the block. Replaced in place, every other line of the entry as it was; the
/// verifier must be a local, unmetered member, as the CLI holds it.
[[nodiscard]] HttpResponse admin_set_suite_validate(const AdminConfigContext& context,
                                                    std::string_view name,
                                                    const HttpRequest& request);
/// The twin of `config set-suite --orchestrate on|off` (27t): `{"orchestrate":
/// true | false}`, written in place -- `orchestrate: true`, or the key
/// removed -- every other line of the entry as it was; on, every member a
/// symphony the suite would offer reaches must be local and unmetered, as the
/// CLI holds it.
[[nodiscard]] HttpResponse admin_set_suite_orchestrate(const AdminConfigContext& context,
                                                       std::string_view name,
                                                       const HttpRequest& request);
/// The twin of `config set-default-suite`: `{"name"}`, `off` for none.
[[nodiscard]] HttpResponse admin_set_default_suite(const AdminConfigContext& context,
                                                   const HttpRequest& request);

}  // namespace apogee::httpserver
