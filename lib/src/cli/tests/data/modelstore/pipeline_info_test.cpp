#include "modelstore/pipeline_info.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "support/env_guard.h"
#include "support/pipeline_model.h"

/// The pipeline directory reader (49a), over directories built in the test
/// in Wan 2.2 TI2V-5B's layout: what a whole pipeline says, and -- the
/// dishonesty guards -- every way one cannot load said as "cannot load"
/// naming the missing piece, never a guess filling the gap.
namespace {

using apogee::models::PipelineComponentKind;
using apogee::models::PipelineInfo;
using apogee::models::read_pipeline_info;
using apogee::testing::PipelineModelSpec;
using apogee::testing::write_fixture_file;
using apogee::testing::write_pipeline_model;

struct Dir {
    apogee::testing::TempDir root{"pipeline-info-" + std::to_string(std::random_device{}())};
    std::filesystem::path pipeline = root.path() / "pipeline";
};

[[nodiscard]] std::string problem_of(const std::filesystem::path& dir) {
    const PipelineInfo info = read_pipeline_info(dir);
    CHECK_FALSE(info.complete);
    return info.problem;
}

}  // namespace

TEST_CASE("a whole pipeline reads its class, its components and every shard",
          "[models][pipeline][info]") {
    const Dir dir;
    write_pipeline_model(dir.pipeline);
    const PipelineInfo info = read_pipeline_info(dir.pipeline);
    INFO(info.problem);
    CHECK(info.complete);
    CHECK(info.problem.empty());
    CHECK(info.index_read);
    CHECK(info.class_name == "WanPipeline");
    CHECK(info.diffusers_version == "0.35.0");
    // In name order; the optional second transformer declared absent, the
    // pipeline's own settings (boundary_ratio, expand_timesteps) no component.
    REQUIRE(info.components.size() == 5);
    const std::vector<std::string> names{"scheduler", "text_encoder", "tokenizer", "transformer",
                                         "vae"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        CHECK(info.components.at(i).name == names.at(i));
    }
    CHECK(info.absent == std::vector<std::string>{"transformer_2"});
    CHECK(info.components.at(0).kind == PipelineComponentKind::Scheduler);
    CHECK(info.components.at(1).kind == PipelineComponentKind::Model);
    CHECK(info.components.at(1).library == "transformers");
    CHECK(info.components.at(1).class_name == "UMT5EncoderModel");
    CHECK(info.components.at(2).kind == PipelineComponentKind::Tokenizer);
    CHECK(info.components.at(3).kind == PipelineComponentKind::Model);
    REQUIRE(info.components.at(3).shards.size() == 2);
    CHECK(info.components.at(3).shards.at(0).name ==
          "diffusion_pytorch_model-00001-of-00002.safetensors");
    CHECK(info.components.at(0).shards.empty());
    CHECK(info.shard_count() == 4);
    CHECK(info.variant().empty());
    // Every byte under the directory, and each component's own.
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir.pipeline)) {
        total += entry.is_regular_file() ? entry.file_size() : 0;
    }
    CHECK(info.bytes == total);
    CHECK(info.components.at(4).files == 2);
    CHECK(info.components.at(4).bytes ==
          std::filesystem::file_size(dir.pipeline / "vae" / "config.json") +
              std::filesystem::file_size(dir.pipeline / "vae" /
                                         "diffusion_pytorch_model.safetensors"));
    CHECK(apogee::models::describe(info) ==
          "WanPipeline, 5 components (scheduler, text_encoder, tokenizer, transformer, vae), "
          "4 shard(s)");
    CHECK(apogee::models::is_pipeline_dir(dir.pipeline));
    CHECK_FALSE(apogee::models::is_pipeline_dir(dir.pipeline / "vae"));
}

TEST_CASE("weights saved as a variant are read whole and their variant said",
          "[models][pipeline][info]") {
    const Dir dir;
    write_pipeline_model(dir.pipeline, PipelineModelSpec{.variant = "fp16"});
    const PipelineInfo info = read_pipeline_info(dir.pipeline);
    INFO(info.problem);
    CHECK(info.complete);
    CHECK(info.variant() == "fp16");
    CHECK(info.components.at(3).variant == "fp16");
    CHECK(apogee::models::describe(info).ends_with(", 4 shard(s), fp16 weights"));

    CHECK(apogee::models::weights_variant("diffusion_pytorch_model.fp16.safetensors") == "fp16");
    CHECK(apogee::models::weights_variant("model-00001-of-00002.bf16.safetensors") == "bf16");
    CHECK(apogee::models::weights_variant("diffusion_pytorch_model.safetensors.index.fp16.json") ==
          "fp16");
    CHECK(apogee::models::weights_variant("diffusion_pytorch_model.safetensors").empty());
    CHECK(apogee::models::weights_variant("model.safetensors.index.json").empty());
    // A dot in a name is a variant only when it names one.
    CHECK(apogee::models::weights_variant("t5.v1.safetensors").empty());
    CHECK(apogee::models::weights_variant("config.json").empty());
}

