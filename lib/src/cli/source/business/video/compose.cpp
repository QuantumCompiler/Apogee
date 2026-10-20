#include "video/compose.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>

#include "contracts/video_run.h"

namespace apogee::video {
namespace {

/// `seconds` for a line: `3.42 s`.
[[nodiscard]] std::string seconds_words(double seconds) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.2f s", seconds);
    return buffer.data();
}

/// A cap's seconds as the schema and the refusals say them: `2`, `10`.
[[nodiscard]] std::string whole(double seconds) {
    return std::to_string(static_cast<int>(seconds));
}

/// The caps in one phrase, for a refusal.
[[nodiscard]] std::string caps_words() {
    return "at most " + std::to_string(kMaxComposeScenes) + " scenes of " +
           whole(kMinSceneSeconds) + "-" + whole(kMaxSceneSeconds) + " s, each narration at most " +
           std::to_string(kMaxNarrationCharacters) + " characters";
}

/// `name`'s extension, lowercased, without its dot.
[[nodiscard]] std::string extension_of(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    std::string out{dot == std::string_view::npos ? std::string_view{} : name.substr(dot + 1)};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// A scene's file in the scenes directory: `s01.mp4`.
[[nodiscard]] std::filesystem::path scene_file(const harness::VideoProject& project,
                                               std::size_t index, std::string_view extension) {
    std::array<char, 16> name{};
    std::snprintf(name.data(), name.size(), "s%02zu", index + 1);
    return project.scenes_dir / (std::string{name.data()} + "." + std::string{extension});
}

/// The plan's schema; with `from_scenes`, each scene first names the scene
/// it came from (1 to `from_scenes`, or null) -- the edit's.
[[nodiscard]] std::string scenes_schema(const std::vector<std::string>& assets,
                                        std::size_t from_scenes) {
    nlohmann::ordered_json properties = nlohmann::ordered_json::object();
    nlohmann::ordered_json required = nlohmann::ordered_json::array();
    if (from_scenes > 0) {
        properties["from"] = {
            {"type", {"integer", "null"}}, {"minimum", 1}, {"maximum", from_scenes}};
        required.push_back("from");
    }
    properties["narration"] = {
        {"type", "string"}, {"minLength", 1}, {"maxLength", kMaxNarrationCharacters}};
    properties["visual"] = {
        {"type", "string"}, {"minLength", 1}, {"maxLength", kMaxVisualCharacters}};
    properties["seconds"] = {
        {"type", "number"}, {"minimum", kMinSceneSeconds}, {"maximum", kMaxSceneSeconds}};
    for (const char* name : {"narration", "visual", "seconds"}) {
        required.push_back(name);
    }
    if (!assets.empty()) {
        nlohmann::ordered_json names = nlohmann::ordered_json::array({nullptr});
        for (const std::string& asset : assets) {
            names.push_back(asset);
        }
        properties["asset"] = {{"enum", names}};
    }
    const nlohmann::ordered_json scene = {{"type", "object"},
                                          {"additionalProperties", false},
                                          {"required", required},
                                          {"properties", properties}};
    const nlohmann::ordered_json schema = {
        {"type", "object"},
        {"additionalProperties", false},
        {"required", {"title", "scenes"}},
        {"properties",
         {{"title", {{"type", "string"}, {"minLength", 1}, {"maxLength", 120}}},
          {"scenes",
           {{"type", "array"},
            {"minItems", 1},
            {"maxItems", kMaxComposeScenes},
            {"items", scene}}}}}};
    return schema.dump();
}

/// A scene as the planner and the editor read it.
[[nodiscard]] nlohmann::ordered_json scene_json(const harness::ProjectScene& scene) {
    nlohmann::ordered_json out = {
        {"narration", scene.narration}, {"visual", scene.visual}, {"seconds", scene.seconds}};
    if (!scene.asset.empty()) {
        out["asset"] = scene.asset;
    }
    return out;
}

/// `from` into `to` byte for byte, its record beside it too.
[[nodiscard]] std::string carry_file(const std::filesystem::path& from,
                                     const std::filesystem::path& to) {
    std::error_code code;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, code);
    if (code) {
        return "could not carry " + from.string() + " over: " + code.message();
    }
    const std::filesystem::path record = harness::video_sidecar_path(from);
    if (std::filesystem::is_regular_file(record, code)) {
        std::filesystem::copy_file(record, harness::video_sidecar_path(to),
                                   std::filesystem::copy_options::overwrite_existing, code);
    }
    return {};
}

}  // namespace

