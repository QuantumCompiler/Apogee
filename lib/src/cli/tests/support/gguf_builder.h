#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

/// Builds GGUF bytes a piece at a time, for tests.
///
/// **Fixtures are built rather than committed.** A real GGUF is megabytes even
/// for a tiny model, and — decisively — the cases that matter are the malformed
/// ones: a truncated download, a length that runs past the end, a Git LFS
/// pointer committed instead of the model. Those cannot be obtained by
/// downloading something; they have to be constructed. Building them also keeps
/// the bytes readable in a diff instead of opaque.
namespace apogee::testing {

class GgufBuilder {
public:
    GgufBuilder& magic(std::string_view value = "GGUF") {
        bytes_.append(value);
        return *this;
    }

    GgufBuilder& u32(std::uint32_t value) {
        bytes_.append(std::string_view{reinterpret_cast<const char*>(&value), sizeof(value)});
        return *this;
    }

    GgufBuilder& u64(std::uint64_t value) {
        bytes_.append(std::string_view{reinterpret_cast<const char*>(&value), sizeof(value)});
        return *this;
    }

    /// A GGUF string: a u64 length followed by the bytes.
    GgufBuilder& text(std::string_view value) {
        u64(value.size());
        bytes_.append(value);
        return *this;
    }

    /// One metadata pair whose value is a string.
    GgufBuilder& string_kv(std::string_view key, std::string_view value) {
        text(key);
        u32(8);  // String
        text(value);
        return *this;
    }

    /// One metadata pair whose value is a u32 — the common "skipped" shape.
    GgufBuilder& u32_kv(std::string_view key, std::uint32_t value) {
        text(key);
        u32(4);  // UInt32
        u32(value);
        return *this;
    }

    /// One metadata pair whose value is an array of i32 -- a per-layer key.
    GgufBuilder& i32_array_kv(std::string_view key, const std::vector<std::int32_t>& values) {
        text(key);
        u32(9);  // Array
        u32(5);  // of Int32
        u64(values.size());
        for (const std::int32_t value : values) {
            bytes_.append(std::string_view{reinterpret_cast<const char*>(&value), sizeof(value)});
        }
        return *this;
    }

    /// One metadata pair whose value is an array of strings -- the shape of a
    /// tokenizer's vocabulary.
    GgufBuilder& string_array_kv(std::string_view key, const std::vector<std::string>& values) {
        text(key);
        u32(9);  // Array
        u32(8);  // of String
        u64(values.size());
        for (const std::string& value : values) {
            text(value);
        }
        return *this;
    }

    /// One metadata pair whose value is `count` f32 zeros -- the shape of a
    /// tokenizer's scores.
    GgufBuilder& f32_array_kv(std::string_view key, std::size_t count) {
        text(key);
        u32(9);  // Array
        u32(6);  // of Float32
        u64(count);
        bytes_.append(count * sizeof(float), '\0');
        return *this;
    }

    /// One metadata pair whose value is an array of bools -- a per-layer flag.
    GgufBuilder& bool_array_kv(std::string_view key, const std::vector<bool>& flags) {
        text(key);
        u32(9);  // Array
        u32(7);  // of Bool
        u64(flags.size());
        for (const bool flag : flags) {
            bytes_.push_back(flag ? '\x01' : '\x00');
        }
        return *this;
    }

    /// One metadata pair whose value is a bool -- a projector's encoder flags.
    GgufBuilder& bool_kv(std::string_view key, bool value) {
        text(key);
        u32(7);  // Bool
        bytes_.push_back(value ? '\x01' : '\x00');
        return *this;
    }

    /// One metadata pair whose value is a float32 -- a key that is not an
    /// integer, which the attention read must step over.
    GgufBuilder& f32_kv(std::string_view key, float value) {
        text(key);
        u32(6);  // Float32
        bytes_.append(std::string_view{reinterpret_cast<const char*>(&value), sizeof(value)});
        return *this;
    }

