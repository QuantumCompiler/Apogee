#include "models/kv_cache.h"

#include <algorithm>
#include <array>
#include <vector>

namespace apogee::models {
namespace {

/// Positions are allocated in multiples of this (llama.cpp pads `n_ctx`).
constexpr std::int64_t kPositionPadding = 256;

/// Families whose full-attention layers come every `full_attention_interval`
/// layers, the rest recurrent -- and 4 when the header leaves it out, as
/// llama.cpp reads them.
constexpr std::int64_t kDefaultAttentionInterval = 4;

/// The positions one decode batch carries (llama.cpp's `n_ubatch`, left at its
/// default): a sliding layer keeps this many past its window.
constexpr std::int64_t kMicroBatch = 512;

/// A family llama.cpp runs with a sliding window, and what it assumes when
/// the header leaves something out (`load_swa_pattern` and each family's
/// hparams in `src/models/`).
struct SlidingFamily {
    std::string_view architecture;
    /// Every this-many layers the last is full; 0 when the header must say.
    std::int64_t default_period = 0;
    /// The window when the header names none; 0 when the header must say.
    std::int64_t default_window = 0;
};

constexpr std::array<SlidingFamily, 4> kSlidingFamilies{{
    {.architecture = "gemma2", .default_period = 2, .default_window = 4096},
    {.architecture = "gemma3", .default_period = 6, .default_window = 0},
    {.architecture = "gemma4", .default_period = 0, .default_window = 0},
    {.architecture = "gpt-oss", .default_period = 2, .default_window = 0},
}};

[[nodiscard]] const SlidingFamily* sliding_family(std::string_view architecture) noexcept {
    for (const SlidingFamily& family : kSlidingFamilies) {
        if (family.architecture == architecture) {
            return &family;
        }
    }
    return nullptr;
}

[[nodiscard]] bool interval_hybrid(std::string_view architecture) noexcept {
    return architecture == "qwen35" || architecture == "qwen35moe" || architecture == "qwen3next";
}

/// Layer `layer`'s value from a key that holds one value for every layer or
/// one per layer; nullopt when it holds neither.
[[nodiscard]] std::optional<std::int64_t> per_layer(const std::vector<std::int64_t>& values,
                                                    std::int64_t layer, std::int64_t layers) {
    if (values.size() == 1) {
        return values.front();
    }
    if (static_cast<std::int64_t>(values.size()) == layers) {
        return values[static_cast<std::size_t>(layer)];
    }
    return std::nullopt;
}

/// Whether layer `layer` slides: a flag per layer, or a period in which every
/// layer but the last does (llama.cpp's `set_swa_pattern`).
[[nodiscard]] std::optional<bool> sliding(const std::vector<std::int64_t>& pattern,
                                          std::int64_t layer, std::int64_t layers) {
    if (static_cast<std::int64_t>(pattern.size()) == layers && layers > 1) {
        return pattern[static_cast<std::size_t>(layer)] != 0;
    }
    if (pattern.size() == 1) {
        const std::int64_t period = pattern.front();
        return period <= 0 || layer % period < period - 1;
    }
    return std::nullopt;
}

/// Which of a model's layers keep a cache, and which of those slide -- as
/// llama.cpp builds them.
class Layers {
public:
    Layers(std::string_view architecture, const AttentionHeader& header)
        : count_{header.block_count},
          hybrid_{interval_hybrid(architecture)},
          main_{hybrid_ ? count_ - std::max<std::int64_t>(header.nextn_predict_layers, 0) : count_},
          interval_{header.full_attention_interval > 0 ? header.full_attention_interval
                                                       : kDefaultAttentionInterval},
          own_cache_{count_ - std::clamp<std::int64_t>(header.shared_kv_layers, 0, count_)} {
        // Only a family llama.cpp runs with a sliding window slides, and only
        // once it has one.
        if (const SlidingFamily* family = sliding_family(architecture); family != nullptr) {
            window_ = header.sliding_window > 0 ? header.sliding_window : family->default_window;
            pattern_ = header.sliding_window_pattern;
            if (pattern_.empty() && family->default_period > 0) {
                pattern_ = {family->default_period};
            }
        }
    }

    /// How far back a sliding layer looks; 0 when none can.
    [[nodiscard]] std::int64_t window() const noexcept {
        return window_;
    }

    /// Whether `layer` keeps a cache of its own: not a hybrid's recurrent or
    /// prediction layer, and not one sharing an earlier layer's.
    [[nodiscard]] bool keeps_cache(std::int64_t layer) const noexcept {
        if (hybrid_ && (layer >= main_ || (layer + 1) % interval_ != 0)) {
            return false;
        }
        return layer < own_cache_;
    }

