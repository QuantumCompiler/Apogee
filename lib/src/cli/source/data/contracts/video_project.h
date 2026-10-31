#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// A composed video's project (50c): the brief, the plan's scenes and what
/// was rendered for each -- the one state a composition keeps. It is the
/// `video.project` document, written beside the output as its sidecar
/// (`video_sidecar_path`), 49's record law scaled up: each scene's clip and
/// narration keep their own records beside them, and this names them. Read
/// back, it is what a run resumes from -- only a scene whose file is missing
/// renders again -- and what an edit (50d) starts from.
namespace apogee::harness {

inline constexpr std::string_view kVideoProjectObject = "video.project";
inline constexpr int kVideoProjectVersion = 1;

/// One sentence of a scene's narration: its text, its seconds as the driver
/// counted them, and where it starts in the scene's WAV.
struct ProjectSentence {
    std::string text;
    double seconds = 0.0;
    double offset = 0.0;
};

/// One scene: what the plan said, the seed its clip holds, and -- once
/// rendered -- its narration and its clip, each with the length measured
/// from the file.
struct ProjectScene {
    std::string narration;
    std::string visual;
    /// The plan's length; the clip may run longer to hold its narration.
    double seconds = 0.0;
    /// A file name from `--assets` the scene takes instead of a generation
    /// (a clip) or starts from (a still); empty for none.
    std::string asset{};
    std::int64_t seed = 0;
    std::filesystem::path voice{};
    double voice_seconds = 0.0;
    std::vector<ProjectSentence> sentences{};
    std::filesystem::path clip{};
    double clip_seconds = 0.0;
    /// An edited project's scene (52c), read off the edit's own record
    /// comparison: the parent's scene it came from, from 1, or 0 for a new
    /// one -- nullopt in a project no edit made.
    std::optional<std::size_t> from{};
    /// Which of its records differ from that scene's, in the diff's order:
    /// `narration`, `prompt`, `duration`, `asset`, `seed`. Empty when
    /// untouched, or new.
    std::vector<std::string> changes{};
    /// It left the parent's order.
    bool moved = false;
};

/// One edit in a project's chain (50d): the instruction (empty for none),
/// the scenes re-rolled, and the project it was made from.
struct ProjectEdit {
    std::string instruction{};
    std::vector<std::size_t> redo{};
    std::filesystem::path from{};
};

struct VideoProject {
    /// `rendering`, `done`, `failed` or `cancelled` -- or `planned`, for a
    /// dry run's project, which nothing has rendered yet.
    std::string status;
    std::string brief;
    std::string title{};
    /// The video, absolute; and the directory its scenes are kept in.
    std::filesystem::path output;
    std::filesystem::path scenes_dir;
    /// The pipeline and the voice model, as their store handles; the voice.
    std::string pipeline{};
    std::string voice_model{};
    std::string voice{};
    double crossfade = 0.0;
    bool captions = true;
    /// `--music` and `--assets`, absolute; empty for none.
    std::filesystem::path music{};
    std::filesystem::path assets{};
    /// The backend that planned, and the turns it took (1, or 2 after the
    /// one correction).
    std::string planner{};
    int plan_attempts = 0;
    std::vector<ProjectScene> scenes;
    /// The captions' SRT, and whether they were burned into the frames --
    /// false when kept beside the video instead.
    std::filesystem::path captions_file{};
    bool captions_burned = false;
    /// What the assembly wrote, in words.
    std::string assembly{};
    std::string error{};
    std::string created{};
    std::string platform{};
    /// The project this one was edited from (50d), empty for an original;
    /// and every edit from the original brief to this one, in order.
    std::filesystem::path parent{};
    std::vector<ProjectEdit> edits{};
    /// An edited project's: the parent's scenes the edit left out, from 1
    /// (52c).
    std::vector<std::size_t> removed{};
};

/// The `video.project` document.
[[nodiscard]] nlohmann::json video_project_document(const VideoProject& project);

/// `document` read back; nullopt with `error` filled when it is not a
/// project this build reads.
[[nodiscard]] std::optional<VideoProject> read_video_project(const nlohmann::json& document,
                                                             std::string& error);

/// Where a project's scenes are kept: `<stem>.scenes/` beside `output`.
[[nodiscard]] std::filesystem::path video_project_scenes_dir(const std::filesystem::path& output);

}  // namespace apogee::harness
