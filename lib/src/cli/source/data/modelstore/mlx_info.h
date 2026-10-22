#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "modelstore/snapshot.h"

/// What an MLX model directory is, read from its files alone (27b): the
/// window `config.json` declares, how its weights are stored, every shard
/// and what the whole directory weighs -- without importing anything or
/// loading a weight.
///
/// **The GGUF header reader's honesty rule, carried over.** A check that
/// reports "loads" for files that cannot load is worse than none (Milestone
/// N). So `complete` claims only what the files can show -- a configuration
/// that parses and is the model's, a tokenizer, every shard the index names,
/// each shard's header within its file -- and a missing `config.json` is
/// "cannot load", never a guess. Whether this `mlx-lm` builds the
/// architecture is a different fact, known only at load (Qwen3-VL's
/// `TypeError`, 27a), and nothing here claims it.
///
/// **Read, measured, not cached** (M2's lesson): a shard's header is an
/// 8-byte length and a JSON table, so the sweep is a few reads per shard.
namespace apogee::models {

/// How an MLX model's weights are stored: quantized by `mlx-lm`'s own scheme
/// (`config.json`'s `quantization`), as its publisher quantized them
/// (`quantization_config.quant_method`), or at a floating-point type.
struct MlxQuantization {
    /// Bits per weight of the default quantization; 0 when not quantized.
    int bits = 0;
    int group_size = 0;
    /// `mlx-lm`'s mode -- `affine`, `mxfp4`, `nvfp4`, `mxfp8` -- or empty
    /// when the config states none (`affine`, `mlx-lm`'s default).
    std::string mode;
    /// Some layers quantized at other bits (a mixed recipe), the highest of
    /// them in `high_bits`.
    bool mixed = false;
    int high_bits = 0;
    /// The publisher's own quantization, when `mlx-lm`'s is absent:
    /// `quantization_config.quant_method` (gpt-oss ships `mxfp4`).
    std::string method;
    /// The floating-point type the config declares -- `bf16`, `f16`, `f32`
    /// -- or empty when it declares none.
    std::string dtype;

    [[nodiscard]] bool quantized() const noexcept {
        return bits > 0;
    }

    /// For a person: "4-bit (affine, group 64)", "mixed 3/6-bit (group 64)",
    /// "mxfp4 (as published)", "bf16", or "precision not stated".
    [[nodiscard]] std::string describe() const;

