#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "cli/video_generate.h"
#include "cli/voice_say.h"
#include "contracts/cancellation.h"
#include "contracts/config.h"
#include "contracts/video_project.h"
#include "modelstore/store.h"
#include "video/compose.h"
#include "video/runtime.h"

/// `apogee video compose` composed (50c): the brief and the files checked,
/// the pipeline, the voice model and its voice resolved, both runtimes made
/// ready, the scenes planned by one structured member call and said with
/// their spend before anything renders, then the project rendered by the
/// composer core -- every clip through `generate_clip`, every narration
/// through `say_text`, each keeping its own record -- and its record, the
/// `video.project` document, kept beside the video from the first step.
///
/// The same command again resumes: a project beside the output for the same
/// brief renders only what it does not hold, with its own settings. An edit
/// project given an `--edit` or `--redo-scene` it does not hold last runs
/// that edit over itself, never drops it (55c).
namespace apogee::commands {

/// What `video compose` was asked for.
struct VideoComposeOptions {
    std::string brief;
    std::filesystem::path output{};
    /// `--music`: a bed under the narration; empty for none.
    std::filesystem::path music{};
    /// `--assets`: the user's stills and clips, offered to the plan by name.
    std::filesystem::path assets{};
    /// `--no-captions` sets it false.
    bool captions = true;
    /// `--model` (the pipeline), `--voice-model`, `--voice`, `--planner`.
    std::string model{};
    std::string voice_model{};
    std::string voice{};
    std::string planner{};
    /// `--from` (50d): a composed video, or its project, to edit -- its
    /// brief and settings taken, the flags above changing them.
    std::filesystem::path from{};
    /// `--edit`: the instruction one structured pass rewrites the plan by.
    std::string edit{};
    /// `--redo-scene`: scenes (from 1) whose seed is re-rolled.
    std::vector<std::size_t> redo{};
    /// `--dry-run`: the edited project and its diff, nothing rendered.
    bool dry_run = false;
};

/// What a composition needs from outside.
struct VideoComposeSeams {
    /// Each runtime made ready for what was resolved -- in the command, the
    /// consent to install its set first.
    std::function<video::VideoReadiness(const ResolvedPipeline&)> ready_video;
    std::function<video::VideoReadiness(const ResolvedVoiceModel&)> ready_voice;
    /// The structured member call on `backend`.
    std::function<video::PlanAnswer(const std::string& backend, const std::string& system,
                                    const std::string& user, const std::string& schema)>
        plan;
    /// One scene's narration and clip -- in the command, `say_text` and
    /// `generate_clip` over their own seams.
    std::function<video::SpokenScene(const ResolvedVoiceModel& model, const std::string& voice,
                                     const std::string& text, const std::filesystem::path& wav)>
        speak;
    std::function<video::RenderedClip(const ResolvedPipeline& pipeline, const video::ClipAsk& ask,
                                      const std::filesystem::path& mp4)>
        render;
    /// The seam's probe and assembly (its `speak`, `render`, `say` and
    /// `save` are this command's own).
    video::ComposeSeams assembly{};
    /// Whether this ffmpeg can burn captions.
    std::function<bool()> can_burn{};
    std::function<std::int64_t()> random_seed{};
    std::function<void(const std::string&)> say{};
    /// Each scene begun, as the composer says it (52b).
    std::function<void(std::size_t scene, std::size_t scenes, std::string_view state)> on_scene{};
    std::string platform{};
};

struct VideoComposeResult {
    int exit_code = 0;
    /// The `video.project` document -- null when refused before a plan.
    nlohmann::json document;
    std::vector<std::string> lines;
    std::string error;
};

/// One `video compose`, composed: refused before anything runs on an empty
/// or overlong brief, an output that is not a new `.mp4` in a directory
/// that exists, a missing bed or assets directory, no pipeline or voice
/// model, an unknown voice, a project beside the output for another brief,
/// a ladder that refuses, or no backend to plan with -- or, on every path
/// that renders, captions it cannot burn whose `.srt` beside the output is
/// already there (55c); else planned (or resumed, or edited over the kept
/// project), the plan said, and rendered.
[[nodiscard]] VideoComposeResult compose_video(const harness::Config& config,
                                               const models::StoreRoots& roots,
                                               const VideoComposeOptions& options,
                                               const VideoComposeSeams& seams,
                                               const harness::CancellationToken& cancellation);

/// The plan as it is said before anything renders: the title and length,
/// each scene, then the spend -- the runs it takes at what size -- and what
/// becomes of the captions.
[[nodiscard]] std::vector<std::string> plan_lines(const harness::VideoProject& project,
                                                  bool can_burn);

/// A narration run's outcome, from `say_text`'s result and its document.
[[nodiscard]] video::SpokenScene spoken_from(const VoiceSayResult& result);

/// A clip run's outcome, from `generate_clip`'s result.
[[nodiscard]] video::RenderedClip rendered_from(const VideoGenerateResult& result);

}  // namespace apogee::commands
