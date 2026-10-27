#include "cli/video_compose.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "contracts/video_project.h"
#include "contracts/video_run.h"
#include "modelstore/snapshot.h"
#include "support/cli_home.h"
#include "support/pipeline_model.h"
#include "support/voice_model.h"
#include "training/python_env.h"

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
            std::ofstream{mp4} << "MP4 seed " << ask.seed;
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
          "there is nothing to compose -- give the brief, or --from a project");
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
    // `-m` names the project's own pipeline: applied, so not said (55c).
    CHECK(run.said.at(1) == "a resumed project keeps its own settings: --voice not applied");
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

namespace {

/// The editor's answer: the plan's three scenes, each from its own, with
/// scene 2's visual changed.
[[nodiscard]] nlohmann::json edited_plan() {
    nlohmann::json out = plan();
    for (std::size_t index = 0; index < 3; ++index) {
        out["scenes"][index]["from"] = index + 1;
    }
    out["scenes"][1]["visual"] = "the moon over water";
    return out;
}

/// A finished composition at `tides.mp4`, then options set to edit it into
/// `edited.mp4`.
void compose_parent(Run& run) {
    REQUIRE(run.go().exit_code == 0);
    run.calls.clear();
    run.said.clear();
    run.options = {};
    run.options.from = run.out_dir / "tides.mp4";
    run.options.output = run.out_dir / "edited.mp4";
}

}  // namespace

TEST_CASE("video compose --edit rewrites the plan, says the diff, and renders only what changed",
          "[cli][video][compose][edit]") {
    Run run;
    compose_parent(run);
    run.options.edit = "make the second scene about the moon";
    run.answer = edited_plan();
    const VideoComposeResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.calls == std::vector<std::string>{
                           "plan on m", "render s02.mp4 seed 8 on org--clip/pipeline/aaaaaaaaaaaa",
                           "join", "mix", "burn"});
    const auto said = [&run](const std::string& line) {
        return std::ranges::find(run.said, line) != run.said.end();
    };
    CHECK(said("editing " + run.sidecar().string() +
               " with m: \"make the second scene about the "
               "moon\""));
    CHECK(said("the edit: 3 scenes, were 3"));
    CHECK(said("  2. scene 2; its prompt changed"));
    CHECK(
        said("spend: 1 clip run at 1280x704, 50 steps -- each takes minutes on Apple silicon, "
             "less on a CUDA GPU; 2 clips already made"));
    // Untouched scenes carried byte for byte; the changed one rendered anew
    // on its held seed.
    const std::filesystem::path scenes = run.out_dir / "edited.scenes";
    CHECK(slurp(scenes / "s01.mp4") == "MP4 seed 7");
    CHECK(slurp(scenes / "s03.mp4") == "MP4 seed 9");
    CHECK(slurp(scenes / "s02.mp4") == "MP4 seed 8");
    CHECK(slurp(scenes / "s01.wav") == "RIFF");
    // The document chains to its parent.
    const nlohmann::json& document = result.document;
    CHECK(document["status"] == "done");
    CHECK(document["brief"] == "a minute on tides");
    CHECK(document["parent"] == run.sidecar().string());
    REQUIRE(document["edits"].size() == 1);
    CHECK(document["edits"][0] ==
          nlohmann::json{{"instruction", "make the second scene about the moon"},
                         {"redo", nlohmann::json::array()},
                         {"from", run.sidecar().string()}});
    CHECK(document["scenes"][1]["visual"] == "the moon over water");
    CHECK(nlohmann::json::parse(slurp(run.out_dir / "edited.mp4.json")) == document);
    // The parent is untouched.
    CHECK(slurp(run.out_dir / "tides.scenes" / "s02.mp4") == "MP4 seed 8");
    CHECK(nlohmann::json::parse(slurp(run.sidecar()))["visual"].is_null());
}

