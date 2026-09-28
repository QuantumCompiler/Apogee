#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "harness/config.h"
#include "models/gguf_inspect.h"

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
/// `trained` or `fits` means unknown and limits nothing.
[[nodiscard]] std::int64_t default_local_window(std::int64_t trained, std::int64_t fits) noexcept;

/// The positions llama.cpp allocates for a window: rounded up to 256.
[[nodiscard]] std::int64_t allocated_positions(std::int64_t window) noexcept;

/// The values the cache keeps per position: over every layer that keeps a
/// cache of its own, its key-value heads times its key and value widths.
///
/// The layers are llama.cpp's: a hybrid model's recurrent layers keep none (a
/// Qwen3.5 or 3.8 has full attention every `full_attention_interval` layers,
/// its prediction layers after the main stack excluded), nor does a layer
/// with no key-value heads, nor one reusing an earlier layer's (Gemma's
/// `shared_kv_layers`). A sliding-window layer keeps a whole window like any
/// other -- contexts keep a full-size one -- at its own widths where the
/// header gives them. Zero for a model with no attention at all; nullopt when
/// the header does not say enough, or the attention is latent (DeepSeek's).
[[nodiscard]] std::optional<std::int64_t> kv_values_per_position(std::string_view architecture,
                                                                 const AttentionHeader& header);

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
