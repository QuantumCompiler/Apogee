#include "models/kv_cache.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "harness/config.h"
#include "models/gguf_inspect.h"
#include "support/gguf_builder.h"

/// The window a local model gets and what its cache costs (26a).
///
/// The per-model numbers below are llama.cpp's own: each geometry is the one
/// the named model's header carries, and each expected size is what llama.cpp
/// logged creating a 32,768-position context over that model at the pinned
/// revision (`llama_kv_cache: size = ...`), recorded in MILESTONES.
namespace {

using apogee::harness::BackendConfig;
using apogee::harness::KvCacheType;
using apogee::models::AttentionHeader;
using apogee::models::GgufInfo;
using Builder = apogee::testing::GgufBuilder;

constexpr std::int64_t kMiB = 1024LL * 1024;

/// What `header`'s cache takes in a context of 32,768 tokens, in bytes.
[[nodiscard]] std::int64_t bytes_at_32k(std::string_view architecture,
                                        const AttentionHeader& header, KvCacheType type) {
    const std::optional<apogee::models::CacheShape> shape =
        apogee::models::cache_shape(architecture, header);
    REQUIRE(shape.has_value());
    return apogee::models::cache_bytes(apogee::models::cache_values(*shape, 32768), type);
}

/// The same, in whole MiB.
[[nodiscard]] std::int64_t mib_at_32k(std::string_view architecture, const AttentionHeader& header,
                                      KvCacheType type) {
    const std::int64_t bytes = bytes_at_32k(architecture, header, type);
    CHECK(bytes % kMiB == 0);
    return bytes / kMiB;
}

/// Values per position over every layer, for a model none of whose layers
/// keep only a window.
[[nodiscard]] std::optional<std::int64_t> per_position(std::string_view architecture,
                                                       const AttentionHeader& header) {
    const std::optional<apogee::models::CacheShape> shape =
        apogee::models::cache_shape(architecture, header);
    if (!shape.has_value()) {
        return std::nullopt;
    }
    CHECK(shape->sliding == 0);
    return shape->full;
}

/// Qwen3.8-27B: 64 blocks and a prediction layer, full attention every 4th.
[[nodiscard]] AttentionHeader qwen38_27b() {
    AttentionHeader header;
    header.context_length = 262144;
    header.block_count = 65;
    header.embedding_length = 5120;
    header.head_count = {24};
    header.head_count_kv = {4};
    header.key_length = 256;
    header.value_length = 256;
    header.full_attention_interval = 4;
    header.nextn_predict_layers = 1;
    return header;
}

/// Qwen3-VL-8B: a plain stack of 36 attention layers.
[[nodiscard]] AttentionHeader qwen3vl_8b() {
    AttentionHeader header;
    header.context_length = 262144;
    header.block_count = 36;
    header.embedding_length = 4096;
    header.head_count = {32};
    header.head_count_kv = {8};
    header.key_length = 128;
    header.value_length = 128;
    return header;
}

/// Gemma 4 12B: five sliding layers, with a 1,024-token window, to one full,
/// each kind at its own widths and head count.
[[nodiscard]] AttentionHeader gemma4_12b() {
    AttentionHeader header;
    header.context_length = 262144;
    header.block_count = 48;
    header.embedding_length = 3840;
    header.head_count = {16};
    header.key_length = 512;
    header.value_length = 512;
    header.key_length_swa = 256;
    header.value_length_swa = 256;
    header.sliding_window = 1024;
    for (int layer = 0; layer < 48; ++layer) {
        const bool full = layer % 6 == 5;
        header.head_count_kv.push_back(full ? 1 : 8);
        header.sliding_window_pattern.push_back(full ? 0 : 1);
    }
    return header;
}

[[nodiscard]] GgufInfo inspect_bytes(const std::string& bytes, const std::string& name) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("apogee-kv-" + name + ".gguf");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const GgufInfo info = apogee::models::inspect_gguf(path);
    std::error_code code;
    std::filesystem::remove(path, code);
    return info;
}

}  // namespace

