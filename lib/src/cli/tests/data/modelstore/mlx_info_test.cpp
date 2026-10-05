#include "modelstore/mlx_info.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>

#include "contracts/config.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"

/// The MLX directory reader (27b), over directories built in the test: what
/// a whole model says, and -- the dishonesty guards -- every way one cannot
/// load said as "cannot load" with its reason, never a guess filling the gap.
namespace {

using apogee::models::MlxInfo;
using apogee::models::read_mlx_info;
using apogee::testing::MlxModelSpec;
using apogee::testing::write_fixture_file;
using apogee::testing::write_mlx_model;

struct Dir {
    apogee::testing::TempDir root{"mlx-info-" + std::to_string(std::random_device{}())};
    std::filesystem::path model = root.path() / "model";
};

}  // namespace

TEST_CASE("a whole quantized model reads its window, its quantization and its shards",
          "[models][mlx][info]") {
    const Dir dir;
    write_mlx_model(dir.model, MlxModelSpec{.shards = 2});
    const MlxInfo info = read_mlx_info(dir.model);
    CHECK(info.complete);
    CHECK(info.problem.empty());
    CHECK(info.mlx_format);
    CHECK(info.model_type == "llama");
    CHECK(info.architecture == "LlamaForCausalLM");
    CHECK(info.context_length == 131072);
    CHECK(info.context_key == "max_position_embeddings");
    CHECK(info.quantization.bits == 4);
    CHECK(info.quantization.group_size == 64);
    CHECK_FALSE(info.quantization.mixed);
    CHECK(info.quantization.dtype == "bf16");
    CHECK(info.quantization.describe() == "4-bit (affine, group 64)");
    CHECK(info.quantization.label() == "4bit");
    REQUIRE(info.shards.size() == 2);
    // The configuration alone: the same facts, no shard opened.
    const MlxInfo config = apogee::models::read_mlx_config(dir.model);
    CHECK(config.config_read);
    CHECK_FALSE(config.complete);
    CHECK(config.context_length == 131072);
    CHECK(config.quantization.bits == 4);
    CHECK(config.shards.empty());
    CHECK(info.shards.at(0).name == "model-00001-of-00002.safetensors");
    CHECK(info.shards.at(1).name == "model-00002-of-00002.safetensors");
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir.model)) {
        if (entry.is_regular_file()) {
            total += entry.file_size();
        }
    }
    CHECK(info.bytes == total);
    CHECK(apogee::models::is_mlx_model_dir(dir.model));
}

TEST_CASE("no config.json cannot load, and nothing is guessed in its place",
          "[models][mlx][info][honesty]") {
    const Dir dir;
    write_mlx_model(dir.model);
    std::filesystem::remove(dir.model / "config.json");
    const MlxInfo info = read_mlx_info(dir.model);
    CHECK_FALSE(info.complete);
    CHECK(info.problem == "cannot load: no config.json in " + dir.model.string());
    CHECK(info.context_length == 0);
    CHECK(info.model_type.empty());
    CHECK_FALSE(info.quantization.quantized());
    CHECK_FALSE(info.mlx_format);
    // What is on disk is still counted: a listing shows its size.
    CHECK(info.shards.size() == 1);
    CHECK(info.bytes > 0);
    CHECK_FALSE(apogee::models::is_mlx_model_dir(dir.model));

    CHECK(read_mlx_info(dir.root.path() / "absent").problem ==
          "cannot load: " + (dir.root.path() / "absent").string() + " is not a directory");
}

