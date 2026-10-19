#include "cli/video_compose.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "contracts/video_project.h"
#include "contracts/video_run.h"
#include "modelstore/snapshot.h"
#include "support/cli_home.h"
#include "support/pipeline_model.h"
#include "support/voice_model.h"

/// `video compose` composed (50c), in process over its seams -- a scripted
/// plan, narration and clips that write files, an assembly that records
/// what it was handed -- and through the real command tree: every refusal
/// before anything runs, the plan said with its spend before the first
/// scene, the project document beside the video from the first step, a
/// failed run resumed by the same command rendering only what was missing,
/// and completion of the verb's names.
namespace {

using apogee::commands::VideoComposeOptions;
using apogee::commands::VideoComposeResult;
using apogee::commands::VideoComposeSeams;
using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The planner's answer: three scenes, the first naming `asset` when given.
[[nodiscard]] nlohmann::json plan(const std::string& asset = {}) {
    nlohmann::json scenes = nlohmann::json::array();
    for (int index = 1; index <= 3; ++index) {
        nlohmann::json scene = {{"narration", "Line " + std::to_string(index) + "."},
                                {"visual", "shot " + std::to_string(index)},
                                {"seconds", 4}};
        if (index == 1 && !asset.empty()) {
            scene["asset"] = asset;
        }
        scenes.push_back(scene);
    }
    return {{"title", "Tides"}, {"scenes", scenes}};
}

struct Run {
    apogee::testing::CliHome home{"models:\n  default: m\nbackends:\n  m:\n    type: mock\n"};
    apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(home.models());
    std::filesystem::path out_dir = home.home() / "out";
    VideoComposeOptions options;
    std::vector<std::string> said;
    std::vector<std::string> calls;
    std::optional<nlohmann::json> answer = plan();
    std::string render_error;
    bool can_burn = true;
    std::int64_t next_seed = 7;
    std::string refuse_video;

    Run() {
        const std::filesystem::path pipeline =
            roots.models / "org--clip" / "pipeline" / "aaaaaaaaaaaa";
        apogee::testing::write_pipeline_model(pipeline);
        apogee::models::Snapshot clip_record;
        clip_record.ref = "org/clip";
        clip_record.revision = "main";
        clip_record.source = "huggingface";
        REQUIRE(apogee::models::write_snapshot(pipeline, clip_record));
        const std::filesystem::path voice =
            roots.models / "hexgrad--Kokoro-82M" / "voice" / "bbbbbbbbbbbb";
        apogee::testing::write_voice_model(voice);
        apogee::models::Snapshot voice_record;
        voice_record.ref = "hexgrad/Kokoro-82M";
        voice_record.revision = "main";
        voice_record.source = "huggingface";
        REQUIRE(apogee::models::write_snapshot(voice, voice_record));
        std::filesystem::create_directories(out_dir);
        options.brief = "a minute on tides";
        options.output = out_dir / "tides.mp4";
        options.model = "org--clip";
    }

