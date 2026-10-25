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
    /// What an MLX directory is (53e), written by the conversion that made it
    /// beside its `transform` -- `chat` for mlx-lm's -- and read by
    /// `read_mlx_kind` (`mlx_info.h`). Empty for a pull and for every set made
    /// before it, and then not written: the files say, else `chat`.
    std::string kind{};
    /// The stored models a drafter runs beside (53i), by the name they are
    /// stored under -- each a model folder under the models root, as
    /// `draft_head_bases` measured them when the drafter was converted. Empty
    /// for everything else, and then not written; empty for a drafter too when
    /// none was stored, which then pairs by family and width alone.
    std::vector<std::string> bases{};
    std::vector<SnapshotFile> files;
};

/// `<dir>/apogee-snapshot.json`.
[[nodiscard]] std::filesystem::path snapshot_record_path(const std::filesystem::path& dir);

[[nodiscard]] bool write_snapshot(const std::filesystem::path& dir, const Snapshot& snapshot);
[[nodiscard]] std::optional<Snapshot> load_snapshot(const std::filesystem::path& dir);

/// What `rename_snapshot_base` did with a record.
enum class BaseRename : std::uint8_t {
    /// No record, or one naming no such base: nothing written.
    Unnamed,
    /// The base renamed where it stood, the record written again.
    Renamed,
    /// It names the base, and could not be rewritten with only that change:
    /// it holds what `write_snapshot` would not write back byte for byte, or
    /// the write failed. Left as it was.
    Failed,
};

/// A drafter's record once a base it names is stored under a new name (M25):
/// `from` replaced by `to` in its `bases` where it stands -- one `to` kept
/// where the record already names it -- and written back through
/// `write_snapshot`, every other byte of the record as it was. The set keeps
/// its id, a digest over its shards. A rename never adds a base a record did
/// not name, and never empties one into pairing by family and width alone.
[[nodiscard]] BaseRename rename_snapshot_base(const std::filesystem::path& dir,
                                              std::string_view from, std::string_view to);

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
    /// `model_type` as written (`gemma4_assistant`): the drafter type mlx-vlm
    /// builds and drafts it as (53i). Empty when the config names none.
    std::string model_type{};
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
/// is, that nothing converted from it could answer a chat, and the way out --
/// `mlx_door`, the command that makes its MLX drafter, where the caller found
/// that conversion offered (`snapshot_conversions` answering `mlx`, 53i;
/// named since 55d), else its base model's conversion.
[[nodiscard]] std::string draft_head_refusal(std::string_view subject, const DraftHead& head,
                                             const std::vector<std::string>& bases,
                                             std::string_view mlx_door = {});

/// The short form a listing's note says: "draft head -- runs beside <base>,
/// never alone".
[[nodiscard]] std::string draft_head_note(const DraftHead& head,
                                          const std::vector<std::string>& bases);

/// One component **Wan's original layout** keeps beside its transformer (53j):
/// a root PyTorch checkpoint, stored and verified like any file and never
/// loaded by Apogee -- only a runtime of the original layout reads it
/// (mlx-video's converter, 53k).
struct WanComponent {
    /// What it is: `text encoder` (`models_t5_*.pth`), `VAE` (`*_VAE.pth`), or
    /// `checkpoint` for any other root `.pth` the set's record lists.
    std::string role{};
    /// Its file at the set's root: as it lies there, else as the record lists
    /// it, else as Wan's releases name it -- the text encoder always
    /// `models_t5_umt5-xxl-enc-bf16.pth`, the VAE by the latent width the
    /// transformer's `out_dim` declares (48 `Wan2.2_VAE.pth`, 16
    /// `Wan2.1_VAE.pth`, any other `*_VAE.pth`).
    std::string file{};
    bool present = false;
};