TEST_CASE("the header's attention geometry is read under its architecture", "[models][kv]") {
    // The architecture names the prefix, and a header may name it after the
    // keys it prefixes -- so they are gathered first and picked out after.
    Builder builder;
    builder.magic().u32(3).u64(1).u64(11);
    builder.u32_kv("qwen35.block_count", 65);
    builder.u32_kv("qwen35.context_length", 262144);
    builder.u32_kv("qwen35.embedding_length", 5120);
    builder.u32_kv("qwen35.attention.head_count", 24);
    builder.u32_kv("qwen35.attention.head_count_kv", 4);
    builder.u32_kv("qwen35.attention.key_length", 256);
    builder.u32_kv("qwen35.attention.value_length", 256);
    builder.u32_kv("qwen35.full_attention_interval", 4);
    // Another architecture's keys are not this model's.
    builder.u32_kv("llama.nextn_predict_layers", 7);
    // A key of the right name and the wrong kind is stepped over.
    builder.f32_kv("qwen35.nextn_predict_layers", 1.0F);
    builder.string_kv("general.architecture", "qwen35");
    builder.tensor("token_embd.weight");

    const GgufInfo info = inspect_bytes(builder.bytes(), "order");

    REQUIRE(info.parsed);
    CHECK(info.architecture == "qwen35");
    CHECK(info.tensors == 1);
    const AttentionHeader& header = info.attention;
    CHECK(header.context_length == 262144);
    CHECK(header.block_count == 65);
    CHECK(header.embedding_length == 5120);
    CHECK(header.head_count == std::vector<std::int64_t>{24});
    CHECK(header.head_count_kv == std::vector<std::int64_t>{4});
    CHECK(header.key_length == 256);
    CHECK(header.value_length == 256);
    CHECK(header.full_attention_interval == 4);
    CHECK(header.nextn_predict_layers == 0);
    CHECK_FALSE(header.latent_attention);
}

TEST_CASE("per-layer arrays are read, and latent attention is noticed", "[models][kv]") {
    Builder builder;
    builder.magic().u32(3).u64(1).u64(7);
    builder.string_kv("general.architecture", "gemma4");
    builder.u32_kv("gemma4.attention.sliding_window", 1024);
    builder.u32_kv("gemma4.block_count", 3);
    builder.i32_array_kv("gemma4.attention.head_count_kv", {8, 8, 1});
    builder.bool_array_kv("gemma4.attention.sliding_window_pattern", {true, true, false});
    builder.u32_kv("gemma4.attention.shared_kv_layers", 0);
    builder.u32_kv("gemma4.attention.kv_lora_rank", 512);
    builder.tensor("token_embd.weight");

    const GgufInfo info = inspect_bytes(builder.bytes(), "arrays");

    REQUIRE(info.parsed);
    CHECK(info.attention.head_count_kv == std::vector<std::int64_t>{8, 8, 1});
    CHECK(info.attention.sliding_window_pattern == std::vector<std::int64_t>{1, 1, 0});
    CHECK(info.attention.sliding_window == 1024);
    CHECK(info.attention.latent_attention);
}

TEST_CASE("a malformed attention key fails the read like any other", "[models][kv]") {
    Builder builder;
    builder.magic().u32(3).u64(0).u64(1);
    builder.text("llama.attention.head_count_kv");
    builder.u32(9);     // Array
    builder.u32(5);     // of Int32
    builder.u64(1000);  // claims more than the file holds

    const GgufInfo info = inspect_bytes(builder.bytes(), "short-array");

    CHECK_FALSE(info.parsed);
    CHECK(info.attention.block_count == 0);
    CHECK(info.attention.head_count_kv.empty());
}

