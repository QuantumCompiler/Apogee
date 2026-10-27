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

/// Compose's flag for its narration's voice model: `--model` names its
/// pipeline, so a refusal asking for a voice model names this one (M29).
constexpr std::string_view kVoiceModelFlag = "--voice-model";

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
/// its own settings. A flag naming what `kept` already has changes nothing
/// and is not said (55c): `-m` naming the kept plan's own pipeline is
/// applied, not ignored. `--planner` is the edit's own when `plans`.
[[nodiscard]] std::vector<std::string> ignored_flags(const VideoComposeOptions& options,
                                                     const harness::VideoProject& kept,
                                                     const models::StoreRoots& roots, bool plans) {
    std::vector<std::string> out;
    std::error_code code;
    const auto same_path = [&code](const std::filesystem::path& given,
                                   const std::filesystem::path& recorded) {
        return !recorded.empty() && std::filesystem::absolute(given, code).lexically_normal() ==
                                        recorded.lexically_normal();
    };
    if (!options.model.empty()) {
        ResolvedPipeline named;
        if (!resolve_video_pipeline(roots, options.model, {}, named).empty() ||
            named.handle != kept.pipeline) {
            out.emplace_back("--model");
        }
    }
    if (!options.voice_model.empty()) {
        ResolvedVoiceModel named;
        if (!resolve_voice_model(roots, options.voice_model, named, kVoiceModelFlag).empty() ||
            named.handle != kept.voice_model) {
            out.emplace_back(kVoiceModelFlag);
        }
    }
    if (!options.voice.empty() && options.voice != kept.voice) {
        out.emplace_back("--voice");
    }
    if (!options.planner.empty() && !plans && options.planner != kept.planner) {
        out.emplace_back("--planner");
    }
    if (!options.music.empty() && !same_path(options.music, kept.music)) {
        out.emplace_back("--music");
    }
    if (!options.assets.empty() && !same_path(options.assets, kept.assets)) {
        out.emplace_back("--assets");
    }
    if (!options.captions && kept.captions) {
        out.emplace_back("--no-captions");
    }
    return out;
}

/// Whether the flags ask for the edit `kept` holds last -- the same command
/// run again, which resumes it rather than editing it again (55c).
[[nodiscard]] bool asks_kept_edit(const harness::VideoProject& kept,
                                  const VideoComposeOptions& options) {
    return !kept.edits.empty() && kept.edits.back().instruction == options.edit &&
           kept.edits.back().redo == options.redo;
}

/// Where an edit over a kept project carries its files first: beside its
/// scenes, under a name nothing on disk holds yet.
[[nodiscard]] std::filesystem::path carry_dir(const std::filesystem::path& scenes_dir) {
    std::filesystem::path out = scenes_dir;
    out += ".edit-" + models::random_weight_id();
    return out;
}

/// Whether `dir` is `scenes_dir`, or a directory an edit over its project
/// carried into.
[[nodiscard]] bool scenes_or_carry(const std::filesystem::path& dir,
                                   const std::filesystem::path& scenes_dir) {
    return dir == scenes_dir ||
           (dir.parent_path() == scenes_dir.parent_path() &&
            dir.filename().string().starts_with(scenes_dir.filename().string() + ".edit-"));
}

/// `file` in `to` when it is in `from`, else as it is.
[[nodiscard]] std::filesystem::path moved_to(const std::filesystem::path& file,
                                             const std::filesystem::path& from,
                                             const std::filesystem::path& to) {
    return !file.empty() && file.parent_path() == from ? to / file.filename() : file;
}

