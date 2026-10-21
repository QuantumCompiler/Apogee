#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    /// How the files were made from `ref`, when a verb made them rather than
    /// a download -- `mlx_lm.convert 4bit` for an MLX model `models convert
    /// --mlx` wrote (27b). Empty for a pull, and then not written.
    std::string transform;
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

/// A **draft head** (2026-10-21): weights speculative decoding runs beside a
/// base model and never alone -- Gemma 4's assistants, a few layers reading
/// the base's hidden states through a projection, which `config.json` says
/// by declaring `backbone_hidden_size`, the base's width. Nothing Apogee runs
/// can chat with one: mlx-lm has no network for it, and llama.cpp loads it
/// only as a draft beside its base, which Apogee does not run (26k). So no
/// surface offers one as a model to convert or register; each says what it is.
struct DraftHead {
    /// `architectures[0]` as `config.json` names it, else `model_type`.
    std::string architecture;
    /// The width of the base model it reads; 0 when not declared.
    std::int64_t backbone_hidden_size = 0;
    /// `model_type` up to its first `_` -- the family a base shares.
    std::string family;
};

/// The draft head `dir`'s `config.json` describes: one declaring
/// `backbone_hidden_size`, or naming an architecture llama.cpp's converter
/// takes as a draft head at the pinned revision (`Gemma4AssistantForCausalLM`,
/// `Gemma4UnifiedAssistantForCausalLM`). nullopt for anything else, or for a
/// directory with no readable `config.json`.
[[nodiscard]] std::optional<DraftHead> snapshot_draft_head(const std::filesystem::path& dir);

/// A model's width and family as its `config.json` says them: `hidden_size`,
/// or `text_config`'s for a multimodal release, and `model_type` up to its
/// first `_`. 0 and empty where not said.
struct ConfigWidth {
    std::int64_t hidden_size = 0;
    std::string family;
};

[[nodiscard]] ConfigWidth config_width(const std::filesystem::path& dir);

/// What a draft head is, in the one wording every surface uses: "a draft head
/// (<architecture>): speculative decoding runs it beside its base model
/// (<base>), reading that model's hidden states, and never alone". The base
/// is the stored models `bases` names, else the width it reads, else unsaid.
[[nodiscard]] std::string describe_draft_head(const DraftHead& head,
                                              const std::vector<std::string>& bases);

/// The refusal a conversion of one says, `subject` its name or path: what it
/// is, that nothing converted from it could answer a chat, and the way out.
[[nodiscard]] std::string draft_head_refusal(std::string_view subject, const DraftHead& head,
                                             const std::vector<std::string>& bases);

/// The short form a listing's note says: "draft head -- runs beside <base>,
/// never alone".
[[nodiscard]] std::string draft_head_note(const DraftHead& head,
                                          const std::vector<std::string>& bases);

/// A **diffusion model's** weights (M20, 2026-10-21): a component of a
/// diffusers pipeline -- a video model's transformer, `WanModel` -- whose
/// `config.json` names the class diffusers builds it as (`_class_name`, beside
/// the `_diffusers_version` that wrote it) and no `architectures`. Neither
/// converter Apogee runs takes one: llama.cpp's finds a model by its
/// architectures, and mlx-lm's builds a language model by its `model_type`.
/// So no surface offers one as a model to convert; each says what it is.
struct DiffusionModel {
    /// `_class_name`: the class diffusers builds it as.
    std::string class_name;
};

/// The diffusion model `dir`'s `config.json` describes: one naming a
/// `_class_name` with a `_diffusers_version` and no `architectures`. nullopt
/// for anything else, or for a directory with no readable `config.json`.
[[nodiscard]] std::optional<DiffusionModel> snapshot_diffusion_model(
    const std::filesystem::path& dir);

/// What a diffusion model is, in the one wording every surface uses: "a
/// diffusion model's weights (<class>, a diffusers class): neither converter
/// takes one -- llama.cpp's finds a model by its architectures, and mlx-lm's
/// builds a language model".
[[nodiscard]] std::string describe_diffusion_model(const DiffusionModel& model);

/// The refusal a conversion of one says, `subject` its name or path.
[[nodiscard]] std::string diffusion_model_refusal(std::string_view subject,
                                                  const DiffusionModel& model);

/// The formats a SafeTensors set converts to, decided once from its own files
/// (M20): `gguf` and `mlx` for a language model's weights, none for a draft
/// head or a diffusion model's. What both conversion ladders refuse, what
/// `models info` offers, and its `data.converts_to`, all read this.
[[nodiscard]] std::vector<std::string> snapshot_conversions(const std::filesystem::path& dir);

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
/// is loaded. Only the tensors `include` accepts by name, when given. nullopt
/// when a header cannot be read.
[[nodiscard]] std::optional<std::int64_t> snapshot_elements(
    const std::filesystem::path& dir, const std::function<bool(std::string_view)>& include = {});

}  // namespace apogee::models