/// **Wan's original layout** (53j, 2026-10-21): a Wan video model as Wan
/// publishes it beside its Diffusers pipeline -- the transformer at the root
/// (`config.json` naming Wan's own class, `WanModel`, and its
/// `diffusion_pytorch_model*.safetensors` shards), with the components every
/// runtime needs beside it as root `.pth` files: the T5 text encoder and the
/// VAE. Known by its config's class, never its repository's name.
struct WanLayout {
    /// `_class_name` as written: `WanModel`.
    std::string class_name{};
    /// The text encoder, the VAE, then any other root `.pth` on record or on
    /// disk, by name.
    std::vector<WanComponent> components{};

    /// Every component is there: the set is whole, as a runtime of the
    /// original layout takes it (53k's question).
    [[nodiscard]] bool whole() const noexcept;
    /// The components' files that are not there, in order.
    [[nodiscard]] std::vector<std::string> missing() const;
};

/// Whether `class_name` is a Wan transformer of Wan's own code (53j):
/// `WanModel`, or a later class named after it (`VaceWanModel`,
/// `WanModel_S2V`) -- never diffusers' port (`WanTransformer3DModel`), which
/// a Diffusers pipeline's transformer names.
[[nodiscard]] bool is_wan_transformer_class(std::string_view class_name) noexcept;

/// The Wan transformer class a `config.json`'s text names, read as
/// `snapshot_diffusion_model` reads a diffusers class (a `_class_name` with a
/// `_diffusers_version` and no `architectures`); empty for anything else. What
/// a SafeTensors pull reads a repository's config with before a byte of
/// weights moves (53j).
[[nodiscard]] std::string wan_transformer_class(std::string_view config_text);

/// The set at `dir` read as Wan's original layout (53j): its class, and each
/// component it needs beside its transformer, there or not. nullopt when its
/// `config.json` names no Wan transformer. The one answer to "is this Wan set
/// whole, and what does it lack" -- what the conversion's refusal, `models
/// info` and an MLX conversion of the original layout (53k) all ask.
[[nodiscard]] std::optional<WanLayout> snapshot_wan_layout(const std::filesystem::path& dir);

/// Whether mlx-video's Wan converter is installed in Apogee's Python
/// environment (53k) -- the caller's reading of that environment, handed down
/// as `EmbeddingBuilds` is: this floor reads none.
using VideoConverts = std::function<bool()>;

/// A **diffusion model's** weights (M20, 2026-10-21): a component of a
/// diffusers pipeline -- a video model's transformer, `WanModel` -- whose
/// `config.json` names the class diffusers builds it as (`_class_name`, beside
/// the `_diffusers_version` that wrote it) and no `architectures`. Neither
/// language model's converter takes one: llama.cpp's finds a model by its
/// architectures, and mlx-lm's builds a language model by its `model_type`.
/// So no surface offers one as a model to convert, and each says what it is
/// -- but Wan's original layout, whole, which mlx-video converts to MLX where
/// it is installed (53k).
struct DiffusionModel {
    /// `_class_name`: the class diffusers builds it as.
    std::string class_name;
    /// Wan's original layout, when the class is a Wan transformer (53j): what
    /// the set is, and what it lacks.
    std::optional<WanLayout> wan{};
    /// The repository the set was pulled from, as its record says --
    /// `owner/repo`, with `@revision` when it was not `main` -- which the way
    /// to complete it and the pipeline that runs it are named by (53j). Empty
    /// without a record.
    std::string ref{};
    /// Whether mlx-video's Wan converter is installed (53k), as the caller
    /// read it (`VideoConverts`): nullopt when not asked, and then the wording
    /// names mlx-video's conversion without saying whether it is there.
    std::optional<bool> mlx_video{};
    /// The MLX video models converted from this set that stand in the store
    /// (M24), by handle (`<model>/mlx/<id>`) -- found by their own records,
    /// never by a name (`mlx_video_conversions`, `store.h`). Empty when none
    /// stands, and when the store was not asked: the wording then says the
    /// conversion is to be made.
    std::vector<std::string> mlx_conversions{};

    /// Wan's original layout, whole, with mlx-video installed (53k): what
    /// `models convert --mlx` takes to an MLX video model.
    [[nodiscard]] bool mlx_convertible() const noexcept;
};