TEST_CASE("each file a load needs, missing or broken, is named as the reason",
          "[models][mlx][info][honesty]") {
    const Dir dir;
    SECTION("config.json that is not JSON") {
        write_mlx_model(dir.model);
        write_fixture_file(dir.model / "config.json", "{ not json");
        CHECK(read_mlx_info(dir.model).problem == "cannot load: config.json is not a JSON object");
    }
    SECTION("config.json replaced by a download record") {
        write_mlx_model(dir.model);
        write_fixture_file(dir.model / "config.json",
                           R"({"source_url": "x", "verification": {}, "file_digest": "y"})");
        CHECK(read_mlx_info(dir.model).problem.find("an Apogee download record") !=
              std::string::npos);
    }
    SECTION("a shard cut short: the header promises more than the file holds") {
        write_mlx_model(dir.model);
        const std::filesystem::path shard = dir.model / "model.safetensors";
        std::filesystem::resize_file(shard, std::filesystem::file_size(shard) - 10);
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK_FALSE(info.complete);
        CHECK(
            info.problem.starts_with("cannot load: model.safetensors is truncated: its header "
                                     "describes "));
    }
    SECTION("a shard cut inside its header") {
        write_mlx_model(dir.model);
        std::filesystem::resize_file(dir.model / "model.safetensors", 20);
        CHECK(read_mlx_info(dir.model).problem.find("model.safetensors is truncated") !=
              std::string::npos);
    }
    SECTION("a shard listing a tensor without its offsets") {
        write_mlx_model(dir.model);
        const std::string header = R"({"w":{"dtype":"F16","shape":[2]}})";
        std::string bytes;
        for (int i = 0; i < 8; ++i) {
            bytes += static_cast<char>(i == 0 ? header.size() : 0);
        }
        write_fixture_file(dir.model / "model.safetensors", bytes + header);
        CHECK(read_mlx_info(dir.model).problem ==
              "cannot load: model.safetensors lists tensor 'w' without its data offsets");
    }
    SECTION("a shard the index names, never arrived") {
        write_mlx_model(dir.model, MlxModelSpec{.shards = 3});
        std::filesystem::remove(dir.model / "model-00002-of-00003.safetensors");
        CHECK(read_mlx_info(dir.model).problem ==
              "cannot load: model-00002-of-00003.safetensors is missing "
              "(model.safetensors.index.json names it)");
    }
    SECTION("no weights at all") {
        write_mlx_model(dir.model);
        std::filesystem::remove(dir.model / "model.safetensors");
        CHECK(read_mlx_info(dir.model).problem ==
              "cannot load: no weights (*.safetensors) in " + dir.model.string());
    }
    SECTION("no tokenizer") {
        write_mlx_model(dir.model, MlxModelSpec{.tokenizer = false});
        CHECK(read_mlx_info(dir.model).problem.starts_with("cannot load: no tokenizer"));
    }
}

TEST_CASE("the window is read where a composite model declares it, and said when absent",
          "[models][mlx][info][window]") {
    const Dir dir;
    write_mlx_model(dir.model, MlxModelSpec{.window = 0});
    SECTION("nowhere") {
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.complete);  // a model without a stated window still loads
        CHECK(info.context_length == 0);
        CHECK(info.context_key.empty());
        // The default, as a GGUF with no trained length gets (26a).
        const apogee::models::MlxWindow window =
            apogee::models::mlx_window(info, apogee::harness::BackendConfig{});
        CHECK(window.window == 32768);
        CHECK(apogee::models::describe(window) == "32768-token window (the default)");
        // A configuration that cannot be read gives no window at all.
        const apogee::models::MlxWindow unknown =
            apogee::models::mlx_window(MlxInfo{}, apogee::harness::BackendConfig{});
        CHECK(unknown.window == 0);
        CHECK(apogee::models::describe(unknown) ==
              "window unknown -- config.json could not be read; set context_size");
    }
    SECTION("a vision-language model's text model") {
        write_fixture_file(dir.model / "config.json",
                           R"({"model_type": "qwen3_vl", "text_config": {"model_type":
                              "qwen3_vl_text", "max_position_embeddings": 262144,
                              "dtype": "bfloat16"}})");
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.context_length == 262144);
        CHECK(info.context_key == "text_config.max_position_embeddings");
        CHECK(info.text_model_type == "qwen3_vl_text");
        CHECK(info.quantization.describe() == "bf16");
    }
    SECTION("an omni model's thinker") {
        write_fixture_file(dir.model / "config.json",
                           R"({"model_type": "qwen3_omni_moe", "thinker_config": {"text_config":
                              {"max_position_embeddings": 65536}}})");
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.context_length == 65536);
        CHECK(info.context_key == "thinker_config.text_config.max_position_embeddings");
    }
    SECTION("the top level first") {
        write_fixture_file(dir.model / "config.json",
                           R"({"n_positions": 2048, "text_config": {"max_position_embeddings":
                              8192}})");
        CHECK(read_mlx_info(dir.model).context_length == 2048);
    }
}

