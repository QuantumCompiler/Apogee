#include "video/compose.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "agentloop/structured.h"
#include "contracts/captions.h"
#include "contracts/video_project.h"
#include "contracts/video_run.h"
#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/fake_ffmpeg.h"

/// The composer's core (50c), over closures and no process: the plan held
/// to a schema with the caps in it, a scene's length stretched to hold its
/// narration, captions and narration timed from measured lengths, and the
/// walk -- every narration once, then every clip once, an asset slotted, a
/// resume filling only gaps, a stop keeping what finished -- then the
/// assembly handed exactly the scenes' files, pinned argv-exact through the
/// fake ffmpeg.
namespace {

using apogee::harness::ProjectScene;
using apogee::harness::VideoProject;
using apogee::video::ClipAsk;
using apogee::video::ComposeOutcome;
using apogee::video::ComposeSeams;
using apogee::video::RenderedClip;
using apogee::video::SpokenScene;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool near(double a, double b) {
    return std::abs(a - b) < 1e-9;
}

/// A sample plan's answer: `count` scenes.
[[nodiscard]] nlohmann::json plan_answer(std::size_t count, const std::string& asset = {}) {
    nlohmann::json scenes = nlohmann::json::array();
    for (std::size_t index = 0; index < count; ++index) {
        nlohmann::json scene = {{"narration", "Line " + std::to_string(index + 1) + "."},
                                {"visual", "a shot"},
                                {"seconds", 4}};
        if (!asset.empty() && index == 0) {
            scene["asset"] = asset;
        }
        scenes.push_back(scene);
    }
    return {{"title", "Tides"}, {"scenes", scenes}};
}

/// A composition's fixture: a project of `count` planned scenes in a temp
/// directory, and seams that write files and remember every call.
struct Compose {
    apogee::testing::TempDir dir{"compose-" + std::to_string(std::random_device{}())};
    VideoProject project;
    /// The narration each scene's text runs to; 2 s when not said.
    std::map<std::string, double> spoken_seconds;
    /// What each file probes to, by name; a rendered clip, as asked.
    std::map<std::string, double> probed;
    std::vector<std::string> calls;
    std::vector<ClipAsk> asks;
    std::vector<std::filesystem::path> joined;
    apogee::platform::AudioMix mixed;
    std::filesystem::path burned_srt;
    std::vector<std::string> saved;
    std::size_t cancel_at_speech = 0;
    std::string render_error;
    bool can_burn = true;

    explicit Compose(std::size_t count = 3) {
        project.status = "rendering";
        project.brief = "tides";
        project.title = "Tides";
        project.output = dir.path() / "tides.mp4";
        project.scenes_dir = apogee::harness::video_project_scenes_dir(project.output);
        project.crossfade = 0.5;
        project.music = dir.path() / "bed.wav";
        for (std::size_t index = 0; index < count; ++index) {
            ProjectScene scene;
            scene.narration = "Line " + std::to_string(index + 1) + ". Then more.";
            scene.visual = "shot " + std::to_string(index + 1);
            scene.seconds = 4.0;
            scene.seed = 100 + static_cast<std::int64_t>(index);
            project.scenes.push_back(scene);
        }
    }

