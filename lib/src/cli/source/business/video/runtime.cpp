#include "video/runtime.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <system_error>
#include <vector>

#include "contracts/assets.h"
#include "contracts/paths.h"
#include "modelstore/pipeline_info.h"
#include "modelstore/voice_info.h"
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

/// A package a runtime needs, and the rung its absence is.
struct Package {
    std::string_view name;
    VideoRefusal missing;
};

/// The rungs every runtime shares (49b, 50a): the environment, then each
/// package in turn, read from files -- refused at the first that is not
/// there, naming `remedy`. True when all are; their versions recorded.
[[nodiscard]] bool probe_environment(const VideoHost& host, std::string_view runs,
                                     std::initializer_list<Package> packages,
                                     std::string_view remedy, VideoReadiness& readiness) {
    const training::PythonEnv env{host.venv};
    if (!env.exists()) {
        readiness = refused(std::move(readiness), VideoRefusal::NoEnvironment,
                            "there is no Python environment at " + host.venv.string() + " -- " +
                                std::string{runs} + " there, never the system Python",
                            std::string{remedy});
        return false;
    }
    readiness.interpreter = env.interpreter();
    for (const Package& package : packages) {
        const std::filesystem::path found = training::find_site_package(host.venv, package.name);
        if (found.empty()) {
            readiness =
                refused(std::move(readiness), package.missing,
                        std::string{package.name} +
                            " is not installed in the Python environment at " + host.venv.string(),
                        std::string{remedy});
            return false;
        }
        const std::string version = training::installed_package_version(found);
        if (package.name == "torch") {
            readiness.torch_version = version;
        } else if (package.name == "diffusers") {
            readiness.diffusers_version = version;
        } else if (package.name == "kokoro") {
            readiness.kokoro_version = version;
        }
    }
    return true;
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
        case VideoRefusal::NoKokoro:
            return "no kokoro";
        case VideoRefusal::NoVoiceModel:
            return "no voice model";
        case VideoRefusal::NotAVoiceModel:
            return "cannot load";
    }
    return "unknown";
}

VideoHost VideoHost::at(const std::filesystem::path& home) {
    VideoHost host;
    host.venv = home / "training" / "venv";
    host.driver = home / harness::bundled_video_driver_relative_path();
    host.voice_driver = home / harness::bundled_voice_driver_relative_path();
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
    if (!probe_environment(
            host, "video generation runs diffusers",
            {{"torch", VideoRefusal::NoTorch}, {"diffusers", VideoRefusal::NoDiffusers}},
            kVideoSetupRemedy, readiness)) {
        return readiness;
    }

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

    // What a LoRA loads through: recorded, never a rung.
    if (const std::filesystem::path peft = training::find_site_package(host.venv, "peft");
        !peft.empty()) {
        readiness.peft = true;
        readiness.peft_version = training::installed_package_version(peft);
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

std::string lora_file(const std::filesystem::path& given, std::filesystem::path& out) {
    std::error_code code;
    const auto safetensors = [](const std::filesystem::path& path) {
        std::string extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".safetensors";
    };
    if (std::filesystem::is_regular_file(given, code)) {
        if (!safetensors(given)) {
            return "the LoRA " + given.string() +
                   " is not a .safetensors file -- a LoRA is applied from its safetensors";
        }
        out = given;
        return {};
    }
    if (std::filesystem::is_directory(given, code)) {
        std::vector<std::filesystem::path> found;
        for (const auto& entry : std::filesystem::directory_iterator(given, code)) {
            std::error_code type;
            if (entry.is_regular_file(type) && safetensors(entry.path())) {
                found.push_back(entry.path());
            }
        }
        if (found.empty()) {
            return "the LoRA directory " + given.string() + " holds no .safetensors file";
        }
        if (found.size() > 1) {
            std::ranges::sort(found);
            std::string names;
            for (const std::filesystem::path& file : found) {
                names += (names.empty() ? "" : ", ") + file.filename().string();
            }
            return "the LoRA directory " + given.string() + " holds " +
                   std::to_string(found.size()) + " .safetensors files (" + names + ") -- name one";
        }
        out = found.front();
        return {};
    }
    return "there is no LoRA at " + given.string();
}

VideoReadiness probe_voice(const VideoHost& host, const std::filesystem::path& model,
                           std::string_view handle) {
    VideoReadiness readiness;
    if (!probe_environment(host, "speech runs kokoro",
                           {{"torch", VideoRefusal::NoTorch}, {"kokoro", VideoRefusal::NoKokoro}},
                           kVoiceSetupRemedy, readiness)) {
        return readiness;
    }

    if (!model.empty()) {
        const std::string name = handle.empty() ? model.string() : std::string{handle};
        std::error_code code;
        if (!std::filesystem::is_directory(model, code)) {
            return refused(std::move(readiness), VideoRefusal::NoVoiceModel,
                           "there is no voice model at " + model.string(),
                           "apogee models pull hexgrad/Kokoro-82M");
        }
        const models::VoiceInfo info = models::read_voice_info(model);
        if (!info.complete) {
            return refused(std::move(readiness), VideoRefusal::NotAVoiceModel,
                           name + " " + info.problem,
                           handle.empty() ? std::string{"pull it again"}
                                          : "apogee models repair " + std::string{handle});
        }
    }

    std::error_code code;
    if (!std::filesystem::is_regular_file(host.voice_driver, code)) {
        return refused(std::move(readiness), VideoRefusal::NoDriver,
                       "the voice driver is missing: " + host.voice_driver.string(),
                       "apogee check --fix");
    }
    readiness.driver = host.voice_driver;
    return readiness;
}

}  // namespace apogee::video
