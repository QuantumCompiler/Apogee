#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "support/mlx_model.h"

/// Diffusion pipeline directories built for tests (49a): `model_index.json`
/// and one directory per component, laid out as Wan 2.2 TI2V-5B's
/// diffusers repository is -- a scheduler, a text encoder, its tokenizer, a
/// sharded transformer and a VAE, with the optional second transformer
/// declared absent -- each weights file a few hundred bytes with a real
/// SafeTensors header.
///
/// **Built, not committed** -- `mlx_model.h`'s reason: the cases that matter
/// are the broken ones (a component that never arrived, a shard cut short,
/// an index naming a shard that is not there), and those are constructed.
/// Never real weights.
namespace apogee::testing {

struct PipelineModelSpec {
    std::string class_name = "WanPipeline";
    /// The transformer's shards, named by its weight index; 1 writes one
    /// `diffusion_pytorch_model.safetensors` and no index.
    int transformer_shards = 2;
    /// Every weights file saved as this variant (`fp16`):
    /// `diffusion_pytorch_model.fp16.safetensors`.
    std::string variant{};
    /// The plain weights AND an `fp16` copy beside them, as a repository
    /// publishing both does -- what a pull leaves the copy of out.
    bool fp16_copies = false;
    /// A component's directory not written at all.
    std::string missing{};
};

[[nodiscard]] inline std::string pipeline_weights_name(const std::string& stem,
                                                       const std::string& variant) {
    return stem + (variant.empty() ? "" : "." + variant) + ".safetensors";
}

/// Every weights file `spec` writes, relative to the pipeline's root.
[[nodiscard]] inline std::vector<std::string> pipeline_shard_names(const PipelineModelSpec& spec) {
    std::vector<std::string> names;
    const auto both = [&](const std::string& component, const std::string& stem) {
        names.push_back(component + "/" + pipeline_weights_name(stem, spec.variant));
    };
    both("text_encoder", "model");
    if (spec.transformer_shards == 1) {
        both("transformer", "diffusion_pytorch_model");
    } else {
        const auto five = [](int n) {
            const std::string text = std::to_string(n);
            return std::string(5 - text.size(), '0') + text;
        };
        for (int i = 1; i <= spec.transformer_shards; ++i) {
            both("transformer",
                 "diffusion_pytorch_model-" + five(i) + "-of-" + five(spec.transformer_shards));
        }
    }
    both("vae", "diffusion_pytorch_model");
    std::vector<std::string> out;
    for (const std::string& name : names) {
        if (spec.missing.empty() || !name.starts_with(spec.missing + "/")) {
            out.push_back(name);
        }
    }
    return out;
}

/// A whole pipeline at `dir`.
inline void write_pipeline_model(const std::filesystem::path& dir,
                                 const PipelineModelSpec& spec = {}) {
    write_fixture_file(dir / "model_index.json",
                       "{\n  \"_class_name\": \"" + spec.class_name +
                           "\",\n  \"_diffusers_version\": \"0.35.0\",\n"
                           "  \"boundary_ratio\": null,\n  \"expand_timesteps\": true,\n"
                           "  \"scheduler\": [\"diffusers\", \"UniPCMultistepScheduler\"],\n"
                           "  \"text_encoder\": [\"transformers\", \"UMT5EncoderModel\"],\n"
                           "  \"tokenizer\": [\"transformers\", \"T5TokenizerFast\"],\n"
                           "  \"transformer\": [\"diffusers\", \"WanTransformer3DModel\"],\n"
                           "  \"transformer_2\": [null, null],\n"
                           "  \"vae\": [\"diffusers\", \"AutoencoderKLWan\"]\n}\n");
    const auto wanted = [&spec](const std::string& component) { return spec.missing != component; };
    if (wanted("scheduler")) {
        write_fixture_file(dir / "scheduler" / "scheduler_config.json",
                           "{\"_class_name\": \"UniPCMultistepScheduler\"}\n");
    }
    if (wanted("tokenizer")) {
        write_fixture_file(dir / "tokenizer" / "tokenizer_config.json",
                           "{\"model_max_length\": 512}\n");
        write_fixture_file(dir / "tokenizer" / "tokenizer.json",
                           "{\"model\": {\"type\": \"Unigram\"}}\n");
    }
    for (const char* component : {"text_encoder", "transformer", "vae"}) {
        if (wanted(component)) {
            write_fixture_file(dir / component / "config.json",
                               std::string{"{\"_class_name\": \""} + component + "\"}\n");
        }
    }
    std::string map;
    int tensor = 0;
    for (const std::string& name : pipeline_shard_names(spec)) {
        const std::string tensor_name = "blocks." + std::to_string(tensor++) + ".weight";
        write_fixture_file(dir / name, safetensors_bytes({{tensor_name, 64}}, "pt"));
        if (spec.fp16_copies) {
            const std::string plain = name.substr(0, name.size() - 12);  // ".safetensors"
            write_fixture_file(dir / (plain + ".fp16.safetensors"),
                               safetensors_bytes({{tensor_name, 32}}, "pt"));
        }
        if (name.starts_with("transformer/") && spec.transformer_shards > 1) {
            map += (map.empty() ? "" : ", ") + std::string{"\""} + tensor_name + "\": \"" +
                   name.substr(12) + "\"";
        }
    }
    if (!map.empty()) {
        write_fixture_file(
            dir / "transformer" /
                ("diffusion_pytorch_model.safetensors.index" +
                 (spec.variant.empty() ? std::string{} : "." + spec.variant) + ".json"),
            "{\"metadata\": {}, \"weight_map\": {" + map + "}}\n");
    }
}

}  // namespace apogee::testing