    [[nodiscard]] ComposeSeams seams() {
        ComposeSeams seams;
        seams.speak = [this](const std::string& text, const std::filesystem::path& wav) {
            calls.push_back("speak " + wav.filename().string());
            SpokenScene out;
            // A leftover the document did not record is gone before it runs.
            CHECK_FALSE(std::filesystem::exists(wav));
            if (cancel_at_speech > 0 && calls.size() == cancel_at_speech) {
                out.cancelled = true;
                return out;
            }
            std::ofstream{wav} << "RIFF";
            out.ok = true;
            out.seconds = spoken_seconds.contains(text) ? spoken_seconds.at(text) : 2.0;
            out.sentences = {
                {.text = text.substr(0, text.find('.') + 1),
                 .seconds = out.seconds * 0.4,
                 .offset = 0.0},
                {.text = "Then more.", .seconds = out.seconds * 0.6, .offset = out.seconds * 0.4}};
            return out;
        };
        seams.render = [this](const ClipAsk& ask, const std::filesystem::path& mp4) {
            calls.push_back("render " + mp4.filename().string());
            asks.push_back(ask);
            RenderedClip out;
            if (!render_error.empty()) {
                out.error = render_error;
                return out;
            }
            std::ofstream{mp4} << "MP4";
            probed[mp4.filename().string()] = ask.seconds + 0.04;
            out.ok = true;
            return out;
        };
        seams.probe = [this](const std::filesystem::path& file,
                             const apogee::platform::FfmpegLimits&,
                             std::string& error) -> std::optional<apogee::platform::MediaInfo> {
            const auto found = probed.find(file.filename().string());
            if (found == probed.end()) {
                error = "no such file";
                return std::nullopt;
            }
            apogee::platform::MediaInfo info;
            info.video = true;
            info.duration = found->second;
            return info;
        };
        seams.join = [this](const std::vector<std::filesystem::path>& clips, double crossfade,
                            const std::filesystem::path& out, const apogee::platform::FfmpegLimits&,
                            std::string&) -> std::optional<apogee::platform::Assembled> {
            calls.push_back("join");
            joined = clips;
            CHECK(near(crossfade, 0.5));
            std::ofstream{out} << "joined";
            return apogee::platform::Assembled{.codec = "h264", .settings = "joined words"};
        };
        seams.mix = [this](const std::filesystem::path& video,
                           const apogee::platform::AudioMix& mix, const std::filesystem::path& out,
                           const apogee::platform::FfmpegLimits&,
                           std::string&) -> std::optional<apogee::platform::Assembled> {
            calls.push_back("mix");
            CHECK(slurp(video) == "joined");
            mixed = mix;
            std::ofstream{out} << "mixed";
            return apogee::platform::Assembled{.codec = "copy", .settings = "mixed words"};
        };
        seams.burn = [this](const std::filesystem::path& video, const std::filesystem::path& srt,
                            const std::filesystem::path& out, const apogee::platform::FfmpegLimits&,
                            std::string&) -> std::optional<apogee::platform::Assembled> {
            calls.push_back("burn");
            CHECK(slurp(video) == "mixed");
            burned_srt = srt;
            std::ofstream{out} << "captioned";
            return apogee::platform::Assembled{.codec = "h264", .settings = "burned words"};
        };
        seams.can_burn = can_burn;
        seams.save = [this](const VideoProject& state) { saved.push_back(state.status); };
        return seams;
    }

    ComposeOutcome go(const apogee::harness::CancellationToken& token = {}) {
        return apogee::video::render_project(project, seams(), token);
    }
};

}  // namespace

TEST_CASE("the plan's schema holds the caps, and names only assets the directory has",
          "[video][compose][plan]") {
    using apogee::agentloop::validate_against;
    const nlohmann::json bare = nlohmann::json::parse(apogee::video::plan_schema({}));
    REQUIRE(apogee::agentloop::validate_schema(bare).ok);
    CHECK(validate_against(bare, plan_answer(3)).ok);
    CHECK_FALSE(validate_against(bare, plan_answer(apogee::video::kMaxComposeScenes + 1)).ok);
    CHECK_FALSE(validate_against(bare, nlohmann::json{{"title", "T"}, {"scenes", {}}}).ok);
    nlohmann::json long_scene = plan_answer(1);
    long_scene["scenes"][0]["seconds"] = 11;
    CHECK_FALSE(validate_against(bare, long_scene).ok);
    nlohmann::json wordy = plan_answer(1);
    wordy["scenes"][0]["narration"] = std::string(apogee::video::kMaxNarrationCharacters + 1, 'a');
    CHECK_FALSE(validate_against(bare, wordy).ok);
    // Without assets a scene names none.
    CHECK_FALSE(validate_against(bare, plan_answer(1, "beach.png")).ok);

    const nlohmann::json offered =
        nlohmann::json::parse(apogee::video::plan_schema({"beach.png", "moon.mp4"}));
    REQUIRE(apogee::agentloop::validate_schema(offered).ok);
    CHECK(validate_against(offered, plan_answer(2, "beach.png")).ok);
    CHECK(validate_against(offered, plan_answer(2)).ok);
    nlohmann::json none = plan_answer(1);
    none["scenes"][0]["asset"] = nullptr;
    CHECK(validate_against(offered, none).ok);
    CHECK_FALSE(validate_against(offered, plan_answer(2, "forest.png")).ok);

    // The text lists each scene's properties in the order a held answer
    // writes them: the narration, then what it shows, then how long.
    const std::string text = apogee::video::plan_schema({"beach.png"});
    CHECK(text.find("\"narration\"") < text.find("\"visual\""));
    CHECK(text.find("\"visual\"") < text.find("\"seconds\""));
    CHECK(text.find("\"seconds\"") < text.find("\"asset\""));
}