TEST_CASE("video compose --redo-scene re-rolls one scene; a voice override speaks them all again",
          "[cli][video][compose][edit]") {
    Run run;
    compose_parent(run);
    run.options.redo = {3};
    VideoComposeResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.calls ==
          std::vector<std::string>{"render s03.mp4 seed 10 on org--clip/pipeline/aaaaaaaaaaaa",
                                   "join", "mix", "burn"});
    CHECK(std::ranges::find(run.said, "  3. scene 3; a new seed") != run.said.end());
    CHECK(result.document["scenes"][2]["seed"] == 10);
    CHECK(result.document["edits"][0]["redo"] == nlohmann::json::array({3}));
    CHECK(result.document["edits"][0]["instruction"].is_null());

    run.calls.clear();
    run.said.clear();
    run.options.redo.clear();
    run.options.output = run.out_dir / "voiced.mp4";
    run.options.voice = "am_adam";
    result = run.go();
    REQUIRE(result.exit_code == 0);
    CHECK(std::ranges::count_if(run.calls, [](const std::string& call) {
              return call.starts_with("speak") && call.find(" as am_adam ") != std::string::npos;
          }) == 3);
    CHECK(std::ranges::none_of(run.calls,
                               [](const std::string& call) { return call.starts_with("render"); }));
    CHECK(std::ranges::find(run.said,
                            "the voice is not the project's: every narration is spoken again") !=
          run.said.end());
}

TEST_CASE("an edit that does not hold, or a dry run, writes nothing",
          "[cli][video][compose][edit]") {
    Run run;
    compose_parent(run);
    run.options.edit = "make it longer";
    run.answer.reset();
    VideoComposeResult result = run.go();
    CHECK(result.exit_code == 1);
    CHECK(result.document.is_null());
    CHECK_THAT(result.error, ContainsSubstring("the edit did not hold to its shape"));
    CHECK(result.error.ends_with("nothing was changed"));
    CHECK_FALSE(std::filesystem::exists(run.out_dir / "edited.mp4.json"));
    CHECK_FALSE(std::filesystem::exists(run.out_dir / "edited.scenes"));

    run.answer = edited_plan();
    run.options.dry_run = true;
    run.calls.clear();
    run.said.clear();
    result = run.go();
    REQUIRE(result.exit_code == 0);
    CHECK(run.calls == std::vector<std::string>{"plan on m"});
    CHECK(std::ranges::find(run.said, "  2. scene 2; its prompt changed") != run.said.end());
    CHECK(result.document["status"] == "planned");
    CHECK(result.document["scenes"][1]["visual"] == "the moon over water");
    CHECK(result.lines == std::vector<std::string>{"a dry run: nothing was rendered or written"});
    CHECK_FALSE(std::filesystem::exists(run.out_dir / "edited.mp4.json"));
    CHECK_FALSE(std::filesystem::exists(run.out_dir / "edited.scenes"));
}

TEST_CASE("an edit is refused before anything runs when it cannot be one",
          "[cli][video][compose][edit]") {
    Run run;
    const auto refused = [&run](const std::function<void(VideoComposeOptions&)>& set) {
        const VideoComposeOptions kept = run.options;
        set(run.options);
        const VideoComposeResult result = run.go();
        run.options = kept;
        CHECK(result.exit_code == 1);
        return result.error;
    };
    CHECK(refused([](VideoComposeOptions& o) { o.edit = "shorter"; }) ==
          "--edit and --redo-scene work on a project -- name it with --from");
    CHECK(refused([](VideoComposeOptions& o) { o.redo = {1}; }) ==
          "--edit and --redo-scene work on a project -- name it with --from");
    CHECK(refused([&run](VideoComposeOptions& o) { o.from = run.out_dir / "tides.mp4"; }) ==
          "--from takes its brief from the project -- give no brief");
    CHECK(refused([&run](VideoComposeOptions& o) {
              o.brief.clear();
              o.from = run.out_dir / "none.mp4";
          }) == "there is no project at " + (run.out_dir / "none.mp4.json").string() +
                    " -- --from takes a composed video or its project");
    compose_parent(run);
    run.options.redo = {4};
    const VideoComposeResult result = run.go();
    CHECK(result.error == "--redo-scene 4: the project has 3 scenes");
    CHECK(run.calls.empty());
}

TEST_CASE("a failed edit run is resumed by the same command, without editing again",
          "[cli][video][compose][edit]") {
    Run run;
    compose_parent(run);
    run.options.edit = "make the second scene about the moon";
    run.answer = edited_plan();
    run.render_error = "the driver said no";
    CHECK(run.go().exit_code == 1);
    run.render_error.clear();
    run.calls.clear();
    const VideoComposeResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.calls ==
          std::vector<std::string>{"render s02.mp4 seed 8 on org--clip/pipeline/aaaaaaaaaaaa",
                                   "join", "mix", "burn"});
    CHECK(result.document["parent"] == run.sidecar().string());
}

