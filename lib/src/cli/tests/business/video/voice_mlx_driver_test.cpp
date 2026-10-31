#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "platform/child_process.h"
#include "platform/ffmpeg.h"
#include "platform/platform.h"
#include "support/env_guard.h"
#include "support/voice_model.h"
#include "training/script_runner.h"
#include "video/voice.h"

/// The shipped `voice_say_mlx.py` (53m) run for real under `python3` -- over
/// the stub `mlx` and `mlx_audio` (`tests/data/backends/scripts`), never the
/// real libraries and never weights: an MLX voice model loaded from its own
/// directory, each segment split into sentences by the PyTorch driver's plain
/// rule and each spoken through Kokoro's `generate` with the voice's own
/// pack, the WAVs written and their durations counted from the samples, the
/// protocol the PyTorch driver's (`voice_driver_test`), every refusal in its
/// own words, and a library's print -- on stdout, on stderr, or straight onto
/// descriptor 1 -- never on the protocol channel.
namespace {

using Catch::Matchers::ContainsSubstring;

const std::filesystem::path kDriver =
    std::filesystem::path{APOGEE_ASSETS_DIR} / "video" / "voice_say_mlx.py";
const std::filesystem::path kStubs =
    std::filesystem::path{APOGEE_TESTS_DIR} / "data" / "backends" / "scripts";

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

struct Run {
    apogee::testing::TempDir dir{"voice-mlx-driver-" + std::to_string(std::random_device{}())};
    std::filesystem::path model = dir.path() / "kokoro-mlx";
    std::filesystem::path out = dir.path() / "out";
    std::filesystem::path record = dir.path() / "record.jsonl";
    nlohmann::json request{{"engine", "mlx"},
                           {"voice", "af_heart"},
                           {"speed", 1.0},
                           {"segments", {"Hello there. How are you?", "Fine!"}}};
    std::string stubs = "stub_mlx:stub_mlx_audio";
    std::vector<std::pair<std::string, std::string>> environment{};
    std::vector<apogee::training::ScriptEvent> lines{};

    Run() {
        apogee::testing::write_mlx_voice_model(model);
        request["model_dir"] = model.string();
        request["out_dir"] = out.string();
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
        apogee::training::ScriptRequest script{.interpreter = python3(),
                                               .script = kDriver,
                                               .arguments = {"--request", file.string()},
                                               .environment = environment};
        script.environment.emplace_back("PYTHONPATH", path);
        script.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
        script.environment.emplace_back("STUB_MLX_AUDIO_RECORD", record.string());
        return apogee::training::run_script(
            script, [this](const apogee::training::ScriptEvent& line) { lines.push_back(line); },
            token);
    }

    [[nodiscard]] std::vector<apogee::video::VoiceEvent> events() const {
        std::vector<apogee::video::VoiceEvent> said;
        for (const apogee::training::ScriptEvent& line : lines) {
            if (auto event = apogee::video::read_voice_event(line)) {
                said.push_back(*event);
            }
        }
        return said;
    }

    [[nodiscard]] std::vector<nlohmann::json> recorded() const {
        std::vector<nlohmann::json> entries;
        std::ifstream in{record};
        for (std::string line; std::getline(in, line);) {
            entries.push_back(nlohmann::json::parse(line));
        }
        return entries;
    }
};

}  // namespace

