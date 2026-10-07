#include "models/convert.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "modelstore/snapshot.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"
#include "support/mlx_model.h"

using apogee::harness::CancellationToken;
using apogee::models::convert_snapshot;
using apogee::models::ConvertFn;
using apogee::models::ConvertResult;

namespace {

void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

/// A SafeTensors shard: the 8-byte little-endian header length, the JSON
/// table of tensors, then zeroed data -- the layout the format specifies.
void write_shard(const std::filesystem::path& path,
                 const std::vector<std::pair<std::string, std::vector<std::int64_t>>>& tensors) {
    std::string table = R"({"__metadata__": {"format": "pt"})";
    std::int64_t offset = 0;
    for (const auto& [name, shape] : tensors) {
        std::int64_t elements = 1;
        std::string dims;
        for (const std::int64_t dimension : shape) {
            elements *= dimension;
            dims += (dims.empty() ? "" : ", ") + std::to_string(dimension);
        }
        table += R"(, ")" + name + R"(": {"dtype": "BF16", "shape": [)" + dims +
                 R"(], "data_offsets": [)" + std::to_string(offset) + ", " +
                 std::to_string(offset + elements * 2) + "]}";
        offset += elements * 2;
    }
    table += "}";
    std::string bytes;
    for (std::size_t i = 0; i < 8; ++i) {
        bytes += static_cast<char>((table.size() >> (8 * i)) & 0xFFU);
    }
    bytes += table;
    bytes += std::string(static_cast<std::size_t>(offset), '\0');
    write_file(path, bytes);
}

struct Fixture {
    apogee::testing::TempDir root{"convert-" + std::to_string(std::random_device{}())};
    std::filesystem::path snapshot = root.path() / "owner--repo";
    std::filesystem::path output = root.path() / "out" / "model-f16.gguf";

    Fixture() {
        write_file(snapshot / "config.json", R"({"architectures": ["LlamaForCausalLM"]})");
        write_shard(snapshot / "model.safetensors", {{"embed", {4, 8}}, {"norm", {8}}});
    }

    [[nodiscard]] bool partial_left() const {
        return std::filesystem::exists(apogee::models::conversion_partial_path(output));
    }
};

/// A converter that writes `bytes` where it is told to and returns `error`,
/// counting its calls and recording what it was given.
struct FakeConverter {
    std::string bytes = apogee::testing::minimal_gguf("llama");
    std::string error;
    int calls = 0;
    std::filesystem::path seen_snapshot;
    std::filesystem::path seen_gguf;

    [[nodiscard]] ConvertFn fn() {
        return [this](const std::filesystem::path& snapshot, const std::filesystem::path& gguf,
                      const CancellationToken&) {
            ++calls;
            seen_snapshot = snapshot;
            seen_gguf = gguf;
            write_file(gguf, bytes);
            return error;
        };
    }
};

}  // namespace

TEST_CASE("a conversion commits a verified GGUF and leaves no partial", "[models][convert]") {
    Fixture fixture;
    FakeConverter fake;
    const ConvertResult result =
        convert_snapshot(fixture.snapshot, fixture.output, fake.fn(), CancellationToken{});
    INFO(result.error);
    REQUIRE(result.ok);
    CHECK(result.path == fixture.output);
    CHECK(result.info.architecture == "llama");
    CHECK(std::filesystem::exists(fixture.output));
    CHECK_FALSE(fixture.partial_left());
    // The converter wrote beside the destination, never at it: a GGUF only
    // ever appears under its real name once it has been read back.
    CHECK(fake.seen_snapshot == fixture.snapshot);
    CHECK(fake.seen_gguf == apogee::models::conversion_partial_path(fixture.output));
}

