#include "video/runtime.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "modelstore/snapshot.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"
#include "support/pipeline_model.h"
#include "support/voice_model.h"
#include "support/wan_original.h"
#include "training/python_env.h"

/// The video runtime's ladder (49b), over installs built in the test: every
/// rung refused with its own reason and its exact fix, in the order they are
/// asked, from files alone -- nothing imported, nothing spawned.
namespace {

using apogee::video::VideoEngine;
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

/// An install the MLX engine runs on (53l): Apple silicon; the environment
/// with mlx-video's Wan generator, the torch its set brings and mlx in it;
/// the MLX driver; the user's Wan set whole in the store, mlx-video's build
/// of it beside it; ffmpeg.
struct MlxInstall {
    apogee::testing::TempDir root{"video-runtime-mlx-" + std::to_string(std::random_device{}())};
    VideoHost host = VideoHost::at(root.path());
    std::filesystem::path site = host.venv / "lib" / "python3.99" / "site-packages";
    std::filesystem::path source;
    std::filesystem::path model;
    std::string handle = std::string{apogee::testing::kWanModel} + "/mlx/bbbbbbbbbbbb";

    MlxInstall() {
        host.target = "macos-arm64";
        host.ffmpeg = "/usr/bin/ffmpeg";
        write(apogee::training::PythonEnv{host.venv}.interpreter(), "#!/bin/sh\n");
        write(site / "mlx_video" / "__init__.py");
        write(site / "mlx_video" / "models" / "wan_2" / "generate.py");
        std::filesystem::create_directories(site / "mlx_video-0.1.0.dist-info");
        write(site / "torch" / "__init__.py");
        write(site / "mlx" / "__init__.py");
        std::filesystem::create_directories(site / "mlx-0.31.2.dist-info");
        write(host.mlx_driver, "# the MLX driver\n");
        source = apogee::testing::write_user_wan_set(host.roots);
        apogee::testing::complete_user_wan_set(source);
        model = apogee::testing::write_converted_wan(host.roots, source);
    }

    [[nodiscard]] std::string source_handle() const {
        return std::string{apogee::testing::kWanModel} + "/safetensors/" +
               source.filename().string();
    }

