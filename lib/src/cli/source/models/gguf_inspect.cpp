#include "models/gguf_inspect.h"

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace apogee::models {
namespace {

/// GGUF metadata value types, from the container spec.
///
/// The base type mirrors the wire field's width on purpose, though the values
/// all fit in a byte: a corrupt file can declare type 256, and with a narrower
/// base that value would truncate to 0 (UInt8) and be silently accepted rather
/// than falling through to "unknown metadata value type".
// NOLINTNEXTLINE(performance-enum-size)
enum class ValueType : std::uint32_t {
    UInt8 = 0,
    Int8 = 1,
    UInt16 = 2,
    Int16 = 3,
    UInt32 = 4,
    Int32 = 5,
    Float32 = 6,
    Bool = 7,
    String = 8,
    Array = 9,
    UInt64 = 10,
    Int64 = 11,
    Float64 = 12,
};

/// Width of a fixed-size value, or 0 for the variable-length types.
[[nodiscard]] std::size_t fixed_width(ValueType type) noexcept {
    switch (type) {
        case ValueType::UInt8:
        case ValueType::Int8:
        case ValueType::Bool:
            return 1;
        case ValueType::UInt16:
        case ValueType::Int16:
            return 2;
        case ValueType::UInt32:
        case ValueType::Int32:
        case ValueType::Float32:
            return 4;
        case ValueType::UInt64:
        case ValueType::Int64:
        case ValueType::Float64:
            return 8;
        case ValueType::String:
        case ValueType::Array:
            return 0;
    }
    return 0;
}

/// Sanity ceilings. Every count in a GGUF is self-declared, so a corrupt file
/// can ask for an allocation the size of the address space. These bounds are
/// far above anything a real model uses and far below anything that hurts.
constexpr std::uint64_t kMaxStringBytes = 1U << 24U;  // 16 MiB
constexpr std::uint64_t kMaxCount = 1U << 24U;        // tensors, kv pairs, array elements

/// Signals a header that cannot be read. Caught in `inspect_gguf`; never
/// escapes this file.
class GgufError final : public std::runtime_error {
public:
    explicit GgufError(const std::string& what) : std::runtime_error(what) {}
};

/// A bounds-checked cursor over the file.
///
/// Every read goes through here so that "ran past the end" is reported once, in
/// one place, rather than being a different bug at each of the six call sites.
class Cursor {
public:
    Cursor(std::ifstream& in, std::uint64_t size) : in_{&in}, size_{size} {}

    void read(char* out, std::uint64_t count) {
        if (count > size_ || offset_ > size_ - count) {
            throw GgufError("header runs past the end of the file (truncated or corrupt)");
        }
        in_->read(out, static_cast<std::streamsize>(count));
        if (!in_->good()) {
            throw GgufError("could not read the file");
        }
        offset_ += count;
    }

    /// Advances without materialising the bytes -- used for values whose
    /// content is not needed, notably the token vocabulary, which is megabytes
    /// of strings in every real model.
    void skip(std::uint64_t count) {
        if (count > size_ || offset_ > size_ - count) {
            throw GgufError("header runs past the end of the file (truncated or corrupt)");
        }
        in_->seekg(static_cast<std::streamoff>(count), std::ios::cur);
        if (!in_->good()) {
            throw GgufError("could not seek within the file");
        }
        offset_ += count;
    }

    template <typename T>
    [[nodiscard]] T number() {
        static_assert(std::is_trivially_copyable_v<T>);
        std::array<char, sizeof(T)> bytes{};
        read(bytes.data(), sizeof(T));
        T value{};
        // GGUF is little-endian. Every platform Apogee targets is too, so this
        // is a copy rather than a byte swap -- but it goes through memcpy
        // rather than a reinterpret_cast, which would be undefined.
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }

    [[nodiscard]] std::string string() {
        const auto length = number<std::uint64_t>();
        if (length > kMaxStringBytes) {
            throw GgufError("a metadata string claims an implausible length");
        }
        std::string value(static_cast<std::size_t>(length), '\0');
        if (length > 0) {
            read(value.data(), length);
        }
        return value;
    }

private:
    std::ifstream* in_;
    std::uint64_t size_;
    std::uint64_t offset_ = 0;
};

/// Steps over one metadata value without keeping it.
void skip_value(Cursor& cursor, ValueType type) {
    if (const std::size_t width = fixed_width(type); width > 0) {
        cursor.skip(width);
        return;
    }
    if (type == ValueType::String) {
        const auto length = cursor.number<std::uint64_t>();
        if (length > kMaxStringBytes) {
            throw GgufError("a metadata string claims an implausible length");
        }
        cursor.skip(length);
        return;
    }
    if (type == ValueType::Array) {
        const auto element = static_cast<ValueType>(cursor.number<std::uint32_t>());
        const auto count = cursor.number<std::uint64_t>();
        if (count > kMaxCount) {
            throw GgufError("a metadata array claims an implausible element count");
        }
        if (element == ValueType::Array) {
            // The spec allows it; no real model emits it, and supporting it
            // would mean unbounded recursion driven by file content.
            throw GgufError("nested metadata arrays are not supported");
        }
        if (const std::size_t width = fixed_width(element); width > 0) {
            cursor.skip(width * count);
            return;
        }
        if (element != ValueType::String) {
            throw GgufError("unknown metadata array element type");
        }
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto length = cursor.number<std::uint64_t>();
            if (length > kMaxStringBytes) {
                throw GgufError("a metadata string claims an implausible length");
            }
            cursor.skip(length);
        }
        return;
    }
    throw GgufError("unknown metadata value type");
}

/// Whether a tensor name belongs to a vision tower or a multimodal projector
/// rather than to the text model.
///
/// The prefixes are llama.cpp's convention: `v.` for the vision tower, `mm.`
/// for the projector. A file with none of these is a plain text model, which is
/// the overwhelmingly common case and the one that must not be misreported.
[[nodiscard]] bool is_vision_tensor(std::string_view name) noexcept {
    return name.starts_with("v.") || name.starts_with("mm.") ||
           name.starts_with("multi_modal_projector.") || name.starts_with("vision_tower.");
}

}  // namespace

