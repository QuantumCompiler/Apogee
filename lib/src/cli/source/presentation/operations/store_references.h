#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "modelstore/store.h"

/// **What a config points into the model store with, declared once** (55d).
///
/// A stored set is named from the config in five places: a backend entry's
/// weights, `model_path`, and its projector, `mmproj_path`; the drafter an
/// mlx entry runs beside them, `draft:` (53i); each LoRA adapter it applies,
/// `adapters:` (53a); and the one `models:` pointer that names a stored set
/// rather than a backend, `models.default_video` (49c) -- the role pointers
/// name backends, never a folder. This walk lists them, and it is the only
/// list: `models delete` names and offers every one pointing into what goes,
/// every rename repoints every one pointing into the folder it moves, and a
/// registration asks it whether an entry already runs from a set. A verb that
/// kept its own list would miss the next kind of reference the day it is
/// added, the way delete once missed `draft:` and the video pointer.
///
/// M25's `kStorePaths` -- `model_path`, `mmproj_path`, `draft` -- is the
/// first three rows of this declaration, absorbed whole.
namespace apogee::commands {

/// One reference a config holds that can point into the model store.
struct StoreReference {
    /// The backend entry that holds it; empty for the `models:` pointer.
    std::string backend{};
    /// Its field: `model_path`, `mmproj_path`, `draft` or `adapters` on an
    /// entry; `default_video` under `models:`.
    std::string field{};
    /// What it names, as written: a path -- `~` and `${VAR}` unexpanded --
    /// or, for `default_video`, a store name, handle, repository or path.
    std::string value{};
    /// An adapter's place in its entry's list.
    std::size_t index = 0;
    /// The entry runs from the folder it names -- its weights, which a
    /// coupled rename follows and without which the entry stops working --
    /// rather than loading something beside them.
    bool weights = false;

    /// Where it is, as the config spells it: `backends.<name>.<field>`, an
    /// adapter's `backends.<name>.adapters[<i>]`, `models.default_video`.
    [[nodiscard]] std::string where() const;
};

/// Every reference `config` holds that can point into the store, in one
/// order: each backend by name -- its `model_path`, `mmproj_path`, `draft`,
/// then its adapters in their list's order -- then `models.default_video`.
/// An empty field is no reference.
[[nodiscard]] std::vector<StoreReference> store_references(const harness::Config& config);

/// The references `backend` (an entry named `name`) holds, in the same order.
[[nodiscard]] std::vector<StoreReference> backend_references(std::string_view name,
                                                             const harness::BackendConfig& backend);

/// The stored model whose directory `file` sits in, under either of `roots`,
/// or empty: where a backend's path points.
[[nodiscard]] std::string stored_model_at(const models::StoreRoots& roots,
                                          const std::filesystem::path& file);

/// The stored model `reference` points into under `roots`, or empty: for a
/// path, the folder it sits in; for `default_video`, the model its name,
/// handle, repository, id or path resolves to in the store.
[[nodiscard]] std::string referenced_model(const models::StoreRoots& roots,
                                           const StoreReference& reference);

/// Whether `reference` points inside the stored weights directory `dir` --
/// one set, `<model>/<format>/<id>` -- so that removing `dir` leaves it
/// naming nothing. A path does when it lies in `dir`; `default_video` when
/// the set it names is `dir` or lies in it, or when it names `dir`'s model
/// (or format) and `dir` is a video model it can run -- a pipeline, or an MLX
/// directory of kind `video`.
[[nodiscard]] bool points_inside(const models::StoreRoots& roots, const StoreReference& reference,
                                 const std::filesystem::path& dir);

/// `reference`'s value once stored model `model` is renamed `renamed`, or
/// nullopt when it does not point into `model`: a path with its one store
/// segment swapped, so a `~` or a `${VAR}` the user wrote stays theirs; the
/// video pointer as it was spelled -- the model's own name, a handle under
/// it, its repository, or a path into it. A bare id needs nothing.
[[nodiscard]] std::optional<std::string> repointed_value(const models::StoreRoots& roots,
                                                         const StoreReference& reference,
                                                         std::string_view model,
                                                         std::string_view renamed);

/// `text` with each of `references` -- `config`'s -- that points into stored
/// model `model` repointed into `renamed`, each through its own transform in
/// the one editor (`contracts/config_edit.h`): `model_path` and `mmproj_path`
/// by their setters, `draft` by `set_backend_field`, an entry's adapters as
/// its whole list (`set_backend_adapters`, every pin kept), the video pointer
/// by `set_default_video`. A pure transform, for one `edit_config_file`.
[[nodiscard]] std::string repoint_references(std::string text, const harness::Config& config,
                                             const models::StoreRoots& roots,
                                             const std::vector<StoreReference>& references,
                                             std::string_view model, std::string_view renamed);

/// `text` with each of `references` -- `config`'s, none of them weights --
/// let go, as a delete settles what it strands (M14, 55d): a `draft:`
/// cleared, byte for byte `config set-backend <name> --draft ""`; an adapter
/// dropped from its entry's list, the rest kept with their pins (the list
/// removed when it empties); `models.default_video` cleared, byte for byte
/// `config set-default-video off`. A weights reference is never cleared: its
/// entry is removed whole, as `config delete-backend` removes one.
[[nodiscard]] std::string clear_references(std::string text, const harness::Config& config,
                                           const std::vector<StoreReference>& references);

/// The one command that lets `reference` -- one of `stranded`, what a delete
/// leaves naming nothing -- go by hand on that state, the door a kept
/// reference is said with: `config set-backend <name> --draft ""`, `config
/// set-backend <name> --adapters <the adapters it keeps>` (every stranded one
/// left out, since that flag checks and pins each it is given; `""` when
/// none is kept), `config set-default-video off`. Empty for a weights
/// reference, whose doors are its entry's (`backend_doors`).
[[nodiscard]] std::string clear_door(const harness::Config& config,
                                     const std::vector<StoreReference>& stranded,
                                     const StoreReference& reference);

}  // namespace apogee::commands
