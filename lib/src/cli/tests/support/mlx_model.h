#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// MLX model directories built for tests (27b): `config.json`, a tokenizer,
/// SafeTensors shards with real headers, and the index naming them.
///
/// **Built, not committed** -- the GGUF builder's reason: the cases that
/// matter are the broken ones (a shard cut short, an index naming a shard
/// that never arrived, a directory with no `config.json`), and those have to
/// be constructed. A shard is a few hundred bytes: an 8-byte length, a JSON
/// table of F16 tensors laid end to end, and their zero bytes.
namespace apogee::testing {

/// One SafeTensors file: each tensor `{name, elements}` as F16, its data
/// offsets laid end to end, saved with `format` in its metadata (`mlx` is
/// what `mlx-lm` writes; `pt` what `transformers` does).
[[nodiscard]] inline std::string safetensors_bytes(
    const std::vector<std::pair<std::string, std::int64_t>>& tensors,
    std::string_view format = "mlx") {
    std::string header = "{\"__metadata__\":{\"format\":\"" + std::string{format} + "\"}";
    std::int64_t offset = 0;
    for (const auto& [name, elements] : tensors) {
        const std::int64_t end = offset + (elements * 2);
        header += ",\"" + name + "\":{\"dtype\":\"F16\",\"shape\":[" + std::to_string(elements) +
                  "],\"data_offsets\":[" + std::to_string(offset) + "," + std::to_string(end) +
                  "]}";
        offset = end;
    }
    header += "}";
    std::string bytes;
    std::uint64_t length = header.size();
    for (int i = 0; i < 8; ++i) {
        bytes += static_cast<char>((length >> (8 * i)) & 0xFFU);
    }
    bytes += header;
    bytes += std::string(static_cast<std::size_t>(offset), '\0');
    return bytes;
}

/// What a fixture model says about itself.
struct MlxModelSpec {
    /// 0: not quantized (the shards still carry `format: mlx`).
    int bits = 4;
    int group_size = 64;
    std::string mode;
    /// 0: `config.json` declares no window.
    std::int64_t window = 131072;
    std::string model_type = "llama";
    int shards = 1;
    /// `model.safetensors.index.json`, naming every shard.
    bool index = true;
    std::string format = "mlx";
    bool tokenizer = true;
};

inline void write_fixture_file(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

/// The shard names `spec` writes: `model.safetensors`, or
/// `model-0000N-of-0000M.safetensors`.
[[nodiscard]] inline std::vector<std::string> mlx_shard_names(const MlxModelSpec& spec) {
    std::vector<std::string> names;
    if (spec.shards == 1) {
        names.emplace_back("model.safetensors");
        return names;
    }
    const auto five = [](int n) {
        const std::string text = std::to_string(n);
        return std::string(5 - text.size(), '0') + text;
    };
    for (int i = 1; i <= spec.shards; ++i) {
        names.push_back("model-" + five(i) + "-of-" + five(spec.shards) + ".safetensors");
    }
    return names;
}

/// A whole model at `dir`.
inline void write_mlx_model(const std::filesystem::path& dir, const MlxModelSpec& spec = {}) {
    std::string config = "{\"architectures\": [\"LlamaForCausalLM\"], \"model_type\": \"" +
                         spec.model_type + "\", \"torch_dtype\": \"bfloat16\"";
    if (spec.window > 0) {
        config += ", \"max_position_embeddings\": " + std::to_string(spec.window);
    }
    if (spec.bits > 0) {
        std::string scheme = "{\"group_size\": " + std::to_string(spec.group_size) +
                             ", \"bits\": " + std::to_string(spec.bits);
        if (!spec.mode.empty()) {
            scheme += ", \"mode\": \"" + spec.mode + "\"";
        }
        scheme += "}";
        config += ", \"quantization\": " + scheme + ", \"quantization_config\": " + scheme;
    }
    config += "}\n";
    write_fixture_file(dir / "config.json", config);
    if (spec.tokenizer) {
        write_fixture_file(dir / "tokenizer.json", "{\"model\": {\"type\": \"BPE\"}}\n");
        write_fixture_file(dir / "tokenizer_config.json",
                           "{\"chat_template\": \"{{ messages }}\"}\n");
    }
    std::string map;
    int layer = 0;
    for (const std::string& name : mlx_shard_names(spec)) {
        const std::string tensor = "model.layers." + std::to_string(layer++) + ".weight";
        write_fixture_file(dir / name, safetensors_bytes({{tensor, 64}}, spec.format));
        map += (map.empty() ? "" : ", ") + std::string{"\""} + tensor + "\": \"" + name + "\"";
    }
    if (spec.index) {
        write_fixture_file(dir / "model.safetensors.index.json",
                           "{\"metadata\": {}, \"weight_map\": {" + map + "}}\n");
    }
}

}  // namespace apogee::testing
