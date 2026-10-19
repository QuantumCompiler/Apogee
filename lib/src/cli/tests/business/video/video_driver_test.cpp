#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "platform/child_process.h"
#include "platform/platform.h"
#include "support/env_guard.h"
#include "support/pipeline_model.h"
#include "training/script_runner.h"
#include "video/generation.h"

/// The shipped `video_generate.py` (49b) run for real under `python3` --
/// over stub `torch` and `diffusers` packages (`scripts/stub_video/`), never
/// the real libraries and never weights: the request read, the device and
/// the type chosen as torch reports them, the pipeline loaded by the class
/// its index names, the call diffusers gets built from the request, a step
/// said per denoising step, the frames written, every refusal in its own
/// words -- and a library's print never reaching the protocol channel.
namespace {

using Catch::Matchers::ContainsSubstring;
using nlohmann::json;

const std::filesystem::path kDriver =
    std::filesystem::path{APOGEE_ASSETS_DIR} / "video" / "video_generate.py";
const std::filesystem::path kStubs =
    std::filesystem::path{APOGEE_TESTS_DIR} / "business" / "video" / "scripts" / "stub_video";

[[nodiscard]] std::string python3() {
    return apogee::platform::find_on_path("python3");
}

#define REQUIRE_PYTHON()                                               \
    do {                                                               \
        if (!apogee::platform::supports_child_processes()) {           \
            SKIP("no child processes on this platform");               \
        }                                                              \
        if (python3().empty()) {                                       \
            SKIP("no python3 on PATH: the driver's suite cannot run"); \
        }                                                              \
    } while (false)

[[nodiscard]] std::vector<json> records_in(const std::filesystem::path& file) {
    std::vector<json> out;
    std::ifstream in{file};
    std::string line;
    while (std::getline(in, line)) {
        out.push_back(json::parse(line));
    }
    return out;
}

/// A pipeline, a request and the driver run on it.
struct Run {
    apogee::testing::TempDir dir{"video-driver-" + std::to_string(std::random_device{}())};
    std::filesystem::path pipeline = dir.path() / "pipeline";
    std::filesystem::path frames = dir.path() / "frames";
    std::filesystem::path record = dir.path() / "record.jsonl";
    json request{{"prompt", "a heron lands on a pier at dusk"},
                 {"seed", 1234},
                 {"width", 64},
                 {"height", 48},
                 {"frames", 3},
                 {"steps", 4}};
    std::vector<std::pair<std::string, std::string>> environment{};
    std::vector<apogee::training::ScriptEvent> lines;

    explicit Run(const apogee::testing::PipelineModelSpec& spec = {}) {
        apogee::testing::write_pipeline_model(pipeline, spec);
        request["pipeline"] = pipeline.string();
        request["frames_dir"] = frames.string();
    }

    apogee::training::ScriptOutcome go(const apogee::harness::CancellationToken& token = {}) {
        const std::filesystem::path file = dir.path() / "request.json";
        std::ofstream{file, std::ios::binary} << request.dump();
        apogee::training::ScriptRequest script{
            .interpreter = python3(),
            .script = kDriver,
            .arguments = {"--request", file.string()},
            .environment = environment,
        };
        script.environment.emplace_back("PYTHONPATH", kStubs.string());
        script.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
        script.environment.emplace_back("STUB_VIDEO_RECORD", record.string());
        return apogee::training::run_script(
            script, [this](const apogee::training::ScriptEvent& line) { lines.push_back(line); },
            token);
    }

    /// The events among what it said.
    [[nodiscard]] std::vector<apogee::video::VideoEvent> events() const {
        std::vector<apogee::video::VideoEvent> out;
        for (const apogee::training::ScriptEvent& line : lines) {
            if (auto event = apogee::video::read_video_event(line)) {
                out.push_back(*event);
            }
        }
        return out;
    }
};

}  // namespace

