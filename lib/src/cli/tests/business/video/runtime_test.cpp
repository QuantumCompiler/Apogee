#include "video/runtime.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "support/env_guard.h"
#include "support/pipeline_model.h"
#include "support/voice_model.h"
#include "training/python_env.h"

/// The video runtime's ladder (49b), over installs built in the test: every
/// rung refused with its own reason and its exact fix, in the order they are
/// asked, from files alone -- nothing imported, nothing spawned.
namespace {

using apogee::video::VideoHost;
using apogee::video::VideoReadiness;
using apogee::video::VideoRefusal;

void write(const std::filesystem::path& path, const std::string& text = {}) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

/// An install with everything a generation needs: the environment with
/// torch and diffusers in it, the driver, a pipeline, ffmpeg.
struct Install {
    apogee::testing::TempDir root{"video-runtime-" + std::to_string(std::random_device{}())};
    VideoHost host = VideoHost::at(root.path());
    std::filesystem::path site = host.venv / "lib" / "python3.99" / "site-packages";
    std::filesystem::path pipeline =
        root.path() / "models" / "org--v" / "pipeline" / "aaaaaaaaaaaa";
    std::string handle = "org--v/pipeline/aaaaaaaaaaaa";

    Install() {
        host.ffmpeg = "/usr/bin/ffmpeg";
        write(apogee::training::PythonEnv{host.venv}.interpreter(), "#!/bin/sh\n");
        package("torch", "2.9.1");
        package("diffusers", "0.35.1");
        write(host.driver, "# the driver\n");
        apogee::testing::write_pipeline_model(pipeline);
    }

    void package(const std::string& name, const std::string& version) const {
        write(site / name / "__init__.py");
        std::filesystem::create_directories(site / (name + "-" + version + ".dist-info"));
    }

    [[nodiscard]] VideoReadiness probe() const {
        return apogee::video::probe_video(host, pipeline, handle);
    }
};

}  // namespace

TEST_CASE("an install with every fact is ready, and says what it found",
          "[video][runtime][ladder]") {
    const Install install;
    CHECK(install.host.venv == install.root.path() / "training" / "venv");
    CHECK(install.host.driver ==
          install.root.path() / "training" / "scripts" / "video_generate.py");
    const VideoReadiness ready = install.probe();
    INFO(ready.message());
    REQUIRE(ready.ready());
    CHECK(ready.interpreter == apogee::training::PythonEnv{install.host.venv}.interpreter());
    CHECK(ready.driver == install.host.driver);
    CHECK(ready.ffmpeg == "/usr/bin/ffmpeg");
    CHECK(ready.torch_version == "2.9.1");
    CHECK(ready.diffusers_version == "0.35.1");
    // The runtime alone, no pipeline asked about.
    CHECK(apogee::video::probe_video(install.host).ready());
}