TEST_CASE("the plan is one call, its answer read or refused with the caps quoted",
          "[video][compose][plan]") {
    std::string asked_user;
    std::string asked_schema;
    const auto planned = apogee::video::plan_scenes(
        "a minute on tides", {"beach.png"},
        [&](const std::string& system, const std::string& user, const std::string& schema) {
            CHECK(system.find("narration") != std::string::npos);
            asked_user = user;
            asked_schema = schema;
            return apogee::video::PlanAnswer{
                .json = plan_answer(2, "beach.png"), .conforms = true, .attempts = 1};
        });
    REQUIRE(planned.error.empty());
    CHECK(asked_user ==
          "Brief: a minute on tides\n\nAt most 8 scenes, each 2 to 10 seconds.\nThe user's "
          "files: beach.png");
    CHECK(asked_schema == apogee::video::plan_schema({"beach.png"}));
    CHECK(planned.title == "Tides");
    REQUIRE(planned.scenes.size() == 2);
    CHECK(planned.scenes[0].asset == "beach.png");
    CHECK(planned.scenes[1].asset.empty());
    CHECK(planned.scenes[1].narration == "Line 2.");
    CHECK(planned.scenes[1].seconds == 4.0);

    const auto refused = apogee::video::plan_scenes(
        "tides", {}, [](const std::string&, const std::string&, const std::string&) {
            return apogee::video::PlanAnswer{.json = plan_answer(9),
                                             .conforms = false,
                                             .attempts = 2,
                                             .error = "/scenes: too many items"};
        });
    CHECK(refused.scenes.empty());
    CHECK(refused.error ==
          "the plan did not hold to its shape after its one correction: "
          "/scenes: too many items -- the caps: at most 8 scenes of 2-10 s, "
          "each narration at most 240 characters");
}

TEST_CASE("a directory offers its stills and clips by name", "[video][compose]") {
    const apogee::testing::TempDir dir{"compose-assets-" + std::to_string(std::random_device{}())};
    for (const char* name : {"b.mp4", "a.PNG", "notes.txt", "c.mov", "d.webp"}) {
        std::ofstream{dir.path() / name} << "x";
    }
    std::filesystem::create_directories(dir.path() / "sub.mp4");
    CHECK(apogee::video::list_assets(dir.path()) ==
          std::vector<std::string>{"a.PNG", "b.mp4", "c.mov", "d.webp"});
    CHECK(apogee::video::list_assets(dir.path() / "missing").empty());
    CHECK(apogee::video::is_still("a.PNG"));
    CHECK(apogee::video::is_still("x.jpeg"));
    CHECK_FALSE(apogee::video::is_still("b.mp4"));
}

TEST_CASE("a scene runs as long as its plan, or as its narration needs, within the caps",
          "[video][compose]") {
    using apogee::video::scene_length;
    // The lead, the speech, the room after it and the next fade.
    CHECK(near(apogee::video::narration_needs(2.0, 0.5), 3.0));
    CHECK(near(*scene_length(5.0, 2.0, 0.5), 5.0));  // the plan is long enough
    CHECK(near(*scene_length(3.0, 4.0, 0.5), 5.0));  // stretched to hold it
    CHECK(near(*scene_length(2.0, 0.5, 0.5), 2.0));
    CHECK(near(*scene_length(1.0, 0.2, 0.0), 2.0));  // never under the shortest
    CHECK(near(*scene_length(5.0, 9.0, 0.5), 10.0));
    CHECK_FALSE(scene_length(5.0, 9.1, 0.5).has_value());  // no scene holds it
}

TEST_CASE("captions and narration are timed from the measured lengths, never guessed",
          "[video][compose]") {
    VideoProject project;
    project.crossfade = 0.5;
    const auto scene = [](double clip, std::vector<apogee::harness::ProjectSentence> sentences) {
        ProjectScene out;
        out.narration = "n";
        out.visual = "v";
        out.voice = "s.wav";
        out.clip_seconds = clip;
        out.sentences = std::move(sentences);
        return out;
    };
    project.scenes = {scene(5.04, {{.text = "One.", .seconds = 1.5, .offset = 0.0}}),
                      scene(4.0, {{.text = "Two.", .seconds = 1.0, .offset = 0.0},
                                  {.text = "Three.", .seconds = 1.25, .offset = 1.0}}),
                      scene(6.0, {{.text = "Four.", .seconds = 2.0, .offset = 0.0}})};
    const std::vector<double> starts = apogee::video::scene_starts(project);
    REQUIRE(starts.size() == 3);
    CHECK(near(starts[0], 0.0));
    CHECK(near(starts[1], 4.54));
    CHECK(near(starts[2], 8.04));

    const std::vector<apogee::harness::CaptionCue> cues = apogee::video::caption_cues(project);
    REQUIRE(cues.size() == 4);
    CHECK(cues[0].text == "One.");
    CHECK(near(cues[0].start, 0.25));
    CHECK(near(cues[0].end, 1.75));
    CHECK(near(cues[2].start, 4.54 + 0.25 + 1.0));
    CHECK(near(cues[2].end, 4.54 + 0.25 + 2.25));
    CHECK(near(cues[3].start, 8.29));
    CHECK(apogee::harness::write_srt(cues).starts_with(
        "1\n00:00:00,250 --> 00:00:01,750\nOne.\n\n2\n00:00:04,790 --> 00:00:05,790\nTwo.\n\n"));

    const std::vector<apogee::platform::NarrationPiece> pieces =
        apogee::video::narration_pieces(project);
    REQUIRE(pieces.size() == 3);
    CHECK(near(pieces[1].offset, 4.79));
    CHECK(near(pieces[2].offset, 8.29));
}

