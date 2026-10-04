#include "modelstore/gguf_inspect.h"

#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
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

/// The largest skip taken by reading through the stream's buffer; anything
/// larger is a seek. A seek throws the buffer away, so the vocabulary's
/// strings, stepped over one seek each, cost a system call apiece: some
/// 300,000 for a 150,000-token model, and nearly all of `models list`'s 14
/// seconds on a 31-model store. Through the buffer they cost a few hundred
/// reads in all (M2, which this replaced a header cache with). An array of
/// scores, hundreds of kilobytes, is still cheaper as one seek.
constexpr std::uint64_t kBufferedSkip = std::uint64_t{64} * 1024U;

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
    Cursor(std::istream& in, std::uint64_t size) : in_{&in}, size_{size} {}

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
    /// of strings in every real model. A small skip reads through the buffer,
    /// a large one seeks: see `kBufferedSkip`.
    void skip(std::uint64_t count) {
        if (count > size_ || offset_ > size_ - count) {
            throw GgufError("header runs past the end of the file (truncated or corrupt)");
        }
        if (count <= kBufferedSkip) {
            in_->ignore(static_cast<std::streamsize>(count));
            if (std::cmp_not_equal(in_->gcount(), count) || !in_->good()) {
                throw GgufError("could not read the file");
            }
        } else {
            in_->seekg(static_cast<std::streamoff>(count), std::ios::cur);
            if (!in_->good()) {
                throw GgufError("could not seek within the file");
            }
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
    std::istream* in_;
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

/// The attention keys worth materialising, after the architecture's prefix.
constexpr std::array<std::string_view, 15> kAttentionKeys{
    "context_length",
    "block_count",
    "embedding_length",
    "attention.head_count",
    "attention.head_count_kv",
    "attention.key_length",
    "attention.value_length",
    "attention.key_length_swa",
    "attention.value_length_swa",
    "attention.sliding_window_pattern",
    "attention.sliding_window",
    "full_attention_interval",
    "nextn_predict_layers",
    "attention.shared_kv_layers",
    "attention.kv_lora_rank",
};

/// Whether `key` is one of `kAttentionKeys` under some prefix. The prefix is
/// the architecture, which the header may name later than these keys, so
/// they are gathered under every prefix and picked out at the end.
[[nodiscard]] bool is_attention_key(std::string_view key) noexcept {
    const std::size_t dot = key.find('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    const std::string_view rest = key.substr(dot + 1);
    for (const std::string_view wanted : kAttentionKeys) {
        if (rest == wanted) {
            return true;
        }
    }
    return false;
}

/// Reads one integer of `type`, or nullopt (having read nothing) for a type
/// that is not one.
[[nodiscard]] std::optional<std::int64_t> read_integer(Cursor& cursor, ValueType type) {
    switch (type) {
        case ValueType::UInt8:
        case ValueType::Bool:
            return cursor.number<std::uint8_t>();
        case ValueType::Int8:
            return cursor.number<std::int8_t>();
        case ValueType::UInt16:
            return cursor.number<std::uint16_t>();
        case ValueType::Int16:
            return cursor.number<std::int16_t>();
        case ValueType::UInt32:
            return cursor.number<std::uint32_t>();
        case ValueType::Int32:
            return cursor.number<std::int32_t>();
        case ValueType::UInt64:
            return static_cast<std::int64_t>(cursor.number<std::uint64_t>());
        case ValueType::Int64:
            return cursor.number<std::int64_t>();
        case ValueType::Float32:
        case ValueType::Float64:
        case ValueType::String:
        case ValueType::Array:
            return std::nullopt;
    }
    return std::nullopt;
}

/// A per-layer array is one entry per block; this bounds what is kept.
constexpr std::uint64_t kMaxLayerValues = 1U << 16U;

/// Reads an integer, a bool, or an array of either, as integers. Anything
/// else is stepped over and comes back empty.
/// Any scalar number as a double -- the sampling keys are floats in one
/// conversion and integers in another. Unset, the value stepped over, for a
/// string, an array or a bool.
[[nodiscard]] std::optional<double> read_number(Cursor& cursor, ValueType type) {
    if (type == ValueType::Float32) {
        return static_cast<double>(cursor.number<float>());
    }
    if (type == ValueType::Float64) {
        return cursor.number<double>();
    }
    if (type == ValueType::Bool || fixed_width(type) == 0) {
        skip_value(cursor, type);
        return std::nullopt;
    }
    return static_cast<double>(read_integer(cursor, type).value_or(0));
}

/// Reads one `general.sampling.*` value into `sampling`; false for a key that
/// is not one of the five it keeps, leaving the cursor where it was.
[[nodiscard]] bool read_sampling(Cursor& cursor, std::string_view key, ValueType type,
                                 GgufSampling& sampling) {
    constexpr std::string_view prefix = "general.sampling.";
    if (!key.starts_with(prefix)) {
        return false;
    }
    const std::string_view name = key.substr(prefix.size());
    std::optional<double>* slot = nullptr;
    if (name == "temp") {
        slot = &sampling.temperature;
    } else if (name == "top_p") {
        slot = &sampling.top_p;
    } else if (name == "min_p") {
        slot = &sampling.min_p;
    } else if (name == "penalty_repeat") {
        slot = &sampling.repeat_penalty;
    } else if (name != "top_k") {
        return false;
    }
    const std::optional<double> value = read_number(cursor, type);
    if (slot != nullptr) {
        *slot = value;
    } else if (value.has_value()) {
        sampling.top_k = static_cast<std::int64_t>(*value);
    }
    return true;
}

[[nodiscard]] std::vector<std::int64_t> read_integers(Cursor& cursor, ValueType type) {
    if (type != ValueType::Array) {
        if (fixed_width(type) == 0 || type == ValueType::Float32 || type == ValueType::Float64) {
            skip_value(cursor, type);
            return {};
        }
        return {*read_integer(cursor, type)};
    }
    const auto element = static_cast<ValueType>(cursor.number<std::uint32_t>());
    const auto count = cursor.number<std::uint64_t>();
    const std::size_t width = fixed_width(element);
    const bool integral =
        width > 0 && element != ValueType::Float32 && element != ValueType::Float64;
    if (!integral || count > kMaxLayerValues) {
        if (count > kMaxCount) {
            throw GgufError("a metadata array claims an implausible element count");
        }
        if (width == 0) {
            if (element == ValueType::Array) {
                throw GgufError("nested metadata arrays are not supported");
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
            return {};
        }
        cursor.skip(width * count);
        return {};
    }
    std::vector<std::int64_t> values;
    values.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        values.push_back(*read_integer(cursor, element));
    }
    return values;
}

/// The architecture's attention keys, out of everything gathered.
[[nodiscard]] AttentionHeader attention_of(
    const std::string& architecture,
    const std::map<std::string, std::vector<std::int64_t>, std::less<>>& gathered) {
    AttentionHeader out;
    const auto values = [&](std::string_view key) -> std::vector<std::int64_t> {
        const auto it = gathered.find(architecture + "." + std::string{key});
        return it == gathered.end() ? std::vector<std::int64_t>{} : it->second;
    };
    const auto one = [&](std::string_view key) -> std::int64_t {
        const std::vector<std::int64_t> found = values(key);
        return found.size() == 1 ? found.front() : 0;
    };
    if (architecture.empty()) {
        return out;
    }
    out.context_length = one("context_length");
    out.block_count = one("block_count");
    out.embedding_length = one("embedding_length");
    out.head_count = values("attention.head_count");
    out.head_count_kv = values("attention.head_count_kv");
    out.key_length = one("attention.key_length");
    out.value_length = one("attention.value_length");
    out.key_length_swa = one("attention.key_length_swa");
    out.value_length_swa = one("attention.value_length_swa");
    out.sliding_window_pattern = values("attention.sliding_window_pattern");
    out.sliding_window = one("attention.sliding_window");
    out.full_attention_interval = one("full_attention_interval");
    out.nextn_predict_layers = one("nextn_predict_layers");
    out.shared_kv_layers = one("attention.shared_kv_layers");
    out.latent_attention = gathered.contains(architecture + ".attention.kv_lora_rank");
    return out;
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
    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        GgufInfo info;
        info.parse_error = "cannot read '" + path.string() + "': " + code.message();
        return info;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        GgufInfo info;
        info.file_size = static_cast<std::int64_t>(size);
        info.parse_error = "cannot open '" + path.string() + "'";
        return info;
    }
    return inspect_gguf(in, size);
}

GgufInfo inspect_gguf(std::istream& in, std::uint64_t size) {
    GgufInfo info;
    info.file_size = static_cast<std::int64_t>(size);

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

        std::map<std::string, std::vector<std::int64_t>, std::less<>> attention;
        for (std::uint64_t i = 0; i < kv_count; ++i) {
            const std::string key = cursor.string();
            const auto type = static_cast<ValueType>(cursor.number<std::uint32_t>());

            if (type == ValueType::Bool &&
                (key == "clip.has_vision_encoder" || key == "clip.has_audio_encoder")) {
                const bool present = cursor.number<std::uint8_t>() != 0;
                (key == "clip.has_vision_encoder" ? info.projector_vision : info.projector_audio) =
                    present;
                continue;
            }
            if (key == "tokenizer.chat_template") {
                info.has_chat_template = true;
                if (type != ValueType::String) {
                    skip_value(cursor, type);
                    continue;
                }
                // Read only for what it says about reasoning (26i): whether
                // the model can be asked not to think, and whether it names
                // reasoning at all.
                const std::string text = cursor.string();
                info.template_thinking.switchable =
                    text.find("enable_thinking") != std::string::npos ||
                    text.find("reasoning_effort") != std::string::npos;
                info.template_thinking.reasons =
                    info.template_thinking.switchable ||
                    text.find("<think>") != std::string::npos ||
                    text.find("reasoning_content") != std::string::npos;
                continue;
            }
            if (is_attention_key(key)) {
                attention.insert_or_assign(key, read_integers(cursor, type));
                continue;
            }
            if (read_sampling(cursor, key, type, info.sampling)) {
                continue;
            }

            // Only these keys and the attention geometry are worth
            // materialising. Everything else is stepped over -- the vocabulary
            // alone is megabytes of strings, and reading it would turn a cheap
            // check into an expensive one.
            if (type == ValueType::UInt32 && key == "general.file_type") {
                info.file_type = cursor.number<std::uint32_t>();
                continue;
            }
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

        info.attention = attention_of(info.architecture, attention);
        info.parsed = true;
    } catch (const GgufError& e) {
        info.parsed = false;
        info.parse_error = e.what();
        // A partial read must not look like a partial success.
        info.tensors = 0;
        info.text_tensors = 0;
        info.file_type = kUnknownFileType;
        info.architecture.clear();
        info.name.clear();
        info.has_chat_template = false;
        info.template_thinking = {};
        info.projector_vision = false;
        info.projector_audio = false;
        info.sampling = {};
    }

    return info;
}

std::string base_model_note() {
    return "most likely a base (pretrained) model: it continues text rather than answering, so "
           "what it says can be confidently wrong, and it cannot use tools. For chat, use its "
           "instruction-tuned release, usually named '-it' or '-Instruct'";
}

}  // namespace apogee::models