/// `text` lowercased.
[[nodiscard]] std::string lowered(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// The project at `path`, or nullopt with `why` filled.
[[nodiscard]] std::optional<harness::VideoProject> load_project(const std::filesystem::path& path,
                                                                std::string& why) {
    std::ifstream in{path, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    try {
        return harness::read_video_project(nlohmann::json::parse(text), why);
    } catch (const std::exception& e) {
        why = e.what();
        return std::nullopt;
    }
}

}  // namespace

std::vector<std::string> plan_lines(const harness::VideoProject& project, bool can_burn) {
    std::vector<std::string> out;
    const std::size_t count = project.scenes.size();
    out.push_back("plan: \"" + project.title + "\" -- " + std::to_string(count) + " scene" +
                  (count == 1 ? "" : "s") + ", about " + seconds_words(video_seconds(project)) +
                  " of video");
    // The spend is what will run: a scene already holding its narration or
    // its clip -- resumed, or carried over by an edit -- costs nothing again.
    const auto held = [](const std::filesystem::path& file) {
        std::error_code code;
        return !file.empty() && std::filesystem::is_regular_file(file, code);
    };
    std::size_t spoken = 0;
    std::size_t generated = 0;
    std::size_t taken = 0;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const harness::ProjectScene& scene = project.scenes[index];
        out.push_back("  " + std::to_string(index + 1) + ". (" + seconds_words(scene.seconds) +
                      ") \"" + scene.narration + "\"");
        const bool asset_clip = !scene.asset.empty() && !video::is_still(scene.asset);
        if (asset_clip) {
            out.push_back("     takes " + scene.asset + " as it is");
        } else {
            out.push_back("     shows: " + shortened(scene.visual, 120) +
                          (scene.asset.empty() ? std::string{} : " (from " + scene.asset + ")"));
        }
        spoken += held(scene.voice) ? 0U : 1U;
        if (held(scene.clip)) {
            ++kept;
        } else if (asset_clip) {
            ++taken;
        } else {
            ++generated;
        }
    }
    std::vector<std::string> parts;
    if (spoken > 0) {
        parts.push_back(std::to_string(spoken) + " narration run" + (spoken == 1 ? "" : "s") +
                        " (faster than real time)");
    }
    if (generated > 0) {
        parts.push_back((spoken > 0 ? "then " : "") + std::to_string(generated) + " clip run" +
                        (generated == 1 ? "" : "s") + " at " + std::to_string(kDefaultVideoWidth) +
                        "x" + std::to_string(kDefaultVideoHeight) + ", " +
                        std::to_string(kDefaultVideoSteps) +
                        " steps -- each takes minutes on Apple silicon, less on a CUDA GPU");
    }
    std::string spend = "spend: ";
    for (std::size_t at = 0; at < parts.size(); ++at) {
        spend += (at == 0 ? "" : ", ") + parts[at];
    }
    if (parts.empty()) {
        spend += "no narration or clip runs -- the assembly alone";
    }
    if (taken > 0) {
        spend += "; " + std::to_string(taken) + " asset" + (taken == 1 ? "" : "s") + " taken as " +
                 (taken == 1 ? "it is" : "they are");
    }
    if (kept > 0) {
        spend += "; " + std::to_string(kept) + " clip" + (kept == 1 ? "" : "s") + " already made";
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
    const auto draw = [&seams] { return seams.random_seed ? seams.random_seed() : 0; };
    const auto say_ignored = [&say](const std::vector<std::string>& ignored) {
        if (ignored.empty()) {
            return;
        }
        std::string named;
        for (const std::string& flag : ignored) {
            named += (named.empty() ? "" : ", ") + flag;
        }
        say("a resumed project keeps its own settings: " + named + " not applied");
    };
    std::error_code code;

    const bool editing = !options.from.empty();
    if (!editing && (!options.edit.empty() || !options.redo.empty())) {
        return refuse("--edit and --redo-scene work on a project -- name it with --from");
    }
    std::string brief = trimmed(options.brief);
    if (editing && !brief.empty()) {
        return refuse("--from takes its brief from the project -- give no brief");
    }
    if (!editing && brief.empty()) {
        return refuse("there is nothing to compose -- give the brief, or --from a project");
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
    if (lowered(output.extension().string()) != ".mp4") {
        return refuse("the video is an mp4 -- name it with .mp4");
    }
    if (std::filesystem::exists(output, code)) {
        return refuse(output.string() +
                      " exists -- a run never writes over a file; name another with -o");
    }
    if (!std::filesystem::is_directory(output.parent_path(), code)) {
        return refuse("no directory " + output.parent_path().string() + " for the video");
    }

    // An edit's parent (50d): a composed video, or its project.
    std::optional<harness::VideoProject> parent;
    std::filesystem::path parent_document;
    if (editing) {
        const std::filesystem::path from = std::filesystem::absolute(options.from, code);
        parent_document = lowered(from.extension().string()) == ".json"
                              ? from
                              : harness::video_sidecar_path(from);
        if (!std::filesystem::is_regular_file(parent_document, code)) {
            return refuse("there is no project at " + parent_document.string() +
                          " -- --from takes a composed video or its project");
        }
        std::string why;
        parent = load_project(parent_document, why);
        if (!parent.has_value()) {
            return refuse(parent_document.string() + " is not a project this build reads (" + why +
                          ")");
        }
        brief = parent->brief;
    }

    // A project beside the output -- the same brief, and for an edit the same
    // parent -- is resumed.
    const std::filesystem::path sidecar = harness::video_sidecar_path(output);
    std::optional<harness::VideoProject> resumed;
    if (std::filesystem::exists(sidecar, code)) {
        std::string why;
        resumed = load_project(sidecar, why);
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
        if (editing && resumed->parent.lexically_normal() != parent_document.lexically_normal()) {
            return refuse(sidecar.string() +
                          " holds a project edited from another -- name another -o, or move it "
                          "away to start again");
        }
    }
    // A kept edit given an edit it does not hold (55c): the user asked for a
    // change, so the edit runs over the kept project -- its plan the parent
    // of this one -- never dropped. The same command again resumes instead.
    const bool edits_again = resumed.has_value() && editing &&
                             (!options.edit.empty() || !options.redo.empty()) &&
                             !asks_kept_edit(*resumed, options);
    if (edits_again && std::ranges::any_of(resumed->scenes, [](const harness::ProjectScene& scene) {
            return !scene.from.has_value();
        })) {
        return refuse(sidecar.string() +
                      " was edited by a build that did not record where its scenes came from, "
                      "so --edit and --redo-scene cannot run over it -- finish it with the "
                      "command that made it, or name another -o");
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
        project.crossfade = editing ? parent->crossfade : platform::kDefaultCrossfade;
        project.captions = options.captions && (!editing || parent->captions);
        if (editing) {
            project.music = parent->music;
            project.assets = parent->assets;
        }
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
            if (video::list_assets(project.assets).empty()) {
                return refuse(project.assets.string() +
                              " holds no stills or clips (png, jpg, jpeg, webp, mp4, mov, m4v, "
                              "webm)");
            }
        }
    }
    // The files an edit offers its plan: the project's own, kept or given.
    if (!project.assets.empty()) {
        asset_names = video::list_assets(project.assets);
    }

    // What runs it: a resumed project's own; an edit's parent's, unless a
    // flag names another; else the flags' or the defaults.
    const auto chosen = [&](const std::string& flag, const std::string& recorded) {
        if (resumed.has_value()) {
            return recorded;
        }
        return editing && flag.empty() ? recorded : flag;
    };
    ResolvedPipeline pipeline;
    if (std::string why = resolve_video_pipeline(
            roots,
            chosen(options.model, resumed.has_value() ? resumed->pipeline
                                  : editing           ? parent->pipeline
                                                      : std::string{}),
            resumed.has_value() || editing ? std::string{} : config.models.default_video, pipeline);
        !why.empty()) {
        return refuse(why);
    }
    ResolvedVoiceModel voice_model;
    if (std::string why = resolve_voice_model(
            roots,
            chosen(options.voice_model, resumed.has_value() ? resumed->voice_model
                                        : editing           ? parent->voice_model
                                                            : std::string{}),
            voice_model, kVoiceModelFlag);
        !why.empty()) {
        return refuse(why);
    }
    // An edit keeps its parent's voice while it keeps its voice model.
    std::string given_voice = options.voice;
    if (resumed.has_value()) {
        given_voice = resumed->voice;
    } else if (editing && given_voice.empty() && voice_model.handle == parent->voice_model) {
        given_voice = parent->voice;
    }
    std::string voice;
    if (std::string why = choose_voice(voice_model.info, given_voice, voice); !why.empty()) {
        return refuse(why);
    }
    const bool can_burn = project.captions && (!seams.can_burn || seams.can_burn());
    // Never write over (55c): on every path that renders -- a fresh run, a
    // resumed one, the plan a dry run kept -- refused before anything runs,
    // unless the file is the project's own, its record naming the captions
    // it kept there. A dry run writes nothing beside the video but its plan,
    // and like the runtimes' readiness the render checks this.
    const bool own_captions = resumed.has_value() && resumed->captions_file.lexically_normal() ==
                                                         captions_beside(output).lexically_normal();
    if (project.captions && !can_burn && !options.dry_run && !own_captions &&
        std::filesystem::exists(captions_beside(output), code)) {
        return refuse(captions_beside(output).string() +
                      " exists -- this ffmpeg cannot burn the captions, so they would be kept "
                      "there; name another -o");
    }
    if (!options.dry_run) {
        if (const video::VideoReadiness ready = seams.ready_video(pipeline); !ready.ready()) {
            return refuse(ready.message());
        }
        if (const video::VideoReadiness ready = seams.ready_voice(voice_model); !ready.ready()) {
            return refuse(ready.message());
        }
    }

    const auto save = [&sidecar](const harness::VideoProject& state) {
        harness::write_file_atomically(sidecar,
                                       harness::video_project_document(state).dump(2) + "\n");
    };
    if (resumed.has_value() && !edits_again) {
        std::size_t spoken = 0;
        std::size_t rendered = 0;
        for (const harness::ProjectScene& scene : project.scenes) {
            spoken += std::filesystem::is_regular_file(scene.voice, code) ? 1U : 0U;
            rendered += std::filesystem::is_regular_file(scene.clip, code) ? 1U : 0U;
        }
        if (project.status == "planned") {
            // A dry run's plan (52c): rendered as planned, never planned
            // again -- or, on paper again, read and said (55c).
            say((options.dry_run ? "reading" : "rendering") +
                std::string{" the plan a dry run kept at "} + sidecar.string());
        } else {
            say("resuming " + sidecar.string() + ": " + std::to_string(spoken) + " of " +
                std::to_string(project.scenes.size()) + " narrations and " +
                std::to_string(rendered) + " clips already there");
        }
        say_ignored(ignored_flags(options, *resumed, roots, /*plans=*/false));
    } else if (editing) {
        // One edit pass, or none; the re-rolls; the diff said before anything
        // renders. Over the parent -- or, given a kept edit, over that (55c).
        const harness::VideoProject& base = edits_again ? *resumed : *parent;
        video::EditedScenes step;
        std::string backend = base.planner;
        if (!options.edit.empty()) {
            backend = harness::resolve_chat_backend(config, options.planner);
            if (backend.empty()) {
                return refuse(
                    "no backend is configured to edit with -- add one with 'apogee config "
                    "add-backend', or name one with --planner");
            }
            say("editing " +
                (edits_again ? "the project kept at " + sidecar.string()
                             : parent_document.string()) +
                " with " + backend + ": \"" + options.edit + "\"");
            step = video::edit_scenes(
                base, options.edit, asset_names,
                [&](const std::string& system, const std::string& user, const std::string& schema) {
                    return seams.plan(backend, system, user, schema);
                },
                draw);
            if (!step.error.empty()) {
                return refuse(step.error);
            }
        } else {
            if (edits_again) {
                say("editing the project kept at " + sidecar.string());
            }
            step = video::same_scenes(base);
        }
        for (const std::size_t scene : options.redo) {
            if (std::string why = video::redo_scene(step, scene, draw()); !why.empty()) {
                return refuse(why);
            }
        }
        // What the document records and the diff says: this project against
        // its parent, an edit over a kept one followed through it.
        const video::EditedScenes edited =
            edits_again ? video::through_kept(*parent, *resumed, step) : step;
        for (const std::string& line : video::diff_lines(edited, parent->scenes.size())) {
            say(line);
        }
        const bool same_voice = voice_model.handle == base.voice_model && voice == base.voice;
        const bool same_pipeline = pipeline.handle == base.pipeline;
        if (edits_again) {
            say_ignored(ignored_flags(options, *resumed, roots, !options.edit.empty()));
        } else {
            if (!same_voice) {
                say("the voice is not the project's: every narration is spoken again");
            }
            if (!same_pipeline) {
                say("the pipeline is not the project's: every clip renders again, its seed held");
            }
        }
        project.title = edited.title;
        project.pipeline = pipeline.handle;
        project.voice_model = voice_model.handle;
        project.voice = voice;
        project.planner = backend;
        project.plan_attempts = options.edit.empty() ? base.plan_attempts : step.attempts;
        project.parent = parent_document;
        project.edits = base.edits;
        project.edits.push_back(harness::ProjectEdit{
            .instruction = options.edit, .redo = options.redo, .from = parent_document});
        project.created = models::now_rfc3339();
        project.platform = seams.platform;
        if (options.dry_run) {
            project.scenes = edited.scenes;
            video::record_provenance(project, edited);
            project.status = "planned";
            result.document = harness::video_project_document(project);
            result.lines.emplace_back("a dry run: nothing was rendered or written");
            return result;
        }
        if (!edits_again) {
            if (std::string why = video::carry_scenes(*parent, step, project.scenes_dir, same_voice,
                                                      same_pipeline);
                !why.empty()) {
                return refuse(why);
            }
            project.scenes = step.scenes;
            video::record_provenance(project, edited);
            project.status = "rendering";
            save(project);
        } else {
            // The kept files are renumbered in the very directory they are
            // carried from, so they go through one of their own: carried
            // there, the project saved over the copies before the kept files
            // go, then the copies moved into place and the project saved
            // again -- at every moment, each file a project on disk names is
            // the file it means.
            const std::filesystem::path carried = carry_dir(project.scenes_dir);
            if (std::string why =
                    video::carry_scenes(*resumed, step, carried, same_voice, same_pipeline);
                !why.empty()) {
                std::filesystem::remove_all(carried, code);
                return refuse(why);
            }
            project.scenes = step.scenes;
            video::record_provenance(project, edited);
            project.status = "rendering";
            save(project);
            std::vector<std::filesystem::path> kept_dirs{project.scenes_dir};
            for (const harness::ProjectScene& scene : resumed->scenes) {
                for (const std::filesystem::path& file : {scene.voice, scene.clip}) {
                    if (!file.empty() && scenes_or_carry(file.parent_path(), project.scenes_dir)) {
                        kept_dirs.push_back(file.parent_path());
                    }
                }
            }
            for (const std::filesystem::path& dir : kept_dirs) {
                std::filesystem::remove_all(dir, code);
            }
            std::filesystem::rename(carried, project.scenes_dir, code);
            if (code) {
                return refuse("could not move the carried scenes into " +
                              project.scenes_dir.string() + ": " + code.message() +
                              " -- they stay in " + carried.string() +
                              ", and the same command resumes");
            }
            for (harness::ProjectScene& scene : project.scenes) {
                scene.voice = moved_to(scene.voice, carried, project.scenes_dir);
                scene.clip = moved_to(scene.clip, carried, project.scenes_dir);
            }
            save(project);
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
            scene.seed = draw();
        }
        project.pipeline = pipeline.handle;
        project.voice_model = voice_model.handle;
        project.voice = voice;
        project.planner = backend;
        project.plan_attempts = planned.attempts;
        project.status = options.dry_run ? "planned" : "rendering";
        project.created = models::now_rfc3339();
        project.platform = seams.platform;
        save(project);
    }
    for (const std::string& line : plan_lines(project, can_burn)) {
        say(line);
    }
    if (options.dry_run) {
        // On paper (52c): the plan call alone -- or none, for a plan already
        // kept -- the project written beside the video as `planned`, and
        // nothing spoken or rendered. The same command without --dry-run
        // renders this plan.
        result.document = harness::video_project_document(project);
        result.lines = {resumed.has_value()
                            ? "a dry run: the plan kept at " + sidecar.string() +
                                  " -- nothing was planned again, spoken or rendered"
                            : "a dry run: one planning call to " + project.planner +
                                  ", nothing spoken or rendered",
                        "the plan, beside it: " + sidecar.string() +
                            " -- the same command without --dry-run renders it"};
        return result;
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
    compose.on_scene = seams.on_scene;
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
