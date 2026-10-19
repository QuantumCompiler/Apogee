#include "cli/video_generate.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "contracts/sha256.h"
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

// ---- the loop over the record (49d) -------------------------------------------------

namespace {

/// A first clip, landed with its record: what the loop runs again.
[[nodiscard]] std::filesystem::path first_clip(Run& run, const std::string& name = "first.mp4") {
    run.options.output = run.dir.path() / "out" / name;
    const VideoGenerateResult result = run.go();
    REQUIRE(result.exit_code == 0);
    run.said.clear();
    return run.options.output;
}

}  // namespace

TEST_CASE("a run from a record runs it again, the seed held and the flags' changes over it",
          "[cli][video][iterate][from]") {
    Run run;
    const std::filesystem::path clip = first_clip(run);
    const std::filesystem::path record = apogee::harness::video_sidecar_path(clip);

    run.options = {};
    run.options.from = clip;
    run.options.prompt = "a heron lands on a pier at dusk, rain";
    run.options.output = run.dir.path() / "out" / "dusk.mp4";
    const VideoGenerateResult again = run.go();
    INFO(again.error);
    REQUIRE(again.exit_code == 0);
    // What came from the record and what the flags changed, said before the run.
    REQUIRE(run.said.size() == 3);
    CHECK(run.said.at(0) == "from " + record.string() +
                                ": model org--clip/pipeline/aaaaaaaaaaaa, 1280x704, 24 fps, 5 s, "
                                "50 steps, seed 4242 (held)");
    CHECK(run.said.at(1) == "changed: the prompt");
    CHECK(run.said.at(2).starts_with("org--clip/pipeline/aaaaaaaaaaaa (the record): 1280x704"));
    // The run itself: the record's settings, the new prompt, the seed held.
    CHECK(run.request["seed"] == 4242);
    CHECK(run.request["prompt"] == "a heron lands on a pier at dusk, rain");
    CHECK(run.request["frames"] == 121);
    // Its record chains to its parent.
    CHECK(again.document["seed_source"] == "held");
    CHECK(again.document["parent"] == nlohmann::json{{"move", "from"},
                                                     {"document", record.string()},
                                                     {"clip", clip.string()},
                                                     {"continued", nullptr}});
    CHECK(again.document["preset"].is_null());

    // Named by the record itself, the seed changed: said as a change.
    run.said.clear();
    run.options.from = record;
    run.options.prompt.clear();
    run.options.seed = 7;
    run.options.output = run.dir.path() / "out" / "seven.mp4";
    const VideoGenerateResult seven = run.go();
    REQUIRE(seven.exit_code == 0);
    CHECK(run.said.at(1) == "changed: seed 7");
    CHECK(run.request["seed"] == 7);
    CHECK(run.request["prompt"] == "a heron lands on a pier at dusk");
    CHECK(seven.document["seed_source"] == "given");

    // Nothing changed is said too: the same run again.
    run.said.clear();
    run.options.seed.reset();
    run.options.output = run.dir.path() / "out" / "same.mp4";
    REQUIRE(run.go().exit_code == 0);
    CHECK(run.said.at(1) == "changed: nothing -- the same run again");
}

TEST_CASE("a run from a record refuses what holds none, and says a record from elsewhere",
          "[cli][video][iterate][from]") {
    Run run;
    const std::filesystem::path clip = first_clip(run);
    run.options = {};
    run.options.output = run.dir.path() / "out" / "next.mp4";
    const auto refused = [&run](const std::filesystem::path& from) {
        run.options.from = from;
        const VideoGenerateResult result = run.go();
        CHECK(result.exit_code == 1);
        return result.error;
    };
    std::ofstream{run.dir.path() / "bare.mp4"} << "MP4";
    CHECK(refused(run.dir.path() / "bare.mp4") ==
          "no record beside " + (run.dir.path() / "bare.mp4").string() +
              " (bare.mp4.json is not there) -- settings are never guessed from pixels; name the "
              "record itself with --from <record.json>");
    CHECK(refused(run.dir.path() / "nowhere.json") ==
          "no record at " + (run.dir.path() / "nowhere.json").string());
    std::ofstream{run.dir.path() / "other.json"} << R"({"object": "list"})";
    CHECK(refused(run.dir.path() / "other.json") ==
          (run.dir.path() / "other.json").string() + " is not a video.generation record");
    run.options.extend = clip;
    CHECK(refused(clip) == "--from and --extend are two moves -- take one");
    run.options.extend.clear();

    // A record made on another platform runs, and says what that means.
    nlohmann::json record = nlohmann::json::parse(slurp(apogee::harness::video_sidecar_path(clip)));
    record["device"]["platform"] = "linux-x64";
    record["device"]["device"] = "cuda";
    std::ofstream{run.dir.path() / "elsewhere.json"} << record.dump();
    run.options.from = run.dir.path() / "elsewhere.json";
    run.said.clear();
    REQUIRE(run.go().exit_code == 0);
    CHECK(run.said.at(1) ==
          "recorded on linux-x64 (cuda); this is macos-arm64 -- the same seed "
          "may not make the same clip here");
}

