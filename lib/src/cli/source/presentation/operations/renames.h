#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "modelstore/store.h"

/// Renames that propagate (46a): a backend and a stored model, each the one
/// core every surface calls -- `config rename-backend` and its admin twin,
/// `models rename` -- so a rename means the same thing wherever it is made.
///
/// **One edit, or none.** Every config change of a rename is one transform
/// through the one writer (`contracts/config_edit.h`): the key and every
/// reference to it land together, comments kept, re-parsed before the file
/// is replaced. A store directory moved for it is moved back when the edit
/// is refused.
///
/// **The coupling** (the spike's rule (a)): automatic where the mapping is
/// one to one -- a backend named the M3 way after the model it alone runs,
/// `<base>-<level>` -- and withheld, and said, where it is shared or the
/// name is the user's own. Never a guess, never half a rename.
namespace apogee::commands {

/// What a rename did, a line each, or why it did nothing.
struct RenameOutcome {
    std::vector<std::string> said;
    /// The refusal, when nothing was renamed.
    std::string refusal;
};

/// The model store `config` names: the models directory, and SafeTensors
/// sets under `paths.hf_dir` when it is set.
[[nodiscard]] models::StoreRoots store_roots_of(const harness::Config& config);

/// A stored model's base name, the way M3 names its backends after it: the
/// part after the last `--` (`google--gemma-4-E2B` -> `gemma-4-E2B`).
[[nodiscard]] std::string model_base_name(std::string_view model);

/// The level of a backend named the M3 way after `model` -- `<base>-<level>`,
/// the level letters and digits (`F16`, `Q4KM`) -- or nullopt.
[[nodiscard]] std::optional<std::string> derived_level(std::string_view backend,
                                                       std::string_view model);

/// The stored model whose directory `file` sits in, under either of `roots`,
/// or empty: where a backend's `model_path` or `mmproj_path` points.
[[nodiscard]] std::string stored_model_at(const models::StoreRoots& roots,
                                          const std::filesystem::path& file);

/// `config rename-backend <old> <new>`: the key and every in-config
/// reference renamed in one edit; the saved chats naming it swept; and the
/// store directory moved with it when the coupling holds -- the backend its
/// sole referrer, named the M3 way, the new name too -- else left, said with
/// why.
[[nodiscard]] RenameOutcome rename_backend_everywhere(const std::filesystem::path& config_path,
                                                      const models::StoreRoots& roots,
                                                      std::string_view old_name,
                                                      std::string_view new_name);

/// `models rename <old> <new>`: the model's directories moved, records
/// riding inside them; every backend pointing into them repointed; the
/// backends named the M3 way after it renamed to the new base, their
/// references and chats with them -- a name already taken left, said.
/// Refused for a model being written (a `.partial` inside it).
[[nodiscard]] RenameOutcome rename_stored_model(const std::filesystem::path& config_path,
                                                const models::StoreRoots& roots,
                                                std::string_view old_model,
                                                std::string_view new_model);

}  // namespace apogee::commands
