#include "modelstore/voice_info.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "support/env_guard.h"
#include "support/voice_model.h"

/// The voice model reader (50a), over directories built in the test in
/// Kokoro-82M's layout: what a whole model says -- its checkpoint, its
/// voices and the languages their names carry -- and every way one cannot
/// load said as "cannot load", naming the piece.
namespace {

using apogee::models::read_voice_info;
using apogee::models::VoiceInfo;
using apogee::testing::write_fixture_file;
using apogee::testing::write_voice_model;

struct Dir {
    apogee::testing::TempDir root{"voice-info-" + std::to_string(std::random_device{}())};
    std::filesystem::path model = root.path() / "kokoro";
};

[[nodiscard]] std::string problem_of(const std::filesystem::path& dir) {
    const VoiceInfo info = read_voice_info(dir);
    CHECK_FALSE(info.complete);
    return info.problem;
}

}  // namespace

TEST_CASE("a whole voice model reads its checkpoint, its voices and their languages",
          "[models][voice][info]") {
    const Dir dir;
    write_voice_model(dir.model);
    const VoiceInfo info = read_voice_info(dir.model);
    INFO(info.problem);
    CHECK(info.complete);
    CHECK(info.config_read);
    CHECK(info.checkpoint == "kokoro-v1_0.pth");
    CHECK(info.checkpoint_bytes == 256);
    REQUIRE(info.voices.size() == 3);
    CHECK(info.voices.at(0).name == "af_heart");
    CHECK(info.voices.at(0).language == "American English");
    CHECK(info.voices.at(2).language == "British English");
    CHECK(info.languages() == std::vector<std::string>{"American English", "British English"});
    CHECK(info.has_voice("am_adam"));
    CHECK_FALSE(info.has_voice("zf_xiaoxiao"));
    CHECK(apogee::models::describe(info) ==
          "kokoro-v1_0.pth, 3 voices (American English, British English)");
    CHECK(apogee::models::is_voice_dir(dir.model));
    CHECK(apogee::models::voice_language("zf_xiaoxiao") == "Mandarin Chinese");
    CHECK(apogee::models::voice_language("x_unknown") == "unstated");
}

TEST_CASE("every way a voice model cannot load is said, naming the piece",
          "[models][voice][info]") {
    const Dir dir;
    const std::filesystem::path& m = dir.model;
    SECTION("no directory") {
        CHECK(problem_of(m) == "cannot load: " + m.string() + " is not a directory");
    }
    SECTION("no configuration") {
        write_voice_model(m);
        std::filesystem::remove(m / "config.json");
        CHECK(problem_of(m) == "cannot load: no config.json in " + m.string());
    }
    SECTION("a configuration that is not JSON") {
        write_voice_model(m);
        write_fixture_file(m / "config.json", "not json");
        CHECK(problem_of(m) == "cannot load: config.json is not a JSON object");
    }
    SECTION("no checkpoint") {
        write_voice_model(m);
        std::filesystem::remove(m / "kokoro-v1_0.pth");
        CHECK(problem_of(m) == "cannot load: no checkpoint (*.pth) in " + m.string());
    }
    SECTION("two checkpoints") {
        write_voice_model(m);
        write_fixture_file(m / "kokoro-v0_19.pth", apogee::testing::zip_bytes());
        CHECK(problem_of(m) ==
              "cannot load: 2 checkpoints (kokoro-v0_19.pth, "
              "kokoro-v1_0.pth) -- a voice model is one");
    }
    SECTION("a checkpoint cut short") {
        write_voice_model(m);
        std::filesystem::resize_file(m / "kokoro-v1_0.pth", 200);
        CHECK(problem_of(m) ==
              "cannot load: kokoro-v1_0.pth is truncated or not a PyTorch save "
              "(no archive end record)");
    }
    SECTION("no voices") {
        write_voice_model(m, {.voices = {}});
        CHECK(problem_of(m) == "cannot load: no voices (voices/*.pt) in " + m.string());
    }
    SECTION("a voice that is not a save") {
        write_voice_model(m);
        write_fixture_file(m / "voices" / "am_adam.pt", "not a zip");
        CHECK(problem_of(m) ==
              "cannot load: voices/am_adam.pt is truncated or not a PyTorch "
              "save (no archive end record)");
    }
}

TEST_CASE("an MLX voice model reads its weights and voices as a voice model's",
          "[models][voice][info][mlx_audio]") {
    // 53m: mlx-audio's build of Kokoro, read for `voice say` as the original
    // is -- its presets the voices it carries, by language.
    const Dir dir;
    apogee::testing::write_mlx_voice_model(dir.model,
                                           {.voices = {"bm_george", "af_heart", "jf_alpha"}});
    const VoiceInfo info = apogee::models::read_mlx_voice_info(dir.model);
    INFO(info.problem);
    CHECK(info.complete);
    CHECK(info.config_read);
    CHECK(info.checkpoint == "model.safetensors");
    REQUIRE(info.voices.size() == 3);
    CHECK(info.voices[0].name == "af_heart");
    CHECK(info.voices[0].language == "American English");
    CHECK(info.voices[2].language == "Japanese");
    CHECK(info.has_voice("af_heart"));
    CHECK_FALSE(info.has_voice("am_adam"));
    CHECK(info.languages() ==
          std::vector<std::string>{"American English", "British English", "Japanese"});
    // Cut short, it is said as the MLX reader says it.
    write_fixture_file(dir.model / "voices" / "af_heart.safetensors", "short");
    const VoiceInfo cut = apogee::models::read_mlx_voice_info(dir.model);
    CHECK_FALSE(cut.complete);
    CHECK(cut.problem.find("voices/af_heart.safetensors is truncated") != std::string::npos);
}

TEST_CASE("a voice model converts to mlx exactly where it is whole and mlx-audio builds Kokoro",
          "[models][voice][mlx_audio][conversions]") {
    // 53m: M20's decision, one more answer -- never a GGUF.
    const Dir dir;
    write_voice_model(dir.model);
    bool asked = false;
    const auto installed = [&asked]() {
        asked = true;
        return true;
    };
    CHECK(apogee::models::voice_conversions(dir.model, installed) ==
          std::vector<std::string>{"mlx"});
    CHECK(asked);
    // Without mlx-audio, or with nothing to ask: none.
    CHECK(apogee::models::voice_conversions(dir.model, [] { return false; }).empty());
    CHECK(apogee::models::voice_conversions(dir.model).empty());
    // A model cut short converts to nothing, and mlx-audio is not asked.
    std::filesystem::remove_all(dir.model / "voices");
    asked = false;
    CHECK(apogee::models::voice_conversions(dir.model, installed).empty());
    CHECK_FALSE(asked);
}