TEST_CASE("a brief planned on paper: one planning call, the plan kept, nothing rendered",
          "[cli][video][compose][dry-run]") {
    // 52c: the plan call alone, the spend said, the project written beside
    // the video as `planned` -- and the same command without --dry-run
    // renders exactly that plan, never planning again.
    Run run;
    run.options.dry_run = true;
    VideoComposeResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.calls == std::vector<std::string>{"plan on m"});
    CHECK(result.document["status"] == "planned");
    CHECK(result.document["scenes"].size() == 3);
    CHECK(result.document["scenes"][0]["voice"].is_null());
    CHECK(result.document["scenes"][0]["clip"].is_null());
    CHECK_FALSE(result.document["scenes"][0].contains("from"));
    CHECK(nlohmann::json::parse(slurp(run.sidecar())) == result.document);
    CHECK(std::ranges::find(run.said, "plan: \"Tides\" -- 3 scenes, about 11.0 s of video") !=
          run.said.end());
    CHECK(std::ranges::any_of(run.said, [](const std::string& line) {
        return line.starts_with("spend: 3 narration runs");
    }));
    CHECK(result.lines ==
          std::vector<std::string>{"a dry run: one planning call to m, nothing spoken or rendered",
                                   "the plan, beside it: " + run.sidecar().string() +
                                       " -- the same command without --dry-run renders it"});
    CHECK_FALSE(std::filesystem::exists(run.out_dir / "tides.mp4"));
    CHECK_FALSE(std::filesystem::exists(run.out_dir / "tides.scenes"));

    // On paper again: the kept plan, no second call.
    run.calls.clear();
    result = run.go();
    REQUIRE(result.exit_code == 0);
    CHECK(run.calls.empty());
    CHECK(result.document["status"] == "planned");

    // Without the flag: that plan, its seeds held, rendered whole.
    run.options.dry_run = false;
    run.said.clear();
    run.answer.reset();  // a second plan call would be refused
    result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    const std::string clip = "org--clip/pipeline/aaaaaaaaaaaa";
    CHECK(std::ranges::none_of(run.calls,
                               [](const std::string& call) { return call.starts_with("plan"); }));
    CHECK(std::ranges::find(run.calls, "render s01.mp4 seed 7 on " + clip) != run.calls.end());
    CHECK(std::ranges::find(run.calls, "render s03.mp4 seed 9 on " + clip) != run.calls.end());
    CHECK(std::ranges::find(run.said, "rendering the plan a dry run kept at " +
                                          run.sidecar().string()) != run.said.end());
    CHECK(result.document["status"] == "done");
}

TEST_CASE("an edit's dry run names each scene's source and what changed",
          "[cli][video][compose][edit][dry-run]") {
    Run run;
    compose_parent(run);
    run.options.edit = "make the second scene about the moon";
    run.answer = edited_plan();
    run.options.dry_run = true;
    const VideoComposeResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    const nlohmann::json& scenes = result.document["scenes"];
    CHECK(std::ranges::find(run.said, "  2. scene 2; its prompt changed") != run.said.end());
    CHECK(scenes[1]["from"] == 2);
    CHECK(scenes[1]["changes"] == nlohmann::json{"prompt"});
    CHECK(scenes[1]["moved"] == false);
    CHECK(scenes[0]["changes"].empty());
    CHECK(result.document["removed"].empty());
}

// ---- 55c: a resumed composition keeps its word ------------------------------

namespace {

/// A finished parent, then an edit of it that failed at scene 2's clip --
/// the kept edit project at `edited.mp4.json`, scenes 1 and 3 carried.
void kept_edit(Run& run) {
    compose_parent(run);
    run.options.edit = "make the second scene about the moon";
    run.answer = edited_plan();
    run.render_error = "the driver said no";
    REQUIRE(run.go().exit_code == 1);
    run.render_error.clear();
    run.calls.clear();
    run.said.clear();
}

/// The editor's answer over the kept edit: its three scenes, scene 3's
/// visual changed.
[[nodiscard]] nlohmann::json storm_plan() {
    nlohmann::json out = edited_plan();
    out["scenes"][2]["visual"] = "a storm at sea";
    return out;
}

[[nodiscard]] std::filesystem::path edited_sidecar(const Run& run) {
    return run.out_dir / "edited.mp4.json";
}

}  // namespace

