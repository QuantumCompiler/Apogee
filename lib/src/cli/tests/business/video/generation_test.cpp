#include "video/generation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "platform/platform.h"
#include "support/env_guard.h"
#include "support/fake_child.h"

/// One clip, generated (49b), over a scripted driver: the request the
/// driver reads, its protocol read into events in order, the frames
/// assembled by a stand-in and the clip moved into place -- and every way a
/// run ends without one leaving the output path as it was and the working
/// directory gone. No Python, no weights, no ffmpeg: the real driver under
/// stub packages is `video_driver_test`'s.
namespace {

using apogee::testing::FakeChild;
using apogee::video::VideoEvent;
using apogee::video::VideoOutcome;
using apogee::video::VideoReadiness;
using apogee::video::VideoRequest;
using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A driver played by the test: on spawn it reads the request it was handed,
/// writes `frames` frames where the request says, and answers with `script`
/// on stdout (and `stderr` there), exiting with `status`.
struct Driver {
    std::string script =
        R"({"message": "loading torch and diffusers"})"
        "\n"
        R"({"event": "ready", "device": "mps", "dtype": "bfloat16", "pipeline": "WanPipeline", "mode": "t2v", "torch": "2.9.1", "diffusers": "0.35.1"})"
        "\n"
        R"({"event": "step", "step": 1, "steps": 3})"
        "\n"
        R"({"event": "step", "step": 2, "steps": 3})"
        "\n"
        R"({"event": "step", "step": 3, "steps": 3})"
        "\n"
        R"({"event": "done", "frames": 5, "width": 64, "height": 48, "seed": 7})"
        "\n";
    std::string stderr_text{};
    int status = 0;
    int frames = 5;
    bool hang = false;
    /// What the driver was spawned with, and the request it read.
    std::vector<std::string> argv{};
    std::string program{};
    nlohmann::json request{};
    bool* terminated = nullptr;

    [[nodiscard]] apogee::training::Spawner spawner() {
        return [this](const apogee::platform::ChildCommand& command,
                      std::string&) -> std::unique_ptr<apogee::platform::ChildProcess> {
            program = command.program;
            argv = command.arguments;
            if (argv.size() == 3) {
                request = nlohmann::json::parse(slurp(argv.at(2)));
                const std::filesystem::path dir{request.at("frames_dir").get<std::string>()};
                std::filesystem::create_directories(dir);
                for (int i = 1; i <= frames; ++i) {
                    std::ofstream{dir / ("f0000" + std::to_string(i) + ".png")} << "PNG" << i;
                }
            }
            auto child = std::make_unique<FakeChild>();
            child->stdout_script = script;
            child->stderr_script = stderr_text;
            child->exit_status = status;
            child->hang = hang;
            return child;
        };
    }
};

/// A stand-in for ffmpeg: the clip says how many frames and at what rate.
struct Assembler {
    std::string fail_with{};
    int calls = 0;

    [[nodiscard]] apogee::video::VideoAssembler fn() {
        return
            [this](const std::filesystem::path& frames, int fps, const std::filesystem::path& out,
                   const apogee::harness::CancellationToken&, std::string& error) -> std::string {
                ++calls;
                if (!fail_with.empty()) {
                    error = fail_with;
                    return {};
                }
                std::size_t count = 0;
                for (const auto& entry : std::filesystem::directory_iterator(frames)) {
                    count += entry.path().extension() == ".png" ? 1U : 0U;
                }
                std::ofstream{out, std::ios::binary} << "MP4 " << count << " frames at " << fps;
                return "h264";
            };
    }
};

struct Run {
    apogee::testing::TempDir dir{"video-generation-" + std::to_string(std::random_device{}())};
    std::filesystem::path work = dir.path() / "cache" / "video";
    std::filesystem::path output = dir.path() / "out" / "clip.mp4";
    VideoReadiness ready{.interpreter = "/venv/bin/python",
                         .driver = "/home/training/scripts/video_generate.py",
                         .ffmpeg = "ffmpeg"};
    VideoRequest request{.pipeline = "/store/org--v/pipeline/aaaaaaaaaaaa",
                         .prompt = "a heron lands on a pier at dusk",
                         .seed = 7,
                         .width = 64,
                         .height = 48,
                         .frames = 5,
                         .steps = 3,
                         .fps = 24};
    Driver driver;
    Assembler assembler;
    std::vector<VideoEvent> events;

    Run() {
        std::filesystem::create_directories(output.parent_path());
    }