TEST_CASE("the cache sizes are llama.cpp's own", "[models][kv]") {
    using apogee::models::cache_bytes;

    // Qwen3.8-27B: 16 full-attention layers of 64, the prediction layer none.
    CHECK(mib_at_32k("qwen35", qwen38_27b(), KvCacheType::Q8_0) == 1088);
    CHECK(mib_at_32k("qwen35", qwen38_27b(), KvCacheType::F16) == 2048);
    // The same layers in a MoE and in Qwen3-Next.
    CHECK(mib_at_32k("qwen35moe", qwen38_27b(), KvCacheType::Q8_0) == 1088);
    CHECK(mib_at_32k("qwen3next", qwen38_27b(), KvCacheType::Q8_0) == 1088);

    CHECK(mib_at_32k("qwen3vl", qwen3vl_8b(), KvCacheType::Q8_0) == 2448);
    CHECK(mib_at_32k("qwen3vl", qwen3vl_8b(), KvCacheType::F16) == 4608);

    // 8 full layers at 1 x (512 + 512) over 32,768 positions; 40 sliding at
    // 8 x (256 + 256) over their window and a batch, 1,536 (26m).
    CHECK(mib_at_32k("gemma4", gemma4_12b(), KvCacheType::Q8_0) == 272 + 255);
    CHECK(mib_at_32k("gemma4", gemma4_12b(), KvCacheType::F16) == 512 + 480);

    // gpt-oss-20b: every other layer slides, over a 128-token window -- 768
    // positions once the batch is added and padded. Its header names no
    // pattern; llama.cpp's for the family is every other layer.
    AttentionHeader gpt_oss;
    gpt_oss.block_count = 24;
    gpt_oss.head_count = {64};
    gpt_oss.head_count_kv = {8};
    gpt_oss.key_length = 64;
    gpt_oss.value_length = 64;
    gpt_oss.sliding_window = 128;
    CHECK(bytes_at_32k("gpt-oss", gpt_oss, KvCacheType::Q8_0) ==
          408 * kMiB + 12LL * 8 * 128 * 768 * 34 / 32);

    // Llama 3.1 8B.
    AttentionHeader llama;
    llama.block_count = 32;
    llama.head_count = {32};
    llama.head_count_kv = {8};
    llama.key_length = 128;
    llama.value_length = 128;
    CHECK(mib_at_32k("llama", llama, KvCacheType::Q8_0) == 2176);
    CHECK(mib_at_32k("llama", llama, KvCacheType::F16) == 4096);
    // q4_0: 18 bytes per 32 values.
    CHECK(cache_bytes(32 * 32768, KvCacheType::Q4_0) == 18 * 32768);
    CHECK(cache_bytes(32, KvCacheType::Q8_0) == 34);
    CHECK(cache_bytes(32, KvCacheType::F16) == 64);
}

