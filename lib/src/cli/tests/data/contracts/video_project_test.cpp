#include "contracts/video_project.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

/// The composer's project document (50c): written, read back whole, and
/// refused when it is not one this build reads.
namespace {

using apogee::harness::ProjectScene;
using apogee::harness::VideoProject;

[[nodiscard]] VideoProject sample() {
    VideoProject project;
    project.status = "rendering";
    project.brief = "a minute on tides";
    project.title = "Tides";
    project.output = std::filesystem::path{"/videos"} / "tides.mp4";
    project.scenes_dir = std::filesystem::path{"/videos"} / "tides.scenes";
    project.pipeline = "org--clip/pipeline/aaaaaaaaaaaa";
    project.voice_model = "hexgrad--Kokoro-82M/voice/bbbbbbbbbbbb";
    project.voice = "af_heart";
    project.crossfade = 0.5;
    project.music = std::filesystem::path{"/music"} / "bed.wav";
    project.planner = "local";
    project.plan_attempts = 2;
    ProjectScene first;
    first.narration = "The sea breathes twice a day.";
    first.visual = "a wide shore at dawn, waves rolling in";
    first.seconds = 5.0;
    first.seed = 42;
    first.voice = project.scenes_dir / "s01.wav";
    first.voice_seconds = 2.4;
    first.sentences = {{.text = "The sea breathes twice a day.", .seconds = 2.4, .offset = 0.0}};
    first.clip = project.scenes_dir / "s01.mp4";
    first.clip_seconds = 5.04;
    ProjectScene second;
    second.narration = "The moon pulls.";
    second.visual = "the moon over water";
    second.seconds = 3.0;
    second.asset = "moon.mp4";
    second.seed = 7;
    project.scenes = {first, second};
    project.created = "2026-10-19T12:00:00Z";
    project.platform = "macos-arm64";
    return project;
}

}  // namespace

TEST_CASE("a project document is written whole and read back the same",
          "[contracts][video][compose]") {
    const VideoProject project = sample();
    const nlohmann::json document = apogee::harness::video_project_document(project);
    CHECK(document["object"] == "video.project");
    CHECK(document["version"] == 1);
    CHECK(document["settings"]["music"] == project.music.string());
    CHECK(document["settings"]["assets"].is_null());
    CHECK(document["plan"]["attempts"] == 2);
    CHECK(document["scenes"][0]["index"] == 1);
    CHECK(document["scenes"][0]["voice"]["sentences"][0]["seconds"] == 2.4);
    CHECK(document["scenes"][1]["asset"] == "moon.mp4");
    CHECK(document["scenes"][1]["voice"].is_null());
    CHECK(document["scenes"][1]["clip"].is_null());
    CHECK(document["captions"].is_null());
    CHECK(document["error"].is_null());

    std::string error;
    const std::optional<VideoProject> read =
        apogee::harness::read_video_project(nlohmann::json::parse(document.dump()), error);
    REQUIRE(read.has_value());
    // Read back, it writes the same document.
    CHECK(apogee::harness::video_project_document(*read) == document);
    CHECK(read->scenes[0].clip == project.scenes[0].clip);
    CHECK(read->scenes[0].sentences.front().text == "The sea breathes twice a day.");
    CHECK(read->scenes[1].seed == 7);
    CHECK(read->captions);
}

TEST_CASE("a document that is not a project this build reads is refused, saying why",
          "[contracts][video][compose]") {
    const nlohmann::json good = apogee::harness::video_project_document(sample());
    const auto refused = [](const nlohmann::json& document) {
        std::string error;
        CHECK_FALSE(apogee::harness::read_video_project(document, error).has_value());
        return error;
    };
    CHECK(refused(nlohmann::json::array()) == "it is not a video.project document");
    nlohmann::json other = good;
    other["object"] = "video.generation";
    CHECK(refused(other) == "it is not a video.project document");
    nlohmann::json later = good;
    later["version"] = 2;
    CHECK(refused(later) == "its version is not one this build reads");
    nlohmann::json empty = good;
    empty["scenes"] = nlohmann::json::array();
    CHECK(refused(empty) == "it holds no scenes");
    nlohmann::json briefless = good;
    briefless["brief"] = "";
    CHECK(refused(briefless) == "it names no brief, output or scenes directory");
    nlohmann::json blind = good;
    blind["scenes"][0]["visual"] = "";
    CHECK(refused(blind) == "a scene has no narration or no visual");
}

TEST_CASE("a project's scenes are kept beside its video", "[contracts][video][compose]") {
    CHECK(apogee::harness::video_project_scenes_dir(std::filesystem::path{"out"} / "tides.mp4") ==
          std::filesystem::path{"out"} / "tides.scenes");
}
