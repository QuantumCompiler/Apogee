#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>

#include "secrets/resolve.h"

/// The stored credentials as the command line and the control plane both show
/// them (37g, the 28h idiom): `auth list --output-format json` prints the very
/// body `GET /v1/admin/auth` serves. Metadata only, by construction: a
/// `CredentialMetadata` has no key field, so nothing here can serialize one.
/// Here rather than in either surface, so neither includes the other.
namespace apogee::operations {

/// `{"object": "list", "data": [{provider, stored_at}], "backends": [{name,
/// type, source, variable?}], "warning"?}` -- each stored slot, then which
/// rung answers for each configured backend that takes a key (the config at
/// `config_path`, against `env`; none listed when it will not load), and why
/// the store could not be read when it could not.
[[nodiscard]] nlohmann::json credentials_document(const std::filesystem::path& config_path,
                                                  const secrets::EnvSnapshot& env);

}  // namespace apogee::operations