TEST_CASE("a failed conversion leaves nothing behind, and says why", "[models][convert]") {
    Fixture fixture;
    FakeConverter fake;
    fake.bytes = "half a GGUF";
    fake.error = "Model FooForCausalLM is not supported";
    const ConvertResult result =
        convert_snapshot(fixture.snapshot, fixture.output, fake.fn(), CancellationToken{});
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.cancelled);
    CHECK(result.error == fake.error);
    CHECK_FALSE(std::filesystem::exists(fixture.output));
    CHECK_FALSE(fixture.partial_left());
}

TEST_CASE("output that is not a GGUF is refused and removed", "[models][convert]") {
    // A converter that exits 0 having written something unreadable must not
    // leave a file a backend would be pointed at.
    Fixture fixture;
    FakeConverter fake;
    fake.bytes = "definitely not a gguf";
    const ConvertResult result =
        convert_snapshot(fixture.snapshot, fixture.output, fake.fn(), CancellationToken{});
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("not a readable GGUF") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.output));
    CHECK_FALSE(fixture.partial_left());
}

TEST_CASE("a cancelled conversion removes its partial and says cancelled", "[models][convert]") {
    Fixture fixture;
    const CancellationToken token = CancellationToken::create();
    const ConvertFn cancelling = [&token](const std::filesystem::path&,
                                          const std::filesystem::path& gguf,
                                          const CancellationToken&) {
        write_file(gguf, "partway");
        token.cancel();  // Ctrl-C while the child was writing
        return std::string{"cancelled"};
    };
    const ConvertResult result =
        convert_snapshot(fixture.snapshot, fixture.output, cancelling, token);
    CHECK_FALSE(result.ok);
    CHECK(result.cancelled);
    CHECK_FALSE(std::filesystem::exists(fixture.output));
    CHECK_FALSE(fixture.partial_left());
}

TEST_CASE("every refusal comes before the converter runs", "[models][convert]") {
    SECTION("not a snapshot") {
        Fixture fixture;
        std::filesystem::remove(fixture.snapshot / "model.safetensors");
        FakeConverter fake;
        const ConvertResult result =
            convert_snapshot(fixture.snapshot, fixture.output, fake.fn(), CancellationToken{});
        CHECK(result.error.find("is not a SafeTensors snapshot") != std::string::npos);
        CHECK(fake.calls == 0);
    }
    SECTION("a config.json replaced by a download record") {
        // What `models pull --safetensors` left before 2026-09-23. The
        // converter would fail on it minutes in, pointing nowhere near the
        // cause; this says what happened and what fixes it.
        Fixture fixture;
        write_file(fixture.snapshot / "config.json",
                   R"({"file": "config.json", "file_digest": "ab", "ref": "o/r:config.json",
                       "source": "huggingface", "source_url": "https://x",
                       "verification": {"size_checked": true}})");
        CHECK(apogee::models::config_is_download_record(fixture.snapshot));
        FakeConverter fake;
        const ConvertResult result =
            convert_snapshot(fixture.snapshot, fixture.output, fake.fn(), CancellationToken{});
        CHECK(result.error.find("download record") != std::string::npos);
        CHECK(result.error.find("pull it again") != std::string::npos);
        CHECK(fake.calls == 0);
    }
    SECTION("an existing output") {
        Fixture fixture;
        write_file(fixture.output, "a model someone is using");
        FakeConverter fake;
        const ConvertResult result =
            convert_snapshot(fixture.snapshot, fixture.output, fake.fn(), CancellationToken{});
        CHECK(result.error.find("already exists") != std::string::npos);
        CHECK(fake.calls == 0);
        std::ifstream in{fixture.output, std::ios::binary};
        const std::string kept{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
        CHECK(kept == "a model someone is using");
    }
}

TEST_CASE("a real config.json is not mistaken for a download record", "[models][convert]") {
    const Fixture fixture;
    CHECK_FALSE(apogee::models::config_is_download_record(fixture.snapshot));
}