/// The diffusion model `dir`'s `config.json` describes: one naming a
/// `_class_name` with a `_diffusers_version` and no `architectures`. nullopt
/// for anything else, or for a directory with no readable `config.json`.
/// Wan's original layout is read with it (`snapshot_wan_layout`, 53j), and,
/// for one, whether `mlx_video` says mlx-video is installed (53k).
[[nodiscard]] std::optional<DiffusionModel> snapshot_diffusion_model(
    const std::filesystem::path& dir, const VideoConverts& mlx_video = {});

/// What a diffusion model is, in the one wording every surface uses: "a
/// diffusion model's weights (<class>, a diffusers class): neither converter
/// takes one -- llama.cpp's finds a model by its architectures, and mlx-lm's
/// builds a language model". For Wan's original layout (53j), what it is,
/// complete or what it lacks and the pull that fetches it; that mlx-video
/// converts it to MLX once it is complete, and whether mlx-video is installed
/// with the setup that installs it (53k); that llama.cpp's converter takes
/// none; and what runs the model on each of `video generate`'s engines (M24)
/// -- its Diffusers pipeline on PyTorch, and its MLX conversion through
/// mlx-video, said made by handle where one stands (`mlx_conversions`):
/// "Wan's original layout (WanModel), missing
/// models_t5_umt5-xxl-enc-bf16.pth and Wan2.2_VAE.pth -- 'apogee models pull
/// <ref> --safetensors' fetches them into this set; once it is complete,
/// mlx-video converts it to an MLX video model (--mlx), and is not installed
/// in Apogee's Python environment -- 'apogee train setup --with mlx-video';
/// llama.cpp's converter takes no diffusion model -- it finds a model by its
/// architectures; what runs this model in 'apogee video generate' is its
/// Diffusers pipeline on PyTorch, pulled whole with 'apogee models pull
/// <ref>-Diffusers', or its MLX conversion on MLX through mlx-video, once
/// made".
[[nodiscard]] std::string describe_diffusion_model(const DiffusionModel& model);

/// The refusal a conversion of one says, `subject` its name or path.
[[nodiscard]] std::string diffusion_model_refusal(std::string_view subject,
                                                  const DiffusionModel& model);

/// The refusal a run of Wan's original layout says (M24): `video generate`
/// handed the set itself, which no engine runs as it lies. `subject` names
/// what was given with its verb -- "'<name>' is", "'<model>' holds" -- and
/// `handle` the set as `models convert` takes it. Then, in the description's
/// words: its Diffusers pipeline on PyTorch, and its MLX conversion through
/// mlx-video -- made, by handle, or `apogee models convert <handle> --mlx`,
/// after the pull of what the set lacks when it is not whole. Empty for a
/// diffusion model that is no Wan layout.
[[nodiscard]] std::string wan_layout_run_refusal(std::string_view subject, std::string_view handle,
                                                 const DiffusionModel& model);

/// An **embedding model** (M21, 2026-10-21): a checkpoint sentence-transformers
/// publishes as a pipeline -- the transformer, then pooling and, for some,
/// dense layers and a normalize step -- which its `modules.json` declares by
/// naming a `Pooling` module. llama.cpp's converter takes one as an embedding
/// model a llamacpp backend embeds with; mlx-lm builds language models, and
/// the mlx backend does not embed, so it converts to GGUF alone.
struct EmbeddingModel {
    /// Its modules in order, each type's last word: `Transformer`, `Pooling`,
    /// `Dense`, `Normalize`.
    std::vector<std::string> modules;
    /// A `Dense` module's weights are there: what llama.cpp's converter
    /// includes under `--sentence-transformers-dense-modules`, and leaves out
    /// without it.
    bool dense = false;
};

/// The embedding model `dir`'s `modules.json` describes: one naming a
/// sentence-transformers `Pooling` module. nullopt for anything else, or for
/// a directory with no readable `modules.json`.
[[nodiscard]] std::optional<EmbeddingModel> snapshot_embedding_model(
    const std::filesystem::path& dir);

