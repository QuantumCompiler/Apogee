#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/config.h"
#include "modelstore/gguf_inspect.h"

/// What a local model's conversation window is, and what its attention cache
/// costs -- worked out from the GGUF header alone, without loading a weight
/// (26a).
///
/// A context keeps a key and a value for every position of its window in every
/// attention layer, and llama.cpp allocates all of it up front. Left at the
/// trained window, that is the largest thing a local chat holds after the
/// weights: Qwen3.8-27B was trained for 262,144 positions, 8 GiB at `f16`.
/// `models info` and `check` state the cost here, and the llamacpp backend
/// sizes its window by the same rules, so the number shown is the number
/// allocated.
namespace apogee::models {

/// The window a local backend gets when its `context_size` is unset.
///
/// Predictable rather than clever: a long conversation compacts at 90% of it
/// (`agentloop/content.h`) instead of the cache taking gigabytes a user never
/// asked for.
inline constexpr std::int64_t kDefaultLocalWindow = 32768;

/// The cache type a local backend gets when its `cache_type` is unset.
inline constexpr harness::KvCacheType kDefaultCacheType = harness::KvCacheType::Q8_0;

/// The unset window: `kDefaultLocalWindow`, or the trained window when that is
/// smaller, or what free memory holds when that is smaller still. A zero in
/// `trained` or `fits` means unknown and limits nothing. `fits` comes from
/// the free memory of the devices a model offloads to, the one read
/// `backends::offload_memory_total` and `apogee system` share (32a).
[[nodiscard]] std::int64_t default_local_window(std::int64_t trained, std::int64_t fits) noexcept;

/// The positions llama.cpp allocates for a window: rounded up to 256.
[[nodiscard]] std::int64_t allocated_positions(std::int64_t window) noexcept;

/// What a context's cache keeps per position, by how long each part is kept.
struct CacheShape {
    /// Values per position in the layers that keep every position of the
    /// context.
    std::int64_t full = 0;
    /// Values per position in the sliding layers, which keep only their
    /// window and a batch (26m).
    std::int64_t sliding = 0;
    /// How far back a sliding layer looks; 0 when no layer slides.
    std::int64_t window = 0;
};

/// The cache's shape: over every layer that keeps a cache of its own, its
/// key-value heads times its key and value widths.
///
/// The layers are llama.cpp's: a hybrid model's recurrent layers keep none (a
/// Qwen3.5 or 3.8 has full attention every `full_attention_interval` layers,
/// its prediction layers after the main stack excluded), nor does a layer
/// with no key-value heads, nor one reusing an earlier layer's (Gemma's
/// `shared_kv_layers`). A layer slides only in a family llama.cpp runs with a
/// sliding window -- Gemma 2, 3 and 4 and gpt-oss, by the header's pattern or
/// llama.cpp's default one for the family -- and there at its own widths
/// where the header gives them. Any other family's layers are full: many
/// converters write `sliding_window` for every model whose config has one,
/// and llama.cpp ignores it for most. Zero for a model with no attention at
/// all; nullopt when the header does not say enough, or the attention is
/// latent (DeepSeek's).
[[nodiscard]] std::optional<CacheShape> cache_shape(std::string_view architecture,
                                                    const AttentionHeader& header);

/// The positions a sliding layer keeps in a context allocated `positions`:
/// its window and a batch (llama.cpp's default of 512), padded to 256, and
/// never more than the context -- llama.cpp's own rule
/// (`llama-kv-cache-iswa.cpp`).
[[nodiscard]] std::int64_t sliding_positions(std::int64_t positions, std::int64_t window) noexcept;

/// The values a context of `window` tokens allocates for a cache of `shape`.
[[nodiscard]] std::int64_t cache_values(const CacheShape& shape, std::int64_t window) noexcept;

/// What `values` cache values take at `type`: 2 bytes each at `f16`, and at
/// `q8_0` and `q4_0` their blocks of 32 -- 34 and 18 bytes.
[[nodiscard]] std::int64_t cache_bytes(std::int64_t values, harness::KvCacheType type) noexcept;

/// The window a local backend gets and what its cache costs, as `models info`
/// and `check` state them.
struct LocalWindow {
    /// In tokens: `context_size` when set, else the default.
    std::int64_t window = 0;
    /// Whether `context_size` set it.
    bool configured = false;
    /// What the model was trained for, or 0 when the header does not say.
    std::int64_t trained = 0;
    harness::KvCacheType cache_type = kDefaultCacheType;
    /// Bytes, for the positions llama.cpp allocates; nullopt when unknown.
    std::optional<std::int64_t> cache_bytes;
    /// The positions a sliding layer keeps, or 0 when none slides.
    std::int64_t sliding_positions = 0;
};

/// The window `backend` gets over the model `info` describes. Free memory can
/// lower an unset window further at load; that is not known here.
[[nodiscard]] LocalWindow local_window(const GgufInfo& info, const harness::BackendConfig& backend);

/// `bytes` in whole MiB, rounded to the nearest: "1088 MiB".
[[nodiscard]] std::string mib(std::int64_t bytes);

/// One line for `check`: "32768-token window, 1088 MiB q8_0 cache" -- "no
/// attention cache" for a model with none, and "cache size not known for this
/// architecture" where the header cannot size it.
[[nodiscard]] std::string describe(const LocalWindow& window);

}  // namespace apogee::models