TEST_CASE("a continuation starts from the clip's last frame, matching its record",
          "[cli][video][iterate][extend]") {
    Run run;
    run.options.resolution = "704x1280";
    const std::filesystem::path clip = first_clip(run, "shot1.mp4");
    std::vector<std::filesystem::path> framed;
    std::filesystem::path frame;
    const auto extend = [&](VideoGenerateOptions options) {
        run.options = std::move(options);
        VideoGenerateSeams seams = run.seams();
        seams.last_frame = [&](const std::filesystem::path& from,
                               const std::filesystem::path& png) {
            framed.push_back(from);
            frame = png;
            std::ofstream{png} << "PNG";
            return std::string{};
        };
        return apogee::commands::generate_clip(run.config, run.roots, run.options, seams, {});
    };
    run.script =
        R"({"event": "ready", "device": "mps", "dtype": "bfloat16", "pipeline": "WanImageToVideoPipeline", "mode": "i2v"})"
        "\n"
        R"({"event": "done", "frames": 5, "width": 704, "height": 1280})"
        "\n";
    VideoGenerateOptions options;
    options.prompt = "the heron takes off";
    options.extend = clip;
    options.output = run.dir.path() / "out" / "shot2.mp4";
    const VideoGenerateResult second = extend(options);
    INFO(second.error);
    REQUIRE(second.exit_code == 0);
    REQUIRE(framed.size() == 1);
    CHECK(framed.front() == clip);
    // The frame is the seed image, taken for the run and gone after it.
    CHECK(run.request["image"] == frame.string());
    CHECK(frame.parent_path() == run.dir.path() / "cache" / "video");
    CHECK_FALSE(std::filesystem::exists(frame));
    // The continuation matches its parent's frame and rate; its seed is new.
    CHECK(run.request["width"] == 704);
    CHECK(run.request["height"] == 1280);
    CHECK(run.said.at(0) == "extending " + clip.string() +
                                " from its last frame; from its record: model "
                                "org--clip/pipeline/aaaaaaaaaaaa, 704x1280, 24 fps");
    CHECK(second.document["mode"] == "i2v");
    CHECK(second.document["image"].is_null());
    CHECK(second.document["seed_source"] == "generated");
    CHECK(second.document["parent"] ==
          nlohmann::json{{"move", "extend"},
                         {"document", apogee::harness::video_sidecar_path(clip).string()},
                         {"clip", clip.string()},
                         {"continued", clip.string()}});

    // Run again from its record: it continues the same clip again.
    framed.clear();
    VideoGenerateOptions again;
    again.from = run.dir.path() / "out" / "shot2.mp4";
    again.output = run.dir.path() / "out" / "shot2b.mp4";
    const VideoGenerateResult rerun = extend(again);
    INFO(rerun.error);
    REQUIRE(rerun.exit_code == 0);
    REQUIRE(framed.size() == 1);
    CHECK(framed.front() == clip);
    CHECK(rerun.document["parent"]["move"] == "from");
    CHECK(rerun.document["parent"]["continued"] == clip.string());
    CHECK(rerun.document["seed"] == second.document["seed"]);

    // What it refuses.
    VideoGenerateOptions both = options;
    both.image = clip;
    both.output = run.dir.path() / "out" / "both.mp4";
    CHECK(extend(both).error ==
          "--extend continues from the clip's last frame; it takes no --image");
    VideoGenerateOptions missing = options;
    missing.extend = run.dir.path() / "gone.mp4";
    missing.output = run.dir.path() / "out" / "missing.mp4";
    CHECK(extend(missing).error ==
          "--extend " + (run.dir.path() / "gone.mp4").string() + " is not a file");
}

