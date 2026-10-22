#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "platform/child_process.h"
#include "platform/platform.h"
#include "support/env_guard.h"
#include "training/script_runner.h"
#include "video/generation.h"

/// The shipped `video_generate_mlx.py` (53l) run for real under `python3` --
/// over the stub `mlx_video` (its Wan generator where the pinned commit holds
/// it, `tests/data/backends/scripts/stub_mlx_video/`) beside the stub `mlx`,
/// never the real libraries and never weights: `video_driver_test`'s pattern
/// mirrored. The request read, the generator handed the settings as its own
/// `main()` hands them, its tokenizer answered from the stored one with the
/// hub offline, a step said per denoising step, the frames written as PNGs
/// in place of the generator's own mp4, every refusal in its own words -- and
/// the generator's prints on both streams never reaching the protocol.
namespace {

using Catch::Matchers::ContainsSubstring;
using nlohmann::json;

const std::filesystem::path kDriver =
    std::filesystem::path{APOGEE_ASSETS_DIR} / "video" / "video_generate_mlx.py";
const std::filesystem::path kStubs =
    std::filesystem::path{APOGEE_TESTS_DIR} / "data" / "backends" / "scripts";

[[nodiscard]] std::string python3() {
    return apogee::platform::find_on_path("python3");
}

#define REQUIRE_PYTHON()                                                   \
    do {                                                                   \
        if (!apogee::platform::supports_child_processes()) {               \
            SKIP("no child processes on this platform");                   \
        }                                                                  \
        if (python3().empty()) {                                           \
            SKIP("no python3 on PATH: the MLX driver's suite cannot run"); \
        }                                                                  \
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

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

/// An MLX video model as mlx-video's converter writes one -- the driver
/// reads only its `config.json` -- the tokenizer a Wan set stores, a request
/// and the driver run on them.
struct Run {
    apogee::testing::TempDir dir{"video-mlx-driver-" + std::to_string(std::random_device{}())};
    std::filesystem::path model = dir.path() / "mlx";
    std::filesystem::path tokenizer = dir.path() / "set" / "google" / "umt5-xxl";
    std::filesystem::path frames = dir.path() / "work" / "frames";
    std::filesystem::path record = dir.path() / "record.jsonl";
    json request{{"engine", "mlx"}, {"prompt", "a heron lands on a pier at dusk"},
                 {"seed", 1234},    {"width", 32},
                 {"height", 16},    {"frames", 5},
                 {"steps", 4}};
    std::string stubs = "stub_mlx:stub_mlx_video";
    std::vector<std::pair<std::string, std::string>> environment{};
    std::vector<apogee::training::ScriptEvent> lines;

    explicit Run(std::string_view model_type = "ti2v") {
        write(model / "config.json", json{{"model_type", model_type},
                                          {"dim", 3072},
                                          {"sample_fps", 24},
                                          {"quantization", {{"group_size", 64}, {"bits", 4}}}}
                                         .dump());
        write(tokenizer / "tokenizer_config.json", "{}");
        write(tokenizer / "spiece.model", "a sentencepiece model, never one");
        request["pipeline"] = model.string();
        request["tokenizer"] = tokenizer.string();
        request["frames_dir"] = frames.string();
    }

    apogee::training::ScriptOutcome go(const apogee::harness::CancellationToken& token = {}) {
        const std::filesystem::path file = dir.path() / "request.json";
        std::ofstream{file, std::ios::binary} << request.dump();
        std::string path;
        for (std::size_t start = 0; start <= stubs.size();) {
            const std::size_t end = std::min(stubs.find(':', start), stubs.size());
            path +=
                (path.empty() ? "" : ":") + (kStubs / stubs.substr(start, end - start)).string();
            start = end + 1;
        }
        apogee::training::ScriptRequest script{
            .interpreter = python3(),
            .script = kDriver,
            .arguments = {"--request", file.string()},
            .environment = environment,
        };
        script.environment.emplace_back("PYTHONPATH", path);
        script.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
        script.environment.emplace_back("STUB_MLX_VIDEO_RECORD", record.string());
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

    /// What the stub generator was called with.
    [[nodiscard]] json call() const {
        for (const json& entry : records_in(record)) {
            if (entry.contains("generate")) {
                return entry;
            }
        }
        return json{};
    }
};

}  // namespace

TEST_CASE("the MLX driver runs mlx-video's generator, says each step, and writes every frame",
          "[video][driver][mlx]") {
    REQUIRE_PYTHON();
    Run run;
    const apogee::training::ScriptOutcome outcome = run.go();
    INFO(outcome.describe());
    REQUIRE(outcome.ok);

    using Kind = apogee::video::VideoEvent::Kind;
    std::vector<int> steps;
    std::optional<apogee::video::VideoEvent> ready;
    std::optional<apogee::video::VideoEvent> done;
    for (const apogee::video::VideoEvent& event : run.events()) {
        if (event.kind == Kind::Ready) {
            ready = event;
        } else if (event.kind == Kind::Step) {
            steps.push_back(event.step);
            CHECK(event.steps == 4);
        } else if (event.kind == Kind::Done) {
            done = event;
        }
    }
    REQUIRE(ready.has_value());
    CHECK(ready->ready.engine == "mlx");
    CHECK(ready->ready.device == "gpu");
    CHECK(ready->ready.dtype == "4-bit");
    CHECK(ready->ready.pipeline == "Wan ti2v");
    CHECK(ready->ready.mode == "t2v");
    CHECK(ready->ready.mlx_video == "0.0-video-stub");
    CHECK(ready->ready.torch.empty());
    CHECK(ready->ready.diffusers.empty());
    // A step said as each denoising step ends -- the diffusers driver's way.
    CHECK(steps == std::vector<int>{1, 2, 3, 4});
    REQUIRE(done.has_value());
    CHECK(done->frames == 5);
    CHECK(done->width == 32);
    CHECK(done->height == 16);
    for (const char* frame :
         {"f00001.png", "f00002.png", "f00003.png", "f00004.png", "f00005.png"}) {
        INFO(frame);
        CHECK(slurp(run.frames / frame).starts_with("\x89PNG\r\n\x1a\n"));
    }
    // Its own mp4 is never written: its save_video is the driver's.
    CHECK_FALSE(std::filesystem::exists(run.frames.parent_path() / "unused.mp4"));

    // What mlx-video's generator was handed, as its own main() hands it.
    const json call = run.call();
    CHECK(call["generate"] == run.model.string());
    CHECK(call["prompt"] == "a heron lands on a pier at dusk");
    CHECK(call["seed"] == 1234);
    CHECK(call["width"] == 32);
    CHECK(call["height"] == 16);
    CHECK(call["num_frames"] == 5);
    CHECK(call["steps"] == 4);
    CHECK(call["negative_prompt"].is_null());
    CHECK(call["image"].is_null());
    CHECK(call["guide_scale"].is_null());
    CHECK(call["shift"].is_null());
    CHECK(call["loras"].is_null());

    // The generator printed on stdout and stderr; none of it reached the
    // protocol, and stderr's tail kept it.
    for (const apogee::training::ScriptEvent& line : run.lines) {
        CHECK(line.text.find("Generation (MLX)") == std::string::npos);
        CHECK(line.text.find("noise on stderr") == std::string::npos);
    }
    CHECK_THAT(outcome.stderr_tail, ContainsSubstring("Wan2.2 TI2V Generation (MLX)"));
    CHECK_THAT(outcome.stderr_tail, ContainsSubstring("stub mlx-video: noise on stderr"));
}

TEST_CASE("the request's options reach mlx-video's generator, its LoRAs onto every model",
          "[video][driver][mlx]") {
    REQUIRE_PYTHON();
    Run run;
    write(run.dir.path() / "seed.png", "PNG");
    write(run.dir.path() / "loras" / "style.safetensors", "weights");
    run.request["negative_prompt"] = "blurry, static";
    run.request["guidance"] = 5.0;
    run.request["shift"] = 3.0;
    run.request["image"] = (run.dir.path() / "seed.png").string();
    run.request["loras"] = json::array(
        {{{"path", (run.dir.path() / "loras" / "style.safetensors").string()}, {"scale", 0.8}}});
    const apogee::training::ScriptOutcome outcome = run.go();
    INFO(outcome.describe());
    REQUIRE(outcome.ok);
    const json call = run.call();
    CHECK(call["negative_prompt"] == "blurry, static");
    CHECK(call["guide_scale"] == 5.0);
    CHECK(call["shift"] == 3.0);
    CHECK(call["image"] == (run.dir.path() / "seed.png").string());
    // mlx-video's `--lora`: onto every model the mode loads, never a half.
    CHECK(call["loras"] == json::array({json::array(
                               {(run.dir.path() / "loras" / "style.safetensors").string(), 0.8})}));
    CHECK(call["loras_high"].is_null());
    CHECK(call["loras_low"].is_null());
    bool i2v = false;
    for (const apogee::video::VideoEvent& event : run.events()) {
        i2v = i2v || event.ready.mode == "i2v";
    }
    CHECK(i2v);
}

TEST_CASE("the stored tokenizer answers mlx-video's hub name, and the hub is never asked",
          "[video][driver][mlx][tokenizer]") {
    REQUIRE_PYTHON();
    SECTION("the request's tokenizer, from its files alone") {
        Run run;
        const apogee::training::ScriptOutcome outcome = run.go();
        INFO(outcome.describe());
        REQUIRE(outcome.ok);
        const std::vector<json> record = records_in(run.record);
        REQUIRE(record.size() == 2);
        CHECK(record.at(1) ==
              json{{"tokenizer", run.tokenizer.string()}, {"local_files_only", true}});
    }
    SECTION("a tokenizer directory without its files fails offline, never fetched") {
        Run run;
        std::filesystem::remove(run.tokenizer / "tokenizer_config.json");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK_FALSE(outcome.ok);
        CHECK(outcome.exit_code == 5);
        CHECK_THAT(outcome.error, ContainsSubstring("generation failed: OSError: We couldn't "
                                                    "connect to 'https://huggingface.co'"));
    }
}

TEST_CASE("every refusal is the MLX driver's own words, with its own exit code",
          "[video][driver][mlx]") {
    REQUIRE_PYTHON();
    SECTION("a directory that is no MLX video model") {
        Run run;
        std::filesystem::remove(run.model / "config.json");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK_THAT(outcome.error,
                   ContainsSubstring(run.model.string() + " is not an MLX video model"));
    }
    SECTION("a frame count that is not 4n+1") {
        Run run;
        run.request["frames"] = 12;
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK(outcome.error == "mlx-video's Wan generator takes 4n+1 frames -- 12 is not one");
        CHECK_FALSE(std::filesystem::exists(run.record));
    }
    SECTION("no tokenizer where the request says") {
        Run run;
        std::filesystem::remove_all(run.tokenizer);
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK(outcome.error == "the tokenizer " + run.tokenizer.string() + " is not there");
    }
    SECTION("a seed image that is not there") {
        Run run;
        run.request["image"] = (run.dir.path() / "missing.png").string();
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK_THAT(outcome.error, ContainsSubstring("missing.png is not there"));
    }
    SECTION("a text-to-video model given a seed image") {
        Run run{"t2v"};
        write(run.dir.path() / "seed.png", "PNG");
        run.request["image"] = (run.dir.path() / "seed.png").string();
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 4);
        CHECK_THAT(outcome.error, ContainsSubstring("is text-to-video (model_type t2v): it takes "
                                                    "no seed image"));
    }
    SECTION("an image-to-video model given none") {
        Run run{"i2v"};
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 4);
        CHECK_THAT(outcome.error, ContainsSubstring("is image-to-video (model_type i2v): it "
                                                    "starts from a seed image"));
    }
    SECTION("the generation itself fails") {
        Run run;
        run.environment.emplace_back("STUB_MLX_VIDEO_ERROR", "[metal::malloc] out of memory");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 5);
        CHECK(outcome.error == "generation failed: RuntimeError: [metal::malloc] out of memory");
    }
    SECTION("mlx-video not installed") {
        Run run;
        run.stubs = "stub_mlx:stub_mlx_missing";
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 3);
        CHECK_THAT(outcome.error, ContainsSubstring("mlx-video is not installed in Apogee's "
                                                    "Python environment"));
        CHECK_THAT(outcome.error, ContainsSubstring("apogee train setup --with mlx-video"));
    }
    SECTION("a LoRA, to a generator that takes none") {
        Run run;
        write(run.dir.path() / "style.safetensors", "weights");
        run.request["loras"] =
            json::array({{{"path", (run.dir.path() / "style.safetensors").string()}}});
        run.environment.emplace_back("STUB_MLX_VIDEO_NO_LORA", "1");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 4);
        CHECK(outcome.error ==
              "this mlx-video's Wan generator takes no LoRA -- generate without --lora");
    }
}