std::string plan_schema(const std::vector<std::string>& assets) {
    return scenes_schema(assets, 0);
}

std::string plan_system_prompt() {
    return "You plan short narrated videos. Split the brief into scenes, in the order they "
           "play. For each scene write:\n"
           "- narration: the words a voice speaks over it -- one or two short sentences of plain "
           "spoken language, no stage directions, no labels. Speech runs about two and a half "
           "words a second, so keep each narration short enough to be spoken within its "
           "scene.\n"
           "- visual: what the camera shows, written as a prompt for a text-to-video model -- "
           "the subject, the setting, the motion, the light and the camera, in one paragraph. "
           "Never ask for words, captions or logos in the frame.\n"
           "- seconds: how long the scene lasts, " +
           whole(kMinSceneSeconds) + " to " + whole(kMaxSceneSeconds) +
           ".\n"
           "Give the video a short title. When the user lists their own files, a scene may "
           "take one by its exact name as its asset -- a clip is shown as it is, a still is "
           "set in motion -- or null for a generated scene. Answer with the JSON object "
           "only.";
}

std::string plan_user_message(std::string_view brief, const std::vector<std::string>& assets) {
    std::string out = "Brief: " + std::string{brief} + "\n\nAt most " +
                      std::to_string(kMaxComposeScenes) + " scenes, each " +
                      whole(kMinSceneSeconds) + " to " + whole(kMaxSceneSeconds) + " seconds.";
    if (!assets.empty()) {
        out += "\nThe user's files:";
        for (const std::string& asset : assets) {
            out += " " + asset + (asset == assets.back() ? "" : ",");
        }
    }
    return out;
}

PlannedScenes plan_scenes(std::string_view brief, const std::vector<std::string>& assets,
                          const PlanFn& plan) {
    PlannedScenes out;
    const PlanAnswer answer =
        plan(plan_system_prompt(), plan_user_message(brief, assets), plan_schema(assets));
    out.attempts = answer.attempts;
    if (!answer.json.has_value() || !answer.conforms) {
        out.error = "the plan did not hold to its shape" +
                    std::string{answer.attempts > 1 ? " after its one correction" : ""} +
                    (answer.error.empty() ? std::string{} : ": " + answer.error) +
                    " -- the caps: " + caps_words();
        return out;
    }
    const nlohmann::json& json = *answer.json;
    out.title = json.value("title", std::string{});
    for (const nlohmann::json& item : json.at("scenes")) {
        harness::ProjectScene scene;
        scene.narration = item.at("narration").get<std::string>();
        scene.visual = item.at("visual").get<std::string>();
        scene.seconds = item.at("seconds").get<double>();
        if (const auto asset = item.find("asset"); asset != item.end() && asset->is_string()) {
            scene.asset = asset->get<std::string>();
        }
        out.scenes.push_back(std::move(scene));
    }
    return out;
}

bool is_still(std::string_view name) {
    const std::string extension = extension_of(name);
    return extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "webp";
}

std::vector<std::string> list_assets(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        std::error_code type;
        if (!entry.is_regular_file(type)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const std::string extension = extension_of(name);
        if (is_still(name) || extension == "mp4" || extension == "mov" || extension == "m4v" ||
            extension == "webm") {
            out.push_back(name);
        }
    }
    std::ranges::sort(out);
    return out;
}

double narration_needs(double spoken, double crossfade) {
    return kNarrationLead + spoken + kNarrationRoom + crossfade;
}

