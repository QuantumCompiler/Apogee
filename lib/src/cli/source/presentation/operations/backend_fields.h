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
[[nodiscard]] BackendFieldsOutcome set_backend_fields(const std::filesystem::path& path,
                                                      std::string_view name,
                                                      const std::vector<BackendFieldEdit>& edits);

}  // namespace apogee::commands
