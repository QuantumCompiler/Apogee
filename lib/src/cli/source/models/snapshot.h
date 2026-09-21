#pragma once

#include <cstdint>
#include <filesystem>
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

}  // namespace apogee::models