TEST_CASE("a composition speaks every scene, then renders every clip, then assembles once",
          "[video][compose][render]") {
    Compose run;
    run.spoken_seconds["Line 2. Then more."] = 4.0;  // needs 5 s: the scene stretches
    const ComposeOutcome outcome = run.go();
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    CHECK(run.calls == std::vector<std::string>{"speak s01.wav", "speak s02.wav", "speak s03.wav",
                                                "render s01.mp4", "render s02.mp4",
                                                "render s03.mp4", "join", "mix", "burn"});
    REQUIRE(run.asks.size() == 3);
    CHECK(run.asks[0].scene == 1);
    CHECK(run.asks[0].prompt == "shot 1");
    CHECK(run.asks[0].seed == 100);
    CHECK(near(run.asks[0].seconds, 4.0));
    CHECK(near(run.asks[1].seconds, 5.0));
    CHECK(run.asks[0].image.empty());

    // The assembly is handed exactly the scenes' files.
    const std::filesystem::path scenes = run.project.scenes_dir;
    CHECK(run.joined == std::vector<std::filesystem::path>{scenes / "s01.mp4", scenes / "s02.mp4",
                                                           scenes / "s03.mp4"});
    REQUIRE(run.mixed.narration.size() == 3);
    CHECK(run.mixed.narration[1].file == scenes / "s02.wav");
    CHECK(near(run.mixed.narration[1].offset, 4.04 - 0.5 + 0.25));
    CHECK(run.mixed.bed == run.project.music);
    CHECK(run.burned_srt == scenes / "captions.srt");
    CHECK(slurp(scenes / "captions.srt") ==
          apogee::harness::write_srt(apogee::video::caption_cues(run.project)));

    // The output touched once, by the rename; nothing half-made left.
    CHECK(slurp(run.project.output) == "captioned");
    CHECK_FALSE(std::filesystem::exists(scenes / ".joined.mp4"));
    CHECK_FALSE(std::filesystem::exists(scenes / ".mixed.mp4"));
    CHECK(run.project.status == "done");
    CHECK(run.project.captions_burned);
    CHECK(run.project.captions_file == scenes / "captions.srt");
    CHECK(run.project.assembly ==
          "joined: joined words; mixed: mixed words; captioned: burned words");
    CHECK(run.project.scenes[1].clip == scenes / "s02.mp4");
    CHECK(near(run.project.scenes[1].clip_seconds, 5.04));
    CHECK(run.project.scenes[2].sentences.size() == 2);
    // Saved at every step: begun, three narrations, three clips, done.
    CHECK(run.saved.size() == 8);
    CHECK(run.saved.back() == "done");
}

TEST_CASE("a resumed composition renders only what its document does not hold",
          "[video][compose][render]") {
    Compose run;
    std::filesystem::create_directories(run.project.scenes_dir);
    const std::filesystem::path scenes = run.project.scenes_dir;
    // Scene 1 whole; scene 2 spoken, its clip recorded but gone; scene 3 not
    // begun -- with a stopped run's leftover WAV it never recorded.
    for (const char* name : {"s01.wav", "s01.mp4", "s02.wav", "s03.wav"}) {
        std::ofstream{scenes / name} << "old";
    }
    run.project.scenes[0].voice = scenes / "s01.wav";
    run.project.scenes[0].voice_seconds = 2.0;
    run.project.scenes[0].clip = scenes / "s01.mp4";
    run.project.scenes[0].clip_seconds = 4.04;
    run.project.scenes[1].voice = scenes / "s02.wav";
    run.project.scenes[1].voice_seconds = 2.0;
    run.project.scenes[1].clip = scenes / "s02.mp4";
    run.project.scenes[1].clip_seconds = 4.04;
    run.probed["s01.mp4"] = 4.04;
    const ComposeOutcome outcome = run.go();
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    CHECK(run.calls == std::vector<std::string>{"speak s03.wav", "render s02.mp4", "render s03.mp4",
                                                "join", "mix", "burn"});
    CHECK(run.asks[0].seed == 101);  // the seed held
    CHECK(slurp(scenes / "s01.mp4") == "old");
}

TEST_CASE("a clip shorter than its narration now needs renders again, its seed held",
          "[video][compose][render]") {
    Compose run{1};
    std::filesystem::create_directories(run.project.scenes_dir);
    const std::filesystem::path scenes = run.project.scenes_dir;
    std::ofstream{scenes / "s01.wav"} << "old";
    std::ofstream{scenes / "s01.mp4"} << "old";
    run.project.scenes[0].voice = scenes / "s01.wav";
    run.project.scenes[0].voice_seconds = 5.0;  // needs 6 s
    run.project.scenes[0].clip = scenes / "s01.mp4";
    run.project.scenes[0].clip_seconds = 4.04;
    std::vector<std::string> said;
    ComposeSeams seams = run.seams();
    seams.say = [&said](const std::string& line) { said.push_back(line); };
    REQUIRE(apogee::video::render_project(run.project, seams, {}).ok);
    CHECK(run.calls.front() == "render s01.mp4");
    CHECK(near(run.asks.front().seconds, 6.0));
    CHECK(run.asks.front().seed == 100);
    CHECK(std::ranges::find(said,
                            "scene 1/1: its clip runs 4.04 s, shorter than its narration "
                            "now needs (6.00 s) -- rendering it again, its seed held") !=
          said.end());
}

