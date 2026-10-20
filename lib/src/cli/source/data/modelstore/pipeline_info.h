#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "modelstore/mlx_info.h"

/// What a diffusion pipeline directory is, read from its files alone (49a):
/// the pipeline class `model_index.json` names, each component it declares,
/// and each component's configuration and weights -- without importing
/// anything or loading a weight.
///
/// A pipeline is not one weights file: its root carries `model_index.json`,
/// and each component (`transformer/`, `vae/`, `text_encoder/`, the
/// tokenizer, the scheduler) sits in a directory of its own with its own
/// configuration and SafeTensors. So the store takes it as a directory whole,
/// as it takes an MLX model.
///
/// **`mlx_info`'s honesty rule, carried over.** `complete` claims only what
/// the files can show -- the index parses and names its components, each
/// named component's directory is there with the file its kind needs, every
/// shard an index names is present and each shard's header lies within its
/// file -- and anything missing is "cannot load", naming the piece, never a
/// guess. Whether this `diffusers` builds the pipeline class is a different
/// fact, known only at load.
namespace apogee::models {

/// `<dir>/model_index.json`: what makes a directory a pipeline.
inline constexpr std::string_view kModelIndex = "model_index.json";

/// What a component is, by the class `model_index.json` names for it -- and
/// so which file it needs: a model its `config.json` and its weights, a
/// scheduler its `scheduler_config.json`, a tokenizer its tokenizer files, a
/// processor its `preprocessor_config.json`.
enum class PipelineComponentKind : std::uint8_t { Model, Scheduler, Tokenizer, Processor };

[[nodiscard]] std::string_view to_string(PipelineComponentKind kind) noexcept;

/// The kind a class name says: `…Scheduler…` a scheduler, `…Tokenizer…` a
/// tokenizer, `…Processor`/`…FeatureExtractor` a processor, anything else a
/// model.
[[nodiscard]] PipelineComponentKind component_kind(std::string_view class_name) noexcept;

struct PipelineComponent {
    /// Its directory, and its key in `model_index.json`.
    std::string name;
    /// The library and class the index names: `diffusers`,
    /// `WanTransformer3DModel`.
    std::string library;
    std::string class_name;
    PipelineComponentKind kind = PipelineComponentKind::Model;
    /// Its `*.safetensors` files, sorted by name.
    std::vector<MlxShard> shards;
    /// The weights variant every shard is saved as (`fp16`, from
    /// `diffusion_pytorch_model.fp16.safetensors`), or empty for plain
    /// weights -- what a load must ask for by name.
    std::string variant;
    /// Every regular file under its directory, and their bytes.
    std::size_t files = 0;
    std::uintmax_t bytes = 0;
};

struct PipelineInfo {
    /// Every component the index names was found whole: its directory, the
    /// file its kind needs, every shard its weight index names, each shard's
    /// header within its file. Not a claim that `diffusers` builds the
    /// pipeline -- only a load says that.
    bool complete = false;
    /// Why not, in a sentence naming the missing piece. Set exactly when
    /// `complete` is false.
    std::string problem;
    /// `model_index.json` was read: what follows comes from it.
    bool index_read = false;
    /// `_class_name` and `_diffusers_version`: `WanPipeline`, `0.35.0`.
    std::string class_name;
    std::string diffusers_version;
    /// Each component the index declares present, sorted by name.
    std::vector<PipelineComponent> components;
    /// The ones it declares absent (`[null, null]`, an optional component
    /// this checkpoint does not ship), sorted.
    std::vector<std::string> absent;
    /// Every regular file under the directory, in bytes.
    std::uintmax_t bytes = 0;

    /// Shards across every component.
    [[nodiscard]] std::size_t shard_count() const noexcept;
    /// The weights variant the model components share, or empty when their
    /// weights are plain (or do not agree).
    [[nodiscard]] std::string variant() const;
};

/// Reads `dir`. Never throws: an unreadable directory is an incomplete one,
/// with the reason.
[[nodiscard]] PipelineInfo read_pipeline_info(const std::filesystem::path& dir);

/// Whether `dir` is a pipeline: `model_index.json` at its root.
[[nodiscard]] bool is_pipeline_dir(const std::filesystem::path& dir);

/// The components `model_index` (the file's text) declares present, sorted --
/// what a pull downloads. Empty when it is not an index that names any.
[[nodiscard]] std::vector<std::string> pipeline_components(std::string_view model_index);

/// The variant a weights file is saved as -- `fp16` for
/// `diffusion_pytorch_model.fp16.safetensors` or
/// `model.safetensors.index.fp16.json` -- or empty for a plain one.
[[nodiscard]] std::string weights_variant(std::string_view file_name);

/// One line: "WanPipeline, 5 components (scheduler, text_encoder,
/// tokenizer, transformer, vae), 9 shard(s)".
[[nodiscard]] std::string describe(const PipelineInfo& info);

}  // namespace apogee::models