    [[nodiscard]] VideoComposeSeams seams() {
        VideoComposeSeams seams;
        seams.ready_video = [this](const apogee::commands::ResolvedPipeline&) {
            apogee::video::VideoReadiness ready;
            if (!refuse_video.empty()) {
                ready.refusal = apogee::video::VideoRefusal::NoDiffusers;
                ready.reason = refuse_video;
                ready.remedy = "apogee train setup --with video";
            }
            return ready;
        };
        seams.ready_voice = [](const apogee::commands::ResolvedVoiceModel&) {
            return apogee::video::VideoReadiness{};
        };
        seams.plan = [this](const std::string& backend, const std::string&, const std::string& user,
                            const std::string&) {
            calls.push_back("plan on " + backend);
            CHECK_THAT(user, ContainsSubstring("a minute on tides"));
            return apogee::video::PlanAnswer{
                .json = answer, .conforms = answer.has_value(), .attempts = 1};
        };
        seams.speak = [this](const apogee::commands::ResolvedVoiceModel& model,
                             const std::string& voice, const std::string& text,
                             const std::filesystem::path& wav) {
            calls.push_back("speak " + wav.filename().string() + " as " + voice + " with " +
                            model.handle);
            std::ofstream{wav} << "RIFF";
            return apogee::video::SpokenScene{
                .ok = true, .seconds = 2.0, .sentences = {{.text = text, .seconds = 2.0}}};
        };
        seams.render = [this](const apogee::commands::ResolvedPipeline& pipeline,
                              const apogee::video::ClipAsk& ask, const std::filesystem::path& mp4) {
            calls.push_back("render " + mp4.filename().string() + " seed " +
                            std::to_string(ask.seed) + " on " + pipeline.handle);
            if (!render_error.empty() && ask.scene == 2) {
                return apogee::video::RenderedClip{.error = render_error};
            }
            std::ofstream{mp4} << "MP4";
            return apogee::video::RenderedClip{.ok = true};
        };
        seams.assembly.probe = [](const std::filesystem::path&,
                                  const apogee::platform::FfmpegLimits&,
                                  std::string&) -> std::optional<apogee::platform::MediaInfo> {
            apogee::platform::MediaInfo info;
            info.video = true;
            info.duration = 4.04;
            return info;
        };
        const auto made = [this](const char* step, const std::filesystem::path& out) {
            calls.emplace_back(step);
            std::ofstream{out} << step;
            return apogee::platform::Assembled{.codec = "h264", .settings = step};
        };
        seams.assembly.join = [made](const std::vector<std::filesystem::path>&, double,
                                     const std::filesystem::path& out,
                                     const apogee::platform::FfmpegLimits&,
                                     std::string&) -> std::optional<apogee::platform::Assembled> {
            return made("join", out);
        };
        seams.assembly.mix = [made](const std::filesystem::path&, const apogee::platform::AudioMix&,
                                    const std::filesystem::path& out,
                                    const apogee::platform::FfmpegLimits&,
                                    std::string&) -> std::optional<apogee::platform::Assembled> {
            return made("mix", out);
        };
        seams.assembly.burn = [made](const std::filesystem::path&, const std::filesystem::path&,
                                     const std::filesystem::path& out,
                                     const apogee::platform::FfmpegLimits&,
                                     std::string&) -> std::optional<apogee::platform::Assembled> {
            return made("burn", out);
        };
        seams.can_burn = [this] { return can_burn; };
        seams.random_seed = [this] { return next_seed++; };
        seams.say = [this](const std::string& line) { said.push_back(line); };
        seams.platform = "macos-arm64";
        return seams;
    }

    VideoComposeResult go() {
        return apogee::commands::compose_video(config, roots, options, seams(), {});
    }

    [[nodiscard]] std::filesystem::path sidecar() const {
        return apogee::harness::video_sidecar_path(out_dir / "tides.mp4");
    }
};

}  // namespace

