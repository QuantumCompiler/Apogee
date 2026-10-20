#include "cli/voice_say.h"

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
#include "platform/ffmpeg.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/fake_child.h"
#include "support/voice_model.h"

/// `voice say` composed (50a), in process over its seams -- a scripted
/// driver writing real WAV files -- and through the real command tree: the
/// voice model and voice resolved, the caps refused before anything runs,
/// the WAV joined and its record kept beside it, and completion of the
/// store's voice models and voices.
namespace {

using apogee::commands::VoiceSayOptions;
using apogee::commands::VoiceSayResult;
using apogee::commands::VoiceSaySeams;
using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

struct Run {
    apogee::testing::TempDir dir{"voice-say-" + std::to_string(std::random_device{}())};
    apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(dir.path() / "models");
    std::filesystem::path model = roots.models / "hexgrad--Kokoro-82M" / "voice" / "aaaaaaaaaaaa";
    std::filesystem::path output = dir.path() / "out" / "hello.wav";
    VoiceSayOptions options;
    nlohmann::json request;
    bool spawned = false;
    int status = 0;
    std::vector<std::string> said;

    Run() {
        apogee::testing::write_voice_model(model);
        apogee::models::Snapshot record;
        record.ref = "hexgrad/Kokoro-82M";
        record.revision = "main";
        record.source = "huggingface";
        REQUIRE(apogee::models::write_snapshot(model, record));
        std::filesystem::create_directories(output.parent_path());
        options.text = "Hello there. How are you?";
        options.output = output;
    }

    [[nodiscard]] VoiceSaySeams seams() {
        VoiceSaySeams seams;
        seams.ready = [](const apogee::commands::ResolvedVoiceModel&) {
            return apogee::video::VideoReadiness{.interpreter = "/venv/bin/python",
                                                 .driver = "/scripts/voice_say.py",
                                                 .torch_version = "2.9.1",
                                                 .kokoro_version = "0.9.4"};
        };
        seams.spawn = [this](const apogee::platform::ChildCommand& command,
                             std::string&) -> std::unique_ptr<apogee::platform::ChildProcess> {
            spawned = true;
            request = nlohmann::json::parse(slurp(command.arguments.at(2)));
            const std::filesystem::path out{request.at("out_dir").get<std::string>()};
            std::filesystem::create_directories(out);
            std::ofstream{out / "s00001.wav", std::ios::binary}
                << apogee::platform::wav_file(std::string(24000 * 2, '\0'), 24000);
            auto child = std::make_unique<apogee::testing::FakeChild>();
            child->stdout_script =
                R"({"event": "ready", "device": "cpu", "torch": "2.9.1", "kokoro": "0.9.4", "sample_rate": 24000})"
                "\n" +
                nlohmann::json{{"event", "segment"},
                               {"index", 1},
                               {"of", 1},
                               {"file", (out / "s00001.wav").string()},
                               {"seconds", 1.0},
                               {"sentences",
                                {{{"text", "Hello there."}, {"seconds", 0.4}},
                                 {{"text", "How are you?"}, {"seconds", 0.6}}}}}
                    .dump() +
                "\n" + R"({"event": "done", "segments": 1, "seconds": 1.0})" + "\n";
            child->exit_status = status;
            return child;
        };
        seams.say = [this](const std::string& line) { said.push_back(line); };
        seams.work_root = dir.path() / "cache" / "video";
        seams.platform = "macos-arm64";
        return seams;
    }

    VoiceSayResult go() {
        return apogee::commands::say_text(roots, options, seams(), {});
    }
};

}  // namespace

TEST_CASE("voice say speaks into one WAV, its sentences timed and its record beside it",
          "[cli][voice][say]") {
    Run run;
    const VoiceSayResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.request["voice"] == "af_heart");  // the default, the model has it
    CHECK(run.request["segments"] == nlohmann::json::array({"Hello there. How are you?"}));
    CHECK(run.said.front() ==
          "hexgrad--Kokoro-82M/voice/aaaaaaaaaaaa: voice af_heart (American "
          "English), speed 1.00");
    std::string error;
    const std::optional<apogee::platform::WavAudio> audio =
        apogee::platform::read_wav(run.output, error);
    REQUIRE(audio.has_value());
    CHECK(audio->seconds() == 1.0);

    const nlohmann::json& document = result.document;
    CHECK(document["object"] == "voice.speech");
    CHECK(document["status"] == "done");
    CHECK(document["model"]["ref"] == "hexgrad/Kokoro-82M");
    CHECK(document["model"]["checkpoint"] == "kokoro-v1_0.pth");
    CHECK(document["language"] == "American English");
    CHECK(document["seconds"] == 1.0);
    CHECK(document["sample_rate"] == 24000);
    REQUIRE(document["segments"].size() == 1);
    CHECK(document["segments"][0]["sentences"][1] ==
          nlohmann::json{{"text", "How are you?"}, {"seconds", 0.6}, {"offset", 0.4}});
    CHECK(document["versions"]["kokoro"] == "0.9.4");
    CHECK(nlohmann::json::parse(slurp(apogee::harness::video_sidecar_path(run.output))) ==
          document);
    REQUIRE(result.lines.size() == 3);
    CHECK(result.lines.at(1) == "1.00 s of speech, voice af_heart, 24000 Hz -- on cpu");
}