TEST_CASE("quantization is said as mlx-lm wrote it: mixed, by mode, or not at all",
          "[models][mlx][info][quant]") {
    const Dir dir;
    SECTION("a mixed recipe's per-layer bits") {
        write_mlx_model(dir.model);
        write_fixture_file(dir.model / "config.json",
                           R"({"model_type": "llama", "quantization": {"group_size": 64, "bits": 4,
               "model.layers.0.mlp.down_proj": {"group_size": 64, "bits": 6},
               "model.embed_tokens": false}})");
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.quantization.mixed);
        CHECK(info.quantization.high_bits == 6);
        CHECK(info.quantization.describe() == "mixed 4/6-bit (group 64)");
        CHECK(info.quantization.label() == "mixed_4_6");
    }
    SECTION("a mode other than affine") {
        write_mlx_model(dir.model, MlxModelSpec{.bits = 4, .group_size = 32, .mode = "mxfp4"});
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.quantization.describe() == "mxfp4 (group 32)");
        CHECK(info.quantization.label() == "mxfp4");
    }
    SECTION("not quantized, saved by mlx-lm: the shards' metadata marks it") {
        write_mlx_model(dir.model, MlxModelSpec{.bits = 0});
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.complete);
        CHECK(info.mlx_format);
        CHECK_FALSE(info.quantization.quantized());
        CHECK(info.quantization.describe() == "bf16");
        CHECK(info.quantization.label() == "bf16");
    }
    SECTION("a Hugging Face snapshot is not mlx-lm's format, whatever it publishes") {
        write_mlx_model(dir.model, MlxModelSpec{.bits = 0, .format = "pt"});
        CHECK_FALSE(read_mlx_info(dir.model).mlx_format);
        CHECK_FALSE(apogee::models::is_mlx_model_dir(dir.model));
        // gpt-oss: the publisher's own quantization, which is not mlx-lm's.
        write_fixture_file(dir.model / "config.json",
                           R"({"model_type": "gpt_oss", "max_position_embeddings": 131072,
                              "quantization_config": {"quant_method": "mxfp4"}})");
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK_FALSE(info.mlx_format);
        CHECK(info.quantization.describe() == "mxfp4 (as published)");
        CHECK(info.quantization.label() == "mxfp4");
        CHECK(info.quantization.dtype.empty());
        CHECK(MlxInfo{}.quantization.describe() == "precision not stated");
    }
}

TEST_CASE("an mlx entry's window is 26a's: configured, else the default under the trained one",
          "[models][mlx][info][window]") {
    MlxInfo info;
    info.config_read = true;
    info.context_length = 131072;
    apogee::harness::BackendConfig backend;
    apogee::models::MlxWindow window = apogee::models::mlx_window(info, backend);
    CHECK(window.window == 32768);
    CHECK_FALSE(window.configured);
    CHECK(window.trained == 131072);
    CHECK(apogee::models::describe(window) ==
          "32768-token window (the default; trained for 131072)");

    info.context_length = 4096;  // trained for less than the default
    CHECK(apogee::models::mlx_window(info, backend).window == 4096);

    backend.context_size = 2048;
    window = apogee::models::mlx_window(info, backend);
    CHECK(window.window == 2048);
    CHECK(window.configured);
    CHECK(apogee::models::describe(window) == "2048-token window (context_size; trained for 4096)");
}