TEST_CASE("a narration no scene can hold stops the run before any clip renders",
          "[video][compose][render]") {
    Compose run;
    run.spoken_seconds["Line 2. Then more."] = 9.5;
    const ComposeOutcome outcome = run.go();
    CHECK_FALSE(outcome.ok);
    CHECK_FALSE(outcome.cancelled);
    CHECK(outcome.error ==
          "scene 2/3's narration runs 9.50 s, and a scene holds 10.00 s with its "
          "lead, the room after it and the next fade -- shorten it");
    CHECK(run.calls == std::vector<std::string>{"speak s01.wav", "speak s02.wav"});
    CHECK(run.project.status == "failed");
    CHECK(run.project.error == outcome.error);
    CHECK(run.saved.back() == "failed");
    CHECK_FALSE(std::filesystem::exists(run.project.output));
}

TEST_CASE("a stopped or failed composition keeps every finished scene",
          "[video][compose][render]") {
    SECTION("stopped mid-narration") {
        Compose run;
        run.cancel_at_speech = 2;
        const ComposeOutcome outcome = run.go();
        CHECK(outcome.cancelled);
        CHECK(run.project.status == "cancelled");
        CHECK(std::filesystem::exists(run.project.scenes[0].voice));
        CHECK(run.project.scenes[1].voice.empty());
    }
    SECTION("stopped before a clip") {
        Compose run;
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        ComposeSeams seams = run.seams();
        const auto speak = seams.speak;
        seams.speak = [&](const std::string& text, const std::filesystem::path& wav) {
            SpokenScene out = speak(text, wav);
            if (wav.filename() == "s03.wav") {
                token.cancel();
            }
            return out;
        };
        const ComposeOutcome outcome = apogee::video::render_project(run.project, seams, token);
        CHECK(outcome.cancelled);
        CHECK(run.asks.empty());
        CHECK(run.project.status == "cancelled");
        CHECK(std::ranges::all_of(run.project.scenes,
                                  [](const ProjectScene& scene) { return !scene.voice.empty(); }));
    }
    SECTION("a clip that fails") {
        Compose run;
        run.render_error = "the driver said no";
        const ComposeOutcome outcome = run.go();
        CHECK_FALSE(outcome.cancelled);
        CHECK(outcome.error == "scene 1/3's clip: the driver said no");
        CHECK(run.project.status == "failed");
    }
}

TEST_CASE("an asset clip is taken as it is, and a still starts its scene's clip",
          "[video][compose][render]") {
    Compose run;
    run.project.assets = run.dir.path() / "assets";
    std::filesystem::create_directories(run.project.assets);
    std::ofstream{run.project.assets / "moon.mp4"} << "MOON";
    std::ofstream{run.project.assets / "beach.png"} << "PNG";
    run.project.scenes[0].asset = "moon.mp4";
    run.project.scenes[2].asset = "beach.png";
    run.probed["moon.mp4"] = 6.0;
    run.probed["s01.mp4"] = 6.0;
    REQUIRE(run.go().ok);
    CHECK(run.calls == std::vector<std::string>{"speak s01.wav", "speak s02.wav", "speak s03.wav",
                                                "render s02.mp4", "render s03.mp4", "join", "mix",
                                                "burn"});
    CHECK(slurp(run.project.scenes_dir / "s01.mp4") == "MOON");
    CHECK(run.asks[1].image == run.project.assets / "beach.png");
    CHECK(run.asks[0].image.empty());

    // One too short for its narration is refused before anything renders.
    Compose short_run;
    short_run.project.assets = run.project.assets;
    short_run.project.scenes[1].asset = "moon.mp4";
    short_run.probed["moon.mp4"] = 1.0;
    const ComposeOutcome outcome = short_run.go();
    CHECK(outcome.error ==
          "scene 2/3's asset moon.mp4 runs 1.00 s, shorter than its narration "
          "needs (3.00 s)");
    CHECK(short_run.asks.empty());
}