TEST_CASE("a kept edit given a new --edit runs it over the kept project, dry or not",
          "[cli][video][compose][edit][resume]") {
    Run run;
    kept_edit(run);
    const std::string kept = slurp(edited_sidecar(run));

    // The audit's probe: a new instruction on paper over the kept edit. It
    // is planned -- one edit call -- said, and nothing is written.
    run.options.edit = "make the third scene a storm";
    run.answer = storm_plan();
    run.options.dry_run = true;
    VideoComposeResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.calls == std::vector<std::string>{"plan on m"});
    CHECK(run.said == std::vector<std::string>{
                          "editing the project kept at " + edited_sidecar(run).string() +
                              " with m: \"make the third scene a storm\"",
                          "the edit: 3 scenes, were 3", "  1. scene 1, untouched",
                          "  2. scene 2; its prompt changed", "  3. scene 3; its prompt changed"});
    CHECK(result.lines == std::vector<std::string>{"a dry run: nothing was rendered or written"});
    CHECK(result.document["status"] == "planned");
    CHECK(result.document["scenes"][2]["visual"] == "a storm at sea");
    CHECK(slurp(edited_sidecar(run)) == kept);

    // Rendered: the kept plan is the edit's parent, so what it kept stays
    // -- scene 1's clip carried in place -- scene 2 (never made) and the
    // storm render, and the document still reads against the project's
    // parent, both edits in its chain.
    run.options.dry_run = false;
    run.calls.clear();
    run.said.clear();
    result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    const std::string clip = "org--clip/pipeline/aaaaaaaaaaaa";
    CHECK(run.calls == std::vector<std::string>{"plan on m", "render s02.mp4 seed 8 on " + clip,
                                                "render s03.mp4 seed 9 on " + clip, "join", "mix",
                                                "burn"});
    const nlohmann::json& document = result.document;
    CHECK(document["status"] == "done");
    CHECK(document["parent"] == run.sidecar().string());
    REQUIRE(document["edits"].size() == 2);
    CHECK(document["edits"][0]["instruction"] == "make the second scene about the moon");
    CHECK(document["edits"][1] == nlohmann::json{{"instruction", "make the third scene a storm"},
                                                 {"redo", nlohmann::json::array()},
                                                 {"from", run.sidecar().string()}});
    CHECK(document["scenes"][0]["changes"].empty());
    CHECK(document["scenes"][1]["changes"] == nlohmann::json{"prompt"});
    CHECK(document["scenes"][2]["changes"] == nlohmann::json{"prompt"});
    const std::filesystem::path scenes = run.out_dir / "edited.scenes";
    CHECK(slurp(scenes / "s01.mp4") == "MP4 seed 7");
    CHECK(slurp(scenes / "s03.mp4") == "MP4 seed 9");
    CHECK(document["scenes"][0]["clip"]["file"] == (scenes / "s01.mp4").string());
    // Nothing is left of the directory the files went through.
    std::size_t beside = 0;
    for (const auto& entry : std::filesystem::directory_iterator{run.out_dir}) {
        beside += entry.path().filename().string().starts_with("edited.scenes.") ? 1U : 0U;
    }
    CHECK(beside == 0);
    CHECK(nlohmann::json::parse(slurp(edited_sidecar(run))) == document);
}

TEST_CASE("a kept edit given --redo-scene re-rolls it over the kept project, or is refused",
          "[cli][video][compose][edit][resume]") {
    Run run;
    kept_edit(run);
    const std::string kept = slurp(edited_sidecar(run));

    // A scene the kept project does not have: refused by name, nothing
    // changed.
    run.options.edit.clear();
    run.options.redo = {7};
    VideoComposeResult result = run.go();
    CHECK(result.exit_code == 1);
    CHECK(result.error == "--redo-scene 7: the project has 3 scenes");
    CHECK(run.calls.empty());
    CHECK(slurp(edited_sidecar(run)) == kept);

    // Scene 1 re-rolled over the kept edit: no edit call, scene 1 rendered
    // on its new seed, scene 2 as it never was made, scene 3 carried.
    run.options.redo = {1};
    run.next_seed = 10;
    run.calls.clear();
    run.said.clear();
    result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    const std::string clip = "org--clip/pipeline/aaaaaaaaaaaa";
    CHECK(run.calls == std::vector<std::string>{"render s01.mp4 seed 10 on " + clip,
                                                "render s02.mp4 seed 8 on " + clip, "join", "mix",
                                                "burn"});
    CHECK(run.said.front() == "editing the project kept at " + edited_sidecar(run).string());
    CHECK(std::ranges::find(run.said, "  1. scene 1; a new seed") != run.said.end());
    CHECK(std::ranges::find(run.said, "  2. scene 2; its prompt changed") != run.said.end());
    REQUIRE(result.document["edits"].size() == 2);
    CHECK(result.document["edits"][1]["instruction"].is_null());
    CHECK(result.document["edits"][1]["redo"] == nlohmann::json::array({1}));
    CHECK(result.document["scenes"][0]["changes"] == nlohmann::json{"seed"});
    CHECK(slurp(run.out_dir / "edited.scenes" / "s03.mp4") == "MP4 seed 9");
}