TEST_CASE("every way a pipeline cannot load is said, naming the missing piece",
          "[models][pipeline][info]") {
    const Dir dir;
    const std::filesystem::path& p = dir.pipeline;
    SECTION("no directory") {
        CHECK(problem_of(p) == "cannot load: " + p.string() + " is not a directory");
    }
    SECTION("no model_index.json") {
        write_pipeline_model(p);
        std::filesystem::remove(p / "model_index.json");
        CHECK(problem_of(p) == "cannot load: no model_index.json in " + p.string());
    }
    SECTION("an index that is not JSON") {
        write_pipeline_model(p);
        write_fixture_file(p / "model_index.json", "not json");
        CHECK(problem_of(p) == "cannot load: model_index.json is not a JSON object");
    }
    SECTION("an index with no pipeline class") {
        write_pipeline_model(p);
        write_fixture_file(p / "model_index.json", R"({"vae": ["diffusers", "AutoencoderKL"]})");
        CHECK(problem_of(p) ==
              "cannot load: model_index.json names no pipeline class (_class_name)");
    }
    SECTION("an index naming no component") {
        write_pipeline_model(p);
        write_fixture_file(p / "model_index.json",
                           R"({"_class_name": "WanPipeline", "transformer_2": [null, null]})");
        CHECK(problem_of(p) == "cannot load: model_index.json names no components");
    }
    SECTION("a component that never arrived") {
        write_pipeline_model(p, PipelineModelSpec{.missing = "vae"});
        CHECK(problem_of(p) ==
              "cannot load: the vae component (AutoencoderKLWan) is missing -- no vae/ directory");
    }
    SECTION("a model component without its configuration") {
        write_pipeline_model(p);
        std::filesystem::remove(p / "text_encoder" / "config.json");
        CHECK(problem_of(p) == "cannot load: text_encoder/ has no config.json");
    }
    SECTION("a model component without weights") {
        write_pipeline_model(p);
        std::filesystem::remove(p / "vae" / "diffusion_pytorch_model.safetensors");
        CHECK(problem_of(p) == "cannot load: vae/ holds no weights (*.safetensors)");
    }
    SECTION("a shard its index names, missing") {
        write_pipeline_model(p);
        std::filesystem::remove(p / "transformer" /
                                "diffusion_pytorch_model-00002-of-00002.safetensors");
        CHECK(problem_of(p) ==
              "cannot load: transformer/diffusion_pytorch_model-00002-of-00002.safetensors is "
              "missing (diffusion_pytorch_model.safetensors.index.json names it)");
    }
    SECTION("a shard cut short") {
        write_pipeline_model(p);
        const std::filesystem::path shard = p / "vae" / "diffusion_pytorch_model.safetensors";
        std::filesystem::resize_file(shard, std::filesystem::file_size(shard) - 16);
        CHECK(problem_of(p).starts_with(
            "cannot load: vae/diffusion_pytorch_model.safetensors is truncated"));
    }
    SECTION("a scheduler without its configuration") {
        write_pipeline_model(p);
        std::filesystem::remove(p / "scheduler" / "scheduler_config.json");
        CHECK(problem_of(p) == "cannot load: scheduler/ has no scheduler_config.json");
    }
    SECTION("a tokenizer without its files") {
        write_pipeline_model(p);
        std::filesystem::remove(p / "tokenizer" / "tokenizer.json");
        std::filesystem::remove(p / "tokenizer" / "tokenizer_config.json");
        CHECK(problem_of(p) ==
              "cannot load: tokenizer/ holds no tokenizer files (tokenizer.json, "
              "tokenizer_config.json or a vocabulary)");
    }
}

TEST_CASE("an index's components are named from its text alone, settings left out",
          "[models][pipeline][info]") {
    CHECK(apogee::models::pipeline_components(
              R"({"_class_name": "WanPipeline", "boundary_ratio": 0.9, "flag": true,
                  "vae": ["diffusers", "AutoencoderKLWan"], "transformer_2": [null, null],
                  "scheduler": ["diffusers", "UniPCMultistepScheduler"], "odd": [1, 2]})") ==
          std::vector<std::string>{"scheduler", "vae"});
    CHECK(apogee::models::pipeline_components("not json").empty());
    CHECK(apogee::models::pipeline_components("[]").empty());

    using apogee::models::component_kind;
    CHECK(component_kind("FlowMatchEulerDiscreteScheduler") == PipelineComponentKind::Scheduler);
    CHECK(component_kind("CLIPTokenizer") == PipelineComponentKind::Tokenizer);
    CHECK(component_kind("CLIPImageProcessor") == PipelineComponentKind::Processor);
    CHECK(component_kind("CLIPFeatureExtractor") == PipelineComponentKind::Processor);
    CHECK(component_kind("HunyuanVideoTransformer3DModel") == PipelineComponentKind::Model);
    CHECK(apogee::models::to_string(PipelineComponentKind::Processor) == "processor");
}