TEST_CASE("the MLX speech driver speaks sentence by sentence and counts every sample",
          "[video][voice][driver][mlx_audio]") {
    REQUIRE_PYTHON();
    Run run;
    const apogee::training::ScriptOutcome outcome = run.go();
    INFO(outcome.describe());
    REQUIRE(outcome.ok);
    using Kind = apogee::video::VoiceEvent::Kind;
    std::vector<apogee::video::VoiceEvent> segments;
    std::optional<apogee::video::VoiceEvent> ready;
    for (const apogee::video::VoiceEvent& event : run.events()) {
        if (event.kind == Kind::Segment) {
            segments.push_back(event);
        } else if (event.kind == Kind::Ready) {
            ready = event;
        }
    }
    // The PyTorch driver's protocol, its engine and MLX's versions in `ready`.
    REQUIRE(ready.has_value());
    CHECK(ready->ready.engine == "mlx");
    CHECK(ready->ready.device == "gpu");
    CHECK(ready->ready.mlx_audio == "0.0-audio-stub");
    CHECK(ready->ready.torch.empty());
    CHECK(ready->ready.sample_rate == 24000);
    REQUIRE(segments.size() == 2);
    // The plain rule: two sentences in the first segment, each its own call.
    REQUIRE(segments.at(0).sentences.size() == 2);
    CHECK(segments.at(0).sentences.at(0).text == "Hello there.");
    CHECK(segments.at(0).sentences.at(1).text == "How are you?");
    // 100 samples a character at 24 kHz: counted, and in the file -- the
    // file's own header says the same length.
    CHECK(segments.at(0).sentences.at(0).seconds == 1200.0 / 24000.0);
    CHECK(segments.at(0).seconds == (1200.0 + 1200.0) / 24000.0);
    for (const apogee::video::VoiceEvent& segment : segments) {
        std::string error;
        const std::optional<apogee::platform::WavAudio> audio =
            apogee::platform::read_wav(segment.file, error);
        REQUIRE(audio.has_value());
        CHECK(audio->seconds() == segment.seconds);
        CHECK(audio->rate == 24000);
        CHECK(audio->channels == 1);
        CHECK(audio->bits == 16);
    }
    CHECK(segments.at(1).seconds == 500.0 / 24000.0);

    // The model loaded from its own directory; each sentence spoken with the
    // voice's own pack, by path, in its language -- never the hub's.
    const std::vector<nlohmann::json> record = run.recorded();
    REQUIRE(record.size() == 4);
    CHECK(record.at(0)["load"] == run.model.string());
    CHECK(record.at(1)["say"] == "Hello there.");
    CHECK(record.at(1)["voice"] == (run.model / "voices" / "af_heart.safetensors").string());
    CHECK(record.at(1)["lang_code"] == "a");
    CHECK(record.at(1)["speed"] == 1.0);
    CHECK(record.at(3)["say"] == "Fine!");
    // The library's prints -- print(), stderr, descriptor 1 itself -- reached
    // no protocol line.
    for (const apogee::training::ScriptEvent& line : run.lines) {
        CHECK(line.text.find("chatter") == std::string::npos);
        CHECK(line.text.find("noise") == std::string::npos);
        CHECK(line.text.find("descriptor 1") == std::string::npos);
    }
}

TEST_CASE("the MLX speech driver takes the speed, and a voice's language from its name",
          "[video][voice][driver][mlx_audio]") {
    REQUIRE_PYTHON();
    Run run;
    run.request["voice"] = "bf_emma";
    run.request["speed"] = 2.0;
    run.request["segments"] = {"Hi."};
    REQUIRE(run.go().ok);
    const std::vector<nlohmann::json> record = run.recorded();
    REQUIRE(record.size() == 2);
    CHECK(record.at(1)["lang_code"] == "b");
    CHECK(record.at(1)["speed"] == 2.0);
    // Half the samples at twice the speed.
    for (const apogee::video::VoiceEvent& event : run.events()) {
        if (event.kind == apogee::video::VoiceEvent::Kind::Segment) {
            CHECK(event.seconds == 150.0 / 24000.0);
        }
    }
}

TEST_CASE("the MLX speech driver refuses in its own words, with its own exit codes",
          "[video][voice][driver][mlx_audio]") {
    REQUIRE_PYTHON();
    SECTION("a voice the model lacks") {
        Run run;
        run.request["voice"] = "zf_nobody";
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK_THAT(outcome.error,
                   ContainsSubstring("has no voice zf_nobody (no voices/zf_nobody.safetensors)"));
        CHECK(run.recorded().empty());
    }
    SECTION("not an MLX voice model") {
        Run run;
        std::filesystem::remove(run.model / "config.json");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK_THAT(outcome.error, ContainsSubstring("is not an MLX voice model (no config.json)"));
    }
    SECTION("mlx-audio not installed") {
        Run run;
        run.stubs = "stub_mlx:stub_mlx_missing";
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 3);
        CHECK_THAT(outcome.error, ContainsSubstring("apogee train setup --with mlx-audio"));
    }
    SECTION("the model does not load") {
        Run run;
        run.environment.emplace_back("STUB_MLX_AUDIO_LOAD_ERROR", "unknown model type");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 4);
        CHECK(outcome.error == "could not load the voice model: RuntimeError: unknown model type");
    }
    SECTION("the speech itself fails") {
        Run run;
        run.environment.emplace_back("STUB_MLX_AUDIO_ERROR", "phonemizer failed");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 5);
        CHECK(outcome.error == "speech failed: RuntimeError: phonemizer failed");
    }
    SECTION("a voice path Kokoro on MLX would read as several") {
        Run run;
        const std::filesystem::path comma = run.dir.path() / "a,b";
        apogee::testing::write_mlx_voice_model(comma);
        run.request["model_dir"] = comma.string();
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK_THAT(outcome.error, ContainsSubstring("holds a comma"));
        CHECK(run.recorded().empty());
    }
    SECTION("stopped mid-speech") {
        Run run;
        run.environment.emplace_back("STUB_MLX_AUDIO_HANG", "1");
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        std::thread stopper{[&run, token] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
            while (std::chrono::steady_clock::now() < deadline &&
                   !(std::filesystem::exists(run.record) &&
                     std::filesystem::file_size(run.record) > 0 && run.recorded().size() >= 2)) {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }
            token.cancel();
        }};
        const apogee::training::ScriptOutcome outcome = run.go(token);
        stopper.join();
        CHECK(outcome.cancelled);
    }
}
