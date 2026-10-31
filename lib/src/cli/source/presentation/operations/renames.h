#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "modelstore/store.h"
#include "operations/store_references.h"

/// Renames that propagate (46a): a backend and a stored model, each the one
/// core every surface calls -- `config rename-backend` and its admin twin,
/// `models rename` -- so a rename means the same thing wherever it is made.
///
/// **One edit, or none.** Every config change of a rename is one transform
/// through the one writer (`contracts/config_edit.h`): the key and every
/// reference to it land together, comments kept, re-parsed before the file
/// is replaced. A store directory moved for it is moved back when the edit
/// is refused. A drafter's record naming a moved directory as its base is
/// rewritten once the edit has landed (M25), and said when it cannot be.
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

/// The quantization level a backend's name ends with, in M3's spelling --
/// `F16`, `BF16`, `F32`, `Q4KM`, `Q80`, `IQ4XS` -- or an MLX precision as the
/// MLX chain names its backends (M16; `4bit`, `8bit`, `bf16`, read from
/// `training::find_mlx_precision`'s table, 55d) -- or nullopt: what makes
/// `<name>-<level>` one quantization of the model `<name>`.
[[nodiscard]] std::optional<std::string> quant_level(std::string_view backend);

/// The models the config runs from the store, by the name their backends
/// share (the user's request): each stored backend `<name>-<level>` gives
/// `<name>` -- `Gemma4-12B-Instruct` for its `-F16` and `-Q4KM` -- and a
/// stored model no such backend runs gives its directory. What `models
/// rename` completes, in the one order.
[[nodiscard]] std::vector<std::string> model_names(const harness::Config& config,
                                                   const models::StoreRoots& roots);

/// `config rename-backend <old> <new>`: the key and every in-config
/// reference renamed in one edit; the saved chats naming it swept; and the
/// store directory moved with it when the coupling holds -- the backend its
/// sole referrer, named the M3 way, the new name too -- else left, said with
/// why. A moved directory takes every path the entry holds into it with it --
/// every reference the one walk lists (`store_references`, 55d): its
/// `draft:` and adapters too -- and `models.default_video` where it names
/// it; and each stored drafter whose record names it as a base names it as it
/// is now called (M25).
[[nodiscard]] RenameOutcome rename_backend_everywhere(const std::filesystem::path& config_path,
                                                      const models::StoreRoots& roots,
                                                      std::string_view old_name,
                                                      std::string_view new_name);

/// `models rename <old> <new>`. `old` is first a model by the name its
/// backends share (the user's request): every stored backend `<old>-<level>`
/// -- a GGUF level or, since 55d, an MLX precision -- renamed `<new>-<level>`,
/// their references and chats with them, and the directory they run from
/// moved to `<new>` when they alone run it, `models.default_video` following
/// it where it names it -- else left, said with why. Otherwise `old` is a stored model's directory:
/// moved, records riding inside; every reference into it repointed -- each the one walk lists
/// (`store_references`, 55d): a backend's `model_path`, `mmproj_path`, `draft:` (M25) and adapters,
/// and `models.default_video`; the backends named the M3 way after the model they run from it
/// renamed to the new base. A name already taken is left, said. With no config file at all, the
/// directory moves and nothing else is read (55d). Either way, a moved directory is renamed in the
/// record of every stored drafter naming it as a base, and in no other (M25): a pairing that held
/// still holds, and none is made that did not. Refused for a model being written (a `.partial`
/// inside it).
[[nodiscard]] RenameOutcome rename_stored_model(const std::filesystem::path& config_path,
                                                const models::StoreRoots& roots,
                                                std::string_view old_model,
                                                std::string_view new_model);

}  // namespace apogee::commands
