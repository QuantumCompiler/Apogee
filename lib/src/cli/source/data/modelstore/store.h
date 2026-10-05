#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"

/// Where model files live: one directory per model, one directory per format
/// inside it, and one directory per set of weights inside that.
///
/// ```
/// <models>/<model>/gguf/<id>/<file>.gguf          (+ <file>.json, + <file>-mmproj.gguf)
/// <safetensors root>/<model>/safetensors/<id>/     (config.json, shards, apogee-snapshot.json)
/// <models>/<model>/mlx/<id>/                        (config.json, tokenizer, shards,
/// apogee-snapshot.json)
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
/// An MLX model (27b): a directory the `mlx` backend runs -- `config.json`,
/// the tokenizer, SafeTensors shards in `mlx-lm`'s format, usually
/// quantized -- pulled from an `mlx-community` build or made by `models
/// convert --mlx`. Runnable like a GGUF, so under the models directory like
/// one, never under `paths.hf_dir`; its id is the SafeTensors rule's, a
/// digest over every shard's sha256.
inline constexpr std::string_view kMlxFormat = "mlx";
inline constexpr std::size_t kWeightIdLength = 12;

/// Where each format's model directories are rooted. SafeTensors sets can be
/// tens of gigabytes, so `paths.hf_dir` may put them on another disk; GGUFs
/// and MLX models -- what a backend runs -- always live under the models
/// directory.
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

/// One set of weights by the name every verb takes: `<model>/<format>/<id>`.
struct WeightsHandle {
    std::string model;
    std::string format;
    std::string id;
};

/// `<model>/<format>/<id>`.
[[nodiscard]] std::string weights_handle(std::string_view model, std::string_view format,
                                         std::string_view id);

/// `text` read as a handle, asking nothing of the disk: a model name with no
/// separator in it, a format, an id. Nullopt for anything else -- a path, a
/// bare model, a ref. What a record's `ref` names when a conversion or a
/// quantization made the file from weights in the store (`models/lineage.h`).
[[nodiscard]] std::optional<WeightsHandle> parse_weights_handle(std::string_view text);

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
/// Its owner marker is written, though (`staging_owner_path`): a staging
/// directory is claimed by this process from the moment it is named.
[[nodiscard]] std::filesystem::path incoming_path(const StoreRoots& roots, std::string_view format,
                                                  std::string_view model);

/// A fresh staging directory beside the ids: `<model>/<format>/.incoming-<random>`.
/// Created. Work happens here and is renamed into place by `commit_weights`,
/// so a half-finished set never sits under an id.
[[nodiscard]] std::filesystem::path make_incoming_dir(const StoreRoots& roots,
                                                      std::string_view format,
                                                      std::string_view model);

/// `<staging>.owner`, beside it: the id of the process filling it. Beside and
/// not inside, so the rename that commits the directory cannot carry it into
/// an id. Removed when the staging directory is committed or removed.
[[nodiscard]] std::filesystem::path staging_owner_path(const std::filesystem::path& staging);

/// A staging directory an interrupted pull, convert or quantize left behind.
struct AbandonedStaging {
    std::filesystem::path dir;
    std::uintmax_t bytes = 0;
};

/// Every abandoned staging directory under both roots: its owner marker names
/// a process that is no longer running -- or, made before markers existed,
/// nothing in it has changed for an hour. One a live process owns is never
/// listed. A killed `models convert` left two 52 GB copies of a model this way
/// (2026-09-23): Ctrl-C during the hash, which nothing cleaned up after.
[[nodiscard]] std::vector<AbandonedStaging> find_abandoned_staging(const StoreRoots& roots);

struct Commit {
    std::filesystem::path dir;
    /// The id was already there -- identical weights -- and the staged copy
    /// was dropped in its favour.
    bool existed = false;
    /// The id was already there without a projector, and the staged one was
    /// moved in beside it.
    bool projector_added = false;
    std::string error;
};

/// `staged` becomes `<model>/<format>/<id>`. When that id already exists the
/// existing directory wins: same id, same weights -- except that a projector
/// it lacks is taken from `staged` (with its record), since the id names the
/// model file alone and a projector made or found later belongs beside it.
[[nodiscard]] Commit commit_weights(const std::filesystem::path& staged,
                                    const std::filesystem::path& final_dir);

/// Renames `from` to `to`, falling back to copy-then-remove when they are on
/// different filesystems (a `paths.hf_dir` on another disk). Error, or empty.
[[nodiscard]] std::string move_path(const std::filesystem::path& from,
                                    const std::filesystem::path& to);

/// Where a model file's vision or audio projector sits: `<stem>-mmproj.gguf`
/// beside it, the name llama.cpp's own releases and Ollama's layers use.
[[nodiscard]] std::filesystem::path projector_path_for(const std::filesystem::path& model_file);

/// Completes `record` for the file it describes -- name, size, sha256, and
/// when -- and writes it beside the file. Error, or empty -- `kStopped` when
/// `progress` said stop.
[[nodiscard]] std::string write_record(const std::filesystem::path& file, Sidecar record,
                                       const HashProgress& progress = {});

