#include "cli/video_generate.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "contracts/video_run.h"
#include "modelstore/snapshot.h"
#include "support/env_guard.h"
#include "support/fake_child.h"
#include "support/pipeline_model.h"

/// `video generate` composed (49c), in process, over its seams: the
/// pipeline from `--model` or `models.default_video` in every spelling, the
/// caps quoted before anything runs, a generation over a scripted driver
/// that writes real frame files and a stand-in assembly, and its record --
/// the document and the sidecar beside a clip that landed, and never beside
/// one that did not.
namespace {

using apogee::commands::VideoGenerateOptions;
using apogee::commands::VideoGenerateResult;
using apogee::commands::VideoGenerateSeams;
using Catch::Matchers::ContainsSubstring;

constexpr std::string_view kHandle = "org--clip/pipeline/aaaaaaaaaaaa";

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A store with one pipeline in it, recorded as pulled, and a run's seams.
struct Run {
    apogee::testing::TempDir dir{"video-generate-" + std::to_string(std::random_device{}())};
    apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(dir.path() / "models");
    std::filesystem::path pipeline = roots.models / "org--clip" / "pipeline" / "aaaaaaaaaaaa";
    std::filesystem::path output = dir.path() / "out" / "clip.mp4";
    apogee::harness::Config config;
    VideoGenerateOptions options;
    /// What the driver was asked, and what it answers.
    nlohmann::json request;
    std::string script =
        R"({"event": "ready", "device": "mps", "dtype": "bfloat16", "pipeline": "WanPipeline", "mode": "t2v", "torch": "2.9.1", "diffusers": "0.35.1"})"
        "\n"
        R"({"event": "step", "step": 1, "steps": 1})"
        "\n"
        R"({"event": "done", "frames": 5, "width": 128, "height": 128})"
        "\n";
    int status = 0;
    bool spawned = false;
    apogee::video::VideoReadiness ready{.interpreter = "/venv/bin/python",
                                        .driver = "/scripts/video_generate.py",
                                        .ffmpeg = "ffmpeg",
                                        .torch_version = "2.9.1",
                                        .diffusers_version = "0.35.1"};
    std::vector<std::string> said;
    std::vector<apogee::video::VideoEvent> events;

    Run() {
        apogee::testing::write_pipeline_model(pipeline);
        apogee::models::Snapshot record;
        record.ref = "Wan-AI/Wan2.2-TI2V-5B-Diffusers";
        record.revision = "main";
        record.source = "huggingface";
        REQUIRE(apogee::models::write_snapshot(pipeline, record));
        std::filesystem::create_directories(output.parent_path());
        config.models.default_video = std::string{kHandle};
        options.prompt = "a heron lands on a pier at dusk";
        options.output = output;
    }

    [[nodiscard]] VideoGenerateSeams seams() {
        VideoGenerateSeams seams;
        seams.ready = [this](const apogee::commands::ResolvedPipeline&) { return ready; };
        seams.run.spawn = [this](const apogee::platform::ChildCommand& command,
                                 std::string&) -> std::unique_ptr<apogee::platform::ChildProcess> {
            spawned = true;
            request = nlohmann::json::parse(slurp(command.arguments.at(2)));
            const std::filesystem::path frames{request.at("frames_dir").get<std::string>()};
            std::filesystem::create_directories(frames);
            std::ofstream{frames / "f00001.png"} << "PNG";
            auto child = std::make_unique<apogee::testing::FakeChild>();
            child->stdout_script = script;
            child->exit_status = status;
            return child;
        };
        seams.run.assemble = [](const std::filesystem::path&, int fps,
                                const std::filesystem::path& out,
                                const apogee::harness::CancellationToken&, std::string&) {
            std::ofstream{out} << "MP4 at " << fps;
            return std::string{"h264"};
        };
        seams.on_event = [this](const apogee::video::VideoEvent& event) {
            events.push_back(event);
        };
        seams.say = [this](const std::string& line) { said.push_back(line); };
        seams.random_seed = [] { return std::int64_t{4242}; };
        seams.work_root = dir.path() / "cache" / "video";
        seams.platform = "macos-arm64";
        return seams;
    }

    VideoGenerateResult go(const apogee::harness::CancellationToken& token = {}) {
        return apogee::commands::generate_clip(config, roots, options, seams(), token);
    }
};

}  // namespace

