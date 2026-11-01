#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "operations/suites.h"

/// Editing a backend in place (46b): the one core `config set-backend` and
/// its admin twin call, so an entry edited over HTTP is byte for byte the
/// entry edited on the command line.
namespace apogee::commands {

/// One field to set: `value` empty clears it.
struct BackendFieldEdit {
    std::string field{};
    std::string value{};
};

/// What an edit did, or why it did nothing.
struct BackendFieldsOutcome {
    std::string said{};
    std::string refusal{};
};

/// `edits` applied to backend `name` in the config at `path`, every one in
/// one edit through the one writer -- each its own transform
/// (`set_backend_field`), only the named fields touched, comments kept,
/// re-parsed before the file is replaced. Refused naming why: no field
/// named, a backend not in the config, a type no backend has, a value its
/// field cannot hold, and `api_key` -- a key is never taken on the command
/// line, only by `auth add`.
///
/// `adapters` (53a) takes `<path>[:<scale>]`, comma-separated, and registers
/// each adapter pinned to the entry's base as it is now: the loader's rules
/// first, on the edit as it would land, then each path made absolute, its
/// file facts asked, and the sha256 of the weights at `model_path` -- as this
/// same edit leaves it -- recorded beside every one (`backends::pin_adapters`),
/// written through `set_backend_adapters`.
///
/// An edit moving `model_path`, or the type, under adapters the entry keeps
/// (55e) -- with no `adapters` among the edits -- is asked as a drafter is:
/// every adapter's pin against the weights the edit leaves it over. The
/// same weights elsewhere keep every pin, and the edit says so; other
/// weights refuse the edit naming each adapter and its pin, with the two
/// edits that carry the move through -- the same one re-pinning the set
/// (`--adapters`), or taking it off (`--adapters ""`).
///
/// `draft` (53i) takes a drafter's MLX directory, its stored handle
/// (`<model>/mlx/<id>`) or a model storing one drafter, written as the
/// drafter's absolute directory -- after it is paired with the model the
/// same edit leaves the entry over, by the backend ladder's own question
/// (`backends::probe_mlx_draft`): refused for a drafter that cannot run
/// beside it. An edit moving the model, or its type, under a drafter the
/// entry keeps is asked the same.
///
/// An edit that leaves an `mlx` entry over an MLX directory whose kind no mlx
/// entry runs is refused by that kind (`kind_refusal`, 55d).
///
/// An edit that leaves the entry with an `mcp_serve:` list (54b) -- the list
/// set, or kept under a moved type or path -- is asked of the entry's
/// provider through `metered`, on the config as the edit leaves it: asked
/// inside the write, of the very text it writes, so every edit the write
/// applies (an adapter set cleared beside a moved type among them) is asked
/// as it lands. A provider that bills each call, or one that cannot say,
/// refuses the edit naming the rule (`harness::mcp_serve_ineligible`), the
/// file untouched. A null probe tells nothing, and unknown is metered; the
/// CLI hands its own
/// (`provider_metered_probe`), the admin twin the one its composition root
/// gave it.
[[nodiscard]] BackendFieldsOutcome set_backend_fields(const std::filesystem::path& path,
                                                      std::string_view name,
                                                      const std::vector<BackendFieldEdit>& edits,
                                                      const MeteredProbe& metered = {});

/// **The kind decides the role** (55d): why entry `name` may not run what it
/// names, or empty when it may. An `mlx` entry over an MLX directory is held
/// to the directory's kind through the one predicate the backend ladder asks
/// (`backends::probe_mlx_kind`, over `models::MlxKind::registers()`): a
/// drafter, a video or voice model, or a word this build does not know is no
/// entry's model, refused with the kind and what uses it. Every other entry,
/// and a directory with no `config.json`, may. The doors that make an entry
/// or point a role at one ask it -- `config add-backend`, `config
/// set-backend`, `config set-default*` and their admin twins -- so a set
/// whose record says `voice` never becomes a chat backend by any of them.
[[nodiscard]] std::string kind_refusal(std::string_view name,
                                       const harness::BackendConfig& backend);

/// **A role is the kind's to take** (M33): why entry `name` may not be
/// `models.<field>` -- one of `models_role_fields()` -- or empty when it may.
/// `kind_refusal` first: a kind no mlx entry runs takes no role. Then the
/// role: `default_embedding` takes what embeds, and every other role -- the
/// chat default and the helpers -- what chats, so an mlx entry over an
/// embedding set mlx-embeddings built takes the embedding role and no other,
/// and one over a chat or vision set every role but that one, each refusal
/// naming the kind, the role, and the role the entry can take. Every other
/// entry, and an mlx directory with no `config.json`, may: the kind is the
/// MLX directory's to say. The doors that point a role ask it -- `config
/// set-default*`, their admin twins and the shell's make default -- and the
/// doors that add an entry ask it for `default` before making it the default
/// on someone's behalf (`add_backend_text`).
[[nodiscard]] std::string role_refusal(std::string_view name, const harness::BackendConfig& backend,
                                       std::string_view field);

/// What adding an entry left (M33): the config's text, and whether
/// `models.default` was pointed at the entry.
struct AddedBackend {
    std::string text{};
    bool default_set = false;
};

/// The one add every door that writes a backend entry by name shares (M33)
/// -- `config add-backend`, each entry `config scan --register` writes, the
/// register chains' entries (pinned to add-backend byte for byte, M3 and
/// M16) and the admin twin: `append_backend`, then, when `content` sets no
/// `models.default` and the entry `chats` (`role_refusal` for `default`
/// empty, asked by the caller before the edit), `models.default` pointed at
/// it in the same edit -- 28b's rule for `providers scan --register`, so
/// every door sets the default alike and says it alike (`default_set_words`).
/// A pure transform, as the editor's are; throws as they do.
[[nodiscard]] AddedBackend add_backend_text(std::string_view content, std::string_view name,
                                            const harness::BackendConfig& backend, bool force,
                                            bool chats);

/// `models.default set to '<name>' (none was set)`: how a door says it made
/// `name` the default on someone's behalf (28b, M33).
[[nodiscard]] std::string default_set_words(std::string_view name);

/// What deleting an entry left (M33): the config's text, and a line for
/// each role pointer that named the entry and was cleared with it.
struct DeletedBackend {
    std::string text{};
    std::vector<std::string> cleared{};
};

/// The one delete every door that removes a backend entry shares (M33) --
/// `config delete-backend`, the shell's remove, `models delete
/// --and-backends` and the admin twin: `delete_backend`, then every
/// `models.<role>` (`models_role_fields()`) that named the entry cleared in
/// the same edit, as `models delete` clears the `default_video` it strands
/// (55d) -- so no role names an entry that is gone, and an add that made
/// itself the default leaves none behind. Each clearing is said as
/// `models.default cleared: it named '<name>'`. A pure transform, as the
/// editor's are; throws as `delete_backend` does.
[[nodiscard]] DeletedBackend delete_backend_text(std::string_view content, std::string_view name);

}  // namespace apogee::commands