    VideoOutcome go(const apogee::harness::CancellationToken& token = {}) {
        apogee::video::VideoSeams seams;
        seams.spawn = driver.spawner();
        seams.assemble = assembler.fn();
        return apogee::video::generate_video(
            ready, request, output, work,
            [this](const VideoEvent& event) { events.push_back(event); }, token, seams);
    }

    /// No working directory left under the cache.
    [[nodiscard]] bool work_left() const {
        if (!std::filesystem::exists(work)) {
            return false;
        }
        return !std::filesystem::is_empty(work);
    }
};

}  // namespace

TEST_CASE("the request the driver reads carries every setting, and only those it has",
          "[video][generation][request]") {
    VideoRequest request{.pipeline = "/p",
                         .prompt = "un héron, 日本の桟橋",
                         .seed = 42,
                         .width = 1280,
                         .height = 704,
                         .frames = 121,
                         .steps = 50,
                         .fps = 24};
    nlohmann::json document = apogee::video::request_document(request, "/work/frames");
    CHECK(document == nlohmann::json{{"pipeline", "/p"},
                                     {"prompt", "un héron, 日本の桟橋"},
                                     {"seed", 42},
                                     {"width", 1280},
                                     {"height", 704},
                                     {"frames", 121},
                                     {"steps", 50},
                                     {"frames_dir", "/work/frames"}});
    // The fps is the assembly's: the driver never sees it.
    CHECK_FALSE(document.contains("fps"));

    request.negative_prompt = "blurry";
    request.image = "/seed.png";
    request.guidance = 5.0;
    request.variant = "fp16";
    document = apogee::video::request_document(request, "/work/frames");
    CHECK(document["negative_prompt"] == "blurry");
    CHECK(document["image"] == "/seed.png");
    CHECK(document["guidance"] == 5.0);
    CHECK(document["variant"] == "fp16");

    // Each LoRA and its strength, in the order given (49e).
    request.loras = {{.file = "/loras/style.safetensors", .scale = 0.8},
                     {.file = "/loras/motion.safetensors"}};
    document = apogee::video::request_document(request, "/work/frames");
    CHECK(document["loras"] ==
          nlohmann::json::array({{{"path", "/loras/style.safetensors"}, {"scale", 0.8}},
                                 {{"path", "/loras/motion.safetensors"}, {"scale", 1.0}}}));
}

TEST_CASE("the driver's lines are read into events, and nothing else is one",
          "[video][generation][protocol]") {
    using apogee::training::classify_script_line;
    using apogee::video::read_video_event;
    const auto ready = read_video_event(classify_script_line(
        R"({"event": "ready", "device": "cuda", "dtype": "bfloat16", "pipeline": "WanPipeline", "mode": "i2v", "torch": "2.9", "diffusers": "0.36"})"));
    REQUIRE(ready.has_value());
    CHECK(ready->kind == VideoEvent::Kind::Ready);
    CHECK(ready->ready.device == "cuda");
    CHECK(ready->ready.mode == "i2v");
    CHECK(ready->ready.diffusers == "0.36");
    const auto step =
        read_video_event(classify_script_line(R"({"event":"step","step":4,"steps":50})"));
    REQUIRE(step.has_value());
    CHECK(step->kind == VideoEvent::Kind::Step);
    CHECK(step->step == 4);
    CHECK(step->steps == 50);
    const auto done = read_video_event(
        classify_script_line(R"({"event":"done","frames":121,"width":1280,"height":704})"));
    REQUIRE(done.has_value());
    CHECK(done->frames == 121);
    const auto message = read_video_event(classify_script_line(R"({"message":"loading"})"));
    REQUIRE(message.has_value());
    CHECK(message->kind == VideoEvent::Kind::Message);
    CHECK(message->text == "loading");
    CHECK_FALSE(read_video_event(classify_script_line(R"({"event":"unheard-of"})")).has_value());
    CHECK_FALSE(read_video_event(classify_script_line(R"({"error":"fatal"})")).has_value());
}

