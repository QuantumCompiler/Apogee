#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
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

/// The shipped `voice_say.py` (50a) run for real under `python3` -- over
/// stub `torch` and `kokoro` (`scripts/stub_video`, `scripts/stub_voice`),
/// never the real libraries and never weights: the model loaded from its
/// own files, each segment split into sentences by the plain rule and each
/// spoken, the WAVs written and their durations counted from the samples,
/// every refusal in its own words, and a library's print never on the
/// protocol channel.
namespace {

using Catch::Matchers::ContainsSubstring;

const std::filesystem::path kDriver =
    std::filesystem::path{APOGEE_ASSETS_DIR} / "video" / "voice_say.py";
const std::filesystem::path kScripts =
    std::filesystem::path{APOGEE_TESTS_DIR} / "business" / "video" / "scripts";

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
    apogee::testing::TempDir dir{"voice-driver-" + std::to_string(std::random_device{}())};
    std::filesystem::path model = dir.path() / "kokoro";
    std::filesystem::path out = dir.path() / "out";
    std::filesystem::path record = dir.path() / "record.jsonl";
    nlohmann::json request{{"voice", "af_heart"},
                           {"speed", 1.0},
                           {"segments", {"Hello there. How are you?", "Fine!"}}};
    std::string stubs = "stub_video:stub_voice";
    std::vector<std::pair<std::string, std::string>> environment{};
    std::vector<apogee::training::ScriptEvent> lines;

    Run() {
        apogee::testing::write_voice_model(model);
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
                (path.empty() ? "" : ":") + (kScripts / stubs.substr(start, end - start)).string();
            start = end + 1;
        }
        apogee::training::ScriptRequest script{.interpreter = python3(),
                                               .script = kDriver,
                                               .arguments = {"--request", file.string()},
                                               .environment = environment};
        script.environment.emplace_back("PYTHONPATH", path);
        script.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
        script.environment.emplace_back("STUB_VOICE_RECORD", record.string());
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
};

}  // namespace

TEST_CASE("the speech driver speaks sentence by sentence and counts every sample",
          "[video][voice][driver]") {
    REQUIRE_PYTHON();
    Run run;
    const apogee::training::ScriptOutcome outcome = run.go();
    INFO(outcome.describe());
    REQUIRE(outcome.ok);
    using Kind = apogee::video::VoiceEvent::Kind;
    std::vector<apogee::video::VoiceEvent> segments;
    const apogee::video::VoiceEvent* ready = nullptr;
    std::vector<apogee::video::VoiceEvent> events = run.events();
    for (const apogee::video::VoiceEvent& event : events) {
        if (event.kind == Kind::Segment) {
            segments.push_back(event);
        } else if (event.kind == Kind::Ready) {
            ready = &event;
        }
    }
    REQUIRE(ready != nullptr);
    CHECK(ready->ready.device == "cpu");
    CHECK(ready->ready.kokoro == "0.0-stub");
    CHECK(ready->ready.sample_rate == 24000);
    REQUIRE(segments.size() == 2);
    // The plain rule: two sentences in the first segment, each its own call.
    REQUIRE(segments.at(0).sentences.size() == 2);
    CHECK(segments.at(0).sentences.at(0).text == "Hello there.");
    CHECK(segments.at(0).sentences.at(1).text == "How are you?");
    // 100 samples a character at 24 kHz: counted, and in the file.
    CHECK(segments.at(0).sentences.at(0).seconds == 1200.0 / 24000.0);
    CHECK(segments.at(0).seconds == (1200.0 + 1200.0) / 24000.0);
    std::string error;
    const std::optional<apogee::platform::WavAudio> audio =
        apogee::platform::read_wav(segments.at(0).file, error);
    REQUIRE(audio.has_value());
    CHECK(audio->seconds() == segments.at(0).seconds);
    CHECK(audio->rate == 24000);
    CHECK(segments.at(1).seconds == 500.0 / 24000.0);

    // The model was loaded from its own files, the voice from its pack.
    std::ifstream in{run.record};
    std::vector<nlohmann::json> record;
    for (std::string line; std::getline(in, line);) {
        record.push_back(nlohmann::json::parse(line));
    }
    REQUIRE(record.size() == 5);
    CHECK(record.at(0)["model"] == (run.model / "kokoro-v1_0.pth").string());
    CHECK(record.at(1)["pipeline"] == "a");
    CHECK(record.at(1)["has_model"] == true);
    CHECK(record.at(2)["voice"] == (run.model / "voices" / "af_heart.pt").string());
    // The library's print reached no protocol line.
    for (const apogee::training::ScriptEvent& line : run.lines) {
        CHECK(line.text.find("chatter") == std::string::npos);
    }
}

TEST_CASE("the speech driver refuses in its own words, with its own exit codes",
          "[video][voice][driver]") {
    REQUIRE_PYTHON();
    SECTION("a voice the model lacks") {
        Run run;
        run.request["voice"] = "zf_nobody";
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 2);
        CHECK_THAT(outcome.error, ContainsSubstring("has no voice zf_nobody"));
    }
    SECTION("kokoro not installed") {
        Run run;
        run.stubs = "stub_video:stub_voice_missing";
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 3);
        CHECK_THAT(outcome.error, ContainsSubstring("apogee train setup --with voice"));
    }
    SECTION("the speech itself fails") {
        Run run;
        run.environment.emplace_back("STUB_VOICE_ERROR", "phonemizer failed");
        const apogee::training::ScriptOutcome outcome = run.go();
        CHECK(outcome.exit_code == 5);
        CHECK(outcome.error == "speech failed: RuntimeError: phonemizer failed");
    }
    SECTION("stopped mid-speech") {
        Run run;
        run.environment.emplace_back("STUB_VOICE_HANG", "1");
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        std::thread stopper{[&run, token] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
            while (std::chrono::steady_clock::now() < deadline &&
                   !(std::filesystem::exists(run.record) &&
                     std::filesystem::file_size(run.record) > 0)) {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }
            token.cancel();
        }};
        const apogee::training::ScriptOutcome outcome = run.go(token);
        stopper.join();
        CHECK(outcome.cancelled);
    }
}
