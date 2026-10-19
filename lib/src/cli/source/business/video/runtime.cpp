#include "video/runtime.h"

#include <system_error>

#include "contracts/assets.h"
#include "contracts/paths.h"
#include "modelstore/pipeline_info.h"
#include "platform/ffmpeg.h"
#include "training/python_env.h"

namespace apogee::video {
namespace {

[[nodiscard]] VideoReadiness refused(VideoReadiness readiness, VideoRefusal refusal,
                                     std::string reason, std::string remedy) {
    readiness.refusal = refusal;
    readiness.reason = std::move(reason);
    readiness.remedy = std::move(remedy);
    return readiness;
}

}  // namespace

std::string_view to_string(VideoRefusal refusal) noexcept {
    switch (refusal) {
        case VideoRefusal::None:
            return "ready";
        case VideoRefusal::NoEnvironment:
            return "no environment";
        case VideoRefusal::NoTorch:
            return "no torch";
        case VideoRefusal::NoDiffusers:
            return "no diffusers";
        case VideoRefusal::NoPipeline:
            return "no pipeline";
        case VideoRefusal::NotAPipeline:
            return "cannot load";
        case VideoRefusal::NoDriver:
            return "no driver";
        case VideoRefusal::NoFfmpeg:
            return "no ffmpeg";
    }
    return "unknown";
}

VideoHost VideoHost::at(const std::filesystem::path& home) {
    VideoHost host;
    host.venv = home / "training" / "venv";
    host.driver = home / harness::bundled_video_driver_relative_path();
    host.ffmpeg = platform::ffmpeg_path();
    return host;
}

VideoHost VideoHost::current() {
    return at(harness::apogee_home());
}

std::string VideoReadiness::message() const {
    return remedy.empty() ? reason : reason + " -- " + remedy;
}

VideoReadiness probe_video(const VideoHost& host, const std::filesystem::path& pipeline,
                           std::string_view handle) {
    VideoReadiness readiness;
    const training::PythonEnv env{host.venv};
    if (!env.exists()) {
        return refused(std::move(readiness), VideoRefusal::NoEnvironment,
                       "there is no Python environment at " + host.venv.string() +
                           " -- video generation runs diffusers there, never the system Python",
                       std::string{kVideoSetupRemedy});
    }
    readiness.interpreter = env.interpreter();
    const std::filesystem::path torch = training::find_site_package(host.venv, "torch");
    if (torch.empty()) {
        return refused(std::move(readiness), VideoRefusal::NoTorch,
                       "torch is not installed in the Python environment at " + host.venv.string(),
                       std::string{kVideoSetupRemedy});
    }
    readiness.torch_version = training::installed_package_version(torch);
    const std::filesystem::path diffusers = training::find_site_package(host.venv, "diffusers");
    if (diffusers.empty()) {
        return refused(
            std::move(readiness), VideoRefusal::NoDiffusers,
            "diffusers is not installed in the Python environment at " + host.venv.string(),
            std::string{kVideoSetupRemedy});
    }
    readiness.diffusers_version = training::installed_package_version(diffusers);

    if (!pipeline.empty()) {
        const std::string name = handle.empty() ? pipeline.string() : std::string{handle};
        std::error_code code;
        if (!std::filesystem::is_directory(pipeline, code)) {
            return refused(std::move(readiness), VideoRefusal::NoPipeline,
                           "there is no pipeline at " + pipeline.string(),
                           "apogee models pull <owner>/<repo> (a diffusers repository)");
        }
        const models::PipelineInfo info = models::read_pipeline_info(pipeline);
        if (!info.complete) {
            return refused(std::move(readiness), VideoRefusal::NotAPipeline,
                           name + " " + info.problem,
                           handle.empty() ? std::string{"pull it again"}
                                          : "apogee models repair " + std::string{handle});
        }
    }

    std::error_code code;
    if (!std::filesystem::is_regular_file(host.driver, code)) {
        return refused(std::move(readiness), VideoRefusal::NoDriver,
                       "the video driver is missing: " + host.driver.string(),
                       "apogee check --fix");
    }
    readiness.driver = host.driver;
    if (host.ffmpeg.empty()) {
        return refused(std::move(readiness), VideoRefusal::NoFfmpeg,
                       "the frames are assembled into an mp4 with ffmpeg, which is not on PATH",
                       "install ffmpeg");
    }
    readiness.ffmpeg = host.ffmpeg;
    return readiness;
}

}  // namespace apogee::video