TEST_CASE("a generation runs the driver, says each step, and puts the clip in place",
          "[video][generation]") {
    Run run;
    const VideoOutcome outcome = run.go();
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    // The interpreter runs the seeded driver on the request, nothing else.
    CHECK(run.driver.program == "/venv/bin/python");
    REQUIRE(run.driver.argv.size() == 3);
    CHECK(run.driver.argv.at(0) == "/home/training/scripts/video_generate.py");
    CHECK(run.driver.argv.at(1) == "--request");
    CHECK(run.driver.request["prompt"] == "a heron lands on a pier at dusk");
    CHECK(run.driver.request["seed"] == 7);
    CHECK(run.driver.request["steps"] == 3);

    // Every event, in order: the note, the device, each step, the frames,
    // the assembly.
    std::vector<VideoEvent::Kind> kinds;
    for (const VideoEvent& event : run.events) {
        kinds.push_back(event.kind);
    }
    using Kind = VideoEvent::Kind;
    CHECK(kinds == std::vector<Kind>{Kind::Message, Kind::Ready, Kind::Step, Kind::Step, Kind::Step,
                                     Kind::Done, Kind::Assembling});
    CHECK(run.events.at(4).step == 3);
    CHECK(run.events.at(4).steps == 3);

    CHECK(outcome.ready.device == "mps");
    CHECK(outcome.ready.torch == "2.9.1");
    CHECK(outcome.frames == 5);
    CHECK(outcome.width == 64);
    CHECK(outcome.codec == "h264");
    CHECK(slurp(run.output) == "MP4 5 frames at 24");
    CHECK_FALSE(run.work_left());
}

TEST_CASE("a generation that ends without a clip leaves the output as it was",
          "[video][generation]") {
    Run run;
    std::ofstream{run.output, std::ios::binary} << "an earlier clip";
    SECTION("the driver crashes: its code and its stderr tail said") {
        run.driver.script = R"({"event": "step", "step": 1, "steps": 3})"
                            "\n";
        run.driver.stderr_text =
            "Traceback (most recent call last):\nRuntimeError: MPS backend "
            "out of memory\n";
        run.driver.status = 1;
        const VideoOutcome outcome = run.go();
        CHECK_FALSE(outcome.ok);
        CHECK_THAT(outcome.error, ContainsSubstring("video_generate.py exited with code 1"));
        CHECK_THAT(outcome.error, ContainsSubstring("MPS backend out of memory"));
    }
    SECTION("the driver refuses in its own words") {
        run.driver.script =
            R"({"error": "this pipeline (HunyuanVideoPipeline) has no image-to-video form -- generate without a seed image"})"
            "\n";
        run.driver.status = 4;
        const VideoOutcome outcome = run.go();
        CHECK_FALSE(outcome.ok);
        CHECK_THAT(outcome.error, ContainsSubstring("has no image-to-video form"));
    }
    SECTION("the driver says done without writing a frame") {
        run.driver.frames = 0;
        const VideoOutcome outcome = run.go();
        CHECK(outcome.error == "the driver ended without writing its frames");
        CHECK(run.assembler.calls == 0);
    }
    SECTION("ffmpeg fails") {
        run.assembler.fail_with = "ffmpeg failed: Unknown encoder 'libx264'";
        const VideoOutcome outcome = run.go();
        CHECK(outcome.error == "ffmpeg failed: Unknown encoder 'libx264'");
    }
    SECTION("cancelled mid-run: the child ended, nothing kept") {
        run.driver.hang = true;
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        token.cancel();
        const VideoOutcome outcome = run.go(token);
        CHECK(outcome.cancelled);
        CHECK(outcome.error == "cancelled");
        CHECK(run.assembler.calls == 0);
    }
    CHECK(slurp(run.output) == "an earlier clip");
    CHECK_FALSE(run.work_left());
}

TEST_CASE("a run refused by the ladder spawns nothing", "[video][generation]") {
    Run run;
    run.ready.refusal = apogee::video::VideoRefusal::NoDiffusers;
    run.ready.reason = "diffusers is not installed in the Python environment at /venv";
    run.ready.remedy = "apogee train setup --with video";
    const VideoOutcome outcome = run.go();
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.error ==
          "diffusers is not installed in the Python environment at /venv -- "
          "apogee train setup --with video");
    CHECK(run.driver.argv.empty());
    CHECK_FALSE(std::filesystem::exists(run.output));
}

TEST_CASE("a dead run's frames are swept by the next run, a live one's never",
          "[video][generation]") {
    Run run;
    const std::filesystem::path dead = run.work / "run-999999999-1";
    const std::filesystem::path live =
        run.work / ("run-" + std::to_string(apogee::platform::current_process_id()) + "-2");
    for (const std::filesystem::path& dir : {dead, live}) {
        std::filesystem::create_directories(dir / "frames");
        std::ofstream{dir / "frames" / "f00001.png"} << "PNG";
    }
    REQUIRE(run.go().ok);
    CHECK_FALSE(std::filesystem::exists(dead));
    CHECK(std::filesystem::exists(live / "frames" / "f00001.png"));
}