TEST_CASE("captions this ffmpeg cannot burn are kept beside the video; off, none are made",
          "[video][compose][render]") {
    SECTION("kept beside") {
        Compose run;
        run.can_burn = false;
        REQUIRE(run.go().ok);
        CHECK(std::ranges::find(run.calls, "burn") == run.calls.end());
        CHECK(slurp(run.project.output) == "mixed");
        const std::filesystem::path beside = run.dir.path() / "tides.srt";
        CHECK(run.project.captions_file == beside);
        CHECK_FALSE(run.project.captions_burned);
        CHECK(slurp(beside) == slurp(run.project.scenes_dir / "captions.srt"));
    }
    SECTION("off") {
        Compose run;
        run.project.captions = false;
        REQUIRE(run.go().ok);
        CHECK(std::ranges::find(run.calls, "burn") == run.calls.end());
        CHECK(run.project.captions_file.empty());
        CHECK_FALSE(std::filesystem::exists(run.project.scenes_dir / "captions.srt"));
    }
}

TEST_CASE("the assembly runs on the seam, handed the scenes' files and their offsets",
          "[video][compose][render]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const apogee::testing::FakeFfmpeg fake;
    Compose run{2};
    using apogee::testing::FakeFfmpeg;
    fake.probe_as("s01.mp4", FakeFfmpeg::probe_text("video", "4.04"));
    fake.probe_as("s02.mp4", FakeFfmpeg::probe_text("video", "4.04"));
    fake.probe_as("s01.wav", FakeFfmpeg::probe_text("audio", "2"));
    fake.probe_as("s02.wav", FakeFfmpeg::probe_text("audio", "2"));
    fake.probe_as("bed.wav", FakeFfmpeg::probe_text("audio", "30"));
    ComposeSeams seams = run.seams();
    seams.probe = apogee::platform::probe_media;
    seams.join = apogee::platform::concat_clips;
    seams.mix = apogee::platform::mix_audio;
    seams.burn = apogee::platform::burn_captions;
    const ComposeOutcome outcome = apogee::video::render_project(run.project, seams, {});
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    const std::filesystem::path scenes = run.project.scenes_dir;
    const std::string argv = fake.logged();
    CHECK(argv.find("-i " + (scenes / "s01.mp4").string() + " -i " + (scenes / "s02.mp4").string() +
                    " -filter_complex ") != std::string::npos);
    CHECK(argv.find("xfade=transition=fade:duration=0.500:offset=3.540[vout]") !=
          std::string::npos);
    CHECK(argv.find("-i " + (scenes / "s01.wav").string() + " -i " + (scenes / "s02.wav").string() +
                    " -stream_loop -1 -i " + run.project.music.string()) != std::string::npos);
    CHECK(argv.find("adelay=250|250[n0]") != std::string::npos);
    CHECK(argv.find("adelay=3790|3790[n1]") != std::string::npos);
    CHECK(argv.find("subtitles=filename=" +
                    apogee::platform::filter_path(scenes / "captions.srt")) != std::string::npos);
    CHECK(slurp(run.project.output) == "MP4 libx264");
    CHECK(run.project.assembly.starts_with("joined: the final profile: h264 at crf 20"));
}

namespace {

/// A finished parent of three scenes, its files in its own scenes directory.
struct Parent {
    apogee::testing::TempDir dir{"compose-parent-" + std::to_string(std::random_device{}())};
    VideoProject project;

    Parent() {
        project.status = "done";
        project.brief = "tides";
        project.title = "Tides";
        project.output = dir.path() / "tides.mp4";
        project.scenes_dir = apogee::harness::video_project_scenes_dir(project.output);
        project.crossfade = 0.5;
        std::filesystem::create_directories(project.scenes_dir);
        for (std::size_t index = 0; index < 3; ++index) {
            const std::string n = std::to_string(index + 1);
            ProjectScene scene;
            scene.narration = "Line " + n + ".";
            scene.visual = "shot " + n;
            scene.seconds = 4.0;
            scene.seed = 100 + static_cast<std::int64_t>(index);
            scene.voice = project.scenes_dir / ("s0" + n + ".wav");
            scene.voice_seconds = 2.0;
            scene.sentences = {{.text = scene.narration, .seconds = 2.0, .offset = 0.0}};
            scene.clip = project.scenes_dir / ("s0" + n + ".mp4");
            scene.clip_seconds = 4.04;
            std::ofstream{scene.voice} << "WAV " << n;
            std::ofstream{apogee::harness::video_sidecar_path(scene.voice)} << "{\"voice\": " << n
                                                                            << "}";
            std::ofstream{scene.clip} << "MP4 " << n;
            std::ofstream{apogee::harness::video_sidecar_path(scene.clip)} << "{\"clip\": " << n
                                                                           << "}";
            project.scenes.push_back(scene);
        }
    }