TEST_CASE("a clip from the pointer: said, written whole, its record beside it",
          "[cli][video][generate]") {
    Run run;
    const VideoGenerateResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(slurp(run.output) == "MP4 at 24");

    // What it said before it ran, and what the driver was asked.
    REQUIRE(run.said.size() == 1);
    CHECK(run.said.front() ==
          "org--clip/pipeline/aaaaaaaaaaaa (models.default_video): 1280x704, 121 frames (5 s at "
          "24 fps), 50 steps, seed 4242");
    CHECK(run.request["seed"] == 4242);
    CHECK(run.request["frames"] == 121);
    CHECK(run.request["width"] == 1280);
    CHECK(run.request["height"] == 704);
    CHECK(run.request["steps"] == 50);
    CHECK(run.request["pipeline"] == run.pipeline.string());
    CHECK_FALSE(run.request.contains("variant"));
    CHECK_FALSE(run.request.contains("image"));

    // The record: the document, and the sidecar that is it.
    const nlohmann::json& document = result.document;
    CHECK(document["object"] == "video.generation");
    CHECK(document["status"] == "done");
    CHECK(document["output"] == run.output.string());
    CHECK(document["sidecar"] == run.output.string() + ".json");
    CHECK(document["model"]["name"] == kHandle);
    CHECK(document["model"]["class"] == "WanPipeline");
    CHECK(document["model"]["ref"] == "Wan-AI/Wan2.2-TI2V-5B-Diffusers");
    CHECK(document["model"]["variant"].is_null());
    CHECK(document["prompt"] == "a heron lands on a pier at dusk");
    CHECK(document["image"].is_null());
    CHECK(document["mode"] == "t2v");
    CHECK(document["seed"] == 4242);
    CHECK(document["seed_source"] == "generated");
    CHECK(document["settings"] == nlohmann::json{{"width", 1280},
                                                 {"height", 704},
                                                 {"duration", 5.0},
                                                 {"fps", 24},
                                                 {"frames", 121},
                                                 {"steps", 50},
                                                 {"guidance", nullptr}});
    CHECK(document["device"] ==
          nlohmann::json{{"device", "mps"}, {"dtype", "bfloat16"}, {"platform", "macos-arm64"}});
    CHECK(document["versions"]["torch"] == "2.9.1");
    CHECK(document["versions"]["diffusers"] == "0.35.1");
    CHECK_FALSE(document["versions"]["apogee"].get<std::string>().empty());
    CHECK(document["codec"] == "h264");
    CHECK_FALSE(document.contains("error"));
    CHECK(nlohmann::json::parse(slurp(apogee::harness::video_sidecar_path(run.output))) ==
          document);

    // The text form: the clip, what it is, where its record is.
    REQUIRE(result.lines.size() == 3);
    CHECK(result.lines.at(0) == run.output.string());
    CHECK(result.lines.at(1).starts_with(
        "1280x704, 5 frames at 24 fps (5 s), 50 steps, seed 4242 -- "));
    CHECK(result.lines.at(1).ends_with(" s on mps"));
    CHECK(result.lines.at(2) == "record: " + run.output.string() + ".json");
}

TEST_CASE(
    "the model flag wins over the pointer, and a pipeline is named every way the store names one",
    "[cli][video][generate][model]") {
    Run run;
    apogee::commands::ResolvedPipeline resolved;
    const auto resolve = [&](std::string_view given, std::string_view pointer = {}) {
        resolved = {};
        return apogee::commands::resolve_video_pipeline(run.roots, given, pointer, resolved);
    };
    for (const std::string& name :
         {std::string{kHandle}, std::string{"aaaaaaaaaaaa"}, std::string{"org--clip"},
          std::string{"org/clip"}, run.pipeline.string()}) {
        INFO(name);
        CHECK(resolve(name, "elsewhere").empty());
        CHECK(resolved.dir == run.pipeline);
        CHECK(resolved.source == "--model");
        CHECK(resolved.pipeline_class == "WanPipeline");
    }
    CHECK(resolve("", kHandle).empty());
    CHECK(resolved.source == "models.default_video");
    CHECK(resolved.handle == kHandle);

    CHECK_THAT(resolve(""), ContainsSubstring("'apogee config set-default-video <pipeline>'"));
    apogee::testing::write_pipeline_model(run.roots.models / "org--clip" / "pipeline" /
                                          "bbbbbbbbbbbb");
    CHECK(resolve("org--clip") ==
          "'org--clip' holds 2 pipelines -- name one:\n  org--clip/pipeline/aaaaaaaaaaaa\n  "
          "org--clip/pipeline/bbbbbbbbbbbb");
    std::filesystem::create_directories(run.roots.models / "org--m" / "gguf" / "cccccccccccc");
    std::ofstream{run.roots.models / "org--m" / "gguf" / "cccccccccccc" / "m.gguf"} << "GGUF";
    CHECK(resolve("org--m/gguf/cccccccccccc") ==
          "'org--m/gguf/cccccccccccc' is a GGUF, not a "
          "pipeline");
    CHECK(resolve("org--m") ==
          "'org--m' holds no pipeline -- 'apogee models list' shows the "
          "store's");
    CHECK(resolve(run.dir.path().string()) ==
          run.dir.path().string() + " is not a pipeline (no model_index.json)");
}