    [[nodiscard]] VideoReadiness probe() const {
        return apogee::video::probe_video_mlx(host, model, handle);
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

TEST_CASE("the MLX engine's install is ready, its tokenizer the source set's own",
          "[video][runtime][ladder][mlx]") {
    // 53l: an MLX video model's ladder, read from files -- the generator's
    // module and torch, the directory whole by 53k's rule, and the T5
    // tokenizer the Wan set it was converted from stores.
    const MlxInstall install;
    CHECK(install.host.mlx_driver ==
          install.root.path() / "training" / "scripts" / "video_generate_mlx.py");
    const VideoReadiness ready = install.probe();
    INFO(ready.message());
    REQUIRE(ready.ready());
    CHECK(ready.engine == VideoEngine::Mlx);
    CHECK(ready.driver == install.host.mlx_driver);
    CHECK(ready.interpreter == apogee::training::PythonEnv{install.host.venv}.interpreter());
    CHECK(ready.tokenizer == install.source / "google" / "umt5-xxl");
    CHECK(ready.mlx_video_version == "0.1.0");
    CHECK(ready.mlx_version == "0.31.2");
    CHECK(ready.ffmpeg == "/usr/bin/ffmpeg");
    // No torch or diffusers asked of it, so none recorded.
    CHECK(ready.torch_version.empty());
    CHECK(ready.diffusers_version.empty());
    // The runtime alone, no model asked about; and the one dispatch.
    CHECK(apogee::video::probe_video_mlx(install.host).ready());
    CHECK(apogee::video::probe_video_engine(install.host, VideoEngine::Mlx, install.model,
                                            install.handle)
              .ready());
}

TEST_CASE("the MLX ladder: every rung refuses with its own reason and its exact fix",
          "[video][runtime][ladder][mlx]") {
    MlxInstall install;
    const std::string venv = install.host.venv.string();
    SECTION("not Apple silicon") {
        install.host.target = "linux-x64";
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NotAppleSilicon);
        CHECK(refused.message() ==
              "MLX runs on Apple silicon alone, and this is linux-x64 -- a diffusers pipeline "
              "runs here on PyTorch: apogee models pull <owner>/<repo>");
    }
    SECTION("no environment") {
        std::filesystem::remove(apogee::training::PythonEnv{install.host.venv}.interpreter());
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoEnvironment);
        CHECK(refused.message() ==
              "there is no Python environment at " + venv +
                  " -- video generation on MLX runs mlx-video there, never the system Python -- "
                  "apogee train setup --with mlx-video");
    }
    SECTION("no mlx-video generator, or no torch beside it") {
        const std::string message =
            "mlx-video is not installed in the Python environment at " + venv +
            " (its Wan generator, mlx_video.models.wan_2.generate, and the torch its set brings, "
            "read from their files) -- apogee train setup --with mlx-video";
        std::filesystem::remove(install.site / "mlx_video" / "models" / "wan_2" / "generate.py");
        CHECK(install.probe().refusal == VideoRefusal::NoMlxVideo);
        CHECK(install.probe().message() == message);
        write(install.site / "mlx_video" / "models" / "wan_2" / "generate.py");
        std::filesystem::remove_all(install.site / "torch");
        CHECK(install.probe().message() == message);
    }
    SECTION("no model there") {
        std::filesystem::remove_all(install.model);
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoPipeline);
        CHECK(refused.message() == "there is no MLX video model at " + install.model.string() +
                                       " -- apogee models convert <a Wan set> --mlx (mlx-video)");
    }
    SECTION("an MLX model of another kind") {
        const std::filesystem::path chat =
            install.root.path() / "models" / "org--m" / "mlx" / "cccccccccccc";
        apogee::testing::write_mlx_model(chat, {.kind = "chat"});
        const VideoReadiness refused =
            apogee::video::probe_video_mlx(install.host, chat, "org--m/mlx/cccccccccccc");
        CHECK(refused.refusal == VideoRefusal::NotAPipeline);
        CHECK(refused.message() ==
              "org--m/mlx/cccccccccccc is an MLX chat model, not a video model -- a language "
              "model: an mlx backend chats with it");
    }
    SECTION("a video model that is not whole") {
        std::filesystem::remove(install.model / "vae.safetensors");
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NotAPipeline);
        CHECK(refused.message() == install.handle + " cannot load: no VAE (vae.safetensors) in " +
                                       install.model.string() +
                                       " -- convert it again: apogee models delete " +
                                       install.handle + ", then apogee models convert " +
                                       install.source_handle() + " --mlx");
    }
    SECTION("its source set gone from the store") {
        std::filesystem::remove_all(install.source);
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoTokenizer);
        CHECK(refused.message() ==
              "mlx-video's Wan generator reads the T5 tokenizer (google/umt5-xxl) from the Wan "
              "set it was converted from, " +
                  install.source_handle() +
                  ", which is not in the store -- apogee models pull Wan-AI/Wan2.2-TI2V-5B "
                  "--safetensors");
    }
    SECTION("its source set lacking a tokenizer file its record lists") {
        std::filesystem::remove(install.source / "google" / "umt5-xxl" / "spiece.model");
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoTokenizer);
        CHECK(refused.message() ==
              "mlx-video's Wan generator reads the T5 tokenizer (google/umt5-xxl) from the Wan "
              "set it was converted from, " +
                  install.source_handle() +
                  ", which lacks google/umt5-xxl/spiece.model -- apogee models repair " +
                  install.source_handle());
    }
    SECTION("a record naming no source") {
        apogee::models::Snapshot record = *apogee::models::load_snapshot(install.model);
        record.ref.clear();
        REQUIRE(apogee::models::write_snapshot(install.model, record));
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoTokenizer);
        CHECK(refused.message() ==
              "mlx-video's Wan generator reads the T5 tokenizer (google/umt5-xxl), which " +
                  install.model.string() +
                  " does not carry, and its record names no Wan set it was converted from -- "
                  "convert its Wan set again: apogee models convert <set> --mlx");
    }
    SECTION("no MLX driver") {
        std::filesystem::remove(install.host.mlx_driver);
        const VideoReadiness refused = install.probe();
        CHECK(refused.refusal == VideoRefusal::NoDriver);
        CHECK(refused.message() == "the MLX video driver is missing: " +
                                       install.host.mlx_driver.string() + " -- apogee check --fix");
    }
    SECTION("no ffmpeg") {
        install.host.ffmpeg.clear();
        CHECK(install.probe().refusal == VideoRefusal::NoFfmpeg);
    }
}

