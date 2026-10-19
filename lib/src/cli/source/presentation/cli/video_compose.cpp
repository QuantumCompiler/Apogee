#include "cli/video_compose.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

#include "contracts/config_edit.h"
#include "contracts/video_run.h"
#include "harness/roles.h"
#include "modelstore/sidecar.h"

namespace apogee::commands {
namespace {

/// `seconds` for a line: `3.4 s`.
[[nodiscard]] std::string seconds_words(double seconds) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.1f s", seconds);
    return buffer.data();
}

/// `text` trimmed of whitespace at both ends.
[[nodiscard]] std::string trimmed(std::string_view text) {
    const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && space(text.back())) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

/// `text` cut to `most` bytes at a character's start, an ellipsis said.
[[nodiscard]] std::string shortened(const std::string& text, std::size_t most) {
    if (text.size() <= most) {
        return text;
    }
    std::size_t cut = most;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return text.substr(0, cut) + "...";
}

/// What the video runs to: every clip, less a crossfade at each boundary.
[[nodiscard]] double video_seconds(const harness::VideoProject& project) {
    double total = 0.0;
    for (const harness::ProjectScene& scene : project.scenes) {
        total += scene.clip_seconds > 0.0 ? scene.clip_seconds : scene.seconds;
    }
    const std::size_t count = project.scenes.size();
    return count > 1 ? total - project.crossfade * static_cast<double>(count - 1) : total;
}

/// `output` with `.srt` for its extension: where unburned captions are kept.
[[nodiscard]] std::filesystem::path captions_beside(const std::filesystem::path& output) {
    std::filesystem::path out = output;
    out.replace_extension(".srt");
    return out;
}

/// What a resumed project's flags would have changed, said: a project keeps
/// its own settings.
[[nodiscard]] std::vector<std::string> ignored_flags(const VideoComposeOptions& options) {
    std::vector<std::string> out;
    if (!options.model.empty()) {
        out.emplace_back("--model");
    }
    if (!options.voice_model.empty()) {
        out.emplace_back("--voice-model");
    }
    if (!options.voice.empty()) {
        out.emplace_back("--voice");
    }
    if (!options.planner.empty()) {
        out.emplace_back("--planner");
    }
    if (!options.music.empty()) {
        out.emplace_back("--music");
    }
    if (!options.assets.empty()) {
        out.emplace_back("--assets");
    }
    if (!options.captions) {
        out.emplace_back("--no-captions");
    }
    return out;
}

}  // namespace

std::vector<std::string> plan_lines(const harness::VideoProject& project, bool can_burn) {
    std::vector<std::string> out;
    const std::size_t count = project.scenes.size();
    out.push_back("plan: \"" + project.title + "\" -- " + std::to_string(count) + " scene" +
                  (count == 1 ? "" : "s") + ", about " + seconds_words(video_seconds(project)) +
                  " of video");
    std::size_t generated = 0;
    std::size_t taken = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const harness::ProjectScene& scene = project.scenes[index];
        std::string head = "  " + std::to_string(index + 1) + ". (" + seconds_words(scene.seconds) +
                           ") \"" + scene.narration + "\"";
        out.push_back(head);
        if (!scene.asset.empty() && !video::is_still(scene.asset)) {
            out.push_back("     takes " + scene.asset + " as it is");
            ++taken;
        } else {
            out.push_back("     shows: " + shortened(scene.visual, 120) +
                          (scene.asset.empty() ? std::string{} : " (from " + scene.asset + ")"));
            ++generated;
        }
    }
    std::string spend = "spend: " + std::to_string(count) + " narration run" +
                        (count == 1 ? "" : "s") + " (faster than real time)";
    if (generated > 0) {
        spend += ", then " + std::to_string(generated) + " clip run" + (generated == 1 ? "" : "s") +
                 " at " + std::to_string(kDefaultVideoWidth) + "x" +
                 std::to_string(kDefaultVideoHeight) + ", " + std::to_string(kDefaultVideoSteps) +
                 " steps -- each takes minutes on Apple silicon, less on a CUDA GPU";
    }
    if (taken > 0) {
        spend += "; " + std::to_string(taken) + " asset" + (taken == 1 ? "" : "s") + " taken as " +
                 (taken == 1 ? "it is" : "they are");
    }
    out.push_back(spend);
    if (!project.captions) {
        out.emplace_back("captions: off");
    } else if (can_burn) {
        out.emplace_back("captions: burned into the frames");
    } else {
        out.push_back("captions: kept beside the video as " +
                      captions_beside(project.output).filename().string() +
                      " (this ffmpeg has no libass to burn them)");
    }
    return out;
}