    /// The plan the editor answers: each scene `{from, narration, visual,
    /// seconds}`, a null `from` written as 0.
    [[nodiscard]] static nlohmann::json answer(
        const std::vector<std::tuple<int, std::string, std::string, double>>& scenes) {
        nlohmann::json out = {{"title", "Tides"}, {"scenes", nlohmann::json::array()}};
        for (const auto& [from, narration, visual, seconds] : scenes) {
            out["scenes"].push_back(
                {{"from", from == 0 ? nlohmann::json(nullptr) : nlohmann::json(from)},
                 {"narration", narration},
                 {"visual", visual},
                 {"seconds", seconds}});
        }
        return out;
    }

    [[nodiscard]] apogee::video::EditedScenes edit(const nlohmann::json& json) const {
        std::int64_t next = 900;
        return apogee::video::edit_scenes(
            project, "the instruction", {},
            [&json](const std::string&, const std::string&, const std::string&) {
                return apogee::video::PlanAnswer{.json = json, .conforms = true, .attempts = 1};
            },
            [&next] { return next++; });
    }
};

using Edit = std::vector<std::tuple<int, std::string, std::string, double>>;

}  // namespace

TEST_CASE("the edit's schema names where each scene came from", "[video][compose][edit]") {
    using apogee::agentloop::validate_against;
    const nlohmann::json schema = nlohmann::json::parse(apogee::video::edit_schema({}, 3));
    REQUIRE(apogee::agentloop::validate_schema(schema).ok);
    CHECK(validate_against(schema, Parent::answer({{1, "a.", "b", 4}, {0, "c.", "d", 4}})).ok);
    CHECK_FALSE(validate_against(schema, Parent::answer({{4, "a.", "b", 4}})).ok);
    CHECK_FALSE(validate_against(schema, plan_answer(1)).ok);  // no `from`
    const std::string text = apogee::video::edit_schema({}, 3);
    CHECK(text.find("\"from\"") < text.find("\"narration\""));
}

TEST_CASE("an edit's diff says what became of each scene, the seeds held",
          "[video][compose][edit]") {
    const Parent parent;
    using apogee::video::diff_lines;
    SECTION("nothing changed") {
        const auto edited = parent.edit(Parent::answer({{1, "Line 1.", "shot 1", 4},
                                                        {2, "Line 2.", "shot 2", 4},
                                                        {3, "Line 3.", "shot 3", 4}}));
        CHECK(diff_lines(edited, 3) ==
              std::vector<std::string>{"the edit: 3 scenes, were 3", "  1. scene 1, untouched",
                                       "  2. scene 2, untouched", "  3. scene 3, untouched"});
        CHECK(std::ranges::all_of(edited.changes,
                                  [](const auto& change) { return change.untouched(); }));
    }
    SECTION("a scene removed") {
        const auto edited =
            parent.edit(Parent::answer({{1, "Line 1.", "shot 1", 4}, {3, "Line 3.", "shot 3", 4}}));
        CHECK(diff_lines(edited, 3) ==
              std::vector<std::string>{"the edit: 2 scenes, were 3", "  1. scene 1, untouched",
                                       "  2. scene 3, untouched", "  scene 2 removed"});
        CHECK(edited.scenes[1].seed == 102);
    }
    SECTION("an intro added") {
        const auto edited = parent.edit(Parent::answer({{0, "Welcome.", "a title card", 3},
                                                        {1, "Line 1.", "shot 1", 4},
                                                        {2, "Line 2.", "shot 2", 4},
                                                        {3, "Line 3.", "shot 3", 4}}));
        CHECK(diff_lines(edited, 3) ==
              std::vector<std::string>{"the edit: 4 scenes, were 3", "  1. new",
                                       "  2. scene 1, untouched", "  3. scene 2, untouched",
                                       "  4. scene 3, untouched"});
        CHECK(edited.scenes[0].seed == 900);  // drawn
        CHECK(edited.scenes[1].seed == 100);  // held
    }
    SECTION("scenes reordered") {
        const auto edited = parent.edit(Parent::answer({{3, "Line 3.", "shot 3", 4},
                                                        {1, "Line 1.", "shot 1", 4},
                                                        {2, "Line 2.", "shot 2", 4}}));
        CHECK(diff_lines(edited, 3) ==
              std::vector<std::string>{"the edit: 3 scenes, were 3", "  1. scene 3, moved",
                                       "  2. scene 1, untouched", "  3. scene 2, untouched"});
    }
    SECTION("a narration, and a prompt and a length, changed") {
        const auto edited = parent.edit(Parent::answer({{1, "Line 1.", "shot 1", 4},
                                                        {2, "A new line.", "shot 2", 4},
                                                        {3, "Line 3.", "the moon", 6}}));
        CHECK(diff_lines(edited, 3) ==
              std::vector<std::string>{"the edit: 3 scenes, were 3", "  1. scene 1, untouched",
                                       "  2. scene 2; its narration changed",
                                       "  3. scene 3; its prompt changed, its length changed"});
        CHECK(edited.scenes[2].seed == 102);
    }
    SECTION("a scene re-rolled") {
        auto edited = apogee::video::same_scenes(parent.project);
        CHECK(apogee::video::redo_scene(edited, 4, 5) ==
              "--redo-scene 4: the project has 3 scenes");
        CHECK(apogee::video::redo_scene(edited, 0, 5) ==
              "--redo-scene 0: the project has 3 scenes");
        REQUIRE(apogee::video::redo_scene(edited, 2, 555).empty());
        CHECK(edited.scenes[1].seed == 555);
        CHECK(diff_lines(edited, 3)[2] == "  2. scene 2; a new seed");
    }
}