TEST_CASE("a frame that cannot be taken refuses the continuation, naming the clip",
          "[cli][video][iterate][extend]") {
    Run run;
    const std::filesystem::path clip = first_clip(run);
    run.options = {};
    run.options.prompt = "on";
    run.options.extend = clip;
    run.options.output = run.dir.path() / "out" / "next.mp4";
    VideoGenerateSeams seams = run.seams();
    seams.last_frame = [](const std::filesystem::path&, const std::filesystem::path&) {
        return std::string{"ffmpeg failed: Invalid data found when processing input"};
    };
    run.spawned = false;
    const VideoGenerateResult result =
        apogee::commands::generate_clip(run.config, run.roots, run.options, seams, {});
    CHECK(result.error == "could not take the last frame of " + clip.string() +
                              ": ffmpeg failed: Invalid data found when processing input");
    CHECK_FALSE(run.spawned);
}

TEST_CASE("a draft, then the final from it: one seed, one frame, only the steps",
          "[cli][video][iterate][presets]") {
    Run run;
    run.options.preset = "draft";
    const std::filesystem::path draft = first_clip(run, "draft.mp4");
    CHECK(run.request["steps"] == apogee::commands::kDraftPreset.steps);
    const nlohmann::json drafted =
        nlohmann::json::parse(slurp(apogee::harness::video_sidecar_path(draft)));
    CHECK(drafted["preset"] == "draft");
    CHECK(drafted["settings"]["steps"] == 16);

    run.options = {};
    run.options.from = draft;
    run.options.preset = "final";
    run.options.output = run.dir.path() / "out" / "final.mp4";
    const VideoGenerateResult final_run = run.go();
    INFO(final_run.error);
    REQUIRE(final_run.exit_code == 0);
    CHECK(run.said.at(1) == "changed: 50 steps (--final)");
    // The same seed, frame and length as the draft: the draft's clip, finished.
    CHECK(run.request["seed"] == drafted["seed"]);
    CHECK(run.request["width"] == drafted["settings"]["width"]);
    CHECK(run.request["height"] == drafted["settings"]["height"]);
    CHECK(run.request["frames"] == drafted["settings"]["frames"]);
    CHECK(run.request["steps"] == 50);
    CHECK(final_run.document["preset"] == "final");
    CHECK(final_run.document["parent"]["move"] == "from");

    // A preset said on a run of its own; --steps given wins over it.
    run.said.clear();
    run.options = {};
    run.options.prompt = "a heron";
    run.options.preset = "draft";
    run.options.steps = 30;
    run.options.output = run.dir.path() / "out" / "thirty.mp4";
    REQUIRE(run.go().exit_code == 0);
    CHECK(run.request["steps"] == 30);
    CHECK(run.said.at(0) == "preset: draft -- 30 steps");
}

TEST_CASE("a record reads back into the run it records, its parent and preset with it",
          "[cli][video][iterate][record]") {
    apogee::harness::VideoRun run;
    run.status = "done";
    run.output = "/clips/b.mp4";
    run.sidecar = "/clips/b.mp4.json";
    run.model = "org--clip/pipeline/aaaaaaaaaaaa";
    run.prompt = "a heron";
    run.seed = 42;
    run.seed_source = "held";
    run.settings = {
        .width = 1280, .height = 704, .duration = 5.0, .fps = 24, .frames = 121, .steps = 16};
    run.platform = "macos-arm64";
    run.device = "mps";
    run.parent = {.move = "from", .document = "/clips/a.mp4.json", .clip = "/clips/a.mp4"};
    run.preset = "draft";
    const nlohmann::json document = apogee::harness::video_run_document(run);
    std::string error;
    const std::optional<apogee::harness::VideoRun> back =
        apogee::harness::parse_video_run(document, error);
    REQUIRE(back.has_value());
    CHECK(apogee::harness::video_run_document(*back) == document);
    CHECK(back->parent.document == "/clips/a.mp4.json");
    CHECK(back->settings.guidance == std::nullopt);

    nlohmann::json no_seed = document;
    no_seed.erase("seed");
    CHECK_FALSE(apogee::harness::parse_video_run(no_seed, error).has_value());
    CHECK(error == "a video.generation record without its model, seed or settings");
    CHECK_FALSE(apogee::harness::parse_video_run(nlohmann::json::array(), error).has_value());
    CHECK(error == "not a video.generation record");
}