std::optional<double> scene_length(double planned, double spoken, double crossfade) {
    const double needs = narration_needs(spoken, crossfade);
    if (needs > kMaxSceneSeconds + 1e-9) {
        return std::nullopt;
    }
    return std::clamp(std::max(planned, needs), kMinSceneSeconds, kMaxSceneSeconds);
}

std::vector<double> scene_starts(const harness::VideoProject& project) {
    std::vector<double> out;
    double at = 0.0;
    for (const harness::ProjectScene& scene : project.scenes) {
        out.push_back(at);
        at += scene.clip_seconds - project.crossfade;
    }
    return out;
}

std::vector<harness::CaptionCue> caption_cues(const harness::VideoProject& project) {
    std::vector<harness::CaptionCue> out;
    const std::vector<double> starts = scene_starts(project);
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        for (const harness::ProjectSentence& sentence : project.scenes[index].sentences) {
            const double start = starts[index] + kNarrationLead + sentence.offset;
            out.push_back({.text = sentence.text, .start = start, .end = start + sentence.seconds});
        }
    }
    return out;
}

std::vector<platform::NarrationPiece> narration_pieces(const harness::VideoProject& project) {
    std::vector<platform::NarrationPiece> out;
    const std::vector<double> starts = scene_starts(project);
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        out.push_back(
            {.file = project.scenes[index].voice, .offset = starts[index] + kNarrationLead});
    }
    return out;
}