    /// For a name: "4bit", "mixed_3_6", "mxfp4", "bf16" -- what a
    /// community build's name ends with -- or empty when nothing says.
    [[nodiscard]] std::string label() const;
};

/// One `*.safetensors` file.
struct MlxShard {
    std::string name;
    std::uintmax_t bytes = 0;
};

struct MlxInfo {
    /// Every file a load needs was found whole: `config.json` (the model's,
    /// not a download record), a tokenizer, every shard the index names, and
    /// each shard's header within its file -- or, for a video model, its
    /// layout's files in a tokenizer's place (`video`). Not a claim that
    /// `mlx-lm` builds the architecture -- only a load says that.
    bool complete = false;
    /// Read as an MLX video model (53k): mlx-video's Wan layout, its
    /// `config.json` beside `t5_encoder.safetensors`, `vae.safetensors` and
    /// the transformer's weights (`model.safetensors`, or a pair's
    /// `low_noise_model.safetensors` and `high_noise_model.safetensors`), each
    /// whole. No tokenizer -- mlx-video reads the text encoder's from its own
    /// source -- and no token window.
    bool video = false;
    /// Why not, in a sentence. Set exactly when `complete` is false.
    std::string problem;
    /// `config.json` was read and is the model's: what follows comes from it.
    bool config_read = false;
    /// The files carry `mlx-lm`'s own marks: `config.json`'s `quantization`,
    /// or shards saved with `format: mlx` in their metadata.
    bool mlx_format = false;
    /// `config.json`'s `model_type`, and a vision-language model's text
    /// model's; `architectures[0]`.
    std::string model_type;
    std::string text_model_type;
    std::string architecture;
    /// The trained window `config.json` declares, 0 when it declares none --
    /// and which key said so (`text_config.max_position_embeddings`).
    std::int64_t context_length = 0;
    std::string context_key;
    MlxQuantization quantization;
    /// Sorted by name.
    std::vector<MlxShard> shards;
    /// Every regular file under the directory, in bytes.
    std::uintmax_t bytes = 0;
};

/// Reads `dir`. Never throws: an unreadable directory is an incomplete one,
/// with the reason. `kind` is what the directory is (53e), when the caller
/// knows it before a record says it -- a conversion's staging (53k); empty,
/// its record's word. A `video` directory is read by its own layout's files
/// (`MlxInfo::video`); every other kind as a language model's.
[[nodiscard]] MlxInfo read_mlx_info(const std::filesystem::path& dir, std::string_view kind = {});

/// The files an MLX video model's directory holds (53k), as mlx-video's Wan
/// converter writes them -- `config.json`, `t5_encoder.safetensors`,
/// `vae.safetensors`, then the transformer's weights -- for a sentence.
inline constexpr std::string_view kMlxVideoFiles =
    "config.json, the T5 text encoder (t5_encoder.safetensors), the VAE (vae.safetensors) and "
    "the transformer's weights";

/// Whether the SafeTensors file `shard`, `size` bytes, is whole: its header
/// reads and every tensor it lists lies within the file -- what tells a
/// whole shard from a half-downloaded one with a perfectly good start. The
/// problem, naming the file, or empty. The pipeline reader's check too (49a).
[[nodiscard]] std::string safetensors_shard_problem(const std::filesystem::path& shard,
                                                    std::uintmax_t size);

/// What `config.json` alone says -- the window, the quantization, the model
/// type -- with no shard opened, and `complete` false: for what is asked at
/// every startup (the backend's window), where the files' wholeness is the
/// load's to find out.
[[nodiscard]] MlxInfo read_mlx_config(const std::filesystem::path& dir);

/// Whether `dir` holds an MLX model in `mlx-lm`'s own format: a
/// `config.json` and a shard, carrying `mlx-lm`'s marks (`mlx_format`). What
/// `models migrate` sends to `mlx/` rather than `safetensors/`.
[[nodiscard]] bool is_mlx_model_dir(const std::filesystem::path& dir);

// --- what kind of MLX directory (53e) --------------------------------------------------

/// **What an MLX directory is, decided once.** Every directory under a
/// model's `mlx/` was taken for a language model, true while mlx-lm was the
/// only converter writing there. Each converter now records what it made
/// (`Snapshot::kind`), and one reader decides: the record's word, else what
/// the files say, else `chat` -- so a directory made before the record reads
/// exactly as it did. The listing, `models info` and `config scan` all ask
/// it; a name is never the evidence (the draft-head and M20 rule).
///
/// The words: `chat` (mlx-lm's language model), `vision` (a vision model,
/// its towers kept), `embedding`, `drafter`, `video`, `voice`. A word this
/// build does not know is shown as written and registered as nothing.
inline constexpr std::string_view kMlxChat = "chat";
inline constexpr std::string_view kMlxVision = "vision";
inline constexpr std::string_view kMlxEmbedding = "embedding";
inline constexpr std::string_view kMlxDrafter = "drafter";
inline constexpr std::string_view kMlxVideo = "video";
inline constexpr std::string_view kMlxVoice = "voice";

/// Where a directory's kind came from.
enum class MlxKindSource {
    /// Its record names it: the conversion that made it wrote it.
    Recorded,
    /// No record names one, and its files make it a vision model -- or, since
    /// 53i, a drafter: its `config.json` declares a draft head.
    Files,
    /// Nothing says otherwise: `chat`, as every MLX directory was before.
    Default,
};

struct MlxKind {
    /// The word, as the record wrote it.
    std::string word{kMlxChat};
    MlxKindSource source = MlxKindSource::Default;

    /// One of the six words.
    [[nodiscard]] bool known() const noexcept;

    /// An mlx backend chats with it: `chat` or `vision`. Every other kind,
    /// and a word this build does not know, is no chat backend.
    [[nodiscard]] bool backend() const noexcept;

    /// An mlx backend embeds with it (53h): `embedding`, which mlx-embeddings
    /// built -- the embedding role's to name, never a chat's.
    [[nodiscard]] bool embeds() const noexcept;

