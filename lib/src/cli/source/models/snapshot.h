#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "models/source_hf.h"

/// A full-weight SafeTensors **snapshot**: a directory holding a model's
/// `config.json`, its tokenizer files and every `*.safetensors` shard --
/// what `apogee models pull <owner>/<repo> --safetensors` lands, and the
/// only thing the training track can fine-tune. Trainable, not runnable:
/// Apogee infers from GGUF, so a snapshot is a backend's *input* at promote
/// time, never a backend itself.
///
/// The record beside it (`apogee-snapshot.json`) is the tree's sidecar:
/// the ref, the revision, the source and one entry per file with the size
/// and the sha256 Hugging Face published. A directory without one is a
/// snapshot the user placed by hand -- listed all the same.
namespace apogee::models {

struct SnapshotFile {
    std::string path;
    std::int64_t size = 0;
    std::string sha256;
};

struct Snapshot {
    std::string ref;
    std::string revision;
    std::string source;
    std::string pulled_at;
    std::vector<SnapshotFile> files;
};

/// `<dir>/apogee-snapshot.json`.
[[nodiscard]] std::filesystem::path snapshot_record_path(const std::filesystem::path& dir);

[[nodiscard]] bool write_snapshot(const std::filesystem::path& dir, const Snapshot& snapshot);
[[nodiscard]] std::optional<Snapshot> load_snapshot(const std::filesystem::path& dir);

/// Whether `dir` looks like a snapshot: a `config.json` and at least one
/// `*.safetensors` shard.
[[nodiscard]] bool is_snapshot_dir(const std::filesystem::path& dir);

/// The architecture `config.json` declares: `architectures[0]` with a
/// trailing `ForCausalLM` (or `ForConditionalGeneration`) dropped, else
/// `model_type`, else empty.
[[nodiscard]] std::string snapshot_architecture(const std::filesystem::path& dir);

/// Every snapshot directory directly under `root`, sorted by name.
[[nodiscard]] std::vector<std::filesystem::path> list_snapshots(const std::filesystem::path& root);

/// Whether `config.json` is an Apogee download record instead of the model's
/// configuration. Until 2026-09-23 `models pull --safetensors` wrote each
/// file's record beside it under a name derived by swapping the extension for
/// `.json`, so every JSON file in the snapshot -- `config.json`,
/// `tokenizer.json` -- was replaced by its own record. Such a snapshot can be
/// neither converted nor trained, and the only repair is a fresh pull; this is
/// how a surface says so instead of failing somewhere downstream.
[[nodiscard]] bool config_is_download_record(const std::filesystem::path& dir);

/// The refusal for such a snapshot, naming the repair -- or empty when
/// `config.json` is the model's. One wording for every surface that reads a
/// snapshot (`models convert`, `train run`).
[[nodiscard]] std::string damaged_snapshot_error(const std::filesystem::path& dir);

/// Whether `file` is an Apogee download record -- a sidecar -- whatever it is
/// named. What the old per-file records left under repository file names.
[[nodiscard]] bool is_download_record(const std::filesystem::path& file);

/// What an older `models pull --safetensors` left wrong in a snapshot, judged
/// against its own `apogee-snapshot.json` -- the one record that was always
/// right, since it lists every file's real size and sha256.
struct SnapshotDamage {
    /// Download records under names the repository does not have
    /// (`model-00001-of-00018.json`, `merges.json`): removed.
    std::vector<std::filesystem::path> stray_records;
    /// Files the record lists that are missing, the wrong size, not what
    /// their digest says, or replaced by a download record: fetched again.
    std::vector<SnapshotFile> refetch;
    /// No record to judge against. Set exactly when nothing could be judged.
    std::string error;

    [[nodiscard]] bool empty() const noexcept {
        return stray_records.empty() && refetch.empty();
    }
};

[[nodiscard]] SnapshotDamage find_snapshot_damage(const std::filesystem::path& dir);

/// Fetches one of the snapshot's files from where it came, to `to`. Error, or
/// empty. A closure, so the repair is tested with no network.
using SnapshotFetchFn =
    std::function<std::string(const SnapshotFile& file, const std::filesystem::path& to)>;

struct SnapshotRepair {
    std::vector<std::string> fetched;
    std::vector<std::string> removed;
    /// The first failure; what was repaired before it stays repaired.
    std::string error;
};

/// Fixes what `find_snapshot_damage` found: each file fetched beside itself,
/// checked against the recorded size and sha256, then renamed over the damaged
/// one; stray records removed. A file that fails its check never replaces
/// anything.
[[nodiscard]] SnapshotRepair repair_snapshot(const std::filesystem::path& dir,
                                             const SnapshotDamage& damage,
                                             const SnapshotFetchFn& fetch);

/// Tensor elements across the snapshot's shards, read from each shard's
/// header alone -- an 8-byte length and a JSON table of shapes -- so nothing
/// is loaded. nullopt when a header cannot be read.
[[nodiscard]] std::optional<std::int64_t> snapshot_elements(const std::filesystem::path& dir);

}  // namespace apogee::models