ComposeOutcome render_project(harness::VideoProject& project, const ComposeSeams& seams,
                              const harness::CancellationToken& cancellation) {
    const auto say = [&seams](const std::string& line) {
        if (seams.say) {
            seams.say(line);
        }
    };
    const auto save = [&seams, &project] {
        if (seams.save) {
            seams.save(project);
        }
    };
    const auto stop = [&](bool cancelled, std::string why) {
        project.status = cancelled ? "cancelled" : "failed";
        project.error = why;
        save();
        return ComposeOutcome{.ok = false, .cancelled = cancelled, .error = std::move(why)};
    };
    const auto have = [](const std::filesystem::path& file) {
        std::error_code code;
        return !file.empty() && std::filesystem::is_regular_file(file, code);
    };
    // A file the document does not record is a stopped run's leftover.
    const auto clear = [](const std::filesystem::path& file) {
        std::error_code code;
        std::filesystem::remove(file, code);
        std::filesystem::remove(harness::video_sidecar_path(file), code);
    };
    platform::FfmpegLimits limits;
    limits.stopped = [cancellation] { return cancellation.stop_requested(); };
    const std::size_t count = project.scenes.size();
    const auto label = [count](std::size_t index) {
        return "scene " + std::to_string(index + 1) + "/" + std::to_string(count);
    };
    const auto begun = [&seams, count](std::size_t index, std::string_view state) {
        if (seams.on_scene) {
            seams.on_scene(index + 1, count, state);
        }
    };

    std::error_code code;
    std::filesystem::create_directories(project.scenes_dir, code);
    if (code) {
        return stop(false, "could not make " + project.scenes_dir.string() + ": " + code.message());
    }
    project.status = "rendering";
    project.error.clear();
    save();

    // Every narration first: cheap, and what proves each scene can hold its
    // own before a clip is rendered.
    for (std::size_t index = 0; index < count; ++index) {
        harness::ProjectScene& scene = project.scenes[index];
        if (cancellation.stop_requested()) {
            return stop(true, "cancelled");
        }
        if (!have(scene.voice)) {
            const std::filesystem::path wav = scene_file(project, index, "wav");
            clear(wav);
            say(label(index) + ": speaking its narration");
            begun(index, "speaking");
            SpokenScene spoken = seams.speak(scene.narration, wav);
            if (!spoken.ok) {
                return stop(spoken.cancelled, spoken.cancelled
                                                  ? std::string{"cancelled"}
                                                  : label(index) + "'s narration: " + spoken.error);
            }
            scene.voice = wav;
            scene.voice_seconds = spoken.seconds;
            scene.sentences = std::move(spoken.sentences);
            save();
        }
        if (!scene_length(scene.seconds, scene.voice_seconds, project.crossfade).has_value()) {
            return stop(false, label(index) + "'s narration runs " +
                                   seconds_words(scene.voice_seconds) + ", and a scene holds " +
                                   seconds_words(kMaxSceneSeconds) +
                                   " with its lead, the room after it and the next fade -- "
                                   "shorten it");
        }
        if (!scene.asset.empty() && !is_still(scene.asset)) {
            const std::filesystem::path asset = project.assets / scene.asset;
            std::string error;
            const std::optional<platform::MediaInfo> info = seams.probe(asset, limits, error);
            if (!info.has_value()) {
                return stop(cancellation.stop_requested(),
                            label(index) + "'s asset " + asset.string() + ": " + error);
            }
            const double needs = narration_needs(scene.voice_seconds, project.crossfade);
            if (info->duration + 0.1 < needs) {
                return stop(false, label(index) + "'s asset " + scene.asset + " runs " +
                                       seconds_words(info->duration) +
                                       ", shorter than its narration needs (" +
                                       seconds_words(needs) + ")");
            }
        }
    }

    for (std::size_t index = 0; index < count; ++index) {
        harness::ProjectScene& scene = project.scenes[index];
        if (cancellation.stop_requested()) {
            return stop(true, "cancelled");
        }
        const double needs = narration_needs(scene.voice_seconds, project.crossfade);
        if (have(scene.clip)) {
            if (scene.clip_seconds + 0.1 >= needs) {
                continue;
            }
            say(label(index) + ": its clip runs " + seconds_words(scene.clip_seconds) +
                ", shorter than its narration now needs (" + seconds_words(needs) +
                ") -- rendering it again, its seed held");
        }
        std::filesystem::path clip;
        if (!scene.asset.empty() && !is_still(scene.asset)) {
            clip = scene_file(project, index, extension_of(scene.asset));
            clear(clip);
            say(label(index) + ": taking " + scene.asset + " as it is");
            begun(index, "asset");
            std::filesystem::copy_file(project.assets / scene.asset, clip,
                                       std::filesystem::copy_options::overwrite_existing, code);
            if (code) {
                return stop(false, label(index) + ": could not copy " + scene.asset + ": " +
                                       code.message());
            }
        } else {
            clip = scene_file(project, index, "mp4");
            clear(clip);
            ClipAsk ask{
                .scene = index + 1,
                .prompt = scene.visual,
                .seconds = *scene_length(scene.seconds, scene.voice_seconds, project.crossfade),
                .seed = scene.seed};
            if (!scene.asset.empty()) {
                ask.image = project.assets / scene.asset;
            }
            say(label(index) + ": rendering its clip -- " + seconds_words(ask.seconds) + ", seed " +
                std::to_string(ask.seed) +
                (ask.image.empty() ? std::string{} : ", from " + scene.asset));
            begun(index, "rendering");
            const RenderedClip rendered = seams.render(ask, clip);
            if (!rendered.ok) {
                return stop(rendered.cancelled, rendered.cancelled
                                                    ? std::string{"cancelled"}
                                                    : label(index) + "'s clip: " + rendered.error);
            }
        }
        std::string error;
        const std::optional<platform::MediaInfo> info = seams.probe(clip, limits, error);
        if (!info.has_value()) {
            return stop(cancellation.stop_requested(),
                        label(index) + ": could not read its clip: " + error);
        }
        scene.clip = clip;
        scene.clip_seconds = info->duration;
        save();
    }

    // The captions: the plan's own sentences, timed by what was measured.
    std::filesystem::path srt;
    if (project.captions) {
        srt = project.scenes_dir / "captions.srt";
        std::ofstream out{srt, std::ios::binary | std::ios::trunc};
        out << harness::write_srt(caption_cues(project));
        if (!out) {
            return stop(false, "could not write " + srt.string());
        }
    }

    std::vector<std::filesystem::path> clips;
    for (const harness::ProjectScene& scene : project.scenes) {
        clips.push_back(scene.clip);
    }
    const std::filesystem::path joined = project.scenes_dir / ".joined.mp4";
    const std::filesystem::path mixed = project.scenes_dir / ".mixed.mp4";
    const std::filesystem::path burned = project.scenes_dir / ".captioned.mp4";
    const auto assembly_failed = [&](const std::string& step, const std::string& error) {
        std::error_code ignored;
        for (const std::filesystem::path& file : {joined, mixed, burned}) {
            std::filesystem::remove(file, ignored);
        }
        return stop(cancellation.stop_requested(),
                    cancellation.stop_requested() ? std::string{"cancelled"} : step + ": " + error);
    };
    std::string error;
    say("joining " + std::to_string(count) + " clip" + (count == 1 ? "" : "s") +
        (count > 1 && project.crossfade > 0.0
             ? " with " + seconds_words(project.crossfade) + " crossfades"
             : std::string{}));
    const std::optional<platform::Assembled> join =
        seams.join(clips, project.crossfade, joined, limits, error);
    if (!join.has_value()) {
        return assembly_failed("joining the clips", error);
    }
    say(std::string{"laying the narration"} + (project.music.empty() ? "" : " over the bed"));
    const std::optional<platform::Assembled> mix = seams.mix(
        joined, platform::AudioMix{.narration = narration_pieces(project), .bed = project.music},
        mixed, limits, error);
    if (!mix.has_value()) {
        return assembly_failed("mixing the narration", error);
    }
    std::string words = "joined: " + join->settings + "; mixed: " + mix->settings;
    std::filesystem::path made = mixed;
    project.captions_burned = false;
    if (project.captions && seams.can_burn) {
        say("burning the captions");
        const std::optional<platform::Assembled> burn =
            seams.burn(mixed, srt, burned, limits, error);
        if (!burn.has_value()) {
            return assembly_failed("burning the captions", error);
        }
        words += "; captioned: " + burn->settings;
        made = burned;
        project.captions_burned = true;
    }
    std::filesystem::rename(made, project.output, code);
    if (code) {
        return assembly_failed("moving the video into place", code.message());
    }
    for (const std::filesystem::path& file : {joined, mixed, burned}) {
        std::filesystem::remove(file, code);
    }
    project.captions_file = srt;
    if (project.captions && !project.captions_burned) {
        // Kept beside the video, where a player finds it.
        std::filesystem::path beside = project.output;
        beside.replace_extension(".srt");
        std::filesystem::copy_file(srt, beside, std::filesystem::copy_options::overwrite_existing,
                                   code);
        if (!code) {
            project.captions_file = beside;
        }
    }
    project.assembly = words;
    project.status = "done";
    project.error.clear();
    save();
    return ComposeOutcome{.ok = true};
}

