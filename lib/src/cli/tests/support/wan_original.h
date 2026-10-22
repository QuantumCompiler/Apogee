#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/sha256.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "support/mlx_model.h"

/// The user's Wan2.2-TI2V-5B set (53j, 2026-10-21): Wan's original layout as
/// `models pull Wan-AI/Wan2.2-TI2V-5B --safetensors` left it before 53j, its
/// record listing ten files and its two root checkpoints dropped. Written
/// inline from the set's own record and config, read in a mirror of the
/// user's store. Small fake bytes stand in for the weights -- never weights
/// -- and the record's sizes are the fake's; the config and configuration
/// are written at the sizes the user's record lists.
namespace apogee::testing {

inline constexpr std::string_view kWanRepo = "Wan-AI/Wan2.2-TI2V-5B";
inline constexpr std::string_view kWanModel = "Wan-AI--Wan2.2-TI2V-5B";

/// Its `config.json`, byte for byte: 251 bytes, a diffusers-written
/// `WanModel` with no `architectures` -- Wan's code saves through diffusers'
/// config mixin.
inline constexpr std::string_view kWanTi2vConfig = R"({
  "_class_name": "WanModel",
  "_diffusers_version": "0.33.0",
  "dim": 3072,
  "eps": 1e-06,
  "ffn_dim": 14336,
  "freq_dim": 256,
  "in_dim": 48,
  "model_type": "ti2v",
  "num_heads": 24,
  "num_layers": 30,
  "out_dim": 48,
  "text_len": 512
}
)";

/// Its `configuration.json`, ModelScope's file, at the record's 43 bytes
/// (nothing here reads it).
inline constexpr std::string_view kWanConfiguration =
    R"({"framework":"Pytorch","task":"text-video"})";

/// The ten files the user's record lists, in the repository's order. The
/// real shards are 9,825,014,472 + 9,995,661,736 + 178,558,176 bytes and
/// their index 72,865.
inline constexpr std::array<std::string_view, 10> kWanRecorded{
    "config.json",
    "configuration.json",
    "diffusion_pytorch_model-00001-of-00003.safetensors",
    "diffusion_pytorch_model-00002-of-00003.safetensors",
    "diffusion_pytorch_model-00003-of-00003.safetensors",
    "diffusion_pytorch_model.safetensors.index.json",
    "google/umt5-xxl/special_tokens_map.json",
    "google/umt5-xxl/spiece.model",
    "google/umt5-xxl/tokenizer.json",
    "google/umt5-xxl/tokenizer_config.json",
};

/// The two root checkpoints that pull dropped: the T5 text encoder and the
/// VAE every runtime needs beside the transformer.
inline constexpr std::string_view kWanTextEncoder = "models_t5_umt5-xxl-enc-bf16.pth";
inline constexpr std::string_view kWanVae = "Wan2.2_VAE.pth";

/// The bytes a test holds for `path`: the config and configuration as
/// written, the index at its real size, and a few bytes named after any
/// other file.
[[nodiscard]] inline std::string wan_bytes(std::string_view path) {
    if (path == "config.json") {
        return std::string{kWanTi2vConfig};
    }
    if (path == "configuration.json") {
        return std::string{kWanConfiguration};
    }
    if (path == "diffusion_pytorch_model.safetensors.index.json") {
        std::string index = R"({"metadata": {"total_size": 19999197184}, "weight_map": {}})";
        index.resize(72865, ' ');
        return index;
    }
    return "fake bytes of " + std::string{path} + ", never weights\n";
}

/// The repository's whole listing as Hugging Face's tree sorts it: the ten,
/// the two checkpoints, and what no SafeTensors pull takes.
[[nodiscard]] inline std::vector<std::string> wan_repository_files() {
    return {".gitattributes",
            "README.md",
            std::string{kWanVae},
            "assets/comp_effic.png",
            "assets/moe_arch.png",
            "config.json",
            "configuration.json",
            "diffusion_pytorch_model-00001-of-00003.safetensors",
            "diffusion_pytorch_model-00002-of-00003.safetensors",
            "diffusion_pytorch_model-00003-of-00003.safetensors",
            "diffusion_pytorch_model.safetensors.index.json",
            "examples/i2v_input.JPG",
            "google/umt5-xxl/special_tokens_map.json",
            "google/umt5-xxl/spiece.model",
            "google/umt5-xxl/tokenizer.json",
            "google/umt5-xxl/tokenizer_config.json",
            std::string{kWanTextEncoder}};
}

/// The user's record: the ten files, each at its fake's size and sha256.
[[nodiscard]] inline models::Snapshot wan_record() {
    models::Snapshot record;
    record.ref = std::string{kWanRepo};
    record.revision = "main";
    record.source = "huggingface";
    record.pulled_at = "2026-10-20T18:02:11Z";
    for (const std::string_view path : kWanRecorded) {
        const std::string bytes = wan_bytes(path);
        record.files.push_back({.path = std::string{path},
                                .size = static_cast<std::int64_t>(bytes.size()),
                                .sha256 = models::sha256_hex(bytes)});
    }
    return record;
}

/// The user's set, as that pull left it, in the store under `roots`: the ten
/// files and their record, at the id the shards' hash names. Its directory.
inline std::filesystem::path write_user_wan_set(const models::StoreRoots& roots) {
    const models::Snapshot record = wan_record();
    const std::filesystem::path dir = models::weights_dir(
        roots, models::kSafetensorsFormat, kWanModel, models::snapshot_weight_id(record.files));
    for (const std::string_view path : kWanRecorded) {
        write_fixture_file(dir / path, wan_bytes(path));
    }
    (void)models::write_snapshot(dir, record);
    return dir;
}

}  // namespace apogee::testing