TEST_CASE("video compose is refused before anything runs, the fix named", "[cli][video][compose]") {
    Run run;
    const auto refused = [&run](const std::function<void(VideoComposeOptions&)>& set) {
        const VideoComposeOptions kept = run.options;
        set(run.options);
        const VideoComposeResult result = run.go();
        run.options = kept;
        CHECK(result.exit_code == 1);
        CHECK(result.document.is_null());
        return result.error;
    };
    const std::filesystem::path out = run.out_dir / "tides.mp4";
    CHECK(refused([](VideoComposeOptions& o) { o.brief = " \n"; }) ==
          "there is nothing to compose -- give the brief");
    CHECK(refused([](VideoComposeOptions& o) { o.brief = std::string(2001, 'a'); }) ==
          "the brief is 2001 characters, over the cap of 2000 -- say it shorter");
    CHECK(refused([&](VideoComposeOptions& o) { o.output = run.out_dir / "tides.mov"; }) ==
          "the video is an mp4 -- name it with .mp4");
    CHECK(refused([&](VideoComposeOptions& o) { o.output = run.out_dir / "no" / "t.mp4"; }) ==
          "no directory " + (run.out_dir / "no").string() + " for the video");
    CHECK(refused([&](VideoComposeOptions& o) { o.music = run.out_dir / "bed.wav"; }) ==
          "there is no music at " + (run.out_dir / "bed.wav").string());
    CHECK(refused([&](VideoComposeOptions& o) { o.assets = run.out_dir / "assets"; }) ==
          "there is no assets directory at " + (run.out_dir / "assets").string());
    std::filesystem::create_directories(run.out_dir / "empty");
    std::ofstream{run.out_dir / "empty" / "notes.txt"} << "x";
    CHECK(refused([&](VideoComposeOptions& o) { o.assets = run.out_dir / "empty"; }) ==
          (run.out_dir / "empty").string() +
              " holds no stills or clips (png, jpg, jpeg, webp, mp4, mov, m4v, webm)");
    CHECK(refused([](VideoComposeOptions& o) { o.voice = "zf_nobody"; }) ==
          "this model has no voice 'zf_nobody' -- its voices: af_heart, am_adam, bf_emma");
    CHECK_THAT(refused([](VideoComposeOptions& o) { o.model = "nope"; }),
               ContainsSubstring("nope"));
    run.refuse_video = "diffusers is not installed in the Python environment at /venv";
    CHECK(refused([](VideoComposeOptions&) {}) ==
          "diffusers is not installed in the Python environment at /venv -- apogee train setup "
          "--with video");
    run.refuse_video.clear();
    // A planner whose answer never holds is refused with the caps quoted.
    run.answer.reset();
    CHECK_THAT(refused([](VideoComposeOptions&) {}),
               ContainsSubstring("the plan did not hold to its shape -- the caps: at most 8 "
                                 "scenes"));
    run.answer = plan();
    // An existing video, and a project beside it for another brief.
    std::ofstream{out} << "an earlier video";
    CHECK(refused([](VideoComposeOptions&) {}) ==
          out.string() + " exists -- a run never writes over a file; name another with -o");
    std::filesystem::remove(out);
    std::ofstream{run.sidecar()} << "{}";
    CHECK(refused([](VideoComposeOptions&) {}) ==
          run.sidecar().string() +
              " is there and is not a project this run can resume (it is not a video.project "
              "document) -- name another -o");
    apogee::harness::VideoProject other;
    other.brief = "another brief";
    other.output = out;
    other.scenes_dir = run.out_dir / "tides.scenes";
    other.scenes = {{.narration = "n", .visual = "v", .seconds = 3.0}};
    std::ofstream{run.sidecar()} << apogee::harness::video_project_document(other).dump();
    CHECK(refused([](VideoComposeOptions&) {}) ==
          run.sidecar().string() +
              " holds a project for another brief -- name another -o, or move it away to start "
              "again");
    // Nothing was spoken or rendered by any of it.
    CHECK(std::ranges::none_of(run.calls, [](const std::string& call) {
        return call.starts_with("speak") || call.starts_with("render");
    }));
}

TEST_CASE("video compose plans, says the plan and its spend, then renders it whole",
          "[cli][video][compose]") {
    Run run;
    const VideoComposeResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    const std::string clip = "org--clip/pipeline/aaaaaaaaaaaa";
    const std::string voice = "hexgrad--Kokoro-82M/voice/bbbbbbbbbbbb";
    CHECK(run.calls == std::vector<std::string>{
                           "plan on m", "speak s01.wav as af_heart with " + voice,
                           "speak s02.wav as af_heart with " + voice,
                           "speak s03.wav as af_heart with " + voice,
                           "render s01.mp4 seed 7 on " + clip, "render s02.mp4 seed 8 on " + clip,
                           "render s03.mp4 seed 9 on " + clip, "join", "mix", "burn"});
    // Said before anything renders: the plan, each scene, the spend.
    const auto at = [&run](const std::string& line) {
        return std::ranges::find(run.said, line) - run.said.begin();
    };
    CHECK(at("plan: \"Tides\" -- 3 scenes, about 11.0 s of video") <
          at("scene 1/3: speaking its narration"));
    CHECK(at("spend: 3 narration runs (faster than real time), then 3 clip runs at 1280x704, 50 "
             "steps -- each takes minutes on Apple silicon, less on a CUDA GPU") <
          at("scene 1/3: speaking its narration"));

    const nlohmann::json& document = result.document;
    CHECK(document["object"] == "video.project");
    CHECK(document["status"] == "done");
    CHECK(document["title"] == "Tides");
    CHECK(document["settings"]["model"] == clip);
    CHECK(document["settings"]["voice_model"] == voice);
    CHECK(document["settings"]["voice"] == "af_heart");
    CHECK(document["settings"]["crossfade"] == 0.5);
    CHECK(document["plan"] == nlohmann::json{{"backend", "m"}, {"attempts", 1}});
    CHECK(document["scenes"][2]["seed"] == 9);
    CHECK(document["scenes"][2]["clip"]["seconds"] == 4.04);
    CHECK(document["captions"]["burned"] == true);
    CHECK(document["platform"] == "macos-arm64");
    CHECK(nlohmann::json::parse(slurp(run.sidecar())) == document);
    CHECK(slurp(run.out_dir / "tides.mp4") == "burn");
    REQUIRE(result.lines.size() == 2);
    CHECK(result.lines[0] == (run.out_dir / "tides.mp4").string() +
                                 ": 11.1 s, 3 scenes narrated by af_heart, captions burned in");
    CHECK(result.lines[1] == "its project, beside it: " + run.sidecar().string());
}

