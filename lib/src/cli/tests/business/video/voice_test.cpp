#include "video/voice.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "platform/ffmpeg.h"
#include "support/env_guard.h"
#include "support/fake_child.h"

/// Speech (50a) over a scripted driver that writes real WAV files: the
/// request it reads, its lines read into events, every segment measured
/// from its own file and kept -- and every way a run ends without them
/// keeping nothing. The real driver under stub libraries is
/// `voice_driver_test`'s.
namespace {

using apogee::video::VoiceEvent;
using apogee::video::VoiceOutcome;
using Catch::Matchers::ContainsSubstring;

/// A driver played by the test: it writes one WAV per segment of `samples`
/// silent samples at 24 kHz, and says so -- or fails as told.
struct Driver {
    std::vector<int> samples{2400, 4800};
    int status = 0;
    std::string stderr_text{};
    bool hang = false;
    bool short_one = false;
    nlohmann::json request{};

    [[nodiscard]] apogee::training::Spawner spawner() {
        return [this](const apogee::platform::ChildCommand& command,
                      std::string&) -> std::unique_ptr<apogee::platform::ChildProcess> {
            std::ifstream in{command.arguments.at(2)};
            request = nlohmann::json::parse(in);
            const std::filesystem::path out{request.at("out_dir").get<std::string>()};
            std::filesystem::create_directories(out);
            std::string script =
                R"({"event": "ready", "device": "cpu", "torch": "2.9.1", "kokoro": "0.9.4", "sample_rate": 24000})"
                "\n";
            const std::size_t count = short_one ? samples.size() - 1 : samples.size();
            for (std::size_t i = 0; i < count; ++i) {
                const std::filesystem::path file = out / ("s0000" + std::to_string(i + 1) + ".wav");
                std::ofstream{file, std::ios::binary} << apogee::platform::wav_file(
                    std::string(static_cast<std::size_t>(samples.at(i)) * 2, '\0'), 24000);
                script += nlohmann::json{{"event", "segment"},
                                         {"index", i + 1},
                                         {"of", samples.size()},
                                         {"file", file.string()},
                                         {"seconds", samples.at(i) / 24000.0},
                                         {"sentences",
                                          {{{"text", "One."}, {"seconds", 0.05}},
                                           {{"text", "Two."}, {"seconds", 0.05}}}}}
                              .dump() +
                          "\n";
            }
            script += R"({"event": "done", "segments": 2, "seconds": 0.3})"
                      "\n";
            auto child = std::make_unique<apogee::testing::FakeChild>();
            child->stdout_script = script;
            child->stderr_script = stderr_text;
            child->exit_status = status;
            child->hang = hang;
            return child;
        };
    }
};

struct Run {
    apogee::testing::TempDir dir{"voice-run-" + std::to_string(std::random_device{}())};
    std::filesystem::path work = dir.path() / "cache" / "video";
    std::filesystem::path kept = dir.path() / "kept";
    apogee::video::VideoReadiness ready{.interpreter = "/venv/bin/python",
                                        .driver = "/scripts/voice_say.py"};
    apogee::video::VoiceRequest request{.model = "/store/hexgrad--K/voice/aaaaaaaaaaaa",
                                        .voice = "af_heart",
                                        .speed = 1.0,
                                        .segments = {"One. Two.", "Three, a longer one."}};
    Driver driver;
    std::vector<VoiceEvent> events;

    VoiceOutcome go(const apogee::harness::CancellationToken& token = {}) {
        return apogee::video::speak(
            ready, request, kept, work,
            [this](const VoiceEvent& event) { events.push_back(event); }, token, driver.spawner());
    }

    [[nodiscard]] bool work_left() const {
        return std::filesystem::exists(work) && !std::filesystem::is_empty(work);
    }
};

}  // namespace

