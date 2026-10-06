#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "contracts/config.h"

/// The TTY-free core that writes a symphony (27q): the same function `apogee
/// symphonies create` and `edit` and the admin plane's `POST`/`PUT
/// /v1/admin/symphonies` call, so a symphony made over HTTP is the
/// byte-identical config entry one made on the command line is -- the
/// `scaffold/agent` precedent.
///
/// A definition is held to everything before a byte is written: rendered as
/// the entry would be and read back through the one parser (roles, never
/// backends; names; caps; keys), then validated (templates, schemas, the
/// input read) -- and, since 27r, walked as the entry will stand among every
/// source, so a definition that loops through another, nests past the cap or
/// plays a name nothing defines is refused naming it -- so a refused one
/// leaves the config as it was. The write is the comment-preserving
/// editor's, never a marshal.
namespace apogee::scaffold {

/// What was written.
struct SymphonyResult {
    std::string name;
    std::filesystem::path config_path;
    /// An existing entry of the name was replaced (`force`).
    bool replaced = false;
};

/// Writes `spec` as the `symphonies:` entry `spec.name`. Throws
/// std::runtime_error with the reason on any refusal: a name that is not
/// one, a definition the parser or the validation refuses (the first
/// problem named, and how many more), an entry of that name without
/// `force`, a config that cannot be read or written.
[[nodiscard]] SymphonyResult create_symphony(const std::filesystem::path& config_path,
                                             const harness::SymphonySpec& spec, bool force);

/// The definition a bare `symphonies create <name>` writes: one stage on the
/// utility role whose prompt is the input -- a skeleton to edit.
[[nodiscard]] harness::SymphonySpec starter_symphony(std::string_view name,
                                                     std::string_view description);

}  // namespace apogee::scaffold
