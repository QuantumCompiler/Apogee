#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "contracts/config.h"

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
    /// each shard's header within its file. Not a claim that `mlx-lm` builds
    /// the architecture -- only a load says that.
    bool complete = false;
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
/// with the reason.
[[nodiscard]] MlxInfo read_mlx_info(const std::filesystem::path& dir);

/// What `config.json` alone says -- the window, the quantization, the model
/// type -- with no shard opened, and `complete` false: for what is asked at
/// every startup (the backend's window), where the files' wholeness is the
/// load's to find out.
[[nodiscard]] MlxInfo read_mlx_config(const std::filesystem::path& dir);

/// Whether `dir` holds an MLX model in `mlx-lm`'s own format: a
/// `config.json` and a shard, carrying `mlx-lm`'s marks (`mlx_format`). What
/// `models migrate` sends to `mlx/` rather than `safetensors/`.
[[nodiscard]] bool is_mlx_model_dir(const std::filesystem::path& dir);

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
