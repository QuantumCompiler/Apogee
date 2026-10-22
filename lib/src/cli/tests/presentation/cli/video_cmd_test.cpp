#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "modelstore/store.h"
#include "support/cli_home.h"
#include "support/mlx_model.h"
#include "support/pipeline_model.h"
#include "support/wan_original.h"

/// `apogee video` and its pointer through the real command tree (49c):
/// `config set-default-video` resolved against the store and written by the
/// one editor, cleared with `off`; the store's pipelines completed for
/// `--model` and the pointer; `video generate` refused before anything runs
/// with the fix named; and `models rename` carrying the pointer with the
/// directory it names.
namespace {

using Catch::Matchers::ContainsSubstring;

constexpr std::string_view kConfig = "backends:\n  mock:\n    type: mock\n";

struct Home {
    apogee::testing::CliHome home{std::string{kConfig}};
    std::filesystem::path pipeline = home.models() / "org--clip" / "pipeline" / "aaaaaaaaaaaa";

    Home() {
        apogee::testing::write_pipeline_model(pipeline);
    }
};

}  // namespace

TEST_CASE("config set-default-video points at a stored pipeline, and clears with off",
          "[cli][video][config]") {
    const Home fixture;
    const apogee::testing::CliHome& home = fixture.home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "set-default-video", "org--clip"}, &out, &err) == 0);
    CHECK(out == "models.default_video = org--clip\n");
    REQUIRE(home.run({"config", "get", "models.default_video"}, &out, &err) == 0);
    CHECK(out == "org--clip\n");
    CHECK_THAT(home.config_text(), ContainsSubstring("default_video: org--clip"));

    // Refused, naming why, the file untouched.
    const std::string before = home.config_text();
    CHECK(home.run({"config", "set-default-video", "nope"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("no model 'nope'"));
    std::filesystem::create_directories(home.models() / "org--m" / "gguf" / "cccccccccccc");
    std::ofstream{home.models() / "org--m" / "gguf" / "cccccccccccc" / "m.gguf"} << "GGUF";
    CHECK(home.run({"config", "set-default-video", "org--m/gguf/cccccccccccc"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("is a GGUF, not a pipeline"));
    CHECK(home.config_text() == before);

    REQUIRE(home.run({"config", "set-default-video", "off"}, &out, &err) == 0);
    CHECK(out == "models.default_video = (none)\n");
}

TEST_CASE("the store's pipelines complete for --model and for the pointer",
          "[cli][video][complete]") {
    const Home fixture;
    std::string out;
    std::string err;
    for (const std::vector<std::string>& line :
         {std::vector<std::string>{"__complete", "video", "generate", "a heron", "--model", ""},
          std::vector<std::string>{"__complete", "config", "set-default-video", ""}}) {
        REQUIRE(fixture.home.run(line, &out, &err) == 0);
        CHECK(out == "org--clip\norg--clip/pipeline/aaaaaaaaaaaa\n");
    }
}

TEST_CASE("video generate is refused before anything runs, the fix named",
          "[cli][video][generate]") {
    const Home fixture;
    const apogee::testing::CliHome& home = fixture.home;
    const std::string clip = (home.home() / "clip.mp4").string();
    std::string out;
    std::string err;
    CHECK(home.run({"video", "generate", "a heron", "-o", clip}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("apogee video: no pipeline to run -- name one with --model, "
                                      "or point models.default_video at one"));
    CHECK(home.run({"video", "generate", "a heron", "-o", clip, "--model", "org--clip",
                    "--duration", "30"},
                   &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("--duration 30 is over the cap of 10 seconds a clip"));
    CHECK(home.run({"video", "generate", "a heron", "--model", "org--clip"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("--output is required"));
    CHECK_FALSE(std::filesystem::exists(clip));
}

TEST_CASE("models rename carries models.default_video with the directory it names",
          "[cli][video][rename]") {
    const Home fixture;
    const apogee::testing::CliHome& home = fixture.home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "set-default-video", "org--clip/pipeline/aaaaaaaaaaaa"}, &out,
                     &err) == 0);
    REQUIRE(home.run({"models", "rename", "org--clip", "org--clips"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("repointed models.default_video: "
                                      "org--clip/pipeline/aaaaaaaaaaaa -> "
                                      "org--clips/pipeline/aaaaaaaaaaaa"));
    REQUIRE(home.run({"config", "get", "models.default_video"}, &out, &err) == 0);
    CHECK(out == "org--clips/pipeline/aaaaaaaaaaaa\n");
    CHECK(
        std::filesystem::is_directory(home.models() / "org--clips" / "pipeline" / "aaaaaaaaaaaa"));
}

TEST_CASE("an MLX video model completes and points like a pipeline, and --engine completes",
          "[cli][video][complete][mlx]") {
    // 53l: the door takes mlx-video's build of a Wan set, so completion
    // offers it -- by model and handle, its source set never -- and the
    // pointer takes it; an MLX model of another kind is offered nowhere.
    const Home fixture;
    const apogee::testing::CliHome& home = fixture.home;
    const apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(home.models());
    const std::filesystem::path source = apogee::testing::write_user_wan_set(roots);
    apogee::testing::complete_user_wan_set(source);
    (void)apogee::testing::write_converted_wan(roots, source);
    apogee::testing::write_mlx_model(home.models() / "org--m" / "mlx" / "cccccccccccc",
                                     {.kind = "chat"});
    const std::string handle = "Wan-AI--Wan2.2-TI2V-5B/mlx/bbbbbbbbbbbb";
    std::string out;
    std::string err;
    for (const std::vector<std::string>& line :
         {std::vector<std::string>{"__complete", "video", "generate", "a heron", "--model", ""},
          std::vector<std::string>{"__complete", "video", "compose", "a brief", "--model", ""},
          std::vector<std::string>{"__complete", "config", "set-default-video", ""}}) {
        REQUIRE(home.run(line, &out, &err) == 0);
        CHECK(out == "org--clip\norg--clip/pipeline/aaaaaaaaaaaa\nWan-AI--Wan2.2-TI2V-5B\n" +
                         handle + "\n");
    }
    // What completion offers, the pointer takes.
    REQUIRE(home.run({"config", "set-default-video", handle}, &out, &err) == 0);
    CHECK(out == "models.default_video = " + handle + "\n");
    REQUIRE(home.run({"config", "set-default-video", "Wan-AI--Wan2.2-TI2V-5B"}, &out, &err) == 0);
    CHECK(home.run({"config", "set-default-video", "org--m/mlx/cccccccccccc"}, &out, &err) != 0);
    CHECK_THAT(err,
               ContainsSubstring("is an MLX chat model, not a pipeline or an MLX video model"));

    REQUIRE(home.run({"__complete", "video", "generate", "a heron", "--engine", ""}, &out, &err) ==
            0);
    CHECK(out == "auto\ntorch\nmlx\n");
}

TEST_CASE("video generate refuses an engine its model does not run on, before anything runs",
          "[cli][video][generate][engine]") {
    const Home fixture;
    const apogee::testing::CliHome& home = fixture.home;
    const std::string clip = (home.home() / "clip.mp4").string();
    std::string out;
    std::string err;
    CHECK(home.run({"video", "generate", "a heron", "-o", clip, "--model", "org--clip", "--engine",
                    "mlx"},
                   &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("apogee video: --engine mlx: "
                                      "'org--clip/pipeline/aaaaaaaaaaaa' is a diffusers pipeline, "
                                      "which runs on PyTorch, not MLX"));
    CHECK(home.run({"video", "generate", "a heron", "-o", clip, "--model", "org--clip", "--engine",
                    "cuda"},
                   &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("--engine takes auto, torch or mlx (got 'cuda')"));
    CHECK_FALSE(std::filesystem::exists(clip));
}
