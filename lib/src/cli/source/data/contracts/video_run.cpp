#include "contracts/video_run.h"

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
    return document;
}

std::filesystem::path video_sidecar_path(const std::filesystem::path& clip) {
    return std::filesystem::path{clip.string() + ".json"};
}

}  // namespace apogee::harness