TEST_CASE("a kept edit whose scenes record no source is refused an edit by name",
          "[cli][video][compose][edit][resume]") {
    // An edit project from before 52c: its scenes say nothing of where they
    // came from, so a new edit could not be read against the parent.
    Run run;
    kept_edit(run);
    nlohmann::json older = nlohmann::json::parse(slurp(edited_sidecar(run)));
    for (nlohmann::json& scene : older["scenes"]) {
        scene.erase("from");
        scene.erase("changes");
        scene.erase("moved");
    }
    older.erase("removed");
    std::ofstream{edited_sidecar(run)} << older.dump(2) << "\n";
    const std::string kept = slurp(edited_sidecar(run));
    run.options.edit = "make the third scene a storm";
    const VideoComposeResult result = run.go();
    CHECK(result.exit_code == 1);
    CHECK(result.error == edited_sidecar(run).string() +
                              " was edited by a build that did not record where its scenes came "
                              "from, so --edit and --redo-scene cannot run over it -- finish it "
                              "with the command that made it, or name another -o");
    CHECK(run.calls.empty());
    CHECK(slurp(edited_sidecar(run)) == kept);
}

TEST_CASE("a kept edit given the edit it holds resumes it, its flags never said ignored",
          "[cli][video][compose][edit][resume]") {
    // The same command again is a resume, not a second edit: no edit call.
    Run run;
    kept_edit(run);
    run.options.planner = "m";
    const VideoComposeResult result = run.go();
    INFO(result.error);
    REQUIRE(result.exit_code == 0);
    CHECK(run.calls ==
          std::vector<std::string>{"render s02.mp4 seed 8 on org--clip/pipeline/aaaaaaaaaaaa",
                                   "join", "mix", "burn"});
    CHECK(run.said.front().starts_with("resuming " + edited_sidecar(run).string()));
    CHECK(std::ranges::none_of(run.said, [](const std::string& line) {
        return line.find("not applied") != std::string::npos;
    }));
}

TEST_CASE("a resumed project says a flag not applied only where it differs from the project",
          "[cli][video][compose][resume]") {
    Run run;
    apogee::testing::write_pipeline_model(run.roots.models / "org--other" / "pipeline" /
                                          "dddddddddddd");
    run.render_error = "the driver said no";
    REQUIRE(run.go().exit_code == 1);
    run.render_error.clear();
    const auto said_unapplied = [&run](const std::function<void(VideoComposeOptions&)>& set) {
        const VideoComposeOptions kept = run.options;
        set(run.options);
        std::filesystem::remove(run.out_dir / "tides.mp4");
        run.said.clear();
        const VideoComposeResult result = run.go();
        run.options = kept;
        INFO(result.error);
        CHECK(result.exit_code == 0);
        std::string line;
        for (const std::string& said : run.said) {
            line = said.find("not applied") != std::string::npos ? said : line;
        }
        return line;
    };
    // The project's own pipeline, by its name or its handle, its voice
    // model and its voice: nothing to say.
    CHECK(said_unapplied([](VideoComposeOptions& o) {
              o.voice = "af_heart";
              o.voice_model = "hexgrad--Kokoro-82M";
          }).empty());
    CHECK(said_unapplied([](VideoComposeOptions& o) {
              o.model = "org--clip/pipeline/aaaaaaaaaaaa";
          }).empty());
    // Another pipeline: kept, and said.
    CHECK(said_unapplied([](VideoComposeOptions& o) { o.model = "org--other"; }) ==
          "a resumed project keeps its own settings: --model not applied");
}