TEST_CASE("the plan is said scene by scene, with its spend and its captions' fate",
          "[cli][video][compose]") {
    apogee::harness::VideoProject project;
    project.title = "Tides";
    project.output = std::filesystem::path{"out"} / "tides.mp4";
    project.crossfade = 0.5;
    project.scenes = {
        {.narration = "The sea breathes.", .visual = "a shore at dawn", .seconds = 4.0},
        {.narration = "The moon pulls.", .visual = "unused", .seconds = 3.0, .asset = "moon.mp4"},
        {.narration = "Twice a day.",
         .visual = std::string(130, 'w'),
         .seconds = 5.0,
         .asset = "beach.png"}};
    CHECK(apogee::commands::plan_lines(project, false) ==
          std::vector<std::string>{
              "plan: \"Tides\" -- 3 scenes, about 11.0 s of video",
              "  1. (4.0 s) \"The sea breathes.\"", "     shows: a shore at dawn",
              "  2. (3.0 s) \"The moon pulls.\"", "     takes moon.mp4 as it is",
              "  3. (5.0 s) \"Twice a day.\"",
              "     shows: " + std::string(120, 'w') + "... (from beach.png)",
              "spend: 3 narration runs (faster than real time), then 2 clip runs at 1280x704, 50 "
              "steps -- each takes minutes on Apple silicon, less on a CUDA GPU; 1 asset taken "
              "as it is",
              "captions: kept beside the video as tides.srt (this ffmpeg has no libass to burn "
              "them)"});
    project.captions = false;
    CHECK(apogee::commands::plan_lines(project, true).back() == "captions: off");
    project.captions = true;
    CHECK(apogee::commands::plan_lines(project, true).back() == "captions: burned into the frames");
}

TEST_CASE("a failed compose keeps its scenes, and the same command resumes it",
          "[cli][video][compose]") {
    Run run;
    run.render_error = "the driver said no";
    VideoComposeResult result = run.go();
    CHECK(result.exit_code == 1);
    CHECK(result.error ==
          "scene 2/3's clip: the driver said no -- every finished scene is kept, "
          "and the same command resumes");
    CHECK(result.document["status"] == "failed");
    CHECK(nlohmann::json::parse(slurp(run.sidecar()))["status"] == "failed");
    CHECK_FALSE(std::filesystem::exists(run.out_dir / "tides.mp4"));

    // Again: no plan, no narration -- scene 1's clip kept, 2 and 3 rendered
    // with the seeds the plan drew.
    run.render_error.clear();
    run.calls.clear();
    run.said.clear();
    run.options.voice = "am_adam";  // a resumed project keeps its own
    result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.calls ==
          std::vector<std::string>{"render s02.mp4 seed 8 on org--clip/pipeline/aaaaaaaaaaaa",
                                   "render s03.mp4 seed 9 on org--clip/pipeline/aaaaaaaaaaaa",
                                   "join", "mix", "burn"});
    CHECK(run.said.front() ==
          "resuming " + run.sidecar().string() + ": 3 of 3 narrations and 1 clips already there");
    CHECK(run.said.at(1) ==
          "a resumed project keeps its own settings: --model, --voice not "
          "applied");
    CHECK(result.document["status"] == "done");
    CHECK(result.document["settings"]["voice"] == "af_heart");
}

