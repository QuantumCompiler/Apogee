#include "contracts/video_project.h"

namespace apogee::harness {
namespace {

[[nodiscard]] nlohmann::json path_or_null(const std::filesystem::path& path) {
    return path.empty() ? nlohmann::json(nullptr) : nlohmann::json(path.string());
}

[[nodiscard]] nlohmann::json text_or_null(const std::string& text) {
    return text.empty() ? nlohmann::json(nullptr) : nlohmann::json(text);
}

[[nodiscard]] std::string string_at(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

[[nodiscard]] double number_at(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? found->get<double>() : 0.0;
}

}  // namespace

nlohmann::json video_project_document(const VideoProject& project) {
    nlohmann::json scenes = nlohmann::json::array();
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        const ProjectScene& scene = project.scenes[index];
        nlohmann::json voice = nullptr;
        if (!scene.voice.empty()) {
            nlohmann::json sentences = nlohmann::json::array();
            for (const ProjectSentence& sentence : scene.sentences) {
                sentences.push_back({{"text", sentence.text},
                                     {"seconds", sentence.seconds},
                                     {"offset", sentence.offset}});
            }
            voice = {{"file", scene.voice.string()},
                     {"seconds", scene.voice_seconds},
                     {"sentences", sentences}};
        }
        nlohmann::json clip = nullptr;
        if (!scene.clip.empty()) {
            clip = {{"file", scene.clip.string()}, {"seconds", scene.clip_seconds}};
        }
        scenes.push_back({{"index", index + 1},
                          {"narration", scene.narration},
                          {"visual", scene.visual},
                          {"seconds", scene.seconds},
                          {"asset", text_or_null(scene.asset)},
                          {"seed", scene.seed},
                          {"voice", voice},
                          {"clip", clip}});
    }
    nlohmann::json edits = nlohmann::json::array();
    for (const ProjectEdit& edit : project.edits) {
        edits.push_back({{"instruction", text_or_null(edit.instruction)},
                         {"redo", edit.redo},
                         {"from", edit.from.string()}});
    }
    nlohmann::json captions = nullptr;
    if (!project.captions_file.empty()) {
        captions = {{"file", project.captions_file.string()}, {"burned", project.captions_burned}};
    }
    return {{"object", kVideoProjectObject},
            {"version", kVideoProjectVersion},
            {"status", project.status},
            {"brief", project.brief},
            {"title", project.title},
            {"output", project.output.string()},
            {"scenes_dir", project.scenes_dir.string()},
            {"settings",
             {{"model", project.pipeline},
              {"voice_model", project.voice_model},
              {"voice", project.voice},
              {"crossfade", project.crossfade},
              {"captions", project.captions},
              {"music", path_or_null(project.music)},
              {"assets", path_or_null(project.assets)}}},
            {"plan", {{"backend", project.planner}, {"attempts", project.plan_attempts}}},
            {"scenes", scenes},
            {"captions", captions},
            {"assembly", text_or_null(project.assembly)},
            {"error", text_or_null(project.error)},
            {"created", project.created},
            {"platform", project.platform},
            {"parent", path_or_null(project.parent)},
            {"edits", edits}};
}

std::optional<VideoProject> read_video_project(const nlohmann::json& document, std::string& error) {
    if (!document.is_object() || string_at(document, "object") != kVideoProjectObject) {
        error = "it is not a video.project document";
        return std::nullopt;
    }
    const auto version = document.find("version");
    if (version == document.end() || !version->is_number_integer() ||
        version->get<int>() > kVideoProjectVersion) {
        error = "its version is not one this build reads";
        return std::nullopt;
    }
    const auto scenes = document.find("scenes");
    if (scenes == document.end() || !scenes->is_array() || scenes->empty()) {
        error = "it holds no scenes";
        return std::nullopt;
    }
    VideoProject project;
    project.status = string_at(document, "status");
    project.brief = string_at(document, "brief");
    project.title = string_at(document, "title");
    project.output = string_at(document, "output");
    project.scenes_dir = string_at(document, "scenes_dir");
    if (project.brief.empty() || project.output.empty() || project.scenes_dir.empty()) {
        error = "it names no brief, output or scenes directory";
        return std::nullopt;
    }
    if (const auto settings = document.find("settings");
        settings != document.end() && settings->is_object()) {
        project.pipeline = string_at(*settings, "model");
        project.voice_model = string_at(*settings, "voice_model");
        project.voice = string_at(*settings, "voice");
        project.crossfade = number_at(*settings, "crossfade");
        const auto captions = settings->find("captions");
        project.captions =
            captions == settings->end() || !captions->is_boolean() || captions->get<bool>();
        project.music = string_at(*settings, "music");
        project.assets = string_at(*settings, "assets");
    }
    if (const auto plan = document.find("plan"); plan != document.end() && plan->is_object()) {
        project.planner = string_at(*plan, "backend");
        project.plan_attempts = static_cast<int>(number_at(*plan, "attempts"));
    }
    for (const nlohmann::json& item : *scenes) {
        if (!item.is_object()) {
            error = "a scene is not an object";
            return std::nullopt;
        }
        ProjectScene scene;
        scene.narration = string_at(item, "narration");
        scene.visual = string_at(item, "visual");
        scene.seconds = number_at(item, "seconds");
        scene.asset = string_at(item, "asset");
        if (const auto seed = item.find("seed"); seed != item.end() && seed->is_number_integer()) {
            scene.seed = seed->get<std::int64_t>();
        }
        if (scene.narration.empty() || scene.visual.empty()) {
            error = "a scene has no narration or no visual";
            return std::nullopt;
        }
        if (const auto voice = item.find("voice"); voice != item.end() && voice->is_object()) {
            scene.voice = string_at(*voice, "file");
            scene.voice_seconds = number_at(*voice, "seconds");
            if (const auto sentences = voice->find("sentences");
                sentences != voice->end() && sentences->is_array()) {
                for (const nlohmann::json& sentence : *sentences) {
                    if (sentence.is_object()) {
                        scene.sentences.push_back({.text = string_at(sentence, "text"),
                                                   .seconds = number_at(sentence, "seconds"),
                                                   .offset = number_at(sentence, "offset")});
                    }
                }
            }
        }
        if (const auto clip = item.find("clip"); clip != item.end() && clip->is_object()) {
            scene.clip = string_at(*clip, "file");
            scene.clip_seconds = number_at(*clip, "seconds");
        }
        project.scenes.push_back(std::move(scene));
    }
    if (const auto captions = document.find("captions");
        captions != document.end() && captions->is_object()) {
        project.captions_file = string_at(*captions, "file");
        const auto burned = captions->find("burned");
        project.captions_burned =
            burned != captions->end() && burned->is_boolean() && burned->get<bool>();
    }
    project.assembly = string_at(document, "assembly");
    project.error = string_at(document, "error");
    project.created = string_at(document, "created");
    project.platform = string_at(document, "platform");
    project.parent = string_at(document, "parent");
    if (const auto edits = document.find("edits"); edits != document.end() && edits->is_array()) {
        for (const nlohmann::json& item : *edits) {
            if (!item.is_object()) {
                continue;
            }
            ProjectEdit edit;
            edit.instruction = string_at(item, "instruction");
            edit.from = string_at(item, "from");
            if (const auto redo = item.find("redo"); redo != item.end() && redo->is_array()) {
                for (const nlohmann::json& scene : *redo) {
                    if (scene.is_number_unsigned()) {
                        edit.redo.push_back(scene.get<std::size_t>());
                    }
                }
            }
            project.edits.push_back(std::move(edit));
        }
    }
    return project;
}

std::filesystem::path video_project_scenes_dir(const std::filesystem::path& output) {
    return output.parent_path() / (output.stem().string() + ".scenes");
}

}  // namespace apogee::harness