std::string edit_schema(const std::vector<std::string>& assets, std::size_t scenes) {
    return scenes_schema(assets, std::max<std::size_t>(scenes, 1));
}

std::string edit_system_prompt() {
    return "You edit the plan of a short narrated video. You are given the plan as it stands, "
           "its scenes numbered, and an instruction. Answer with the whole plan after the "
           "instruction is carried out, in the same shape. Each scene's `from` is the number of "
           "the scene it keeps or changes, or null for a scene the instruction adds. Leave every "
           "scene the instruction does not touch exactly as it is -- the same narration, visual, "
           "seconds and asset, word for word -- and keep the scenes' order unless the "
           "instruction moves them. The plan's rules still hold: each narration one or two "
           "short spoken sentences, each visual a prompt for a text-to-video model, each scene " +
           whole(kMinSceneSeconds) + " to " + whole(kMaxSceneSeconds) +
           " seconds. Answer with the JSON object only.";
}

std::string edit_user_message(const harness::VideoProject& project, std::string_view instruction,
                              const std::vector<std::string>& assets) {
    nlohmann::ordered_json scenes = nlohmann::ordered_json::array();
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        nlohmann::ordered_json scene = {{"from", index + 1}};
        scene.update(scene_json(project.scenes[index]));
        scenes.push_back(scene);
    }
    const nlohmann::ordered_json plan = {{"title", project.title}, {"scenes", scenes}};
    std::string out = "The brief: " + project.brief + "\n\nThe plan:\n" + plan.dump(2) +
                      "\n\nThe instruction: " + std::string{instruction} + "\n\nAt most " +
                      std::to_string(kMaxComposeScenes) + " scenes.";
    if (!assets.empty()) {
        out += "\nThe user's files:";
        for (const std::string& asset : assets) {
            out += " " + asset + (asset == assets.back() ? "" : ",");
        }
    }
    return out;
}