TEST_CASE("the tokenizer is the source set's, wherever the config keeps SafeTensors sets",
          "[video][runtime][mlx][tokenizer]") {
    // `paths.hf_dir`: the set under its own root, the MLX build under the
    // models directory -- the record's handle found in either.
    const apogee::testing::TempDir root{"video-tokenizer-" +
                                        std::to_string(std::random_device{}())};
    const apogee::models::StoreRoots roots{.models = root.path() / "models",
                                           .safetensors = root.path() / "hf"};
    const std::filesystem::path source = apogee::testing::write_user_wan_set(roots);
    const std::filesystem::path model = apogee::testing::write_converted_wan(roots, source);
    std::string reason;
    std::string remedy;
    CHECK(apogee::video::find_mlx_video_tokenizer(roots, model, reason, remedy) ==
          source / "google" / "umt5-xxl");
    CHECK(reason.empty());
    // Converted from a path outside the store: that path.
    apogee::models::Snapshot record = *apogee::models::load_snapshot(model);
    record.ref = source.string();
    REQUIRE(apogee::models::write_snapshot(model, record));
    CHECK(apogee::video::find_mlx_video_tokenizer(roots, model, reason, remedy) ==
          source / "google" / "umt5-xxl");
}

TEST_CASE("the engine is the model's directory's, and --engine only names it or is refused",
          "[video][runtime][engine]") {
    const MlxInstall install;
    const std::filesystem::path pipeline =
        install.root.path() / "models" / "org--v" / "pipeline" / "aaaaaaaaaaaa";
    apogee::testing::write_pipeline_model(pipeline);
    const std::filesystem::path chat =
        install.root.path() / "models" / "org--m" / "mlx" / "cccccccccccc";
    apogee::testing::write_mlx_model(chat);
    CHECK(apogee::video::engine_of_directory(pipeline) == VideoEngine::Torch);
    CHECK(apogee::video::engine_of_directory(install.model) == VideoEngine::Mlx);
    CHECK_FALSE(apogee::video::engine_of_directory(chat).has_value());
    CHECK_FALSE(apogee::video::engine_of_directory(install.source).has_value());
    CHECK_FALSE(apogee::video::engine_of_directory(install.root.path() / "nothing").has_value());

    VideoEngine engine = VideoEngine::Torch;
    for (const std::string_view asked : {"", "auto", "mlx"}) {
        INFO(asked);
        CHECK(apogee::video::choose_video_engine(asked, VideoEngine::Mlx, "m", engine).empty());
        CHECK(engine == VideoEngine::Mlx);
    }
    CHECK(apogee::video::choose_video_engine("torch", VideoEngine::Torch, "p", engine).empty());
    CHECK(engine == VideoEngine::Torch);
    CHECK(apogee::video::choose_video_engine("torch", VideoEngine::Mlx, "w/mlx/b", engine) ==
          "'w/mlx/b' is an MLX video model, which runs on MLX (mlx-video), not PyTorch -- "
          "PyTorch runs a diffusers pipeline: 'apogee models pull <owner>/<repo>' stores one "
          "(Wan-AI's are its repositories ending -Diffusers), and 'apogee train setup --with "
          "video' installs what runs it");
    CHECK(apogee::video::choose_video_engine("mlx", VideoEngine::Torch, "o/pipeline/a", engine) ==
          "'o/pipeline/a' is a diffusers pipeline, which runs on PyTorch, not MLX -- MLX runs an "
          "MLX video model: Wan's original layout converted with 'apogee models convert <set> "
          "--mlx', after 'apogee train setup --with mlx-video'");
    CHECK(apogee::video::choose_video_engine("cuda", VideoEngine::Torch, "p", engine) ==
          "--engine takes auto, torch or mlx (got 'cuda')");
    CHECK(apogee::video::to_string(VideoEngine::Mlx) == "mlx");
    CHECK(apogee::video::to_string(VideoEngine::Torch) == "torch");
    CHECK(apogee::video::video_engine_names().size() == 3);
    CHECK(apogee::video::video_engine_names().front() == "auto");
}