TEST_CASE("the MLX driver stopped mid-denoise is ended, and says nothing after",
          "[video][driver][mlx]") {
    REQUIRE_PYTHON();
    Run run;
    run.environment.emplace_back("STUB_MLX_VIDEO_HANG", "1");
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    std::thread stopper{[&run, token] {
        // Stop once the generator has been called -- it is denoising, and hangs.
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
    for (const apogee::video::VideoEvent& event : run.events()) {
        CHECK(event.kind != apogee::video::VideoEvent::Kind::Done);
    }
}

TEST_CASE("a whole generation over the real MLX driver lands one clip and nothing else",
          "[video][driver][mlx][generation]") {
    REQUIRE_PYTHON();
    // The generation core over the shipped MLX driver and the stubs, the
    // assembly a stand-in: the one generation, its engine MLX.
    Run run;
    const apogee::testing::EnvGuard path{
        "PYTHONPATH", (kStubs / "stub_mlx").string() + ":" + (kStubs / "stub_mlx_video").string()};
    apogee::video::VideoReadiness ready;
    ready.engine = apogee::video::VideoEngine::Mlx;
    ready.interpreter = python3();
    ready.driver = kDriver;
    ready.ffmpeg = "ffmpeg";
    const apogee::video::VideoRequest request{.pipeline = run.model,
                                              .prompt = "un héron sur le quai",
                                              .seed = 99,
                                              .width = 32,
                                              .height = 32,
                                              .frames = 9,
                                              .steps = 2,
                                              .fps = 16,
                                              .engine = apogee::video::VideoEngine::Mlx,
                                              .tokenizer = run.tokenizer};
    apogee::video::VideoSeams seams;
    seams.assemble = [](const std::filesystem::path& frames, int fps,
                        const std::filesystem::path& out, const apogee::harness::CancellationToken&,
                        std::string&) {
        std::size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(frames)) {
            count += entry.path().extension() == ".png" ? 1U : 0U;
        }
        std::ofstream{out} << "MP4 of " << count << " frames at " << fps;
        return std::string{"h264"};
    };
    std::vector<apogee::video::VideoEvent> events;
    const std::filesystem::path work = run.dir.path() / "cache";
    const apogee::video::VideoOutcome outcome = apogee::video::generate_video(
        ready, request, run.dir.path() / "clip.mp4", work,
        [&events](const apogee::video::VideoEvent& event) { events.push_back(event); }, {}, seams);
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    CHECK(outcome.frames == 9);
    CHECK(outcome.ready.engine == "mlx");
    CHECK(outcome.ready.pipeline == "Wan ti2v");
    CHECK(slurp(run.dir.path() / "clip.mp4") == "MP4 of 9 frames at 16");
    CHECK(std::filesystem::is_empty(work));
    const std::size_t steps = static_cast<std::size_t>(
        std::ranges::count_if(events, [](const apogee::video::VideoEvent& event) {
            return event.kind == apogee::video::VideoEvent::Kind::Step;
        }));
    CHECK(steps == 2);
}

TEST_CASE("the MLX driver's guards come before any library is imported", "[video][driver][mlx]") {
    // Read, not run: the hub offline before transformers could reach it, and
    // descriptor 1 on stderr before mlx-video could print to the protocol.
    std::ifstream in{kDriver, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const std::size_t first_import = text.find("        import transformers");
    REQUIRE(first_import != std::string::npos);
    CHECK(text.find("from mlx_video") > first_import);
    for (const std::string guard :
         {"os.environ[\"HF_HUB_OFFLINE\"] = \"1\"", "os.environ[\"TRANSFORMERS_OFFLINE\"] = \"1\"",
          "os.dup2(2, 1)", "sys.stdout = sys.stderr",
          "signal.signal(signal.SIGINT, signal.SIG_IGN)"}) {
        INFO(guard);
        const std::size_t at = text.find(guard);
        REQUIRE(at != std::string::npos);
        CHECK(at < first_import);
    }
}
