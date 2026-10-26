#include "cli/voice_say.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cli/job_stream.h"
#include "contracts/video_run.h"
#include "modelstore/snapshot.h"
#include "platform/ffmpeg.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/fake_child.h"
#include "support/mlx_model.h"
#include "support/voice_model.h"
#include "training/python_env.h"

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
    /// What the ladder answers, and the `ready` line the driver writes: the
    /// PyTorch engine's unless a test says (53m).
    apogee::video::VideoReadiness readiness{.interpreter = "/venv/bin/python",
                                            .driver = "/scripts/voice_say.py",
                                            .torch_version = "2.9.1",
                                            .kokoro_version = "0.9.4"};
    std::string ready_line =
        R"({"event": "ready", "device": "cpu", "torch": "2.9.1", "kokoro": "0.9.4", "sample_rate": 24000})";
    /// The models the ladder was asked about.
    std::vector<apogee::commands::ResolvedVoiceModel> asked{};

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
        seams.ready = [this](const apogee::commands::ResolvedVoiceModel& resolved) {
            asked.push_back(resolved);
            return readiness;
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
            child->stdout_script = ready_line + "\n" +
                                   nlohmann::json{{"event", "segment"},
                                                  {"index", 1},
                                                  {"of", 1},
                                                  {"file", (out / "s00001.wav").string()},
                                                  {"seconds", 1.0},
                                                  {"sentences",
                                                   {{{"text", "Hello there."}, {"seconds", 0.4}},
                                                    {{"text", "How are you?"}, {"seconds", 0.6}}}}}
                                       .dump() +
                                   "\n" + R"({"event": "done", "segments": 1, "seconds": 1.0})" +
                                   "\n";
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

TEST_CASE("voice say records its engine: PyTorch's record unchanged but for it",
          "[cli][voice][say][engine]") {
    // 53m: a voice model as Kokoro publishes it speaks on PyTorch exactly as
    // before -- its request, its record key by key -- the record gaining
    // `engine` alone.
    Run run;
    const VoiceSayResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK_FALSE(run.request.contains("engine"));
    REQUIRE(run.asked.size() == 1);
    CHECK(run.asked.front().engine == apogee::video::VideoEngine::Torch);
    std::set<std::string> keys;
    for (const auto& [key, value] : result.document.items()) {
        keys.insert(key);
    }
    CHECK(keys == std::set<std::string>{"object", "status", "output", "sidecar", "model", "voice",
                                        "language", "speed", "text", "segments", "seconds",
                                        "sample_rate", "device", "versions", "run_seconds",
                                        "created_at", "engine"});
    CHECK(result.document["engine"] == "torch");
    CHECK(result.document["versions"] ==
          nlohmann::json{{"torch", "2.9.1"},
                         {"kokoro", "0.9.4"},
                         {"apogee", result.document["versions"]["apogee"]}});
    CHECK(run.said.front().find("MLX") == std::string::npos);
}

TEST_CASE("voice say speaks an MLX voice model on MLX, its record saying so",
          "[cli][voice][say][engine][mlx_audio]") {
    // 53m: mlx-audio's build of Kokoro, by its handle: the engine its
    // directory says, the request naming it, MLX's versions recorded beside an
    // empty torch and kokoro, the segment measured from its own WAV as ever.
    Run run;
    const std::filesystem::path mlx =
        run.roots.models / "hexgrad--Kokoro-82M" / "mlx" / "bbbbbbbbbbbb";
    apogee::testing::write_mlx_voice_model(mlx);
    run.options.model = "hexgrad--Kokoro-82M/mlx/bbbbbbbbbbbb";
    run.readiness = apogee::video::VideoReadiness{.interpreter = "/venv/bin/python",
                                                  .driver = "/scripts/voice_say_mlx.py",
                                                  .engine = apogee::video::VideoEngine::Mlx,
                                                  .mlx_version = "0.32.4",
                                                  .mlx_audio_version = "0.6.1"};
    run.ready_line =
        R"({"event": "ready", "device": "gpu", "engine": "mlx", "mlx": "0.32.4", "mlx_audio": "0.6.1", "sample_rate": 24000})";
    const VoiceSayResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    REQUIRE(run.asked.size() == 1);
    CHECK(run.asked.front().engine == apogee::video::VideoEngine::Mlx);
    CHECK(run.asked.front().dir == mlx);
    CHECK(run.request["engine"] == "mlx");
    CHECK(run.request["model_dir"] == mlx.string());
    CHECK(run.request["voice"] == "af_heart");  // today's default, which the build carries
    const nlohmann::json& document = result.document;
    CHECK(document["engine"] == "mlx");
    CHECK(document["model"]["name"] == "hexgrad--Kokoro-82M/mlx/bbbbbbbbbbbb");
    CHECK(document["model"]["ref"] == "hexgrad--Kokoro-82M/voice/aaaaaaaaaaaa");
    CHECK(document["model"]["checkpoint"] == "model.safetensors");
    CHECK(document["device"]["device"] == "gpu");
    CHECK(document["versions"]["mlx"] == "0.32.4");
    CHECK(document["versions"]["mlx_audio"] == "0.6.1");
    CHECK(document["versions"]["torch"] == "");
    CHECK(document["versions"]["kokoro"] == "");
    CHECK(document["seconds"] == 1.0);
    CHECK(document["segments"][0]["seconds"] == 1.0);
    CHECK(run.said.front() ==
          "hexgrad--Kokoro-82M/mlx/bbbbbbbbbbbb: voice af_heart (American English), speed 1.00, "
          "on MLX (mlx-audio)");
    REQUIRE(result.lines.size() == 3);
    CHECK(result.lines.at(1) == "1.00 s of speech, voice af_heart, 24000 Hz -- on MLX (gpu)");
    CHECK(apogee::commands::narrate_voice_event({.kind = apogee::video::VoiceEvent::Kind::Ready,
                                                 .ready = {.device = "gpu", .engine = "mlx"}}) ==
          "speaking on MLX (gpu)");
}

TEST_CASE("the streamed ready says the engine a speech runs on, on both engines",
          "[cli][voice][say][engine][job]") {
    // M24: the `ready` event the stream relays (52b) carries the speech's
    // engine, its device as it was -- the same word the record says.
    const auto streamed = [](Run& run) {
        std::ostringstream out;
        apogee::commands::JsonReporter events{out};
        VoiceSaySeams seams = run.seams();
        seams.on_event = [&events](const apogee::video::VoiceEvent& event) {
            apogee::commands::relay_voice_event(events, event, 0);
        };
        const VoiceSayResult result = apogee::commands::say_text(run.roots, run.options, seams, {});
        INFO(result.error);
        REQUIRE(result.exit_code == 0);
        std::vector<nlohmann::json> ready;
        std::istringstream lines{out.str()};
        for (std::string line; std::getline(lines, line);) {
            const nlohmann::json event = nlohmann::json::parse(line);
            if (event.at("type") == "ready") {
                ready.push_back(event);
            }
        }
        REQUIRE(ready.size() == 1);
        CHECK(ready.front().at("engine") == result.document.at("engine"));
        return ready.front();
    };
    // PyTorch's driver says no engine: the speech's is said for it.
    Run torch;
    CHECK(streamed(torch) ==
          nlohmann::json{{"type", "ready"}, {"device", "cpu"}, {"engine", "torch"}});
    // mlx-audio's build of Kokoro, as 53m's case runs it.
    Run mlx;
    apogee::testing::write_mlx_voice_model(mlx.roots.models / "hexgrad--Kokoro-82M" / "mlx" /
                                           "bbbbbbbbbbbb");
    mlx.options.model = "hexgrad--Kokoro-82M/mlx/bbbbbbbbbbbb";
    mlx.readiness = apogee::video::VideoReadiness{.interpreter = "/venv/bin/python",
                                                  .driver = "/scripts/voice_say_mlx.py",
                                                  .engine = apogee::video::VideoEngine::Mlx,
                                                  .mlx_version = "0.32.4",
                                                  .mlx_audio_version = "0.6.1"};
    mlx.ready_line =
        R"({"event": "ready", "device": "gpu", "engine": "mlx", "mlx": "0.32.4", "mlx_audio": "0.6.1", "sample_rate": 24000})";
    CHECK(streamed(mlx) == nlohmann::json{{"type", "ready"}, {"device", "gpu"}, {"engine", "mlx"}});
}

TEST_CASE("voice say's --engine only names the model's engine, or is refused before anything",
          "[cli][voice][say][engine][mlx_audio]") {
    Run run;
    apogee::testing::write_mlx_voice_model(run.roots.models / "hexgrad--Kokoro-82M" / "mlx" /
                                           "bbbbbbbbbbbb");
    const auto with = [&run](const std::string& model, const std::string& engine) {
        run.options.model = model;
        run.options.engine = engine;
        run.spawned = false;
        run.asked.clear();
        std::filesystem::remove(run.output);
        std::filesystem::remove(apogee::harness::video_sidecar_path(run.output));
        return run.go();
    };
    const std::string original = "hexgrad--Kokoro-82M/voice/aaaaaaaaaaaa";
    const std::string built = "hexgrad--Kokoro-82M/mlx/bbbbbbbbbbbb";
    // Its own engine, or auto: it runs.
    for (const std::string engine : {"auto", "torch"}) {
        INFO(engine);
        CHECK(with(original, engine).exit_code == 0);
        CHECK(run.spawned);
    }
    // The other: refused before the ladder, nothing spawned.
    VoiceSayResult refused = with(original, "mlx");
    CHECK(refused.exit_code == 1);
    CHECK(refused.error ==
          "--engine mlx: '" + original +
              "' is a voice model as Kokoro publishes it, which speaks on PyTorch (kokoro), not "
              "MLX -- MLX speaks with its MLX build: 'apogee models convert " +
              original + " --mlx' makes one, after 'apogee train setup --with mlx-audio'");
    CHECK(run.asked.empty());
    CHECK_FALSE(run.spawned);
    refused = with(built, "torch");
    CHECK(refused.error.starts_with("--engine torch: '" + built +
                                    "' is an MLX voice model, which speaks on MLX (mlx-audio), "
                                    "not PyTorch"));
    CHECK_FALSE(run.spawned);
    CHECK(with(original, "cuda").error == "--engine takes auto, torch or mlx (got 'cuda')");
    CHECK_FALSE(std::filesystem::exists(run.output));
}

TEST_CASE("an MLX voice model resolves by handle, model or path, and only as a voice model",
          "[cli][voice][say][mlx_audio]") {
    Run run;
    const std::filesystem::path built =
        run.roots.models / "hexgrad--Kokoro-82M" / "mlx" / "bbbbbbbbbbbb";
    apogee::testing::write_mlx_voice_model(built, {.voices = {"bf_emma", "jf_alpha"}});
    apogee::commands::ResolvedVoiceModel resolved;
    REQUIRE(apogee::commands::resolve_voice_model(run.roots, built.string(), resolved).empty());
    CHECK(resolved.engine == apogee::video::VideoEngine::Mlx);
    CHECK(resolved.info.complete);
    CHECK(resolved.info.has_voice("jf_alpha"));
    // Outside the store, a directory speaks on what its files and record say.
    const std::filesystem::path elsewhere = run.dir.path() / "elsewhere" / "kokoro-mlx";
    apogee::testing::write_mlx_voice_model(elsewhere);
    apogee::commands::ResolvedVoiceModel outside;
    REQUIRE(apogee::commands::resolve_voice_model(run.roots, elsewhere.string(), outside).empty());
    CHECK(outside.engine == apogee::video::VideoEngine::Mlx);
    CHECK(outside.dir == elsewhere);
    REQUIRE(apogee::commands::resolve_voice_model(run.roots, run.model.string(), outside).empty());
    CHECK(outside.engine == apogee::video::VideoEngine::Torch);
    // With no af_heart among its voices, its first is the default.
    std::string voice;
    REQUIRE(apogee::commands::choose_voice(resolved.info, "", voice).empty());
    CHECK(voice == "bf_emma");
    // The model holding the original and its build holds two voice models.
    CHECK(apogee::commands::resolve_voice_model(run.roots, "hexgrad--Kokoro-82M", resolved) ==
          "'hexgrad--Kokoro-82M' holds 2 voice models -- name one:\n  "
          "hexgrad--Kokoro-82M/mlx/bbbbbbbbbbbb\n  hexgrad--Kokoro-82M/voice/aaaaaaaaaaaa");
    // An MLX directory of another kind is no voice model.
    apogee::testing::write_mlx_model(run.roots.models / "org--m" / "mlx" / "cccccccccccc",
                                     {.kind = "chat"});
    CHECK(apogee::commands::resolve_voice_model(run.roots, "org--m/mlx/cccccccccccc", resolved) ==
          "'org--m/mlx/cccccccccccc' is an MLX chat model, not a voice model -- a language "
          "model: an mlx backend chats with it");
    CHECK(apogee::commands::resolve_voice_model(run.roots, "org--m", resolved) ==
          "'org--m' holds no voice model");
}

TEST_CASE("a runtime not installed ends a streamed voice say on its error, its words on stderr too",
          "[cli][voice][say][stream]") {
    // M29: the stream carried its `job` line alone, the words on stderr.
    const apogee::testing::CliHome home{"backends:\n  m:\n    type: mock\n"};
    apogee::testing::write_voice_model(home.models() / "hexgrad--Kokoro-82M" / "voice" /
                                       "aaaaaaaaaaaa");
    const std::filesystem::path interpreter =
        apogee::training::PythonEnv{home.home() / "training" / "venv"}.interpreter();
    std::filesystem::create_directories(interpreter.parent_path());
    std::ofstream{interpreter} << "#!fake";
    const std::filesystem::path wav = home.home() / "hi.wav";
    std::string out;
    std::string err;
    CHECK(home.run({"voice", "say", "Hi.", "-o", wav.string(), "--output-format", "stream-json"},
                   &out, &err) == 1);
    std::vector<nlohmann::json> events;
    std::istringstream lines{out};
    for (std::string line; std::getline(lines, line);) {
        events.push_back(nlohmann::json::parse(line));
    }
    REQUIRE(events.size() == 2);
    CHECK(events.front().at("type") == "job");
    CHECK(events.back().at("type") == "error");
    const std::string refusal =
        "torch and kokoro are not installed in Apogee's Python environment -- apogee train setup "
        "--with voice";
    CHECK(events.back().at("message") == refusal);
    CHECK(err == "apogee voice say: " + refusal + "\n");
    CHECK_FALSE(std::filesystem::exists(wav));
}