/// What `write_record` and `commit_gguf` answer when their hash was stopped.
inline constexpr std::string_view kStopped = "stopped";

/// Puts `projector` and its record into `dir` under the same name: a hard
/// link when both are on one filesystem, so a quantized copy and the model it
/// came from share one projector's bytes, else a copy. Never replaces a file.
/// Error, or empty.
[[nodiscard]] std::string share_projector(const std::filesystem::path& projector,
                                          const std::filesystem::path& dir);

/// A GGUF just written into `staging` (from `make_incoming_dir`), committed:
/// hashed, its record completed and written beside it (`record` carries the
/// provenance -- ref, source, transform), and the directory renamed to the id.
/// A projector already recorded in `staging` goes with it.
struct StoredFile {
    /// The stored file: `file`'s new home, or the GGUF already stored under
    /// the same id when identical weights were there first.
    std::filesystem::path file;
    /// The projector beside it once committed, whichever run put it there.
    std::filesystem::path projector;
    bool existed = false;
    /// `existed`, and this commit supplied the projector the directory lacked.
    bool projector_added = false;
    std::string error;
};

/// `progress` hears the hash of `file` -- the one slow step, tens of
/// gigabytes -- and can stop it, leaving `staging` for the caller to remove.
[[nodiscard]] StoredFile commit_gguf(const StoreRoots& roots, std::string_view model,
                                     const std::filesystem::path& staging,
                                     const std::filesystem::path& file, Sidecar record,
                                     const HashProgress& progress = {});

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

/// The stored GGUF whose model file is `file` -- a backend's `model_path`,
/// expanded -- among `ggufs`, compared as normal paths; nullopt when none is
/// (M7: what `models delete <backend>` deletes).
[[nodiscard]] std::optional<StoredGguf> stored_gguf_at(const std::vector<StoredGguf>& ggufs,
                                                       const std::filesystem::path& file);

/// The stored GGUFs whose model file is `<stem>.gguf` -- the name `models
/// list` notes under the row, and what `config add-backend <stem>` fills
/// itself from (M7). More than one when the same name was stored twice.
[[nodiscard]] std::vector<StoredGguf> stored_ggufs_named(const std::vector<StoredGguf>& ggufs,
                                                         std::string_view stem);

/// The backend type a stored format runs as, in the config's spelling --
/// `llamacpp` for `gguf`, `mlx` for `mlx` (27b) -- or empty for one no
/// backend type runs directly. One row per format, beside the formats
/// themselves (M7).
[[nodiscard]] std::string_view backend_type_for_format(std::string_view format) noexcept;

/// One stored MLX model (27b): a whole directory, the shards never apart.
struct StoredMlx {
    std::string model;
    std::string id;
    std::filesystem::path dir;
    /// The record's `pulled_at`, when it has a record. For display.
    std::string arrived;
};

/// Every stored MLX model, by model then id -- every directory under a
/// model's `mlx/`, whole or not, so one that cannot load is listed and said
/// rather than hidden. `model` narrows to one.
[[nodiscard]] std::vector<StoredMlx> list_store_mlx(const StoreRoots& roots,
                                                    std::string_view model = {});

/// The stored MLX model whose directory is `dir` -- an `mlx` backend's
/// `model_path`, expanded -- compared as normal paths; nullopt when none is.
[[nodiscard]] std::optional<StoredMlx> stored_mlx_at(const std::vector<StoredMlx>& stored,
                                                     const std::filesystem::path& dir);

/// The name a backend over `stored` would carry, as `config add-backend`
/// fills it (M7's rule, 27b's row): the repository part of its model's name,
/// with its precision appended unless the name already ends with it --
/// `Llama-3.2-1B-Instruct-4bit` for an `mlx-community` build of that name,
/// `Llama-3.2-1B-Instruct-8bit` for an 8-bit conversion of Meta's.
[[nodiscard]] std::string stored_mlx_name(const StoredMlx& stored);

/// The stored MLX models whose name (`stored_mlx_name`) is `name`.
[[nodiscard]] std::vector<StoredMlx> stored_mlx_named(const std::vector<StoredMlx>& stored,
                                                      std::string_view name);

struct StoredDirectory {
    std::filesystem::path dir;
    /// Identical weights were already stored: the staged copy was dropped.
    bool existed = false;
    std::string error;
};

/// An MLX directory just written into `staging` (from `incoming_path`),
/// committed (27b): every file in `record` with its size and sha256 -- one
/// it already lists with a digest of that size (what a download verified) is
/// not hashed again -- the record written into the directory as
/// `apogee-snapshot.json`, and the directory renamed to its id, a digest over
/// every shard's sha256 (`snapshot_weight_id`), so identical weights find the
/// directory they already occupy. `progress` hears the bytes hashed across
/// every file and can stop it (`kStopped`), leaving `staging` for the caller
/// to remove.
[[nodiscard]] StoredDirectory commit_mlx(const StoreRoots& roots, std::string_view model,
                                         const std::filesystem::path& staging, Snapshot record,
                                         const HashProgress& progress = {});

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