TEST_CASE("voice say is refused before anything runs, the fix named", "[cli][voice][say]") {
    Run run;
    const auto refused = [&run](const std::function<void(VoiceSayOptions&)>& set) {
        run.options = {};
        run.options.text = "Hello.";
        run.options.output = run.output;
        set(run.options);
        const VoiceSayResult result = run.go();
        CHECK(result.exit_code == 1);
        CHECK(result.document.is_null());
        return result.error;
    };
    CHECK(refused([](VoiceSayOptions& o) { o.text = "  \n"; }) ==
          "there is nothing to say -- give the text to speak");
    CHECK(refused([](VoiceSayOptions& o) { o.text = std::string(4001, 'a'); }) ==
          "the text is 4001 characters, over the cap of 4000 a run -- speak it in parts");
    CHECK(refused([](VoiceSayOptions& o) { o.speed = 3.0; }) == "--speed 3 is outside 0.5-2");
    CHECK(refused([](VoiceSayOptions& o) { o.voice = "zf_nobody"; }) ==
          "this model has no voice 'zf_nobody' -- its voices: af_heart, am_adam, bf_emma");
    CHECK(refused([](VoiceSayOptions& o) { o.model = "nope"; }).starts_with("no model 'nope' in "));
    std::ofstream{run.output} << "an earlier take";
    CHECK(refused([](VoiceSayOptions&) {}) ==
          run.output.string() + " exists -- a run never writes over a file; name another with -o");
    CHECK(slurp(run.output) == "an earlier take");
    std::filesystem::remove(run.output);
    // Two voice models: the store's one is no longer one.
    apogee::testing::write_voice_model(run.roots.models / "other--V" / "voice" / "bbbbbbbbbbbb");
    CHECK_THAT(refused([](VoiceSayOptions&) {}),
               ContainsSubstring("the store holds 2 voice models -- name one with --model:"));
    CHECK_FALSE(run.spawned);
    // An empty store names the reference model to pull.
    apogee::commands::ResolvedVoiceModel resolved;
    CHECK(apogee::commands::resolve_voice_model(
              apogee::models::StoreRoots::at(run.dir.path() / "empty"), "", resolved) ==
          "no voice model in the store -- 'apogee models pull hexgrad/Kokoro-82M' stores the "
          "reference one");
}

TEST_CASE("a failed voice say writes nothing and says so in its document", "[cli][voice][say]") {
    Run run;
    run.status = 5;
    const VoiceSayResult result = run.go();
    CHECK(result.exit_code == 1);
    CHECK(result.document["status"] == "failed");
    CHECK(result.document["sidecar"].is_null());
    CHECK(result.error.ends_with(run.output.string() + " was not written"));
    CHECK_FALSE(std::filesystem::exists(run.output));
    CHECK_FALSE(std::filesystem::exists(apogee::harness::video_sidecar_path(run.output)));
}

TEST_CASE("the store's voice models and their voices complete for voice say",
          "[cli][voice][complete]") {
    const apogee::testing::CliHome home{"backends:\n  m:\n    type: mock\n"};
    apogee::testing::write_voice_model(home.models() / "hexgrad--K" / "voice" / "aaaaaaaaaaaa");
    std::string out;
    std::string err;
    REQUIRE(home.run({"__complete", "voice", "say", "hi", "--model", ""}, &out, &err) == 0);
    CHECK(out == "hexgrad--K\nhexgrad--K/voice/aaaaaaaaaaaa\n");
    REQUIRE(home.run({"__complete", "voice", "say", "hi", "--voice", ""}, &out, &err) == 0);
    CHECK(out == "af_heart\nam_adam\nbf_emma\n");
    CHECK(home.run({"voice", "say", "hi"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("--output is required"));
}