video::SpokenScene spoken_from(const VoiceSayResult& result) {
    video::SpokenScene out;
    out.cancelled = result.exit_code == 130;
    if (result.exit_code != 0 || result.document.is_null() ||
        result.document.value("status", std::string{}) != "done") {
        out.error =
            result.error.empty() ? std::string{"the narration was not written"} : result.error;
        return out;
    }
    out.ok = true;
    out.seconds = result.document.value("seconds", 0.0);
    // Each sentence's offset is the document's own: from the file's start.
    for (const nlohmann::json& segment :
         result.document.value("segments", nlohmann::json::array())) {
        for (const nlohmann::json& sentence : segment.value("sentences", nlohmann::json::array())) {
            out.sentences.push_back({.text = sentence.value("text", std::string{}),
                                     .seconds = sentence.value("seconds", 0.0),
                                     .offset = sentence.value("offset", 0.0)});
        }
    }
    return out;
}

video::RenderedClip rendered_from(const VideoGenerateResult& result) {
    video::RenderedClip out;
    out.cancelled = result.exit_code == 130;
    out.ok = result.exit_code == 0 && !result.document.is_null() &&
             result.document.value("status", std::string{}) == "done";
    if (!out.ok) {
        out.error = result.error.empty() ? std::string{"the clip was not written"} : result.error;
    }
    return out;
}