TEST_CASE("which layers keep a cache follows llama.cpp", "[models][kv]") {
    using apogee::models::cache_shape;

    SECTION("a hybrid's interval defaults to 4, and only that family has one") {
        AttentionHeader header = qwen38_27b();
        header.full_attention_interval = 0;
        CHECK(per_position("qwen35", header) == 16 * 4 * 512);
        header.full_attention_interval = 2;
        CHECK(per_position("qwen35", header) == 32 * 4 * 512);
        // Another architecture carrying the key is a plain stack of 65.
        CHECK(per_position("llama", header) == 65 * 4 * 512);
    }

    SECTION("prediction layers past the main stack keep none on a hybrid") {
        AttentionHeader header = qwen38_27b();
        header.block_count = 8;
        header.nextn_predict_layers = 0;
        CHECK(per_position("qwen35", header) == 2 * 4 * 512);
        // With the last layer a prediction layer, layer 7 is past the stack.
        header.nextn_predict_layers = 1;
        CHECK(per_position("qwen35", header) == 1 * 4 * 512);
    }

    SECTION("layers reusing an earlier layer's cache keep none") {
        AttentionHeader header = qwen3vl_8b();
        header.shared_kv_layers = 6;
        CHECK(per_position("gemma4", header) == 30 * 8 * 256);
        header.shared_kv_layers = 100;
        CHECK(per_position("gemma4", header) == 0);
    }

    SECTION("a layer with no key-value heads keeps none") {
        AttentionHeader header = qwen3vl_8b();
        header.block_count = 4;
        header.head_count_kv = {0, 8, 0, 8};
        CHECK(per_position("jamba", header) == 2 * 8 * 256);
        header.head_count_kv = {0};
        CHECK(per_position("mamba", header) == 0);
    }

    SECTION("an absent key-value head count is the head count") {
        AttentionHeader header = qwen3vl_8b();
        header.head_count_kv.clear();
        CHECK(per_position("llama", header) == 36 * 32 * 256);
    }

    SECTION("absent widths are the embedding over the heads, each alone") {
        AttentionHeader header = qwen3vl_8b();
        // A value width that is not the fallback's, so a fallback that
        // overrode it would show.
        header.key_length = 0;
        header.value_length = 96;
        CHECK(per_position("llama", header) == 36 * 8 * (4096 / 32 + 96));
        header.value_length = 0;
        CHECK(per_position("llama", header) == 36 * 8 * 256);
        header.head_count = {0};
        CHECK_FALSE(per_position("llama", header).has_value());
        header.head_count = {32};
        header.embedding_length = 0;
        CHECK_FALSE(per_position("llama", header).has_value());
    }

    SECTION("a sliding pattern given as a period") {
        AttentionHeader header = gemma4_12b();
        header.head_count_kv = {8};
        header.sliding_window_pattern = {6};
        // Layers 5, 11, ... are full at 512 + 512; the other 40 slide.
        std::optional<apogee::models::CacheShape> shape = cache_shape("gemma3", header);
        REQUIRE(shape.has_value());
        CHECK(shape->full == 8 * 8 * 1024);
        CHECK(shape->sliding == 40 * 8 * 512);
        CHECK(shape->window == 1024);
        // Only the sliding widths differ: a width left out is the full one.
        header.value_length_swa = 0;
        shape = cache_shape("gemma3", header);
        REQUIRE(shape.has_value());
        CHECK(shape->sliding == 40 * 8 * 768);
        // Gemma 3's own pattern, when the header names none: every sixth full.
        header.sliding_window_pattern.clear();
        shape = cache_shape("gemma3", header);
        REQUIRE(shape.has_value());
        CHECK(shape->full == 8 * 8 * 1024);
    }

    SECTION("only the families llama.cpp runs with a sliding window slide") {
        // Converters write `sliding_window` for every model whose config has
        // one; llama.cpp ignores it for Qwen2, Mistral and the rest, whose
        // layers keep every position.
        AttentionHeader header = qwen3vl_8b();
        header.sliding_window = 4096;
        std::optional<apogee::models::CacheShape> shape = cache_shape("qwen2", header);
        REQUIRE(shape.has_value());
        CHECK(shape->full == 36 * 8 * 256);
        CHECK(shape->sliding == 0);
        CHECK(shape->window == 0);

        // Gemma 3 slides only once it has a window.
        header.sliding_window = 0;
        shape = cache_shape("gemma3", header);
        REQUIRE(shape.has_value());
        CHECK(shape->sliding == 0);

        // Gemma 2's window is 4,096 when the header leaves it out, and every
        // other layer slides: 18 of 36.
        shape = cache_shape("gemma2", header);
        REQUIRE(shape.has_value());
        CHECK(shape->window == 4096);
        CHECK(shape->full == 18 * 8 * 256);
        CHECK(shape->sliding == 18 * 8 * 256);

        // A family whose pattern says no layer slides has no window.
        AttentionHeader flat = gemma4_12b();
        flat.sliding_window_pattern.assign(48, 0);
        shape = cache_shape("gemma4", flat);
        REQUIRE(shape.has_value());
        CHECK(shape->sliding == 0);
        CHECK(shape->window == 0);
    }

    SECTION("what the header does not say is unknown, never a guess") {
        AttentionHeader header = gemma4_12b();
        header.sliding_window_pattern.clear();
        CHECK_FALSE(per_position("gemma4", header).has_value());

        header = qwen3vl_8b();
        header.head_count_kv = {8, 8, 8};  // neither one value nor one per layer
        CHECK_FALSE(per_position("llama", header).has_value());

        header = qwen3vl_8b();
        header.latent_attention = true;
        CHECK_FALSE(per_position("deepseek2", header).has_value());

        header = qwen3vl_8b();
        header.block_count = 0;
        CHECK_FALSE(per_position("llama", header).has_value());
    }
}