TEST_CASE("a snapshot's size is read from its shard headers alone", "[models][convert]") {
    const Fixture fixture;
    write_shard(fixture.snapshot / "model-00002.safetensors", {{"head", {3, 5}}});
    // 4*8 + 8 from the first shard, 3*5 from the second; __metadata__ is not
    // a tensor.
    CHECK(apogee::models::snapshot_elements(fixture.snapshot) == 55);

    write_file(fixture.snapshot / "broken.safetensors", "short");
    CHECK_FALSE(apogee::models::snapshot_elements(fixture.snapshot).has_value());

    const apogee::testing::TempDir empty{"convert-empty-" + std::to_string(std::random_device{}())};
    CHECK_FALSE(apogee::models::snapshot_elements(empty.path()).has_value());
}

TEST_CASE("the size estimate follows the precision", "[models][convert]") {
    using apogee::models::estimated_gguf_bytes;
    CHECK(estimated_gguf_bytes(3200, "f16") == 6400);
    CHECK(estimated_gguf_bytes(3200, "bf16") == 6400);
    CHECK(estimated_gguf_bytes(3200, "auto") == 6400);
    CHECK(estimated_gguf_bytes(3200, "f32") == 12800);
    CHECK(estimated_gguf_bytes(3200, "q8_0") == 3400);  // 34 bytes per block of 32
}

TEST_CASE("a model's encoders are what its config declares", "[models][convert][projector]") {
    const Fixture fixture;
    using apogee::models::snapshot_encoders;
    // The fixture's config names no encoder.
    CHECK_FALSE(snapshot_encoders(fixture.snapshot).any());

    write_file(fixture.snapshot / "config.json",
               R"({"architectures": ["Qwen3_5ForConditionalGeneration"],
                   "vision_config": {"depth": 27}, "language_model_only": false})");
    CHECK(snapshot_encoders(fixture.snapshot).vision);
    CHECK(snapshot_encoders(fixture.snapshot).reads() == "images");

    write_file(fixture.snapshot / "config.json",
               R"({"vision_config": {"depth": 1}, "audio_config": {"d_model": 8}})");
    CHECK(snapshot_encoders(fixture.snapshot).reads() == "images and audio");

    // Declared text-only, or declared empty: nothing to project.
    write_file(fixture.snapshot / "config.json",
               R"({"vision_config": {"depth": 1}, "language_model_only": true})");
    CHECK_FALSE(snapshot_encoders(fixture.snapshot).any());
    write_file(fixture.snapshot / "config.json", R"({"vision_config": null})");
    CHECK_FALSE(snapshot_encoders(fixture.snapshot).any());
    write_file(fixture.snapshot / "config.json", "not json");
    CHECK_FALSE(snapshot_encoders(fixture.snapshot).any());
}

TEST_CASE("an encoder's tensors are told apart for the size estimate",
          "[models][convert][projector]") {
    using apogee::models::is_encoder_tensor;
    CHECK(is_encoder_tensor("model.visual.blocks.0.attn.qkv.weight"));
    CHECK(is_encoder_tensor("vision_tower.encoder.layers.0.mlp.fc1.weight"));
    CHECK(is_encoder_tensor("multi_modal_projector.linear_1.weight"));
    CHECK(is_encoder_tensor("model.audio_tower.layers.0.self_attn.k_proj.weight"));
    CHECK(is_encoder_tensor("model.vision_embedder.patch_embedder.weight"));  // Gemma 4 unified
    CHECK(is_encoder_tensor("model.embed_audio.embedding_projection.weight"));
    CHECK_FALSE(is_encoder_tensor("model.language_model.layers.0.mlp.up_proj.weight"));
    CHECK_FALSE(is_encoder_tensor("mtp.fc.weight"));
    CHECK_FALSE(is_encoder_tensor("lm_head.weight"));

    const Fixture fixture;
    write_shard(fixture.snapshot / "model-00002.safetensors",
                {{"model.visual.patch_embed.proj.weight", {2, 3}}});
    // The fixture's first shard holds 40 text elements; this one 6 encoder ones.
    CHECK(apogee::models::snapshot_elements(fixture.snapshot) == 46);
    CHECK(apogee::models::snapshot_elements(fixture.snapshot, is_encoder_tensor) == 6);
}

