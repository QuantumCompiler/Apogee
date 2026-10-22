#include "models/convert.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "modelstore/snapshot.h"
#include "support/draft_head_configs.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"
#include "support/mlx_model.h"
#include "support/wan_original.h"

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

TEST_CASE("a draft head is refused by both ladders, whatever type it is converted as",
          "[models][convert][mlx][draft_head]") {
    // The user's Gemma4-12B-Assistant (2026-10-21): mlx-lm has no network for
    // it -- even as `gemma4_unified`, building one stops at 'sliding_attention'
    // -- and a GGUF of it is no backend, so neither ladder converts one.
    const MlxWork work;
    write_file(work.snapshot / "config.json",
               R"({"architectures": ["Gemma4UnifiedAssistantForCausalLM"],
                   "model_type": "gemma4_unified_assistant", "backbone_hidden_size": 3840})");
    const std::string mlx = apogee::models::mlx_conversion_refusal(work.snapshot, work.out);
    CHECK(mlx == work.snapshot.string() +
                     " is a draft head (Gemma4UnifiedAssistantForCausalLM): speculative decoding "
                     "runs it beside its base model (a 3840-wide gemma4 model), reading that "
                     "model's hidden states, and never alone -- nothing converted from it could "
                     "answer a chat; convert its base model instead");
    CHECK(apogee::models::conversion_refusal(work.snapshot, work.root.path() / "out.gguf") == mlx);
    // M15's override renames the type in a view; the weights stay a draft head.
    const std::filesystem::path overlay = work.root.path() / "overlay";
    REQUIRE(
        apogee::models::stage_model_type_overlay(work.snapshot, overlay, "gemma4_unified").empty());
    CHECK(apogee::models::mlx_conversion_refusal(overlay, work.out).find("is a draft head") !=
          std::string::npos);
}

TEST_CASE(
    "a draft head converts to MLX on the drafter's route alone; GGUF and every other route "
    "refuse it as before",
    "[models][convert][mlx][draft_head][drafter]") {
    // 53i: the draft-heads change's refusal lifted for mlx-vlm's drafter, the
    // route the command takes only where the installed mlx-vlm drafts it.
    for (const apogee::testing::DraftHeadPair& pair : apogee::testing::kUserDraftHeads) {
        INFO(pair.head_model);
        const MlxWork work;
        write_file(work.snapshot / "config.json", pair.head_config);
        const std::string kept = apogee::models::mlx_conversion_refusal(work.snapshot, work.out);
        CHECK(kept.find("is a draft head") != std::string::npos);
        for (const std::string_view other : {"chat", "vision", "embedding"}) {
            CHECK(apogee::models::mlx_conversion_refusal(work.snapshot, work.out, other) == kept);
        }
        CHECK(apogee::models::conversion_refusal(work.snapshot, work.root.path() / "out.gguf") ==
              kept);
        CHECK(apogee::models::mlx_conversion_refusal(work.snapshot, work.out, "drafter").empty());
        // Converted, it is recorded a drafter -- what the route names it.
        const apogee::models::MlxConvertResult made = apogee::models::convert_snapshot_to_mlx(
            work.snapshot, work.out, 0, writes({.bits = 0, .model_type = "gemma4_assistant"}), {},
            "drafter");
        INFO(made.error);
        REQUIRE(made.ok);
        CHECK(made.kind == "drafter");
    }
    // The drafter's route takes a draft head alone.
    const MlxWork plain;
    CHECK(apogee::models::mlx_conversion_refusal(plain.snapshot, plain.out, "drafter")
              .find("is no draft head: only a draft head converts to a drafter") !=
          std::string::npos);
}

TEST_CASE("a diffusion model is refused by both ladders before any converter runs",
          "[models][convert][mlx][diffusion]") {
    // The user's Wan2.2-TI2V-5B (M20, 2026-10-21): its MLX conversion ran
    // three seconds and failed in mlx-lm's own words -- the weights are a
    // diffusers transformer, which neither converter takes. Since 53j the
    // refusal says it is Wan's original layout, what it lacks and what runs
    // it; with no record, no repository is named.
    const MlxWork work;
    write_file(work.snapshot / "config.json", std::string{apogee::testing::kWanTi2vConfig});
    const std::string mlx = apogee::models::mlx_conversion_refusal(work.snapshot, work.out);
    CHECK(mlx == work.snapshot.string() +
                     " is Wan's original layout (WanModel), missing "
                     "models_t5_umt5-xxl-enc-bf16.pth and Wan2.2_VAE.pth -- pulling its "
                     "repository again with --safetensors fetches them into this set; neither "
                     "converter takes it -- llama.cpp's finds a model by its architectures, and "
                     "mlx-lm's builds a language model; what runs this model today is its "
                     "Diffusers pipeline in 'apogee video generate', pulled whole (a repository "
                     "with model_index.json at its root)");
    CHECK(apogee::models::conversion_refusal(work.snapshot, work.root.path() / "out.gguf") == mlx);
}