    /// Whether `layer` slides; nullopt when the pattern cannot say.
    [[nodiscard]] std::optional<bool> slides(std::int64_t layer) const {
        if (window_ <= 0) {
            return false;
        }
        return sliding(pattern_, layer, count_);
    }

private:
    std::int64_t count_;
    bool hybrid_;
    std::int64_t main_;
    std::int64_t interval_;
    std::int64_t own_cache_;
    std::int64_t window_ = 0;
    std::vector<std::int64_t> pattern_;
};

/// A layer's key width plus its value width: a sliding layer's own where the
/// header gives them, and an absent one the embedding over the (first
/// layer's) heads. Nullopt when neither says.
[[nodiscard]] std::optional<std::int64_t> key_and_value(const AttentionHeader& header,
                                                        bool slides) {
    std::int64_t key =
        slides && header.key_length_swa > 0 ? header.key_length_swa : header.key_length;
    std::int64_t value =
        slides && header.value_length_swa > 0 ? header.value_length_swa : header.value_length;
    if (key <= 0 || value <= 0) {
        const std::optional<std::int64_t> query_heads =
            per_layer(header.head_count, 0, header.block_count);
        if (!query_heads.has_value() || *query_heads <= 0 || header.embedding_length <= 0) {
            return std::nullopt;
        }
        const std::int64_t width = header.embedding_length / *query_heads;
        key = key > 0 ? key : width;
        value = value > 0 ? value : width;
    }
    return key + value;
}

}  // namespace

std::int64_t default_local_window(std::int64_t trained, std::int64_t fits) noexcept {
    std::int64_t window = kDefaultLocalWindow;
    if (trained > 0) {
        window = std::min(window, trained);
    }
    if (fits > 0) {
        window = std::min(window, fits);
    }
    return window;
}

std::int64_t allocated_positions(std::int64_t window) noexcept {
    if (window <= 0) {
        return 0;
    }
    return (window + kPositionPadding - 1) / kPositionPadding * kPositionPadding;
}

std::optional<CacheShape> cache_shape(std::string_view architecture,
                                      const AttentionHeader& header) {
    if (header.block_count <= 0 || header.latent_attention) {
        return std::nullopt;
    }
    const Layers layers{architecture, header};
    // llama.cpp reads an absent key-value head count as the head count.
    const std::vector<std::int64_t>& kv_heads =
        header.head_count_kv.empty() ? header.head_count : header.head_count_kv;

    CacheShape shape;
    shape.window = layers.window();
    for (std::int64_t layer = 0; layer < header.block_count; ++layer) {
        if (!layers.keeps_cache(layer)) {
            continue;
        }
        const std::optional<std::int64_t> heads = per_layer(kv_heads, layer, header.block_count);
        if (!heads.has_value()) {
            return std::nullopt;
        }
        if (*heads <= 0) {
            continue;
        }
        const std::optional<bool> slides = layers.slides(layer);
        if (!slides.has_value()) {
            return std::nullopt;
        }
        const std::optional<std::int64_t> widths = key_and_value(header, *slides);
        if (!widths.has_value()) {
            return std::nullopt;
        }
        (*slides ? shape.sliding : shape.full) += *heads * *widths;
    }
    if (shape.sliding == 0) {
        shape.window = 0;
    }
    return shape;
}

std::int64_t sliding_positions(std::int64_t positions, std::int64_t window) noexcept {
    if (window <= 0) {
        return 0;
    }
    return allocated_positions(std::min(positions, window + kMicroBatch));
}

std::int64_t cache_values(const CacheShape& shape, std::int64_t window) noexcept {
    const std::int64_t positions = allocated_positions(window);
    return (shape.full * positions) + (shape.sliding * sliding_positions(positions, shape.window));
}

std::int64_t cache_bytes(std::int64_t values, harness::KvCacheType type) noexcept {
    switch (type) {
        case harness::KvCacheType::F16:
            return values * 2;
        case harness::KvCacheType::Q8_0:
            return values * 34 / 32;
        case harness::KvCacheType::Q4_0:
            return values * 18 / 32;
    }
    return values * 2;
}

LocalWindow local_window(const GgufInfo& info, const harness::BackendConfig& backend) {
    LocalWindow out;
    out.trained = info.attention.context_length;
    out.cache_type = backend.cache_type.value_or(kDefaultCacheType);
    if (backend.context_size.has_value() && *backend.context_size > 0) {
        out.window = *backend.context_size;
        out.configured = true;
    } else {
        out.window = default_local_window(out.trained, 0);
    }
    if (const std::optional<CacheShape> shape = cache_shape(info.architecture, info.attention);
        shape.has_value()) {
        out.cache_bytes = cache_bytes(cache_values(*shape, out.window), out.cache_type);
        out.sliding_positions = sliding_positions(allocated_positions(out.window), shape->window);
    }
    return out;
}

std::string mib(std::int64_t bytes) {
    constexpr std::int64_t kMiB = std::int64_t{1024} * 1024;
    return std::to_string((bytes + kMiB / 2) / kMiB) + " MiB";
}

std::string describe(const LocalWindow& window) {
    std::string out = std::to_string(window.window) + "-token window, ";
    if (!window.cache_bytes.has_value()) {
        return out + "cache size not known for this architecture";
    }
    if (*window.cache_bytes == 0) {
        return out + "no attention cache";
    }
    return out + mib(*window.cache_bytes) + " " +
           std::string{harness::to_string(window.cache_type)} + " cache";
}

}  // namespace apogee::models