TEST_CASE("a snapshot without a chat template is a base model", "[models][convert]") {
    const Fixture fixture;
    using apogee::models::snapshot_has_chat_template;
    CHECK_FALSE(snapshot_has_chat_template(fixture.snapshot));

    // Gemma 4 12B, the base release: a tokenizer config with no template.
    write_file(fixture.snapshot / "tokenizer_config.json", R"({"model_max_length": 262144})");
    CHECK_FALSE(snapshot_has_chat_template(fixture.snapshot));
    write_file(fixture.snapshot / "tokenizer_config.json", R"({"chat_template": null})");
    CHECK_FALSE(snapshot_has_chat_template(fixture.snapshot));

    // Where instruction-tuned releases keep theirs.
    write_file(fixture.snapshot / "tokenizer_config.json",
               R"({"chat_template": "{% for m in messages %}{{ m.content }}{% endfor %}"})");
    CHECK(snapshot_has_chat_template(fixture.snapshot));
    std::filesystem::remove(fixture.snapshot / "tokenizer_config.json");
    write_file(fixture.snapshot / "chat_template.jinja", "{{ messages }}");
    CHECK(snapshot_has_chat_template(fixture.snapshot));
}

// ---- into MLX (27b) -------------------------------------------------------------------

namespace {

/// A full-weight snapshot and a staging path, as `models convert --mlx`
/// hands the ladder.
struct MlxWork {
    apogee::testing::TempDir root{"convert-mlx-" + std::to_string(std::random_device{}())};
    std::filesystem::path snapshot = root.path() / "org--m" / "safetensors" / "aaaaaaaaaaaa";
    std::filesystem::path out = root.path() / "org--m" / "mlx" / ".incoming-0123456789ab";

    MlxWork() {
        apogee::testing::write_mlx_model(snapshot,
                                         apogee::testing::MlxModelSpec{.bits = 0, .format = "pt"});
    }
};

/// What `mlx_lm.convert` writes, played in process.
[[nodiscard]] apogee::models::MlxConvertFn writes(const apogee::testing::MlxModelSpec& spec) {
    return [spec](const std::filesystem::path&, const std::filesystem::path& out,
                  const CancellationToken&) {
        apogee::testing::write_mlx_model(out, spec);
        return std::string{};
    };
}

}  // namespace

TEST_CASE("a conversion into MLX lands a whole model, quantized as asked",
          "[models][convert][mlx]") {
    const MlxWork work;
    const apogee::models::MlxConvertResult result = apogee::models::convert_snapshot_to_mlx(
        work.snapshot, work.out, 4, writes({}), CancellationToken{});
    REQUIRE(result.ok);
    CHECK(result.error.empty());
    CHECK(result.info.complete);
    CHECK(result.info.quantization.bits == 4);
    CHECK(result.info.context_length == 131072);
    CHECK(std::filesystem::exists(work.out / "config.json"));
}