    /// One tensor descriptor: name, dim count, dims, ggml type, offset.
    GgufBuilder& tensor(std::string_view name, std::uint32_t dimensions = 2) {
        text(name);
        u32(dimensions);
        for (std::uint32_t i = 0; i < dimensions; ++i) {
            u64(16);
        }
        u32(0);
        u64(0);
        return *this;
    }

    [[nodiscard]] const std::string& bytes() const noexcept {
        return bytes_;
    }

    /// Writes what has been built to `path`. Returns false if it could not.
    [[nodiscard]] bool write_to(const std::filesystem::path& path) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(bytes_.data(), static_cast<std::streamsize>(bytes_.size()));
        return out.good();
    }

private:
    std::string bytes_;
};

/// A minimal well-formed GGUF: one architecture key and one tensor.
[[nodiscard]] inline std::string minimal_gguf(std::string_view architecture) {
    GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(1);
    builder.string_kv("general.architecture", architecture);
    builder.tensor("token_embd.weight");
    return builder.bytes();
}

/// A multimodal projector's header (26b): its vision and audio encoder
/// flags, and one vision tensor.
[[nodiscard]] inline std::string projector_gguf(bool vision, bool audio) {
    GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(3);
    builder.string_kv("general.architecture", "clip");
    builder.bool_kv("clip.has_vision_encoder", vision);
    builder.bool_kv("clip.has_audio_encoder", audio);
    builder.tensor("v.patch_embd.weight");
    return builder.bytes();
}

/// A header with Qwen3.8-27B's attention geometry (26a): trained for 262,144
/// positions, full attention every 4th of 64 blocks plus a prediction layer,
/// 4 key-value heads of 256. Its cache is 1,088 MiB at 32K and q8_0.
[[nodiscard]] inline std::string qwen38_like_gguf() {
    GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(9);
    builder.string_kv("general.architecture", "qwen35");
    builder.u32_kv("qwen35.context_length", 262144);
    builder.u32_kv("qwen35.block_count", 65);
    builder.u32_kv("qwen35.attention.head_count", 24);
    builder.u32_kv("qwen35.attention.head_count_kv", 4);
    builder.u32_kv("qwen35.attention.key_length", 256);
    builder.u32_kv("qwen35.attention.value_length", 256);
    builder.u32_kv("qwen35.full_attention_interval", 4);
    builder.u32_kv("qwen35.nextn_predict_layers", 1);
    builder.tensor("token_embd.weight");
    return builder.bytes();
}

/// A header with Gemma 4 12B's attention geometry (26m): 48 blocks, five
/// sliding layers (a 1,024-token window, 8 heads of 256) to one full (1 head
/// of 512). Its cache is 527 MiB at 32K and q8_0.
[[nodiscard]] inline std::string gemma4_like_gguf() {
    std::vector<std::int32_t> heads;
    std::vector<bool> slides;
    for (int layer = 0; layer < 48; ++layer) {
        const bool full = layer % 6 == 5;
        heads.push_back(full ? 1 : 8);
        slides.push_back(!full);
    }
    GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(11);
    builder.string_kv("general.architecture", "gemma4");
    builder.u32_kv("gemma4.context_length", 262144);
    builder.u32_kv("gemma4.block_count", 48);
    builder.u32_kv("gemma4.attention.head_count", 16);
    builder.i32_array_kv("gemma4.attention.head_count_kv", heads);
    builder.u32_kv("gemma4.attention.key_length", 512);
    builder.u32_kv("gemma4.attention.value_length", 512);
    builder.u32_kv("gemma4.attention.key_length_swa", 256);
    builder.u32_kv("gemma4.attention.value_length_swa", 256);
    builder.u32_kv("gemma4.attention.sliding_window", 1024);
    builder.bool_array_kv("gemma4.attention.sliding_window_pattern", slides);
    builder.tensor("token_embd.weight");
    return builder.bytes();
}

}  // namespace apogee::testing
