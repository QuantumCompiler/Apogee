#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "models/sidecar.h"
#include "models/snapshot.h"

/// Where model files live: one directory per model, one directory per format
/// inside it, and one directory per set of weights inside that.
///
/// ```
/// <models>/<model>/gguf/<id>/<file>.gguf          (+ <file>.json, + <file>-mmproj.gguf)
/// <safetensors root>/<model>/safetensors/<id>/     (config.json, shards, apogee-snapshot.json)
/// ```
///
/// **Declared once, here.** Every command that writes, finds, lists or removes
/// a model file asks this file where -- the same rule the install layout
/// follows (`harness/layout.h`), for the same reason: the old flat layout was
/// restated in pull, list, delete, check and training, and each copy was
/// slightly different.
///
/// **An id is the weights' own hash.** The first 12 hex characters of the
/// file's sha256 for a GGUF, of a digest over every shard's sha256 for a
/// SafeTensors set. So pulling identical weights again finds the directory
/// they already occupy instead of a second copy, a different revision or a
/// new fine-tune gets its own directory beside the old one, and nothing a
/// later download or training run produces can overwrite what an earlier one
/// left. Where no digest can be had, a random id of the same shape.
namespace apogee::models {

inline constexpr std::string_view kGgufFormat = "gguf";
inline constexpr std::string_view kSafetensorsFormat = "safetensors";
inline constexpr std::size_t kWeightIdLength = 12;

/// Where each format's model directories are rooted. SafeTensors sets can be
/// tens of gigabytes, so `paths.hf_dir` may put them on another disk; GGUFs
/// always live under the models directory.
struct StoreRoots {
    std::filesystem::path models;
    std::filesystem::path safetensors;

    /// Both roots at `models`.
    [[nodiscard]] static StoreRoots at(const std::filesystem::path& models);