    /// An mlx backend runs it at all: it chats with it (`backend`) or embeds
    /// with it (`embeds`). What `config scan --register` registers as an
    /// `mlx` entry; a drafter, a video or voice model, and a word this build
    /// does not know, never.
    [[nodiscard]] bool registers() const noexcept;
};

/// The kind of the MLX directory `dir`: its record's `kind`, else `drafter`
/// when its `config.json` declares a draft head (`snapshot_draft_head`, 53i --
/// a drafter pulled whole, as the MLX community publishes one, is never taken
/// for a chat model), else `vision` when its files say so
/// (`mlx_vision_files`), else `chat`. Never throws: an unreadable record is no
/// record.
[[nodiscard]] MlxKind read_mlx_kind(const std::filesystem::path& dir);

/// Whether `dir`'s files make a vision model: `config.json` declares a
/// `vision_config`, and the processor that turns a picture into its input is
/// beside it (`preprocessor_config.json` or `processor_config.json`). The
/// fact the mlx backend reads images by (27c), and the one an unrecorded
/// directory's kind is inferred from.
[[nodiscard]] bool mlx_vision_files(const std::filesystem::path& dir);

/// What a kind is and what uses it, in the one wording every surface says:
/// "an embedding model: the embedding role uses it, never a chat backend";
/// for a word this build does not know, "kind '<word>', not one this apogee
/// knows: nothing here runs it".
[[nodiscard]] std::string describe_mlx_kind(std::string_view word);

/// `models info`'s line: the word, where it came from, and what it is --
/// "chat (no kind recorded) -- a language model: an mlx backend chats with it".
[[nodiscard]] std::string describe(const MlxKind& kind);

// --- a drafter beside its base (53i) ---------------------------------------------------

/// A **drafter** as its directory says it (53i): a draft head mlx-vlm built --
/// Gemma 4's assistants, a few layers proposing tokens for their base to
/// verify -- which an mlx entry runs beside its base through mlx-vlm's
/// speculative path (`draft:`), never alone and never as a backend of its own.
struct MlxDrafter {
    /// The draft head its `config.json` declares: the architecture, the
    /// family and width of the base it reads, its `model_type`.
    DraftHead head{};
    /// The bases its record names (`Snapshot::bases`): the stored models
    /// `draft_head_bases` measured when it was converted. Empty when it names
    /// none -- a pull, or a conversion with no base stored -- and then it pairs
    /// by family and width alone.
    std::vector<std::string> bases{};
};

/// The drafter at `dir`, or nullopt when it is none: its kind is not
/// `drafter` (`read_mlx_kind`), or its `config.json` declares no draft head.
[[nodiscard]] std::optional<MlxDrafter> read_mlx_drafter(const std::filesystem::path& dir);

/// What a drafter is, in one phrase: "a drafter (Gemma4AssistantForCausalLM)
/// for a 2560-wide gemma4 model, its record naming Gemma4-E4B-Instruct".
[[nodiscard]] std::string describe(const MlxDrafter& drafter);

/// Why the drafter at `drafter` cannot run beside the MLX model at `base`, in
/// one sentence naming the way out -- or empty when it can. File facts alone,
/// asked in order: a drafter is there (`read_mlx_drafter`) and says how wide
/// a model it reads; the base is a model mlx-vlm loads to chat with -- kind
/// `vision`, never mlx-lm's build (`chat`): mlx-vlm's speculative path loads
/// the base through mlx-vlm, and mlx-lm's build is laid out for mlx-lm with
/// its towers left out -- of the drafter's family and as wide as it reads
/// (`config_width`); and, where its record names bases, `base` is a stored
/// MLX model of one of them -- under `models_root`, the store's MLX root.
/// Whether the installed mlx-vlm drafts its type is the backend's question
/// (`backends::probe_mlx_draft`), asked after this.
[[nodiscard]] std::string mlx_drafter_refusal(const std::filesystem::path& base,
                                              const std::filesystem::path& drafter,
                                              const std::filesystem::path& models_root);

/// How wide an MLX embedding model's vectors are (53h), from the directory's
/// own files: the last sentence-transformers `Dense` module its
/// `modules.json` names whose `config.json` states `out_features` -- what
/// the pipeline ends in -- else `config.json`'s `hidden_size` (a composite's
/// `text_config.hidden_size`), what the pooled vector is when no Dense
/// reshapes it. mlx-embeddings' converter copies only the top level's files,
/// so a directory it wrote states the hidden size, which Gemma 3's two dense
/// layers map back to. Read, never guessed: 0 when nothing states it.
struct MlxEmbeddingWidth {
    std::int64_t width = 0;
    /// Where it was read: `the last Dense's out_features (3_Dense)`, `its
    /// hidden_size`, `its text_config.hidden_size`. Empty when unknown.
    std::string source{};

    [[nodiscard]] bool known() const noexcept {
        return width > 0;
    }
};

[[nodiscard]] MlxEmbeddingWidth read_mlx_embedding_width(const std::filesystem::path& dir);

/// One phrase: "768 wide (its hidden_size)", or "width unknown -- its
/// config.json states no hidden_size" -- what `models info` says.
[[nodiscard]] std::string describe(const MlxEmbeddingWidth& width);

/// The window an `mlx` backend gets over a model (26a's chain): its
/// `context_size` when set, else 26a's default -- 32,768, or the trained
/// window when smaller -- so an MLX chat warns at 80% and compacts at 90% of
/// the same window a GGUF chat of the model would. 0 when `config.json`
/// could not be read and nothing is configured.
struct MlxWindow {
    std::int64_t window = 0;
    bool configured = false;
    std::int64_t trained = 0;
};

[[nodiscard]] MlxWindow mlx_window(const MlxInfo& info, const harness::BackendConfig& backend);

/// One line: "32768-token window (the default; trained for 131072)".
/// `context_size` set over a window the model was not trained for is said.
[[nodiscard]] std::string describe(const MlxWindow& window);

}  // namespace apogee::models