namespace {

/// A LoRA file in the run's directory, its content its name.
[[nodiscard]] std::filesystem::path lora(const Run& run, const std::string& name) {
    const std::filesystem::path path = run.dir.path() / "loras" / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << "weights of " << name;
    return path;
}

}  // namespace

TEST_CASE("a LoRA's strength is read after its last colon, and only a number is one",
          "[cli][video][lora]") {
    using apogee::commands::parse_lora;
    std::filesystem::path path;
    double scale = 0.0;
    CHECK(parse_lora("style.safetensors", path, scale).empty());
    CHECK(path == "style.safetensors");
    CHECK(scale == 1.0);
    CHECK(parse_lora("style.safetensors:0.8", path, scale).empty());
    CHECK(path == "style.safetensors");
    CHECK(scale == 0.8);
    CHECK(parse_lora("C:\\loras\\style.safetensors", path, scale).empty());
    CHECK(path == "C:\\loras\\style.safetensors");
    CHECK(scale == 1.0);
    CHECK(parse_lora("dir/a:b.safetensors", path, scale).empty());
    CHECK(path == "dir/a:b.safetensors");
    CHECK(parse_lora("style.safetensors:2", path, scale).empty());
    CHECK(parse_lora("style.safetensors:3", path, scale) ==
          "--lora style.safetensors:3: its strength 3 is outside (0, 2]");
    CHECK(parse_lora("style.safetensors:0", path, scale) ==
          "--lora style.safetensors:0: its strength 0 is outside (0, 2]");
    CHECK(parse_lora("style.safetensors:-1", path, scale) ==
          "--lora style.safetensors:-1: its strength -1 is outside (0, 2]");
    CHECK(parse_lora(":0.5", path, scale) ==
          "--lora takes <path>[:<scale>] -- name the LoRA's file");
}

TEST_CASE("video generate --lora hands each LoRA to the driver and records its hash",
          "[cli][video][lora]") {
    Run run;
    run.ready.peft = true;
    run.ready.peft_version = "0.17.1";
    const std::filesystem::path style = lora(run, "style.safetensors");
    // Spelled with the platform's separator, as the directory walk that finds
    // the one file returns it: on Windows a joined "motion/..." keeps its '/'.
    const std::filesystem::path motion = lora(run, "motion/motion.safetensors").lexically_normal();
    run.options.loras = {style.string() + ":0.8", motion.parent_path().string()};
    const VideoGenerateResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.request["loras"] ==
          nlohmann::json::array({{{"path", style.string()}, {"scale", 0.8}},
                                 {{"path", motion.string()}, {"scale", 1.0}}}));
    CHECK(result.document["loras"] ==
          nlohmann::json::array(
              {{{"path", style.string()},
                {"sha256", apogee::models::sha256_hex("weights of style.safetensors")},
                {"scale", 0.8}},
               {{"path", motion.string()},
                {"sha256", apogee::models::sha256_hex("weights of motion/motion.safetensors")},
                {"scale", 1.0}}}));
    CHECK(result.document["versions"]["peft"] == "0.17.1");
    CHECK(run.said.back().ends_with(", LoRAs: style.safetensors x0.8, motion.safetensors x1"));
    // Read back, the record holds them.
    std::string error;
    const auto record = apogee::harness::parse_video_run(result.document, error);
    REQUIRE(record.has_value());
    REQUIRE(record->loras.size() == 2);
    CHECK(record->loras[0].scale == 0.8);
    CHECK(record->peft == "0.17.1");

    // A run without LoRAs asks exactly what it asked before.
    Run plain;
    REQUIRE(plain.go().exit_code == 0);
    CHECK_FALSE(plain.request.contains("loras"));
}