    [[nodiscard]] const std::filesystem::path& root_for(std::string_view format) const noexcept;
};

// --- names and ids -------------------------------------------------------------------

/// A model directory's name from free text: the characters a filesystem
/// dislikes (`/ : @ \` and spaces) become `-`, leading dots are dropped. For a
/// Hugging Face repository use `repo_directory_name` (`owner--repo`) instead.
[[nodiscard]] std::string safe_model_name(std::string_view text);

[[nodiscard]] bool is_weight_id(std::string_view text) noexcept;

/// The id of a GGUF from its sha256 (hex, any case, optionally `sha256:`
/// prefixed). Empty when `sha256` is not one.
[[nodiscard]] std::string weight_id_from_digest(std::string_view sha256);

/// The id of a SafeTensors set: a sha256 over `<path>\t<sha256>\n` for every
/// `*.safetensors` entry, sorted by path. Empty when there is no shard or a
/// shard has no digest -- the caller then hashes what landed, or falls back.
[[nodiscard]] std::string snapshot_weight_id(const std::vector<SnapshotFile>& files);

/// A random id of the same shape, for weights nothing can be hashed for.
[[nodiscard]] std::string random_weight_id();

// --- paths ---------------------------------------------------------------------------

[[nodiscard]] std::filesystem::path model_dir(const StoreRoots& roots, std::string_view format,
                                              std::string_view model);
[[nodiscard]] std::filesystem::path weights_dir(const StoreRoots& roots, std::string_view format,
                                                std::string_view model, std::string_view id);

/// An existing `<model>/<format>/<id>` in whichever root holds it -- SafeTensors
/// sets pulled before `paths.hf_dir` was set still live under the models
/// directory -- else where one would be written.
[[nodiscard]] std::filesystem::path find_weights_dir(const StoreRoots& roots,
                                                     std::string_view format,
                                                     std::string_view model, std::string_view id);

/// A staging path beside the ids, `<model>/<format>/.incoming-<random>`, NOT
/// created -- for a writer that insists on creating its destination itself.
[[nodiscard]] std::filesystem::path incoming_path(const StoreRoots& roots, std::string_view format,
                                                  std::string_view model);

/// A fresh staging directory beside the ids: `<model>/<format>/.incoming-<random>`.
/// Created. Work happens here and is renamed into place by `commit_weights`,
/// so a half-finished set never sits under an id.
[[nodiscard]] std::filesystem::path make_incoming_dir(const StoreRoots& roots,
                                                      std::string_view format,
                                                      std::string_view model);

struct Commit {
    std::filesystem::path dir;
    /// The id was already there -- identical weights -- and the staged copy
    /// was dropped in its favour.
    bool existed = false;
    std::string error;
};

/// `staged` becomes `<model>/<format>/<id>`. When that id already exists the
/// existing directory wins: same id, same weights.
[[nodiscard]] Commit commit_weights(const std::filesystem::path& staged,
                                    const std::filesystem::path& final_dir);

/// Renames `from` to `to`, falling back to copy-then-remove when they are on
/// different filesystems (a `paths.hf_dir` on another disk). Error, or empty.
[[nodiscard]] std::string move_path(const std::filesystem::path& from,
                                    const std::filesystem::path& to);

/// A GGUF just written into `staging` (from `make_incoming_dir`), committed:
/// hashed, its record completed and written beside it (`record` carries the
/// provenance -- ref, source, transform), and the directory renamed to the id.
struct StoredFile {
    /// The stored file: `file`'s new home, or the GGUF already stored under
    /// the same id when identical weights were there first.
    std::filesystem::path file;
    bool existed = false;
    std::string error;
};

[[nodiscard]] StoredFile commit_gguf(const StoreRoots& roots, std::string_view model,
                                     const std::filesystem::path& staging,
                                     const std::filesystem::path& file, Sidecar record);

// --- what is stored ------------------------------------------------------------------

struct StoredGguf {
    std::string model;
    std::string id;
    std::filesystem::path dir;
    /// The model file: the directory's one `*.gguf` that is not a projector.
    std::filesystem::path file;
    /// Its vision projector, when one sits beside it.
    std::filesystem::path projector;
};

struct StoredSnapshot {
    std::string model;
    std::string id;
    std::filesystem::path dir;
    /// The record's `pulled_at`, when it has a record. For display.
    std::string arrived;
    /// When it landed: its record's time, else the directory's. What "newest"
    /// compares -- file times are compared, never converted, since that
    /// conversion differs across the five platforms' standard libraries.
    std::filesystem::file_time_type landed{};
};

/// Every stored GGUF, by model then id. `model` narrows to one.
[[nodiscard]] std::vector<StoredGguf> list_store_ggufs(const StoreRoots& roots,
                                                       std::string_view model = {});

/// Every stored SafeTensors set, by model then id, from both roots (new sets
/// go under `paths.hf_dir`; ones pulled before it was set stay where they
/// are). `model` narrows to one.
[[nodiscard]] std::vector<StoredSnapshot> list_store_snapshots(const StoreRoots& roots,
                                                               std::string_view model = {});

/// The model's most recent SafeTensors set -- what `convert` and `train`
/// read when no id is named.
[[nodiscard]] std::optional<StoredSnapshot> newest_snapshot(const StoreRoots& roots,
                                                            std::string_view model);

/// Every model directory name, from both roots, sorted and unique.
[[nodiscard]] std::vector<std::string> list_store_models(const StoreRoots& roots);

/// What a user named: a whole model, one format of it, or one set of weights.
struct StoreTarget {
    std::string model;
    /// Empty for a whole model.
    std::string format;
    /// Empty unless one set of weights is named.
    std::string id;
    /// The directory (or, for a path outside the store, the path itself).
    std::filesystem::path path;
    /// A path outside both roots: the caller decides whether to take it.
    bool outside = false;
    /// Why nothing matched. Set exactly when nothing did.
    std::string error{};
};

/// `given` as a model (`owner/repo`, `owner--repo`, a directory name), as
/// `<model>/<format>/<id>`, as a bare id, or as a path -- inside the store it
/// is read back into its parts; outside, it is returned as `outside`.
[[nodiscard]] StoreTarget resolve_store_target(const StoreRoots& roots, std::string_view given);

/// Removes one stored set of weights -- its whole `<id>` directory -- and
/// then the format and model directories if that emptied them. Error, or empty.
[[nodiscard]] std::string remove_weights(const std::filesystem::path& weights_dir);

// --- the layout before this one ------------------------------------------------------

struct LegacyGguf {
    std::filesystem::path file;
    /// `<file>.json`, when present.
    std::filesystem::path sidecar;
    /// `<stem>-mmproj.gguf` and its record, when present.
    std::filesystem::path projector;
    std::filesystem::path projector_sidecar;
    /// The model it belongs to: from its record's ref, else the file's stem.
    std::string model;
};

struct LegacySnapshot {
    std::filesystem::path dir;
    std::string model;
};

/// What the flat layout left: GGUFs directly in the models directory, and
/// SafeTensors directories directly under either root. Nothing reads these
/// in place any more -- `apogee models migrate` moves them in, and `check`
/// says to.
struct LegacyLayout {
    std::vector<LegacyGguf> ggufs;
    std::vector<LegacySnapshot> snapshots;

    [[nodiscard]] bool empty() const noexcept {
        return ggufs.empty() && snapshots.empty();
    }
};

[[nodiscard]] LegacyLayout find_legacy(const StoreRoots& roots);

/// The refusal a command gives when `given` names something only the old
/// layout has, or empty.
[[nodiscard]] std::string legacy_refusal(const StoreRoots& roots, std::string_view given);

}  // namespace apogee::models