TEST_CASE("the caps refuse quoting their values, before anything runs",
          "[cli][video][generate][caps]") {
    Run run;
    const auto refused = [&run](const std::function<void(VideoGenerateOptions&)>& set) {
        run.options = {};
        run.options.prompt = "a heron";
        run.options.output = run.output;
        set(run.options);
        const VideoGenerateResult result = run.go();
        CHECK(result.exit_code == 1);
        CHECK(result.document.is_null());
        return result.error;
    };
    CHECK(refused([](VideoGenerateOptions& o) { o.duration = 30.0; }) ==
          "--duration 30 is over the cap of 10 seconds a clip -- generate a shorter clip");
    CHECK(refused([](VideoGenerateOptions& o) { o.duration = 0.0; }) ==
          "--duration must be above 0 seconds");
    CHECK(refused([](VideoGenerateOptions& o) { o.resolution = "1300x704"; }) ==
          "--resolution 1300x704: each side must be a multiple of 16");
    CHECK(refused([](VideoGenerateOptions& o) { o.resolution = "4096x704"; }) ==
          "--resolution 4096x704: the cap is 1920 pixels a side");
    CHECK(refused([](VideoGenerateOptions& o) { o.resolution = "64x64"; }) ==
          "--resolution 64x64: the least is 128 pixels a side");
    CHECK(refused([](VideoGenerateOptions& o) { o.resolution = "wide"; }) ==
          "--resolution takes WIDTHxHEIGHT, like 1280x704 (got 'wide')");
    CHECK(refused([](VideoGenerateOptions& o) { o.steps = 200; }) ==
          "--steps 200 is over the cap of 100 steps");
    CHECK(refused([](VideoGenerateOptions& o) { o.steps = 0; }) == "--steps must be at least 1");
    CHECK(refused([](VideoGenerateOptions& o) { o.fps = 120; }) ==
          "--fps 120 is outside 4-60 frames a second");
    CHECK(refused([](VideoGenerateOptions& o) { o.prompt.clear(); }) ==
          "a prompt is required -- say what the clip shows");
    CHECK_FALSE(run.spawned);

    using apogee::commands::video_frames;
    CHECK(video_frames(5.0, 24) == 121);
    CHECK(video_frames(1.0, 8) == 9);
    CHECK(video_frames(10.0, 24) == 241);
    CHECK(video_frames(0.1, 8) == 5);
}