TEST_CASE("the unset window is 32K, the trained window when smaller, or what fits",
          "[models][kv]") {
    using apogee::models::default_local_window;

    CHECK(default_local_window(262144, 0) == 32768);
    CHECK(default_local_window(32768, 0) == 32768);
    CHECK(default_local_window(4096, 0) == 4096);
    CHECK(default_local_window(0, 0) == 32768);
    CHECK(default_local_window(262144, 20000) == 20000);
    CHECK(default_local_window(262144, 100000) == 32768);
    CHECK(default_local_window(8192, 4096) == 4096);
    CHECK(default_local_window(4096, 8192) == 4096);
}

TEST_CASE("positions are allocated in multiples of 256", "[models][kv]") {
    using apogee::models::allocated_positions;

    CHECK(allocated_positions(32768) == 32768);
    CHECK(allocated_positions(5000) == 5120);
    CHECK(allocated_positions(256) == 256);
    CHECK(allocated_positions(257) == 512);
    CHECK(allocated_positions(1) == 256);
    CHECK(allocated_positions(0) == 0);
}

TEST_CASE("a sliding layer keeps its window and a batch, padded, never past the context",
          "[models][kv]") {
    // llama.cpp's rule (llama-kv-cache-iswa.cpp), checked against its own
    // sizes: 1,536 positions on Gemma 4, 768 on gpt-oss.
    using apogee::models::sliding_positions;

    CHECK(sliding_positions(32768, 1024) == 1536);
    CHECK(sliding_positions(32768, 128) == 768);
    CHECK(sliding_positions(1024, 1024) == 1024);
    CHECK(sliding_positions(512, 4096) == 512);
    CHECK(sliding_positions(32768, 0) == 0);

    apogee::models::CacheShape shape;
    shape.full = 10;
    shape.sliding = 3;
    shape.window = 1024;
    CHECK(apogee::models::cache_values(shape, 32768) == 10 * 32768 + 3 * 1536);
    CHECK(apogee::models::cache_values(shape, 5000) == 10 * 5120 + 3 * 1536);
    CHECK(apogee::models::cache_values(shape, 1000) == 10 * 1024 + 3 * 1024);
}

TEST_CASE("a backend's window and cache are stated as they will be allocated", "[models][kv]") {
    GgufInfo info;
    info.parsed = true;
    info.architecture = "qwen35";
    info.attention = qwen38_27b();

    BackendConfig backend;
    backend.type = apogee::harness::BackendType::LlamaCpp;

    const apogee::models::LocalWindow unset = apogee::models::local_window(info, backend);
    CHECK(unset.window == 32768);
    CHECK_FALSE(unset.configured);
    CHECK(unset.trained == 262144);
    CHECK(unset.cache_type == KvCacheType::Q8_0);
    CHECK(unset.cache_bytes == 1088 * kMiB);

    // An explicit context_size is the window, exactly as written; the cache
    // is what llama.cpp allocates for it.
    backend.context_size = 5000;
    backend.cache_type = KvCacheType::F16;
    const apogee::models::LocalWindow set = apogee::models::local_window(info, backend);
    CHECK(set.window == 5000);
    CHECK(set.configured);
    CHECK(set.cache_type == KvCacheType::F16);
    CHECK(set.cache_bytes == 16LL * 4 * 512 * 5120 * 2);

    // Past the default, too: the user knows something Apogee does not.
    backend.context_size = 131072;
    CHECK(apogee::models::local_window(info, backend).window == 131072);

    CHECK(set.sliding_positions == 0);

    info.attention.latent_attention = true;
    CHECK_FALSE(apogee::models::local_window(info, backend).cache_bytes.has_value());

    // A Gemma 4: its sliding layers keep their window, and say so.
    GgufInfo gemma;
    gemma.parsed = true;
    gemma.architecture = "gemma4";
    gemma.attention = gemma4_12b();
    const apogee::models::LocalWindow sliding =
        apogee::models::local_window(gemma, BackendConfig{});
    CHECK(sliding.window == 32768);
    CHECK(sliding.sliding_positions == 1536);
    CHECK(sliding.cache_bytes == (272 + 255) * kMiB);
}
