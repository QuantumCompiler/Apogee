#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// Editing a backend in place (46b): the one core `config set-backend` and
/// its admin twin call, so an entry edited over HTTP is byte for byte the
/// entry edited on the command line.
namespace apogee::commands {

/// One field to set: `value` empty clears it.
struct BackendFieldEdit {
    std::string field;
    std::string value;
};

/// What an edit did, or why it did nothing.
struct BackendFieldsOutcome {
    std::string said;
    std::string refusal;
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
/// `draft` (53i) takes a drafter's MLX directory, its stored handle
/// (`<model>/mlx/<id>`) or a model storing one drafter, written as the
/// drafter's absolute directory -- after it is paired with the model the
/// same edit leaves the entry over, by the backend ladder's own question
/// (`backends::probe_mlx_draft`): refused for a drafter that cannot run
/// beside it. An edit moving the model, or its type, under a drafter the
/// entry keeps is asked the same.
[[nodiscard]] BackendFieldsOutcome set_backend_fields(const std::filesystem::path& path,
                                                      std::string_view name,
                                                      const std::vector<BackendFieldEdit>& edits);

}  // namespace apogee::commands