TEST_CASE("captions this ffmpeg cannot burn are kept beside the video, said up front",
          "[cli][video][compose]") {
    Run run;
    run.can_burn = false;
    const VideoComposeResult result = run.go();
    REQUIRE(result.exit_code == 0);
    CHECK(std::ranges::find(run.calls, "burn") == run.calls.end());
    CHECK(std::ranges::find(run.said,
                            "captions: kept beside the video as tides.srt (this ffmpeg "
                            "has no libass to burn them)") != run.said.end());
    CHECK(std::filesystem::exists(run.out_dir / "tides.srt"));
    CHECK(result.lines[0].ends_with("captions kept beside it as tides.srt"));

    // And a file already there is never written over.
    Run again;
    again.can_burn = false;
    std::ofstream{again.out_dir / "tides.srt"} << "mine";
    const VideoComposeResult refused = again.go();
    CHECK(refused.exit_code == 1);
    CHECK(refused.error == (again.out_dir / "tides.srt").string() +
                               " exists -- this ffmpeg cannot burn the captions, so they would "
                               "be kept there; name another -o");
}

TEST_CASE("an asset the plan names is offered from the directory and slotted",
          "[cli][video][compose]") {
    Run run;
    std::filesystem::create_directories(run.out_dir / "assets");
    std::ofstream{run.out_dir / "assets" / "beach.png"} << "PNG";
    run.options.assets = run.out_dir / "assets";
    run.answer = plan("beach.png");
    const VideoComposeResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(result.document["scenes"][0]["asset"] == "beach.png");
    CHECK(result.document["settings"]["assets"] == (run.out_dir / "assets").string());
}

TEST_CASE("a narration and a clip run read their own records", "[cli][video][compose]") {
    apogee::commands::VoiceSayResult spoke;
    spoke.document = {{"status", "done"},
                      {"seconds", 2.5},
                      {"segments",
                       {{{"seconds", 2.5},
                         {"sentences",
                          {{{"text", "One."}, {"seconds", 1.0}, {"offset", 0.0}},
                           {{"text", "Two."}, {"seconds", 1.5}, {"offset", 1.0}}}}}}}};
    const apogee::video::SpokenScene scene = apogee::commands::spoken_from(spoke);
    CHECK(scene.ok);
    CHECK(scene.seconds == 2.5);
    REQUIRE(scene.sentences.size() == 2);
    CHECK(scene.sentences[1].text == "Two.");
    CHECK(scene.sentences[1].offset == 1.0);
    apogee::commands::VoiceSayResult stopped;
    stopped.exit_code = 130;
    stopped.error = "cancelled";
    CHECK(apogee::commands::spoken_from(stopped).cancelled);
    CHECK_FALSE(apogee::commands::spoken_from(stopped).ok);

    apogee::commands::VideoGenerateResult rendered;
    rendered.document = {{"status", "done"}};
    CHECK(apogee::commands::rendered_from(rendered).ok);
    apogee::commands::VideoGenerateResult failed;
    failed.exit_code = 1;
    failed.error = "boom";
    CHECK(apogee::commands::rendered_from(failed).error == "boom");
}

TEST_CASE("video compose completes its names and requires its output",
          "[cli][video][compose][complete]") {
    const apogee::testing::CliHome home{"backends:\n  m:\n    type: mock\n"};
    apogee::testing::write_voice_model(home.models() / "hexgrad--K" / "voice" / "aaaaaaaaaaaa");
    std::string out;
    std::string err;
    REQUIRE(home.run({"__complete", "video", "compose", "b", "--voice", ""}, &out, &err) == 0);
    CHECK(out == "af_heart\nam_adam\nbf_emma\n");
    REQUIRE(home.run({"__complete", "video", "compose", "b", "--voice-model", ""}, &out, &err) ==
            0);
    CHECK(out == "hexgrad--K\nhexgrad--K/voice/aaaaaaaaaaaa\n");
    REQUIRE(home.run({"__complete", "video", "compose", "b", "--planner", ""}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("m"));
    CHECK(home.run({"video", "compose", "b"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("--output is required"));
}