TEST_CASE("speech runs the driver on its request and keeps every segment, measured",
          "[video][voice]") {
    Run run;
    const VoiceOutcome outcome = run.go();
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    CHECK(run.driver.request["model_dir"] == "/store/hexgrad--K/voice/aaaaaaaaaaaa");
    CHECK(run.driver.request["voice"] == "af_heart");
    CHECK(run.driver.request["segments"] ==
          nlohmann::json::array({"One. Two.", "Three, a longer one."}));
    std::vector<VoiceEvent::Kind> kinds;
    for (const VoiceEvent& event : run.events) {
        kinds.push_back(event.kind);
    }
    using Kind = VoiceEvent::Kind;
    CHECK(kinds == std::vector<Kind>{Kind::Ready, Kind::Segment, Kind::Segment, Kind::Done});
    CHECK(outcome.ready.kokoro == "0.9.4");
    REQUIRE(outcome.segments.size() == 2);
    // Kept where asked, by number, each measured from its own header.
    CHECK(outcome.segments.at(0).file == run.kept / "s00001.wav");
    CHECK(outcome.segments.at(0).seconds == 0.1);
    CHECK(outcome.segments.at(1).seconds == 0.2);
    CHECK(outcome.segments.at(1).text == "Three, a longer one.");
    REQUIRE(outcome.segments.at(0).sentences.size() == 2);
    CHECK(outcome.segments.at(0).sentences.at(1).text == "Two.");
    CHECK(outcome.seconds == 0.1 + 0.2);
    CHECK(outcome.sample_rate == 24000);
    CHECK_FALSE(run.work_left());

    // Joined into one file: the samples one after another.
    const std::filesystem::path joined = run.dir.path() / "all.wav";
    REQUIRE(apogee::video::join_segments(outcome.segments, joined).empty());
    std::string error;
    const std::optional<apogee::platform::WavAudio> audio =
        apogee::platform::read_wav(joined, error);
    REQUIRE(audio.has_value());
    CHECK(std::abs(audio->seconds() - 0.3) < 1e-9);
    CHECK(audio->rate == 24000);
    CHECK_FALSE(std::filesystem::exists(joined.string() + ".partial"));
}

TEST_CASE("speech that ends without every segment keeps nothing", "[video][voice]") {
    Run run;
    SECTION("the driver crashes: its code and tail") {
        run.driver.status = 1;
        run.driver.stderr_text = "RuntimeError: espeak not found\n";
        const VoiceOutcome outcome = run.go();
        CHECK_THAT(outcome.error, ContainsSubstring("voice_say.py exited with code 1"));
        CHECK_THAT(outcome.error, ContainsSubstring("espeak not found"));
    }
    SECTION("a segment the driver never spoke") {
        run.driver.short_one = true;
        CHECK(run.go().error == "the driver spoke 1 of 2 segment(s)");
    }
    SECTION("cancelled") {
        run.driver.hang = true;
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        token.cancel();
        const VoiceOutcome outcome = run.go(token);
        CHECK(outcome.cancelled);
    }
    SECTION("refused by the ladder: nothing spawned") {
        run.ready.refusal = apogee::video::VideoRefusal::NoKokoro;
        run.ready.reason = "kokoro is not installed";
        CHECK(run.go().error == "kokoro is not installed");
        CHECK(run.driver.request.is_null());
    }
    CHECK_FALSE(std::filesystem::exists(run.kept));
    CHECK_FALSE(run.work_left());
}

TEST_CASE("a WAV's length is read from its header, and a broken one refused",
          "[video][voice][wav]") {
    const apogee::testing::TempDir dir{"wav-" + std::to_string(std::random_device{}())};
    const std::filesystem::path file = dir.path() / "a.wav";
    std::ofstream{file, std::ios::binary}
        << apogee::platform::wav_file(std::string(48000, '\0'), 24000);
    std::string error;
    const std::optional<apogee::platform::WavAudio> audio = apogee::platform::read_wav(file, error);
    REQUIRE(audio.has_value());
    CHECK(audio->seconds() == 1.0);
    CHECK(audio->channels == 1);
    CHECK(audio->bits == 16);
    std::filesystem::resize_file(file, 1000);
    CHECK_FALSE(apogee::platform::read_wav(file, error).has_value());
    CHECK(error == file.string() + " is cut short: its data chunk runs past its end");
    std::ofstream{file, std::ios::binary} << "not audio";
    CHECK_FALSE(apogee::platform::read_wav(file, error).has_value());
    CHECK(error == file.string() + " is not a WAV file");
}