TEST_CASE("the driver loads the pipeline, says where, steps, and writes every frame",
          "[video][driver]") {
    REQUIRE_PYTHON();
    Run run;
    const apogee::training::ScriptOutcome outcome = run.go();
    INFO(outcome.describe());
    REQUIRE(outcome.ok);

    const std::vector<apogee::video::VideoEvent> events = run.events();
    using Kind = apogee::video::VideoEvent::Kind;
    std::vector<int> steps;
    const apogee::video::VideoEvent* ready = nullptr;
    const apogee::video::VideoEvent* done = nullptr;
    for (const apogee::video::VideoEvent& event : events) {
        if (event.kind == Kind::Ready) {
            ready = &event;
        } else if (event.kind == Kind::Step) {
            steps.push_back(event.step);
            CHECK(event.steps == 4);
        } else if (event.kind == Kind::Done) {
            done = &event;
        }
    }
    REQUIRE(ready != nullptr);
    CHECK(ready->ready.device == "cpu");
    CHECK(ready->ready.dtype == "float32");
    CHECK(ready->ready.pipeline == "WanPipeline");
    CHECK(ready->ready.mode == "t2v");
    CHECK(ready->ready.torch == "0.0-stub");
    CHECK(ready->ready.diffusers == "0.0-stub");
    CHECK(steps == std::vector<int>{1, 2, 3, 4});
    REQUIRE(done != nullptr);
    CHECK(done->frames == 3);
    CHECK(done->width == 64);
    CHECK(done->height == 48);
    for (const char* frame : {"f00001.png", "f00002.png", "f00003.png"}) {
        CHECK(std::filesystem::is_regular_file(run.frames / frame));
    }

    // What diffusers was asked: the request, the seed on a CPU generator,
    // PIL frames, local files only.
    const std::vector<json> record = records_in(run.record);
    REQUIRE(record.size() == 2);
    CHECK(record.at(0)["class"] == "WanPipeline");
    CHECK(record.at(0)["options"]["local_files_only"] == "True");
    CHECK(record.at(0)["options"]["torch_dtype"] == "float32");
    CHECK_FALSE(record.at(0)["options"].contains("variant"));
    const json& call = record.at(1);
    CHECK(call["prompt"] == "a heron lands on a pier at dusk");
    CHECK(call["seed"] == 1234);
    CHECK(call["generator_device"] == "cpu");
    CHECK(call["frames"] == 3);
    CHECK(call["steps"] == 4);
    CHECK(call["height"] == 48);
    CHECK(call["width"] == 64);
    CHECK(call["output_type"] == "pil");
    CHECK(call["device"] == "cpu");
    CHECK(call["negative_prompt"].is_null());
    CHECK(call["guidance"].is_null());

    // The library printed on stdout; none of it reached the protocol.
    for (const apogee::training::ScriptEvent& line : run.lines) {
        CHECK(line.text.find("a library's chatter") == std::string::npos);
    }
    CHECK_THAT(outcome.stderr_tail, ContainsSubstring("a library's chatter on stdout"));
}

TEST_CASE("the device is torch's to report, and the request's options reach the call",
          "[video][driver]") {
    REQUIRE_PYTHON();
    Run run{{.variant = "fp16"}};
    run.environment.emplace_back("STUB_VIDEO_DEVICE", "mps");
    run.request["negative_prompt"] = "blurry, static";
    run.request["guidance"] = 5.0;
    run.request["variant"] = "fp16";
    const apogee::training::ScriptOutcome outcome = run.go();
    INFO(outcome.describe());
    REQUIRE(outcome.ok);
    const std::vector<json> record = records_in(run.record);
    REQUIRE(record.size() == 2);
    CHECK(record.at(0)["options"]["variant"] == "fp16");
    CHECK(record.at(0)["options"]["torch_dtype"] == "bfloat16");
    CHECK(record.at(1)["device"] == "mps");
    // The noise is seeded on the CPU whatever the device: one seed, one clip.
    CHECK(record.at(1)["generator_device"] == "cpu");
    CHECK(record.at(1)["negative_prompt"] == "blurry, static");
    CHECK(record.at(1)["guidance"] == 5.0);
}

TEST_CASE("a seed image runs the pipeline's image-to-video form, or is refused by name",
          "[video][driver]") {
    REQUIRE_PYTHON();
    SECTION("Wan's: its sibling class over the same components") {
        Run run;
        std::ofstream{run.dir.path() / "seed.png"} << "PNG";
        run.request["image"] = (run.dir.path() / "seed.png").string();
        const apogee::training::ScriptOutcome outcome = run.go();
        INFO(outcome.describe());
        REQUIRE(outcome.ok);
        const std::vector<json> record = records_in(run.record);
        REQUIRE(record.size() == 3);
        CHECK(record.at(1)["from_pipe"] == "WanPipeline");
        CHECK(record.at(1)["into"] == "WanImageToVideoPipeline");
        CHECK(record.at(2)["call"] == "WanImageToVideoPipeline");
        CHECK(record.at(2)["image"] == (run.dir.path() / "seed.png").string());
        bool i2v = false;
        for (const apogee::video::VideoEvent& event : run.events()) {
            i2v = i2v || event.ready.mode == "i2v";
        }
        CHECK(i2v);
    }
    SECTION("a pipeline with no such form") {
        Run run{{.class_name = "HunyuanVideoPipeline"}};
        std::ofstream{run.dir.path() / "seed.png"} << "PNG";
        run.request["image"] = (run.dir.path() / "seed.png").string();
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK_FALSE(outcome.ok);
        CHECK(outcome.exit_code == 4);
        CHECK(outcome.error ==
              "this pipeline (HunyuanVideoPipeline) has no image-to-video form -- generate "
              "without a seed image");
    }
}