TEST_CASE("mlx-video's Wan generator takes 4n+1 frames, and a model's mode is its config's",
          "[video][runtime][mlx]") {
    for (const int frames : {1, 5, 9, 81, 121}) {
        CHECK(apogee::video::mlx_video_frames(frames));
    }
    for (const int frames : {0, 2, 4, 6, 80, 120, -3}) {
        CHECK_FALSE(apogee::video::mlx_video_frames(frames));
    }
    const MlxInstall install;
    CHECK(apogee::video::mlx_video_mode(install.model) == "ti2v");
    CHECK(apogee::video::mlx_video_mode(install.root.path()).empty());
}

namespace {

/// An install the MLX speech engine runs on (53m): Apple silicon; the
/// environment with mlx-audio's Kokoro, the TTS loader beside it, misaki and
/// mlx in it; the MLX speech driver; an MLX voice model in the store.
struct MlxVoiceInstall {
    apogee::testing::TempDir root{"voice-runtime-mlx-" + std::to_string(std::random_device{}())};
    VideoHost host = VideoHost::at(root.path());
    std::filesystem::path site = host.venv / "lib" / "python3.99" / "site-packages";
    std::filesystem::path model = root.path() / "models" / "hexgrad--K" / "mlx" / "dddddddddddd";
    std::string handle = "hexgrad--K/mlx/dddddddddddd";

    MlxVoiceInstall() {
        host.target = "macos-arm64";
        write(apogee::training::PythonEnv{host.venv}.interpreter(), "#!/bin/sh\n");
        write(site / "mlx_audio" / "__init__.py");
        write(site / "mlx_audio" / "tts" / "__init__.py");
        write(site / "mlx_audio" / "tts" / "utils.py");
        write(site / "mlx_audio" / "tts" / "models" / "kokoro" / "__init__.py");
        std::filesystem::create_directories(site / "mlx_audio-0.6.1.dist-info");
        write(site / "misaki" / "__init__.py");
        write(site / "mlx" / "__init__.py");
        std::filesystem::create_directories(site / "mlx-0.32.4.dist-info");
        write(host.voice_mlx_driver, "# the MLX speech driver\n");
        apogee::testing::write_mlx_voice_model(model);
    }

    [[nodiscard]] VideoReadiness probe() const {
        return apogee::video::probe_voice_mlx(host, model, handle);
    }
};

}  // namespace

TEST_CASE("the MLX speech engine's install is ready, and says what it found",
          "[video][runtime][ladder][voice][mlx_audio]") {
    const MlxVoiceInstall install;
    const VideoReadiness ready = install.probe();
    INFO(ready.message());
    REQUIRE(ready.ready());
    CHECK(ready.engine == VideoEngine::Mlx);
    CHECK(ready.driver == install.host.voice_mlx_driver);
    CHECK(ready.interpreter == apogee::training::PythonEnv{install.host.venv}.interpreter());
    CHECK(ready.mlx_audio_version == "0.6.1");
    CHECK(ready.mlx_version == "0.32.4");
    // No ffmpeg, no torch, no kokoro asked for: its own rungs alone.
    VideoHost bare = install.host;
    bare.ffmpeg.clear();
    CHECK(apogee::video::probe_voice_engine(bare, VideoEngine::Mlx, install.model, install.handle)
              .ready());
    // The PyTorch engine's ladder for the same host asks for its own set.
    CHECK(apogee::video::probe_voice_engine(install.host, VideoEngine::Torch).refusal ==
          VideoRefusal::NoTorch);
}