TEST_CASE("rendering a kept plan never writes over an .srt beside the video",
          "[cli][video][compose][dry-run]") {
    // The plan on paper; then a file of the user's where unburnable
    // captions would be kept; then the plan rendered -- refused before
    // anything runs, the file untouched.
    Run run;
    run.can_burn = false;
    run.options.dry_run = true;
    REQUIRE(run.go().exit_code == 0);
    std::ofstream{run.out_dir / "tides.srt"} << "mine";
    run.options.dry_run = false;
    run.calls.clear();
    const VideoComposeResult refused = run.go();
    CHECK(refused.exit_code == 1);
    CHECK(refused.error == (run.out_dir / "tides.srt").string() +
                               " exists -- this ffmpeg cannot burn the captions, so they would "
                               "be kept there; name another -o");
    CHECK(run.calls.empty());
    CHECK(slurp(run.out_dir / "tides.srt") == "mine");
    CHECK(nlohmann::json::parse(slurp(run.sidecar()))["status"] == "planned");

    // A failed run resumed is held to it too.
    Run failed;
    failed.can_burn = false;
    failed.render_error = "the driver said no";
    REQUIRE(failed.go().exit_code == 1);
    failed.render_error.clear();
    std::ofstream{failed.out_dir / "tides.srt"} << "mine";
    failed.calls.clear();
    CHECK(failed.go().exit_code == 1);
    CHECK(failed.calls.empty());
    CHECK(slurp(failed.out_dir / "tides.srt") == "mine");

    // The project's own captions -- its record names them -- are its to
    // write again: a finished video gone, the same command remakes it.
    Run done;
    done.can_burn = false;
    REQUIRE(done.go().exit_code == 0);
    REQUIRE(nlohmann::json::parse(slurp(done.sidecar()))["captions"]["file"] ==
            (done.out_dir / "tides.srt").string());
    std::filesystem::remove(done.out_dir / "tides.mp4");
    const VideoComposeResult again = done.go();
    INFO(again.error);
    CHECK(again.exit_code == 0);
    CHECK(std::filesystem::exists(done.out_dir / "tides.mp4"));
}

TEST_CASE("a dry run over a kept plan says it reads the plan, never that it renders",
          "[cli][video][compose][dry-run]") {
    Run run;
    run.options.dry_run = true;
    REQUIRE(run.go().exit_code == 0);
    run.said.clear();
    const VideoComposeResult result = run.go();
    REQUIRE(result.exit_code == 0);
    CHECK(run.said ==
          std::vector<std::string>{
              "reading the plan a dry run kept at " + run.sidecar().string(),
              "plan: \"Tides\" -- 3 scenes, about 11.0 s of video", "  1. (4.0 s) \"Line 1.\"",
              "     shows: shot 1", "  2. (4.0 s) \"Line 2.\"", "     shows: shot 2",
              "  3. (4.0 s) \"Line 3.\"", "     shows: shot 3",
              "spend: 3 narration runs (faster than real time), then 3 clip runs at 1280x704, "
              "50 steps -- each takes minutes on Apple silicon, less on a CUDA GPU",
              "captions: burned into the frames"});
    CHECK(std::ranges::none_of(run.said, [](const std::string& line) {
        return line.find("rendering") != std::string::npos;
    }));
}

TEST_CASE("two voice models in the store are refused naming compose's own flag",
          "[cli][video][compose]") {
    // M29: compose's flag is --voice-model; --model names its pipeline.
    Run run;
    apogee::testing::write_voice_model(run.roots.models / "other--V" / "voice" / "cccccccccccc");
    const VideoComposeResult result = run.go();
    CHECK(result.exit_code == 1);
    CHECK(result.document.is_null());
    CHECK(result.error ==
          "the store holds 2 voice models -- name one with --voice-model:\n"
          "  hexgrad--Kokoro-82M/voice/bbbbbbbbbbbb\n  other--V/voice/cccccccccccc");
    CHECK(run.calls.empty());
}

TEST_CASE("a runtime not installed ends a streamed compose on its error, its words on stderr too",
          "[cli][video][compose][stream]") {
    // M29: the stream carried its `job` line alone, the words on stderr.
    const Run run;
    const std::filesystem::path interpreter =
        apogee::training::PythonEnv{run.home.home() / "training" / "venv"}.interpreter();
    std::filesystem::create_directories(interpreter.parent_path());
    std::ofstream{interpreter} << "#!fake";
    std::string out;
    std::string err;
    CHECK(run.home.run({"video", "compose", "a minute on tides", "-o", run.options.output.string(),
                        "--model", "org--clip", "--output-format", "stream-json"},
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
        "torch and diffusers are not installed in Apogee's Python environment -- apogee train "
        "setup --with video";
    CHECK(events.back().at("message") == refusal);
    CHECK(err == "apogee video compose: " + refusal + "\n");
    CHECK_FALSE(std::filesystem::exists(run.options.output));
}