TEST_CASE("an embedding model is refused by the MLX ladder alone; its GGUF is the conversion",
          "[models][convert][mlx][embedding]") {
    // The user's Gemma-Embedding-300M (M21, 2026-10-21): mlx-lm's strict
    // load refused it after starting, and no MLX build of it could embed.
    const MlxWork work;
    write_file(work.snapshot / "config.json",
               R"({"architectures": ["Gemma3TextModel"], "model_type": "gemma3_text"})");
    write_file(work.snapshot / "modules.json",
               R"([{"idx": 0, "path": "", "type": "sentence_transformers.models.Transformer"},
                   {"idx": 1, "path": "1_Pooling",
                    "type": "sentence_transformers.models.Pooling"}])");
    CHECK(apogee::models::mlx_conversion_refusal(work.snapshot, work.out) ==
          work.snapshot.string() +
              " is an embedding model (sentence-transformers: Transformer, Pooling): it converts "
              "to a GGUF a llamacpp backend embeds with -- mlx-lm's converter builds language "
              "models, and the mlx backend does not embed -- convert it without --mlx");
    CHECK(apogee::models::conversion_refusal(work.snapshot, work.root.path() / "out.gguf").empty());
}

TEST_CASE("the MLX ladder lets an embedding model through for mlx-embeddings' route alone",
          "[models][convert][mlx][embedding]") {
    // 53h: the caller has found the installed mlx-embeddings builds the
    // set's type, and its route's kind is `embedding`; every other route
    // keeps M21's refusal, worded as it was.
    const MlxWork work;
    write_file(work.snapshot / "config.json",
               R"({"architectures": ["Gemma3TextModel"], "model_type": "gemma3_text"})");
    write_file(work.snapshot / "modules.json",
               R"([{"idx": 0, "path": "", "type": "sentence_transformers.models.Transformer"},
                   {"idx": 1, "path": "1_Pooling",
                    "type": "sentence_transformers.models.Pooling"}])");
    CHECK(apogee::models::mlx_conversion_refusal(work.snapshot, work.out, "embedding").empty());
    for (const char* kind : {"chat", "vision", ""}) {
        INFO(kind);
        CHECK(apogee::models::mlx_conversion_refusal(work.snapshot, work.out, kind)
                  .ends_with("and the mlx backend does not embed -- convert it without --mlx"));
    }
    // Converted through the route: committed as an embedding model.
    const apogee::models::MlxConvertResult result = apogee::models::convert_snapshot_to_mlx(
        work.snapshot, work.out, 8,
        writes({.bits = 8, .model_type = "gemma3_text", .kind = "embedding"}), {}, "embedding");
    INFO(result.error);
    REQUIRE(result.ok);
    CHECK(result.kind == "embedding");
    // A language model is not the embedder's to let through: nothing to lift.
    const MlxWork plain;
    CHECK(apogee::models::mlx_conversion_refusal(plain.snapshot, plain.out, "embedding").empty());
}

TEST_CASE("the MLX size estimate follows the bits", "[models][convert][mlx]") {
    CHECK(apogee::models::estimated_mlx_bytes(1'000'000'000, 4) == 500'000'000 + 62'500'000);
    CHECK(apogee::models::estimated_mlx_bytes(1'000'000'000, 8) == 1'000'000'000 + 62'500'000);
    CHECK(apogee::models::estimated_mlx_bytes(1'000'000'000, 0) == 2'000'000'000);
}

namespace {

[[nodiscard]] std::string read_all(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("a model_type overlay is the snapshot linked, its config.json alone written anew",
          "[models][convert][mlx][model_type]") {
    // M15: the view a conversion under --model-type reads. The snapshot is
    // never written; every other file is the snapshot's own, linked.
    const apogee::testing::TempDir dir{"model-type-overlay-" +
                                       std::to_string(std::random_device{}())};
    const std::filesystem::path snapshot = dir.path() / "snapshot";
    apogee::testing::write_mlx_model(
        snapshot, {.bits = 0, .model_type = "gemma4_unified_assistant", .format = "pt"});
    std::filesystem::create_directories(snapshot / "extra");
    std::ofstream{snapshot / "extra" / "notes.txt"} << "kept";
    const std::string config_before = read_all(snapshot / "config.json");
    CHECK(apogee::models::snapshot_model_type(snapshot) == "gemma4_unified_assistant");

    const std::filesystem::path overlay = dir.path() / "overlay";
    REQUIRE(apogee::models::stage_model_type_overlay(snapshot, overlay, "gemma4_unified").empty());
    CHECK(apogee::models::snapshot_model_type(overlay) == "gemma4_unified");
    CHECK(read_all(snapshot / "config.json") == config_before);
    // Every other file: there, the snapshot's own bytes -- linked, so no copy.
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(snapshot)) {
        if (!entry.is_regular_file() || entry.path().filename() == "config.json") {
            continue;
        }
        const std::filesystem::path twin = overlay / entry.path().lexically_relative(snapshot);
        INFO(twin.string());
        REQUIRE(std::filesystem::is_regular_file(twin));
        CHECK(std::filesystem::equivalent(entry.path(), twin));
        ++files;
    }
    CHECK(files >= 3);  // a shard, the tokenizer files, the notes
    CHECK_FALSE(std::filesystem::equivalent(snapshot / "config.json", overlay / "config.json"));
    // Everything but the type, kept.
    nlohmann::json was = nlohmann::json::parse(config_before);
    nlohmann::json now = nlohmann::json::parse(read_all(overlay / "config.json"));
    was.erase("model_type");
    now.erase("model_type");
    CHECK(now == was);

    // Removing the view leaves the snapshot whole.
    std::filesystem::remove_all(overlay);
    CHECK(read_all(snapshot / "extra" / "notes.txt") == "kept");
    CHECK(apogee::models::stage_model_type_overlay(dir.path() / "none", dir.path() / "o", "x") ==
          (dir.path() / "none").string() + "/config.json cannot be read as JSON");
}