/// What an embedding model is, in the one wording every surface uses: "an
/// embedding model (sentence-transformers: Transformer, Pooling, ...): it
/// converts to a GGUF a llamacpp backend embeds with -- mlx-lm's converter
/// builds language models, and the mlx backend does not embed".
[[nodiscard]] std::string describe_embedding_model(const EmbeddingModel& model);

/// The refusal an MLX conversion of one says, `subject` its name or path.
[[nodiscard]] std::string embedding_model_mlx_refusal(std::string_view subject,
                                                      const EmbeddingModel& model);

/// What an embedding model is where the installed mlx-embeddings builds it
/// (53h): "an embedding model (sentence-transformers: ...): it converts to a
/// GGUF a llamacpp backend embeds with, and through mlx-embeddings to an MLX
/// model an mlx backend embeds with".
[[nodiscard]] std::string describe_embedding_model_both(const EmbeddingModel& model);

/// Whether the mlx-embeddings installed in Apogee's Python environment builds
/// a model of `model_type` (53h) -- the caller's reading of that environment,
/// handed down: this floor reads none. Empty, or answering false, decides
/// nothing new.
using EmbeddingBuilds = std::function<bool(std::string_view model_type)>;

/// Whether the mlx-vlm installed there drafts with a model of `model_type`
/// (53i): its speculative path holds a drafter of that type -- the caller's
/// reading, handed down as `EmbeddingBuilds` is. Empty, or answering false,
/// decides nothing new.
using DrafterBuilds = std::function<bool(std::string_view model_type)>;

/// The formats a SafeTensors set converts to, decided once from its own files
/// (M20): `gguf` and `mlx` for a language model's weights, `gguf` alone for an
/// embedding model's (M21), none for a draft head or a diffusion model's.
/// What both conversion ladders refuse, what `models info` offers, and its
/// `data.converts_to`, all read this. A set with towers converts to `mlx`
/// too (53f): which converter builds it is the route's, below. An embedding
/// model gains `mlx` where `mlx_embeddings` says the installed mlx-embeddings
/// builds its `config.json`'s `model_type` (53h); anywhere else -- that type
/// unbuilt, or no mlx-embeddings to ask -- M21's answer stands. A draft head
/// gains `mlx` alone -- an MLX drafter, never a GGUF -- where `mlx_drafters`
/// says the installed mlx-vlm drafts its `model_type` (53i); anywhere else
/// the draft-heads change's answer stands: none. Wan's original layout gains
/// `mlx` alone -- an MLX video model, never a GGUF -- exactly where it is
/// whole (`snapshot_wan_layout`) and `mlx_video` says mlx-video is installed
/// (53k); anywhere else M20's answer stands: none.
[[nodiscard]] std::vector<std::string> snapshot_conversions(
    const std::filesystem::path& dir, const EmbeddingBuilds& mlx_embeddings = {},
    const DrafterBuilds& mlx_drafters = {}, const VideoConverts& mlx_video = {});

/// A set's **towers** (53f, 2026-10-21): what its `config.json` declares
/// beside its language model -- a `vision_config`, an `audio_config`, or an
/// omni model's `thinker_config`, which nests both -- each an object. mlx-lm
/// builds the language model alone and leaves them out; mlx-vlm builds the
/// whole model, its towers kept, and its build is recorded as the kind
/// `vision` (53e). The keys found, in that order; empty for a language
/// model's weights, one declaring itself `language_model_only`, or a
/// directory with no readable `config.json`. The set's own declaration, never
/// its name -- read as the GGUF path reads its encoders, with an omni model's
/// `thinker_config` beside them.
[[nodiscard]] std::vector<std::string> snapshot_towers(const std::filesystem::path& dir);

/// The towers as one phrase: "vision_config, audio_config".
[[nodiscard]] std::string describe_towers(const std::vector<std::string>& towers);

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