TEST_CASE("every refusal is the driver's own words, with its own exit code", "[video][driver]") {
    REQUIRE_PYTHON();
    SECTION("an image pipeline makes no video") {
        Run run{{.class_name = "StableDiffusionPipeline"}};
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 4);
        CHECK(outcome.error ==
              "StableDiffusionPipeline does not generate video (it takes no num_frames)");
    }
    SECTION("the generation itself fails") {
        Run run;
        run.environment.emplace_back("STUB_VIDEO_ERROR", "MPS backend out of memory");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 5);
        CHECK(outcome.error == "generation failed: RuntimeError: MPS backend out of memory");
    }
    SECTION("a directory that is no pipeline") {
        Run run;
        std::filesystem::remove(run.pipeline / "model_index.json");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK(outcome.error == run.pipeline.string() + " is not a pipeline (no model_index.json)");
    }
    SECTION("a seed image that is not there") {
        Run run;
        run.request["image"] = (run.dir.path() / "missing.png").string();
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK_THAT(outcome.error, ContainsSubstring("missing.png is not there"));
    }
    SECTION("a request missing a setting") {
        Run run;
        run.request.erase("steps");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK_THAT(outcome.error, ContainsSubstring("the request names no steps"));
    }
}

TEST_CASE("a driver stopped mid-denoise is ended, and says nothing after", "[video][driver]") {
    REQUIRE_PYTHON();
    Run run;
    run.environment.emplace_back("STUB_VIDEO_HANG", "1");
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    std::thread stopper{[&run, token] {
        // Stop once the first step is said -- it is denoising, and hangs.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
        while (std::chrono::steady_clock::now() < deadline) {
            if (std::filesystem::exists(run.record) && std::filesystem::file_size(run.record) > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds{200});
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
        token.cancel();
    }};
    const apogee::training::ScriptOutcome outcome = run.go(token);
    stopper.join();
    CHECK(outcome.cancelled);
    CHECK_FALSE(outcome.ok);
    CHECK_FALSE(std::filesystem::exists(run.frames / "f00001.png"));
}

TEST_CASE("a whole generation over the real driver lands one clip and nothing else",
          "[video][driver][generation]") {
    REQUIRE_PYTHON();
    // The generation core over the shipped driver and the stubs, with the
    // assembly a stand-in: the request it writes is the one the driver reads.
    const apogee::testing::TempDir dir{"video-generation-real-" +
                                       std::to_string(std::random_device{}())};
    apogee::testing::write_pipeline_model(dir.path() / "pipeline");
    const apogee::testing::EnvGuard path{"PYTHONPATH", kStubs.string()};
    apogee::video::VideoReadiness ready;
    ready.interpreter = python3();
    ready.driver = kDriver;
    ready.ffmpeg = "ffmpeg";
    apogee::video::VideoRequest request{.pipeline = dir.path() / "pipeline",
                                        .prompt = "un héron sur le quai",
                                        .seed = 99,
                                        .width = 32,
                                        .height = 32,
                                        .frames = 2,
                                        .steps = 2,
                                        .fps = 16};
    apogee::video::VideoSeams seams;
    seams.assemble = [](const std::filesystem::path& frames, int fps,
                        const std::filesystem::path& out, const apogee::harness::CancellationToken&,
                        std::string&) {
        std::ofstream{out} << "MP4 from " << frames.filename().string() << " at " << fps;
        return std::string{"h264"};
    };
    const apogee::video::VideoOutcome outcome = apogee::video::generate_video(
        ready, request, dir.path() / "clip.mp4", dir.path() / "work", {}, {}, seams);
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    CHECK(outcome.frames == 2);
    CHECK(outcome.ready.pipeline == "WanPipeline");
    std::ifstream in{dir.path() / "clip.mp4"};
    CHECK(std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}} ==
          "MP4 from frames at 16");
    CHECK(std::filesystem::is_empty(dir.path() / "work"));
}

TEST_CASE("the driver's guards come before any library is imported", "[video][driver]") {
    // Read, not run: offline before diffusers could reach the network, and
    // descriptor 1 on stderr before a library could print to the protocol.
    std::ifstream in{kDriver, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const std::size_t first_import = text.find("    import torch");
    REQUIRE(first_import != std::string::npos);
    for (const std::string guard :
         {"os.environ[\"HF_HUB_OFFLINE\"] = \"1\"", "os.environ[\"DIFFUSERS_OFFLINE\"] = \"1\"",
          "os.dup2(2, 1)", "sys.stdout = sys.stderr",
          "signal.signal(signal.SIGINT, signal.SIG_IGN)"}) {
        INFO(guard);
        const std::size_t at = text.find(guard);
        REQUIRE(at != std::string::npos);
        CHECK(at < first_import);
    }
}