TEST_CASE("the MLX speech ladder: every rung refuses with its own reason and its exact fix",
          "[video][runtime][ladder][voice][mlx_audio]") {
    MlxVoiceInstall install;
    const auto message = [&install] { return install.probe().message(); };

    // The voice model's own rungs.
    apogee::testing::write_fixture_file(install.model / "voices" / "am_adam.safetensors", "short");
    CHECK(install.probe().refusal == VideoRefusal::NotAVoiceModel);
    CHECK(message() == install.handle +
                           " cannot load: voices/am_adam.safetensors is truncated or not a "
                           "SafeTensors file (its header does not fit in its 5 bytes) -- convert "
                           "it again: apogee models delete " +
                           install.handle +
                           ", then apogee models convert hexgrad--Kokoro-82M/voice/aaaaaaaaaaaa "
                           "--mlx");
    std::filesystem::remove_all(install.model);
    apogee::testing::write_mlx_model(install.model, {.kind = "chat"});
    CHECK(install.probe().refusal == VideoRefusal::NotAVoiceModel);
    CHECK(message() == install.handle +
                           " is an MLX chat model, not a voice model -- a language model: an mlx "
                           "backend chats with it");
    std::filesystem::remove_all(install.model);
    CHECK(install.probe().refusal == VideoRefusal::NoVoiceModel);
    CHECK(message() == "there is no MLX voice model at " + install.model.string() +
                           " -- apogee models convert <a voice model> --mlx (mlx-audio)");

    // The driver, asked last.
    std::filesystem::remove(install.host.voice_mlx_driver);
    CHECK(apogee::video::probe_voice_mlx(install.host).message() ==
          "the MLX voice driver is missing: " + install.host.voice_mlx_driver.string() +
              " -- apogee check --fix");

    // mlx-audio's Kokoro, and the misaki it reads text through.
    std::filesystem::remove_all(install.site / "misaki");
    CHECK(apogee::video::probe_voice_mlx(install.host).refusal == VideoRefusal::NoMlxAudio);
    CHECK(apogee::video::probe_voice_mlx(install.host).message() ==
          "mlx-audio is not installed in the Python environment at " + install.host.venv.string() +
              " (its Kokoro, mlx_audio.tts.models.kokoro, and the misaki it reads text through, "
              "read from their files) -- apogee train setup --with mlx-audio");
    write(install.site / "misaki" / "__init__.py");
    std::filesystem::remove_all(install.site / "mlx_audio" / "tts" / "models" / "kokoro");
    CHECK(apogee::video::probe_voice_mlx(install.host).refusal == VideoRefusal::NoMlxAudio);

    // The environment, then the platform.
    std::filesystem::remove(apogee::training::PythonEnv{install.host.venv}.interpreter());
    CHECK(apogee::video::probe_voice_mlx(install.host).message() ==
          "there is no Python environment at " + install.host.venv.string() +
              " -- speech on MLX runs mlx-audio there, never the system Python -- apogee train "
              "setup --with mlx-audio");
    install.host.target = "linux-x64";
    CHECK(apogee::video::probe_voice_mlx(install.host).refusal == VideoRefusal::NotAppleSilicon);
    CHECK(apogee::video::probe_voice_mlx(install.host).message() ==
          "MLX runs on Apple silicon alone, and this is linux-x64 -- a voice model as Kokoro "
          "publishes it speaks here on PyTorch: apogee models pull hexgrad/Kokoro-82M");
    CHECK(apogee::video::to_string(VideoRefusal::NoMlxAudio) == "no mlx-audio");
}
