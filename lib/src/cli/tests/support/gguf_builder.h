#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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

}  // namespace apogee::testing