TEST_CASE("the output is written once, whole, or not at all -- and said either way",
          "[cli][video][generate][output]") {
    Run run;
    SECTION("an existing clip is never written over") {
        std::ofstream{run.output} << "an earlier clip";
        const VideoGenerateResult result = run.go();
        CHECK(result.exit_code == 1);
        CHECK(result.error == run.output.string() +
                                  " exists -- a generation never writes over a file; name another "
                                  "with -o");
        CHECK(slurp(run.output) == "an earlier clip");
        CHECK_FALSE(run.spawned);
    }
    SECTION("no directory for it") {
        run.options.output = run.dir.path() / "nowhere" / "clip.mp4";
        CHECK(run.go().error ==
              "no directory " + (run.dir.path() / "nowhere").string() + " for the clip");
    }
    SECTION("a seed image that is not there") {
        run.options.image = run.dir.path() / "missing.png";
        CHECK(run.go().error ==
              "--image " + (run.dir.path() / "missing.png").string() + " is not a file");
    }
    SECTION("the ladder refuses: its words, nothing spawned") {
        run.ready.refusal = apogee::video::VideoRefusal::NoFfmpeg;
        run.ready.reason = "the frames are assembled into an mp4 with ffmpeg, which is not on PATH";
        run.ready.remedy = "install ffmpeg";
        CHECK(run.go().error ==
              "the frames are assembled into an mp4 with ffmpeg, which is not on "
              "PATH -- install ffmpeg");
        CHECK_FALSE(run.spawned);
    }
    SECTION("the driver fails: a document that says so, no clip, no sidecar") {
        run.script = R"({"error": "generation failed: RuntimeError: MPS backend out of memory"})"
                     "\n";
        run.status = 5;
        const VideoGenerateResult result = run.go();
        CHECK(result.exit_code == 1);
        CHECK(result.document["status"] == "failed");
        CHECK(result.document["sidecar"].is_null());
        CHECK_THAT(result.document["error"].get<std::string>(),
                   ContainsSubstring("MPS backend out of memory"));
        CHECK(result.error.ends_with(" -- " + run.output.string() + " was not written"));
        CHECK_FALSE(std::filesystem::exists(run.output));
        CHECK_FALSE(std::filesystem::exists(apogee::harness::video_sidecar_path(run.output)));
    }
    SECTION("cancelled: said, exit 130, nothing written") {
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        token.cancel();
        const VideoGenerateResult result = run.go(token);
        CHECK(result.exit_code == 130);
        CHECK(result.document["status"] == "cancelled");
        CHECK(result.error ==
              "cancelled -- nothing was written (" + run.output.string() + " untouched)");
        CHECK_FALSE(std::filesystem::exists(run.output));
    }
}

TEST_CASE("a given seed is the seed used, and a seed image makes it image-to-video",
          "[cli][video][generate]") {
    Run run;
    std::ofstream{run.dir.path() / "seed.png"} << "PNG";
    run.options.seed = 7;
    run.options.image = run.dir.path() / "seed.png";
    run.options.model = "org--clip";
    run.options.resolution = "704x1280";
    run.script =
        R"({"event": "ready", "device": "cuda", "dtype": "bfloat16", "pipeline": "WanImageToVideoPipeline", "mode": "i2v"})"
        "\n"
        R"({"event": "done", "frames": 5, "width": 704, "height": 1280})"
        "\n";
    const VideoGenerateResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.request["seed"] == 7);
    CHECK(run.request["image"] == (run.dir.path() / "seed.png").string());
    CHECK(run.request["width"] == 704);
    CHECK(result.document["seed_source"] == "given");
    CHECK(result.document["mode"] == "i2v");
    CHECK(result.document["image"] == (run.dir.path() / "seed.png").string());
    CHECK(run.said.front().starts_with("org--clip/pipeline/aaaaaaaaaaaa (--model): 704x1280"));
}

TEST_CASE("the narration says the device and the scale to expect, then what happened",
          "[cli][video][generate][narration]") {
    using apogee::commands::narrate_video_event;
    using Kind = apogee::video::VideoEvent::Kind;
    apogee::video::VideoEvent ready{.kind = Kind::Ready};
    ready.ready = {.device = "mps", .dtype = "bfloat16", .mode = "t2v"};
    CHECK(narrate_video_event(ready) ==
          "running on mps (bfloat16) -- expect minutes per clip on Apple silicon");
    ready.ready = {.device = "cuda", .dtype = "bfloat16", .mode = "i2v"};
    CHECK(narrate_video_event(ready) ==
          "running on cuda (bfloat16), from the seed image -- expect a minute or more per clip "
          "on a GPU");
    ready.ready = {.device = "cpu", .dtype = "float32", .mode = "t2v"};
    CHECK(narrate_video_event(ready) ==
          "running on cpu (float32) -- expect hours per clip on the "
          "CPU -- a GPU or Apple silicon is far faster");
    CHECK_FALSE(narrate_video_event({.kind = Kind::Step, .step = 1, .steps = 2}).has_value());
    CHECK(narrate_video_event({.kind = Kind::Done, .frames = 121, .width = 1280, .height = 704}) ==
          "121 frames written, 1280x704");
    CHECK(narrate_video_event({.kind = Kind::Assembling, .frames = 121}) ==
          "assembling 121 frames into the clip (ffmpeg)");
    CHECK(narrate_video_event({.kind = Kind::Message, .text = "loading the pipeline"}) ==
          "loading the pipeline");
}