EditedScenes edit_scenes(const harness::VideoProject& parent, std::string_view instruction,
                         const std::vector<std::string>& assets, const PlanFn& edit,
                         const std::function<std::int64_t()>& seed) {
    EditedScenes out;
    const PlanAnswer answer =
        edit(edit_system_prompt(), edit_user_message(parent, instruction, assets),
             edit_schema(assets, parent.scenes.size()));
    out.attempts = answer.attempts;
    if (!answer.json.has_value() || !answer.conforms) {
        out.error = "the edit did not hold to its shape" +
                    std::string{answer.attempts > 1 ? " after its one correction" : ""} +
                    (answer.error.empty() ? std::string{} : ": " + answer.error) +
                    " -- the caps: " + caps_words() + "; nothing was changed";
        return out;
    }
    const nlohmann::json& json = *answer.json;
    out.title = json.value("title", parent.title);
    std::vector<bool> kept(parent.scenes.size(), false);
    for (const nlohmann::json& item : json.at("scenes")) {
        harness::ProjectScene scene;
        scene.narration = item.at("narration").get<std::string>();
        scene.visual = item.at("visual").get<std::string>();
        scene.seconds = item.at("seconds").get<double>();
        if (const auto asset = item.find("asset"); asset != item.end() && asset->is_string()) {
            scene.asset = asset->get<std::string>();
        }
        SceneChange change;
        const nlohmann::json& from = item.at("from");
        if (from.is_number_integer() && from.get<std::int64_t>() >= 1 &&
            static_cast<std::size_t>(from.get<std::int64_t>()) <= parent.scenes.size()) {
            change.from = static_cast<std::size_t>(from.get<std::int64_t>());
            const harness::ProjectScene& was = parent.scenes[change.from - 1];
            kept[change.from - 1] = true;
            scene.seed = was.seed;
            change.narration = scene.narration != was.narration;
            change.visual = scene.visual != was.visual;
            change.seconds = std::abs(scene.seconds - was.seconds) > 1e-9;
            change.asset = scene.asset != was.asset;
        } else {
            scene.seed = seed ? seed() : 0;
        }
        out.scenes.push_back(std::move(scene));
        out.changes.push_back(change);
    }
    for (std::size_t index = 0; index < kept.size(); ++index) {
        if (!kept[index]) {
            out.removed.push_back(index + 1);
        }
    }
    // In place: the longest run of kept scenes still in the parent's order;
    // every other kept scene moved.
    const std::size_t count = out.changes.size();
    std::vector<std::size_t> length(count, 0);
    std::vector<std::size_t> before(count, count);
    std::size_t best = count;
    for (std::size_t index = 0; index < count; ++index) {
        if (out.changes[index].from == 0) {
            continue;
        }
        length[index] = 1;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (out.changes[earlier].from != 0 &&
                out.changes[earlier].from < out.changes[index].from &&
                length[earlier] + 1 > length[index]) {
                length[index] = length[earlier] + 1;
                before[index] = earlier;
            }
        }
        if (best == count || length[index] >= length[best]) {
            best = index;
        }
    }
    std::vector<bool> in_place(count, false);
    for (std::size_t at = best; at < count; at = before[at]) {
        in_place[at] = true;
    }
    for (std::size_t index = 0; index < count; ++index) {
        out.changes[index].moved = out.changes[index].from != 0 && !in_place[index];
    }
    return out;
}

