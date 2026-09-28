#include "models/kv_cache.h"

#include <algorithm>
#include <vector>

namespace apogee::models {
namespace {

/// Positions are allocated in multiples of this (llama.cpp pads `n_ctx`).
constexpr std::int64_t kPositionPadding = 256;

/// Families whose full-attention layers come every `full_attention_interval`
/// layers, the rest recurrent -- and 4 when the header leaves it out, as
/// llama.cpp reads them.
constexpr std::int64_t kDefaultAttentionInterval = 4;

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

std::optional<std::int64_t> kv_values_per_position(std::string_view architecture,
                                                   const AttentionHeader& header) {
    const std::int64_t layers = header.block_count;
    if (layers <= 0 || header.latent_attention) {
        return std::nullopt;
    }
    const bool hybrid = interval_hybrid(architecture);
    const std::int64_t main_layers =
        hybrid ? layers - std::max<std::int64_t>(header.nextn_predict_layers, 0) : layers;
    const std::int64_t interval = header.full_attention_interval > 0
                                      ? header.full_attention_interval
                                      : kDefaultAttentionInterval;
    const std::int64_t own_cache_layers =
        layers - std::clamp<std::int64_t>(header.shared_kv_layers, 0, layers);
    const bool sliding_widths = header.key_length_swa > 0 || header.value_length_swa > 0;
    // llama.cpp reads an absent key-value head count as the head count.
    const std::vector<std::int64_t>& kv_heads =
        header.head_count_kv.empty() ? header.head_count : header.head_count_kv;

    std::int64_t total = 0;
    for (std::int64_t layer = 0; layer < layers; ++layer) {
        if (hybrid && (layer >= main_layers || (layer + 1) % interval != 0)) {
            continue;
        }
        if (layer >= own_cache_layers) {
            continue;
        }
        const std::optional<std::int64_t> heads = per_layer(kv_heads, layer, layers);
        if (!heads.has_value()) {
            return std::nullopt;
        }
        if (*heads <= 0) {
            continue;
        }

        bool slides = false;
        if (sliding_widths) {
            const std::optional<bool> found = sliding(header.sliding_window_pattern, layer, layers);
            if (!found.has_value()) {
                return std::nullopt;
            }
            slides = *found;
        }
        std::int64_t key =
            slides && header.key_length_swa > 0 ? header.key_length_swa : header.key_length;
        std::int64_t value =
            slides && header.value_length_swa > 0 ? header.value_length_swa : header.value_length;
        if (key <= 0 || value <= 0) {
            // Absent, a width is the embedding over the (first layer's) heads.
            const std::optional<std::int64_t> query_heads = per_layer(header.head_count, 0, layers);
            if (!query_heads.has_value() || *query_heads <= 0 || header.embedding_length <= 0) {
                return std::nullopt;
            }
            const std::int64_t width = header.embedding_length / *query_heads;
            key = key > 0 ? key : width;
            value = value > 0 ? value : width;
        }
        total += *heads * (key + value);
    }
    return total;
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
    if (const std::optional<std::int64_t> values =
            kv_values_per_position(info.architecture, info.attention);
        values.has_value()) {
        out.cache_bytes = cache_bytes(*values * allocated_positions(out.window), out.cache_type);
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
