#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "support/mlx_model.h"

/// Voice model directories built for tests (50a), laid out as Kokoro-82M's
/// repository is: `config.json`, one checkpoint (`kokoro-v1_0.pth`) and a
/// `voices/` directory of packs. Each `.pth`/`.pt` is a few bytes shaped as
/// a PyTorch save is -- a zip archive's local header first and its end
/// record last -- so the reader's wholeness check has something real to
/// read. Never weights; built, not committed, for the broken cases' sake.
namespace apogee::testing {

struct VoiceModelSpec {
    std::vector<std::string> voices{"af_heart", "am_adam", "bf_emma"};
    std::string checkpoint = "kokoro-v1_0.pth";
};

/// A zip-shaped file `size` bytes long: what the reader takes for whole.
[[nodiscard]] inline std::string zip_bytes(std::size_t size = 96) {
    std::string bytes{"PK\x03\x04", 4};
    bytes += std::string(size > 30 ? size - 30 : 0, 'w');
    bytes += std::string{"PK\x05\x06", 4};
    bytes += std::string(22, '\0');
    return bytes;
}

/// A whole voice model at `dir`.
inline void write_voice_model(const std::filesystem::path& dir, const VoiceModelSpec& spec = {}) {
    write_fixture_file(dir / "config.json",
                       "{\"istftnet\": {\"upsample_rates\": [10, 6]}, \"plbert\": {}, "
                       "\"vocab\": {\"a\": 43}}\n");
    write_fixture_file(dir / spec.checkpoint, zip_bytes(256));
    for (const std::string& voice : spec.voices) {
        write_fixture_file(dir / "voices" / (voice + ".pt"), zip_bytes());
    }
}

/// An MLX voice model (53m), as mlx-audio's converter writes Kokoro and
/// `models convert --mlx` records it: `config.json` naming `model_type:
/// kokoro`, the weights (`model.safetensors`, `format: mlx`) and a
/// `voices/<name>.safetensors` pack, its `voice` tensor, for every voice --
/// with the record saying `voice` and the voice model it was made from.
struct MlxVoiceModelSpec {
    std::vector<std::string> voices{"af_heart", "am_adam", "bf_emma"};
    /// The voice model the record names it made from; no record when empty.
    std::string ref = "hexgrad--Kokoro-82M/voice/aaaaaaaaaaaa";
    /// 0: unquantized (bf16).
    int bits = 0;
};

inline void write_mlx_voice_model(const std::filesystem::path& dir,
                                  const MlxVoiceModelSpec& spec = {}) {
    std::string config =
        "{\"istftnet\": {\"upsample_rates\": [10, 6]}, \"plbert\": {}, "
        "\"vocab\": {\"a\": 43}, \"model_type\": \"kokoro\"";
    if (spec.bits > 0) {
        config +=
            ", \"quantization\": {\"group_size\": 64, \"bits\": " + std::to_string(spec.bits) +
            ", \"mode\": \"affine\"}";
    }
    write_fixture_file(dir / "config.json", config + "}\n");
    write_fixture_file(dir / "model.safetensors",
                       safetensors_bytes({{"bert.embeddings.word_embeddings.weight", 64},
                                          {"decoder.weight", 32}}));
    for (const std::string& voice : spec.voices) {
        write_fixture_file(dir / "voices" / (voice + ".safetensors"),
                           safetensors_bytes({{"voice", 16}}));
    }
    if (!spec.ref.empty()) {
        write_fixture_file(dir / "apogee-snapshot.json",
                           "{\"ref\": \"" + spec.ref +
                               "\", \"source\": \"convert\", \"transform\": "
                               "\"mlx_audio.convert bf16\", \"kind\": \"voice\", "
                               "\"files\": []}\n");
    }
}

/// An MLX Kokoro as the MLX community publishes it (M22), read off
/// `mlx-community/Kokoro-82M-bf16`'s file list, 2026-10-24: `config.json` in
/// Kokoro's own shape -- its `istftnet` decoder, `plbert` text encoder and
/// `vocab`, and no `model_type` (with `bits`, the `quantization` the 6- and
/// 8-bit builds add) -- the weights as `kokoro-v1_0.safetensors`, and each
/// voice under `voices/` twice, its SafeTensors pack beside the original's
/// `.pt`. No tokenizer, no record: what a pull brings. The config is cut to
/// the keys read there.
inline void write_published_mlx_kokoro(const std::filesystem::path& dir,
                                       const MlxVoiceModelSpec& spec = {}) {
    std::string config =
        "{\"istftnet\": {\"upsample_rates\": [10, 6], \"gen_istft_n_fft\": 20, "
        "\"upsample_initial_channel\": 512}, \"plbert\": {\"hidden_size\": 768, "
        "\"num_attention_heads\": 12, \"num_hidden_layers\": 12}, "
        "\"vocab\": {\";\": 1, \":\": 2, \",\": 3, \".\": 4, \"!\": 5}";
    if (spec.bits > 0) {
        config +=
            ", \"quantization\": {\"group_size\": 64, \"bits\": " + std::to_string(spec.bits) + "}";
    }
    write_fixture_file(dir / "config.json", config + "}\n");
    write_fixture_file(dir / "kokoro-v1_0.safetensors",
                       safetensors_bytes({{"bert.embeddings.word_embeddings.weight", 64},
                                          {"decoder.weight", 32}}));
    for (const std::string& voice : spec.voices) {
        write_fixture_file(dir / "voices" / (voice + ".safetensors"),
                           safetensors_bytes({{"voice", 16}}));
        write_fixture_file(dir / "voices" / (voice + ".pt"), zip_bytes());
    }
}

}  // namespace apogee::testing