TEST_CASE("the ladder: every rung refuses with its own reason and its exact fix",
          "[video][runtime][ladder]") {
    Install install;
    const std::string venv = install.host.venv.string();
    SECTION("no environment") {
        std::filesystem::remove(apogee::training::PythonEnv{install.host.venv}.interpreter());
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoEnvironment);
        CHECK(refused.message() ==
              "there is no Python environment at " + venv +
                  " -- video generation runs diffusers there, never the system Python -- apogee "
                  "train setup --with video");
    }
    SECTION("no torch") {
        std::filesystem::remove_all(install.site / "torch");
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoTorch);
        CHECK(refused.message() == "torch is not installed in the Python environment at " + venv +
                                       " -- apogee train setup --with video");
    }
    SECTION("no diffusers") {
        std::filesystem::remove_all(install.site / "diffusers");
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoDiffusers);
        CHECK(refused.message() == "diffusers is not installed in the Python environment at " +
                                       venv + " -- apogee train setup --with video");
        CHECK(refused.torch_version == "2.9.1");  // what was found is kept
    }
    SECTION("no pipeline there") {
        std::filesystem::remove_all(install.pipeline);
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoPipeline);
        CHECK(refused.message() ==
              "there is no pipeline at " + install.pipeline.string() +
                  " -- apogee models pull <owner>/<repo> (a diffusers repository)");
    }
    SECTION("a pipeline that cannot load") {
        std::filesystem::remove_all(install.pipeline / "vae");
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NotAPipeline);
        CHECK(refused.message() ==
              "org--v/pipeline/aaaaaaaaaaaa cannot load: the vae component (AutoencoderKLWan) is "
              "missing -- no vae/ directory -- apogee models repair org--v/pipeline/aaaaaaaaaaaa");
    }
    SECTION("no driver") {
        std::filesystem::remove(install.host.driver);
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoDriver);
        CHECK(refused.message() == "the video driver is missing: " + install.host.driver.string() +
                                       " -- apogee check --fix");
    }
    SECTION("no ffmpeg") {
        install.host.ffmpeg.clear();
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoFfmpeg);
        CHECK(refused.message() ==
              "the frames are assembled into an mp4 with ffmpeg, which is not on PATH -- install "
              "ffmpeg");
    }
}

TEST_CASE("the ladder asks the environment first, the packages before the pipeline",
          "[video][runtime][ladder]") {
    Install install;
    // Everything missing at once: the first rung is the one said.
    std::filesystem::remove(install.host.driver);
    std::filesystem::remove_all(install.pipeline);
    install.host.ffmpeg.clear();
    std::filesystem::remove_all(install.site / "diffusers");
    CHECK(install.probe().refusal == VideoRefusal::NoDiffusers);
    install.package("diffusers", "0.35.1");
    CHECK(install.probe().refusal == VideoRefusal::NoPipeline);
    apogee::testing::write_pipeline_model(install.pipeline);
    CHECK(install.probe().refusal == VideoRefusal::NoDriver);
    write(install.host.driver);
    CHECK(install.probe().refusal == VideoRefusal::NoFfmpeg);
    CHECK(apogee::video::to_string(VideoRefusal::NotAPipeline) == "cannot load");
}

TEST_CASE("the voice ladder: its own rungs, the environment's shared",
          "[video][runtime][ladder][voice]") {
    // 50a: torch and kokoro, the voice model read whole, the speech driver.
    Install install;
    install.package("kokoro", "0.9.4");
    const std::filesystem::path model =
        install.root.path() / "models" / "hexgrad--K" / "voice" / "aaaaaaaaaaaa";
    apogee::testing::write_voice_model(model);
    write(install.host.voice_driver, "# the speech driver\n");
    const std::string handle = "hexgrad--K/voice/aaaaaaaaaaaa";
    const auto probe = [&] { return apogee::video::probe_voice(install.host, model, handle); };
    const VideoReadiness ready = probe();
    INFO(ready.message());
    REQUIRE(ready.ready());
    CHECK(ready.kokoro_version == "0.9.4");
    CHECK(ready.driver == install.host.voice_driver);
    // No ffmpeg is asked for: speech is written as WAV.
    install.host.ffmpeg.clear();
    CHECK(probe().ready());

    write(model / "voices" / "am_adam.pt", "not a save");
    CHECK(probe().refusal == VideoRefusal::NotAVoiceModel);
    CHECK(probe().message() == handle +
                                   " cannot load: voices/am_adam.pt is truncated or not a PyTorch "
                                   "save (no archive end record) -- apogee models repair " +
                                   handle);
    std::filesystem::remove_all(model);
    CHECK(probe().message() == "there is no voice model at " + model.string() +
                                   " -- apogee models pull hexgrad/Kokoro-82M");
    std::filesystem::remove(install.host.voice_driver);
    CHECK(apogee::video::probe_voice(install.host).message() ==
          "the voice driver is missing: " + install.host.voice_driver.string() +
              " -- apogee check --fix");
    std::filesystem::remove_all(install.site / "kokoro");
    CHECK(apogee::video::probe_voice(install.host).message() ==
          "kokoro is not installed in the Python environment at " + install.host.venv.string() +
              " -- apogee train setup --with voice");
    std::filesystem::remove(apogee::training::PythonEnv{install.host.venv}.interpreter());
    CHECK(apogee::video::probe_voice(install.host).message() ==
          "there is no Python environment at " + install.host.venv.string() +
              " -- speech runs kokoro there, never the system Python -- apogee train setup "
              "--with voice");
}

