#include "contracts/video_run.h"

#include <fstream>

namespace apogee::harness {

nlohmann::json video_run_document(const VideoRun& run) {
    nlohmann::json settings{{"width", run.settings.width},       {"height", run.settings.height},
                            {"duration", run.settings.duration}, {"fps", run.settings.fps},
                            {"frames", run.settings.frames},     {"steps", run.settings.steps}};
    settings["guidance"] =
        run.settings.guidance.has_value() ? nlohmann::json(*run.settings.guidance) : nullptr;
    nlohmann::json document{
        {"object", "video.generation"},
        {"status", run.status},
        {"output", run.output.string()},
        {"sidecar",
         run.sidecar.empty() ? nlohmann::json(nullptr) : nlohmann::json(run.sidecar.string())},
        {"model",
         {{"name", run.model},
          {"path", run.model_path.string()},
          {"class", run.pipeline_class},
          {"ref", run.model_ref},
          {"revision", run.model_revision},
          {"variant",
           run.variant.empty() ? nlohmann::json(nullptr) : nlohmann::json(run.variant)}}},
        {"prompt", run.prompt},
        {"image", run.image.empty() ? nlohmann::json(nullptr) : nlohmann::json(run.image.string())},
        {"mode", run.mode},
        {"seed", run.seed},
        {"seed_source", run.seed_source},
        {"settings", std::move(settings)},
        {"device", {{"device", run.device}, {"dtype", run.dtype}, {"platform", run.platform}}},
        {"versions", {{"torch", run.torch}, {"diffusers", run.diffusers}, {"apogee", run.apogee}}},
        {"codec", run.codec.empty() ? nlohmann::json(nullptr) : nlohmann::json(run.codec)},
        {"seconds", run.seconds},
        {"created_at", run.created_at},
    };
    if (!run.error.empty()) {
        document["error"] = run.error;
    }
    document["parent"] =
        run.parent.move.empty()
            ? nlohmann::json(nullptr)
            : nlohmann::json{
                  {"move", run.parent.move},
                  {"document", run.parent.document.empty()
                                   ? nlohmann::json(nullptr)
                                   : nlohmann::json(run.parent.document.string())},
                  {"clip", run.parent.clip.empty() ? nlohmann::json(nullptr)
                                                   : nlohmann::json(run.parent.clip.string())},
                  {"continued", run.parent.continued.empty()
                                    ? nlohmann::json(nullptr)
                                    : nlohmann::json(run.parent.continued.string())}};
    document["preset"] = run.preset.empty() ? nlohmann::json(nullptr) : nlohmann::json(run.preset);
    return document;
}

namespace {

[[nodiscard]] std::string text_at(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

}  // namespace

std::optional<VideoRun> parse_video_run(const nlohmann::json& document, std::string& error) {
    if (!document.is_object() || text_at(document, "object") != "video.generation") {
        error = "not a video.generation record";
        return std::nullopt;
    }
    const auto settings = document.find("settings");
    const auto model = document.find("model");
    const auto seed = document.find("seed");
    if (settings == document.end() || !settings->is_object() || model == document.end() ||
        !model->is_object() || seed == document.end() || !seed->is_number_integer() ||
        text_at(*model, "name").empty()) {
        error = "a video.generation record without its model, seed or settings";
        return std::nullopt;
    }
    VideoRun run;
    run.status = text_at(document, "status");
    run.output = text_at(document, "output");
    run.sidecar = text_at(document, "sidecar");
    run.model = text_at(*model, "name");
    run.model_path = text_at(*model, "path");
    run.pipeline_class = text_at(*model, "class");
    run.model_ref = text_at(*model, "ref");
    run.model_revision = text_at(*model, "revision");
    run.variant = text_at(*model, "variant");
    run.prompt = text_at(document, "prompt");
    run.image = text_at(document, "image");
    run.mode = text_at(document, "mode");
    run.seed = seed->get<std::int64_t>();
    run.seed_source = text_at(document, "seed_source");
    const auto number = [&settings](const char* key) -> std::optional<double> {
        const auto found = settings->find(key);
        return found != settings->end() && found->is_number() ? std::optional{found->get<double>()}
                                                              : std::nullopt;
    };
    run.settings.width = static_cast<int>(number("width").value_or(0));
    run.settings.height = static_cast<int>(number("height").value_or(0));
    run.settings.duration = number("duration").value_or(0.0);
    run.settings.fps = static_cast<int>(number("fps").value_or(0));
    run.settings.frames = static_cast<int>(number("frames").value_or(0));
    run.settings.steps = static_cast<int>(number("steps").value_or(0));
    run.settings.guidance = number("guidance");
    if (const auto device = document.find("device");
        device != document.end() && device->is_object()) {
        run.device = text_at(*device, "device");
        run.dtype = text_at(*device, "dtype");
        run.platform = text_at(*device, "platform");
    }
    if (const auto versions = document.find("versions");
        versions != document.end() && versions->is_object()) {
        run.torch = text_at(*versions, "torch");
        run.diffusers = text_at(*versions, "diffusers");
        run.apogee = text_at(*versions, "apogee");
    }
    run.codec = text_at(document, "codec");
    if (const auto seconds = document.find("seconds");
        seconds != document.end() && seconds->is_number()) {
        run.seconds = seconds->get<double>();
    }
    run.created_at = text_at(document, "created_at");
    run.error = text_at(document, "error");
    if (const auto parent = document.find("parent");
        parent != document.end() && parent->is_object()) {
        run.parent.move = text_at(*parent, "move");
        run.parent.document = text_at(*parent, "document");
        run.parent.clip = text_at(*parent, "clip");
        run.parent.continued = text_at(*parent, "continued");
    }
    run.preset = text_at(document, "preset");
    return run;
}

std::optional<VideoRun> read_video_run(const std::filesystem::path& path, std::string& error) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        error = "cannot read " + path.string();
        return std::nullopt;
    }
    const nlohmann::json document = nlohmann::json::parse(in, nullptr, false);
    if (document.is_discarded()) {
        error = path.string() + " is not JSON";
        return std::nullopt;
    }
    std::string why;
    std::optional<VideoRun> run = parse_video_run(document, why);
    if (!run.has_value()) {
        error = path.string() + " is " + why;
    }
    return run;
}

std::filesystem::path video_sidecar_path(const std::filesystem::path& clip) {
    return std::filesystem::path{clip.string() + ".json"};
}

}  // namespace apogee::harness