EditedScenes same_scenes(const harness::VideoProject& parent) {
    EditedScenes out;
    out.title = parent.title;
    for (std::size_t index = 0; index < parent.scenes.size(); ++index) {
        const harness::ProjectScene& was = parent.scenes[index];
        harness::ProjectScene scene;
        scene.narration = was.narration;
        scene.visual = was.visual;
        scene.seconds = was.seconds;
        scene.asset = was.asset;
        scene.seed = was.seed;
        out.scenes.push_back(std::move(scene));
        out.changes.push_back(SceneChange{.from = index + 1});
    }
    return out;
}

std::string redo_scene(EditedScenes& edited, std::size_t scene, std::int64_t seed) {
    if (scene < 1 || scene > edited.scenes.size()) {
        return "--redo-scene " + std::to_string(scene) + ": the project has " +
               std::to_string(edited.scenes.size()) + " scene" +
               (edited.scenes.size() == 1 ? "" : "s");
    }
    edited.scenes[scene - 1].seed = seed;
    edited.changes[scene - 1].reseeded = true;
    return {};
}

std::vector<std::string> diff_lines(const EditedScenes& edited, std::size_t parent_scenes) {
    std::vector<std::string> out;
    const std::size_t count = edited.scenes.size();
    out.push_back("the edit: " + std::to_string(count) + " scene" + (count == 1 ? "" : "s") +
                  ", were " + std::to_string(parent_scenes));
    for (std::size_t index = 0; index < count; ++index) {
        const SceneChange& change = edited.changes[index];
        std::string line = "  " + std::to_string(index + 1) + ". ";
        if (change.from == 0) {
            out.push_back(line + "new");
            continue;
        }
        std::vector<std::string> what;
        if (change.narration) {
            what.emplace_back("its narration changed");
        }
        if (change.visual) {
            what.emplace_back("its prompt changed");
        }
        if (change.seconds) {
            what.emplace_back("its length changed");
        }
        if (change.asset) {
            what.emplace_back("its asset changed");
        }
        if (change.reseeded) {
            what.emplace_back("a new seed");
        }
        line += "scene " + std::to_string(change.from) + (change.moved ? ", moved" : "");
        if (what.empty()) {
            line += change.moved ? "" : ", untouched";
        } else {
            line += "; ";
            for (std::size_t at = 0; at < what.size(); ++at) {
                line += (at == 0 ? "" : ", ") + what[at];
            }
        }
        out.push_back(line);
    }
    for (const std::size_t removed : edited.removed) {
        out.push_back("  scene " + std::to_string(removed) + " removed");
    }
    return out;
}

std::string carry_scenes(const harness::VideoProject& parent, EditedScenes& edited,
                         const std::filesystem::path& scenes_dir, bool same_voice,
                         bool same_pipeline) {
    std::error_code code;
    std::filesystem::create_directories(scenes_dir, code);
    if (code) {
        return "could not make " + scenes_dir.string() + ": " + code.message();
    }
    harness::VideoProject numbering;
    numbering.scenes_dir = scenes_dir;
    for (std::size_t index = 0; index < edited.scenes.size(); ++index) {
        const SceneChange& change = edited.changes[index];
        if (change.from == 0) {
            continue;
        }
        const harness::ProjectScene& was = parent.scenes[change.from - 1];
        harness::ProjectScene& scene = edited.scenes[index];
        if (same_voice && !change.narration && std::filesystem::is_regular_file(was.voice, code)) {
            const std::filesystem::path wav = scene_file(numbering, index, "wav");
            if (std::string error = carry_file(was.voice, wav); !error.empty()) {
                return error;
            }
            scene.voice = wav;
            scene.voice_seconds = was.voice_seconds;
            scene.sentences = was.sentences;
        }
        if (same_pipeline && !change.visual && !change.seconds && !change.asset &&
            !change.reseeded && std::filesystem::is_regular_file(was.clip, code)) {
            std::string extension = was.clip.extension().string();
            if (!extension.empty()) {
                extension.erase(0, 1);
            }
            const std::filesystem::path clip = scene_file(numbering, index, extension);
            if (std::string error = carry_file(was.clip, clip); !error.empty()) {
                return error;
            }
            scene.clip = clip;
            scene.clip_seconds = was.clip_seconds;
        }
    }
    return {};
}

}  // namespace apogee::video