TEST_CASE("an edit that does not hold changes nothing, saying so", "[video][compose][edit]") {
    const Parent parent;
    const auto edited = apogee::video::edit_scenes(
        parent.project, "make it longer", {},
        [](const std::string&, const std::string& user, const std::string&) {
            // The editor is shown the plan as it stands, numbered.
            CHECK(user.find("\"from\": 2") != std::string::npos);
            CHECK(user.find("The instruction: make it longer") != std::string::npos);
            return apogee::video::PlanAnswer{.json = plan_answer(2),
                                             .conforms = false,
                                             .attempts = 2,
                                             .error = "/scenes/0: required property 'from'"};
        },
        [] { return 1; });
    CHECK(edited.scenes.empty());
    CHECK(edited.error ==
          "the edit did not hold to its shape after its one correction: "
          "/scenes/0: required property 'from' -- the caps: at most 8 scenes of "
          "2-10 s, each narration at most 240 characters; nothing was changed");
}

TEST_CASE("an edit carries over byte for byte what it did not change, and renders the rest",
          "[video][compose][edit]") {
    const Parent parent;
    // An intro added; scene 1 after it; scene 2's narration changed; scene
    // 3's prompt changed.
    auto edited = parent.edit(Parent::answer({{0, "Welcome.", "a title card", 3},
                                              {1, "Line 1.", "shot 1", 4},
                                              {2, "A new line.", "shot 2", 4},
                                              {3, "Line 3.", "the moon", 4}}));
    Compose child{0};
    const std::filesystem::path scenes = child.project.scenes_dir;
    REQUIRE(apogee::video::carry_scenes(parent.project, edited, scenes, true, true).empty());
    // Scene 1, now the second: both files, and their records.
    CHECK(slurp(scenes / "s02.wav") == "WAV 1");
    CHECK(slurp(scenes / "s02.mp4") == "MP4 1");
    CHECK(slurp(scenes / "s02.mp4.json") == "{\"clip\": 1}");
    CHECK(edited.scenes[1].clip_seconds == 4.04);
    // Scene 2's clip, not its narration; scene 3's narration, not its clip.
    CHECK(slurp(scenes / "s03.mp4") == "MP4 2");
    CHECK_FALSE(std::filesystem::exists(scenes / "s03.wav"));
    CHECK(slurp(scenes / "s04.wav") == "WAV 3");
    CHECK_FALSE(std::filesystem::exists(scenes / "s04.mp4"));
    CHECK(edited.scenes[0].voice.empty());

    // Rendered: exactly what was not carried.
    child.project.scenes = edited.scenes;
    child.probed["s02.mp4"] = 4.04;
    child.probed["s03.mp4"] = 4.04;
    REQUIRE(child.go().ok);
    CHECK(child.calls == std::vector<std::string>{"speak s01.wav", "speak s03.wav",
                                                  "render s01.mp4", "render s04.mp4", "join", "mix",
                                                  "burn"});
    CHECK(child.asks[1].seed == 102);  // scene 3's seed held through its new prompt
    CHECK(slurp(scenes / "s02.mp4") == "MP4 1");

    // Another voice carries no narration; another pipeline no clip.
    auto again = parent.edit(Parent::answer(
        {{1, "Line 1.", "shot 1", 4}, {2, "Line 2.", "shot 2", 4}, {3, "Line 3.", "shot 3", 4}}));
    const apogee::testing::TempDir other{"compose-other-" + std::to_string(std::random_device{}())};
    REQUIRE(apogee::video::carry_scenes(parent.project, again, other.path(), false, true).empty());
    CHECK(std::ranges::all_of(again.scenes, [](const ProjectScene& s) { return s.voice.empty(); }));
    CHECK(std::ranges::none_of(again.scenes, [](const ProjectScene& s) { return s.clip.empty(); }));
    auto third = parent.edit(Parent::answer(
        {{1, "Line 1.", "shot 1", 4}, {2, "Line 2.", "shot 2", 4}, {3, "Line 3.", "shot 3", 4}}));
    const apogee::testing::TempDir another{"compose-another-" +
                                           std::to_string(std::random_device{}())};
    REQUIRE(
        apogee::video::carry_scenes(parent.project, third, another.path(), true, false).empty());
    CHECK(std::ranges::all_of(third.scenes, [](const ProjectScene& s) { return s.clip.empty(); }));
}