VideoComposeResult compose_video(const harness::Config& config, const models::StoreRoots& roots,
                                 const VideoComposeOptions& options, const VideoComposeSeams& seams,
                                 const harness::CancellationToken& cancellation) {
    VideoComposeResult result;
    const auto refuse = [&result](std::string why) {
        result.exit_code = 1;
        result.error = std::move(why);
        return result;
    };
    const auto say = [&seams](const std::string& line) {
        if (seams.say) {
            seams.say(line);
        }
    };
    std::error_code code;

    const std::string brief = trimmed(options.brief);
    if (brief.empty()) {
        return refuse("there is nothing to compose -- give the brief");
    }
    if (brief.size() > video::kMaxBriefCharacters) {
        return refuse("the brief is " + std::to_string(brief.size()) +
                      " characters, over the cap of " + std::to_string(video::kMaxBriefCharacters) +
                      " -- say it shorter");
    }
    if (options.output.empty()) {
        return refuse("--output is required");
    }
    const std::filesystem::path output = std::filesystem::absolute(options.output, code);
    std::string extension = output.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".mp4") {
        return refuse("the video is an mp4 -- name it with .mp4");
    }
    if (std::filesystem::exists(output, code)) {
        return refuse(output.string() +
                      " exists -- a run never writes over a file; name another with -o");
    }
    if (!std::filesystem::is_directory(output.parent_path(), code)) {
        return refuse("no directory " + output.parent_path().string() + " for the video");
    }

    // A project beside the output for the same brief is resumed.
    const std::filesystem::path sidecar = harness::video_sidecar_path(output);
    std::optional<harness::VideoProject> resumed;
    if (std::filesystem::exists(sidecar, code)) {
        std::ifstream in{sidecar, std::ios::binary};
        const std::string text{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
        std::string why;
        try {
            resumed = harness::read_video_project(nlohmann::json::parse(text), why);
        } catch (const std::exception& e) {
            why = e.what();
        }
        if (!resumed.has_value()) {
            return refuse(sidecar.string() +
                          " is there and is not a project this run can resume (" + why +
                          ") -- name another -o");
        }
        if (resumed->brief != brief) {
            return refuse(sidecar.string() +
                          " holds a project for another brief -- name another -o, or move it "
                          "away to start again");
        }
    }

    harness::VideoProject project;
    std::vector<std::string> asset_names;
    if (resumed.has_value()) {
        project = *resumed;
        project.output = output;
    } else {
        project.brief = brief;
        project.output = output;
        project.scenes_dir = harness::video_project_scenes_dir(output);
        project.crossfade = platform::kDefaultCrossfade;
        project.captions = options.captions;
        if (!options.music.empty()) {
            project.music = std::filesystem::absolute(options.music, code);
            if (!std::filesystem::is_regular_file(project.music, code)) {
                return refuse("there is no music at " + project.music.string());
            }
        }
        if (!options.assets.empty()) {
            project.assets = std::filesystem::absolute(options.assets, code);
            if (!std::filesystem::is_directory(project.assets, code)) {
                return refuse("there is no assets directory at " + project.assets.string());
            }
            asset_names = video::list_assets(project.assets);
            if (asset_names.empty()) {
                return refuse(project.assets.string() +
                              " holds no stills or clips (png, jpg, jpeg, webp, mp4, mov, m4v, "
                              "webm)");
            }
        }
    }

    // What runs it: a resumed project's own, else the flags' or the defaults.
    ResolvedPipeline pipeline;
    if (std::string why = resolve_video_pipeline(
            roots, resumed.has_value() ? resumed->pipeline : options.model,
            resumed.has_value() ? std::string{} : config.models.default_video, pipeline);
        !why.empty()) {
        return refuse(why);
    }
    ResolvedVoiceModel voice_model;
    if (std::string why = resolve_voice_model(
            roots, resumed.has_value() ? resumed->voice_model : options.voice_model, voice_model);
        !why.empty()) {
        return refuse(why);
    }
    std::string voice;
    if (std::string why = choose_voice(voice_model.info,
                                       resumed.has_value() ? resumed->voice : options.voice, voice);
        !why.empty()) {
        return refuse(why);
    }
    const bool can_burn = project.captions && (!seams.can_burn || seams.can_burn());
    if (project.captions && !can_burn && !resumed.has_value() &&
        std::filesystem::exists(captions_beside(output), code)) {
        return refuse(captions_beside(output).string() +
                      " exists -- this ffmpeg cannot burn the captions, so they would be kept "
                      "there; name another -o");
    }
    if (const video::VideoReadiness ready = seams.ready_video(pipeline); !ready.ready()) {
        return refuse(ready.message());
    }
    if (const video::VideoReadiness ready = seams.ready_voice(voice_model); !ready.ready()) {
        return refuse(ready.message());
    }

    const auto save = [&sidecar](const harness::VideoProject& state) {
        harness::write_file_atomically(sidecar,
                                       harness::video_project_document(state).dump(2) + "\n");
    };
    if (resumed.has_value()) {
        std::size_t spoken = 0;
        std::size_t rendered = 0;
        for (const harness::ProjectScene& scene : project.scenes) {
            spoken += std::filesystem::is_regular_file(scene.voice, code) ? 1 : 0;
            rendered += std::filesystem::is_regular_file(scene.clip, code) ? 1 : 0;
        }
        say("resuming " + sidecar.string() + ": " + std::to_string(spoken) + " of " +
            std::to_string(project.scenes.size()) + " narrations and " + std::to_string(rendered) +
            " clips already there");
        if (const std::vector<std::string> ignored = ignored_flags(options); !ignored.empty()) {
            std::string named;
            for (const std::string& flag : ignored) {
                named += (named.empty() ? "" : ", ") + flag;
            }
            say("a resumed project keeps its own settings: " + named + " not applied");
        }
    } else {
        const std::string backend = harness::resolve_chat_backend(config, options.planner);
        if (backend.empty()) {
            return refuse(
                "no backend is configured to plan with -- add one with 'apogee config "
                "add-backend', or name one with --planner");
        }
        say("planning the scenes with " + backend);
        const video::PlannedScenes planned = video::plan_scenes(
            brief, asset_names,
            [&](const std::string& system, const std::string& user, const std::string& schema) {
                return seams.plan(backend, system, user, schema);
            });
        if (!planned.error.empty()) {
            return refuse(planned.error);
        }
        project.title = planned.title;
        project.scenes = planned.scenes;
        for (harness::ProjectScene& scene : project.scenes) {
            scene.seed = seams.random_seed ? seams.random_seed() : 0;
        }
        project.pipeline = pipeline.handle;
        project.voice_model = voice_model.handle;
        project.voice = voice;
        project.planner = backend;
        project.plan_attempts = planned.attempts;
        project.status = "rendering";
        project.created = models::now_rfc3339();
        project.platform = seams.platform;
        save(project);
    }
    for (const std::string& line : plan_lines(project, can_burn)) {
        say(line);
    }

    video::ComposeSeams compose = seams.assembly;
    compose.speak = [&](const std::string& text, const std::filesystem::path& wav) {
        return seams.speak(voice_model, voice, text, wav);
    };
    compose.render = [&](const video::ClipAsk& ask, const std::filesystem::path& mp4) {
        return seams.render(pipeline, ask, mp4);
    };
    compose.can_burn = can_burn;
    compose.say = seams.say;
    compose.save = save;
    const video::ComposeOutcome outcome = video::render_project(project, compose, cancellation);
    result.document = harness::video_project_document(project);
    if (!outcome.ok) {
        result.exit_code = outcome.cancelled ? 130 : 1;
        result.error = (outcome.cancelled ? std::string{"cancelled"} : outcome.error) +
                       " -- every finished scene is kept, and the same command resumes";
        return result;
    }
    const std::string captions =
        !project.captions ? std::string{"no captions"}
        : project.captions_burned
            ? std::string{"captions burned in"}
            : "captions kept beside it as " + project.captions_file.filename().string();
    result.lines.push_back(output.string() + ": " + seconds_words(video_seconds(project)) + ", " +
                           std::to_string(project.scenes.size()) + " scenes narrated by " +
                           project.voice + ", " + captions);
    result.lines.push_back("its project, beside it: " + sidecar.string());
    return result;
}

}  // namespace apogee::commands