TEST_CASE("a LoRA is refused before anything runs, naming its fact and its fix",
          "[cli][video][lora]") {
    Run run;
    run.ready.peft = true;
    const std::filesystem::path style = lora(run, "style.safetensors");
    const auto refused = [&run](const std::function<void(VideoGenerateOptions&)>& set) {
        const VideoGenerateOptions kept = run.options;
        set(run.options);
        const VideoGenerateResult result = run.go();
        run.options = kept;
        CHECK(result.exit_code == 1);
        return result.error;
    };
    CHECK(refused([&](VideoGenerateOptions& o) {
              o.loras = std::vector<std::string>(5, style.string());
          }) == "5 LoRAs is over the cap of 4 a run");
    CHECK(refused([&](VideoGenerateOptions& o) {
              o.loras = {(run.dir.path() / "none.safetensors").string()};
          }) == "there is no LoRA at " + (run.dir.path() / "none.safetensors").string());
    CHECK(refused([&](VideoGenerateOptions& o) { o.loras = {style.string() + ":9"}; }) ==
          "--lora " + style.string() + ":9: its strength 9 is outside (0, 2]");
    CHECK(refused([&](VideoGenerateOptions& o) {
              o.loras = {style.string()};
              o.no_lora = true;
          }) == "--lora and --no-lora are two sets -- take one");
    run.ready.peft = false;
    CHECK(refused([&](VideoGenerateOptions& o) { o.loras = {style.string()}; }) ==
          "a LoRA loads through peft, which the video runtime does not have yet -- apogee train "
          "setup --with video");
    CHECK_FALSE(run.spawned);
}

TEST_CASE("a run from a record holds its LoRAs, verified; --lora replaces and --no-lora clears",
          "[cli][video][lora][from]") {
    Run run;
    run.ready.peft = true;
    const std::filesystem::path style = lora(run, "style.safetensors");
    run.options.loras = {style.string() + ":0.7"};
    const std::filesystem::path clip = first_clip(run);
    const auto again = [&run, &clip](const std::string& name,
                                     const std::function<void(VideoGenerateOptions&)>& set) {
        run.options = {};
        run.options.from = clip;
        run.options.output = run.dir.path() / "out" / name;
        set(run.options);
        run.said.clear();
        run.request = nullptr;
        return run.go();
    };

    // Held, as the seed is, and verified by its hash.
    VideoGenerateResult result = again("held.mp4", [](VideoGenerateOptions&) {});
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK_THAT(run.said.at(0), ContainsSubstring("seed 4242 (held), 1 LoRA (held)"));
    CHECK(run.request["loras"] ==
          nlohmann::json::array({{{"path", style.string()}, {"scale", 0.7}}}));
    CHECK(result.document["loras"][0]["sha256"] ==
          apogee::models::sha256_hex("weights of style.safetensors"));

    // --no-lora clears them, said.
    result = again("plain.mp4", [](VideoGenerateOptions& o) { o.no_lora = true; });
    REQUIRE(result.exit_code == 0);
    CHECK(run.said.at(1) == "changed: no LoRAs");
    CHECK_FALSE(run.request.contains("loras"));
    CHECK(result.document["loras"] == nlohmann::json::array());

    // --lora replaces the whole set.
    const std::filesystem::path other = lora(run, "other.safetensors");
    result = again("other.mp4", [&other](VideoGenerateOptions& o) { o.loras = {other.string()}; });
    REQUIRE(result.exit_code == 0);
    CHECK(run.said.at(1) == "changed: 1 LoRA given");
    CHECK(run.request["loras"] ==
          nlohmann::json::array({{{"path", other.string()}, {"scale", 1.0}}}));

    // A file changed since the run, or gone, is refused by name.
    std::ofstream{style, std::ios::binary} << "retrained";
    result = again("changed.mp4", [](VideoGenerateOptions&) {});
    CHECK(result.exit_code == 1);
    CHECK(result.error == "the record's LoRA " + style.string() +
                              " has changed since that run (its sha256 is not the recorded one) "
                              "-- --lora runs it as it is now, --no-lora runs without");
    std::filesystem::remove(style);
    result = again("gone.mp4", [](VideoGenerateOptions&) {});
    CHECK(result.error == "the record's LoRA " + style.string() +
                              " is gone -- --lora names the set to run with, --no-lora runs "
                              "without");
}