TEST_CASE("an MLX conversion that fails, stops or lands wrong leaves nothing",
          "[models][convert][mlx]") {
    const MlxWork work;
    SECTION("the converter fails, mid-write") {
        const apogee::models::MlxConvertResult result = apogee::models::convert_snapshot_to_mlx(
            work.snapshot, work.out, 4,
            [](const std::filesystem::path&, const std::filesystem::path& out,
               const CancellationToken&) {
                write_file(out / "model-00001-of-00002.safetensors", "half");
                return std::string{"ValueError: Model type gemma9 not supported."};
            },
            CancellationToken{});
        CHECK_FALSE(result.ok);
        CHECK_FALSE(result.cancelled);
        CHECK(result.error == "ValueError: Model type gemma9 not supported.");
    }
    SECTION("Ctrl-C mid-run") {
        const CancellationToken token = CancellationToken::create();
        const apogee::models::MlxConvertResult result = apogee::models::convert_snapshot_to_mlx(
            work.snapshot, work.out, 4,
            [&token](const std::filesystem::path&, const std::filesystem::path& out,
                     const CancellationToken&) {
                write_file(out / "model-00001-of-00002.safetensors", "half");
                token.cancel();
                return std::string{"cancelled"};
            },
            token);
        CHECK(result.cancelled);
        CHECK(result.error == "cancelled");
    }
    SECTION("a shard cut short") {
        const apogee::models::MlxConvertResult result = apogee::models::convert_snapshot_to_mlx(
            work.snapshot, work.out, 4,
            [](const std::filesystem::path&, const std::filesystem::path& out,
               const CancellationToken&) {
                apogee::testing::write_mlx_model(out);
                std::filesystem::resize_file(out / "model.safetensors", 40);
                return std::string{};
            },
            CancellationToken{});
        CHECK(result.error.starts_with(
            "the converter finished but its output is not a loadable MLX model -- cannot load: "
            "model.safetensors is truncated"));
    }
    SECTION("not quantized as asked") {
        const apogee::models::MlxConvertResult result = apogee::models::convert_snapshot_to_mlx(
            work.snapshot, work.out, 4, writes({.bits = 8}), CancellationToken{});
        CHECK(result.error ==
              "the converter finished but did not quantize to 4 bits (its "
              "config.json says 8-bit (affine, group 64))");
    }
    CHECK_FALSE(std::filesystem::exists(work.out));
    // The snapshot it read is untouched.
    CHECK(apogee::models::read_mlx_info(work.snapshot).complete);
}

TEST_CASE("every MLX refusal comes before the converter runs", "[models][convert][mlx]") {
    const MlxWork work;
    bool ran = false;
    const apogee::models::MlxConvertFn never = [&ran](const std::filesystem::path&,
                                                      const std::filesystem::path&,
                                                      const CancellationToken&) {
        ran = true;
        return std::string{};
    };
    const auto refusal = [&](const std::filesystem::path& snapshot) {
        return apogee::models::convert_snapshot_to_mlx(snapshot, work.out, 4, never,
                                                       CancellationToken{})
            .error;
    };
    CHECK(refusal(work.root.path() / "nothing").find("is not a SafeTensors snapshot") !=
          std::string::npos);

    // Already quantized by mlx-lm: it runs as it is.
    const std::filesystem::path quantized = work.root.path() / "quantized";
    apogee::testing::write_mlx_model(quantized);
    CHECK(refusal(quantized) == quantized.string() +
                                    " is already an MLX model (4-bit (affine, group 64)) -- an "
                                    "mlx backend runs it as it is; convert the full-weight release "
                                    "instead");

    // A publisher's own quantization is a full-weight release mlx-lm reads.
    write_file(work.snapshot / "config.json",
               R"({"model_type": "gpt_oss", "quantization_config": {"quant_method": "mxfp4"}})");
    CHECK(apogee::models::mlx_conversion_refusal(work.snapshot, work.out).empty());

    // Something already at the staging path.
    write_file(work.out / "x", "x");
    CHECK(refusal(work.snapshot).starts_with("something already exists at "));
    CHECK(std::filesystem::exists(work.out / "x"));  // and it stays
    CHECK_FALSE(ran);
}

TEST_CASE("the MLX size estimate follows the bits", "[models][convert][mlx]") {
    CHECK(apogee::models::estimated_mlx_bytes(1'000'000'000, 4) == 500'000'000 + 62'500'000);
    CHECK(apogee::models::estimated_mlx_bytes(1'000'000'000, 8) == 1'000'000'000 + 62'500'000);
    CHECK(apogee::models::estimated_mlx_bytes(1'000'000'000, 0) == 2'000'000'000);
}