GgufInfo inspect_gguf(const std::filesystem::path& path) {
    GgufInfo info;

    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        info.parse_error = "cannot read '" + path.string() + "': " + code.message();
        return info;
    }
    info.file_size = static_cast<std::int64_t>(size);

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        info.parse_error = "cannot open '" + path.string() + "'";
        return info;
    }

    try {
        Cursor cursor{in, size};

        std::array<char, 4> magic{};
        cursor.read(magic.data(), magic.size());
        if (std::string_view{magic.data(), magic.size()} != "GGUF") {
            throw GgufError("not a GGUF file (missing the GGUF magic)");
        }

        info.version = cursor.number<std::uint32_t>();
        const auto tensor_count = cursor.number<std::uint64_t>();
        const auto kv_count = cursor.number<std::uint64_t>();
        if (tensor_count > kMaxCount || kv_count > kMaxCount) {
            throw GgufError("header claims an implausible tensor or metadata count");
        }
        info.tensors = static_cast<std::int64_t>(tensor_count);

        for (std::uint64_t i = 0; i < kv_count; ++i) {
            const std::string key = cursor.string();
            const auto type = static_cast<ValueType>(cursor.number<std::uint32_t>());

            // Only two keys are worth materialising. Everything else is stepped
            // over -- the vocabulary alone is megabytes of strings, and reading
            // it would turn a cheap check into an expensive one.
            if (type == ValueType::String &&
                (key == "general.architecture" || key == "general.name")) {
                std::string value = cursor.string();
                if (key == "general.architecture") {
                    info.architecture = std::move(value);
                } else {
                    info.name = std::move(value);
                }
                continue;
            }
            skip_value(cursor, type);
        }

        for (std::uint64_t i = 0; i < tensor_count; ++i) {
            const std::string name = cursor.string();
            const auto dimensions = cursor.number<std::uint32_t>();
            if (dimensions > 8) {
                throw GgufError("a tensor claims an implausible number of dimensions");
            }
            cursor.skip(std::uint64_t{8} * dimensions);  // dims
            cursor.skip(4);                              // ggml type
            cursor.skip(8);                              // data offset

            if (!is_vision_tensor(name)) {
                ++info.text_tensors;
            }
        }

        info.parsed = true;
    } catch (const GgufError& e) {
        info.parsed = false;
        info.parse_error = e.what();
        // A partial read must not look like a partial success.
        info.tensors = 0;
        info.text_tensors = 0;
        info.architecture.clear();
        info.name.clear();
    }

    return info;
}

}  // namespace apogee::models