TEST_CASE("a LoRA is a .safetensors file, or a directory holding exactly one",
          "[video][runtime][lora]") {
    // 49e: file facts only -- Apogee never reads a LoRA's weights.
    const apogee::testing::TempDir dir{"video-lora-" + std::to_string(std::random_device{}())};
    const auto file = [&dir](const std::string& name) {
        const std::filesystem::path path = dir.path() / name;
        write(path, "weights");
        return path;
    };
    std::filesystem::path out;
    const std::filesystem::path style = file("style.safetensors");
    CHECK(apogee::video::lora_file(style, out).empty());
    CHECK(out == style);
    const std::filesystem::path loud = file("LOUD.SAFETENSORS");
    CHECK(apogee::video::lora_file(loud, out).empty());
    const std::filesystem::path pickle = file("old.bin");
    CHECK(apogee::video::lora_file(pickle, out) ==
          "the LoRA " + pickle.string() +
              " is not a .safetensors file -- a LoRA is applied from its safetensors");
    const std::filesystem::path one = file("one/x.safetensors");
    file("one/README.md");
    CHECK(apogee::video::lora_file(dir.path() / "one", out).empty());
    CHECK(out == one);
    file("none/README.md");
    CHECK(apogee::video::lora_file(dir.path() / "none", out) ==
          "the LoRA directory " + (dir.path() / "none").string() + " holds no .safetensors file");
    file("two/b.safetensors");
    file("two/a.safetensors");
    CHECK(apogee::video::lora_file(dir.path() / "two", out) ==
          "the LoRA directory " + (dir.path() / "two").string() +
              " holds 2 .safetensors files (a.safetensors, b.safetensors) -- name one");
    CHECK(apogee::video::lora_file(dir.path() / "missing", out) ==
          "there is no LoRA at " + (dir.path() / "missing").string());
}

TEST_CASE("the ladder notes peft, and never refuses for it", "[video][runtime][lora]") {
    const Install install;
    VideoReadiness ready = install.probe();
    CHECK(ready.ready());
    CHECK_FALSE(ready.peft);
    install.package("peft", "0.17.1");
    ready = install.probe();
    CHECK(ready.peft);
    CHECK(ready.peft_version == "0.17.1");
}

TEST_CASE("on a Python the voice set does not run on, the ladder names the rebuild",
          "[video][runtime][ladder][voice][m19]") {
    // The user's environment (2026-10-21): Homebrew's 3.14.5, torch in it,
    // kokoro never installable there. The refusal names what can succeed --
    // what `check`'s row and the app's install sheet both read.
    Install install;
    write(install.host.venv / "pyvenv.cfg", "home = /x\nversion = 3.14.5\n");
    const VideoReadiness refused = apogee::video::probe_voice(install.host);
    CHECK(refused.refusal == VideoRefusal::NoKokoro);
    CHECK(refused.message() ==
          "the Python environment is on Python 3.14.5, and the voice set runs "
          "on >=3.10,<3.13 -- apogee train setup --rebuild --with voice");
    // On a Python it runs on, the install as before.
    write(install.host.venv / "pyvenv.cfg", "home = /x\nversion = 3.12.13\n");
    CHECK(apogee::video::probe_voice(install.host).message() ==
          "kokoro is not installed in the Python environment at " + install.host.venv.string() +
              " -- apogee train setup --with voice");
}
