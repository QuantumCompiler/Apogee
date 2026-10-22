#include "video/runtime.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <system_error>
#include <vector>

#include "contracts/assets.h"
#include "contracts/paths.h"
#include "modelstore/mlx_info.h"
#include "modelstore/pipeline_info.h"
#include "modelstore/snapshot.h"
#include "modelstore/voice_info.h"
#include "platform/ffmpeg.h"
#include "platform/platform.h"
#include "training/mlx_convert.h"
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

/// `--engine`'s words, `auto` first.
constexpr std::array<std::string_view, 3> kEngineNames{"auto", "torch", "mlx"};

/// The pull a Wan set stored under `model` -- `owner--repo`, as the store
/// names a Hugging Face repository -- comes back by.
[[nodiscard]] std::string pull_of(std::string_view model) {
    const std::size_t separator = model.find("--");
    if (separator == std::string_view::npos) {
        return "apogee models pull <owner>/<repo> --safetensors";
    }
    return "apogee models pull " + std::string{model.substr(0, separator)} + "/" +
           std::string{model.substr(separator + 2)} + " --safetensors";
}

}  // namespace

std::string_view to_string(VideoEngine engine) noexcept {
    return engine == VideoEngine::Mlx ? "mlx" : "torch";
}

std::optional<VideoEngine> video_engine_from_string(std::string_view text) noexcept {
    if (text == "torch") {
        return VideoEngine::Torch;
    }
    if (text == "mlx") {
        return VideoEngine::Mlx;
    }
    return std::nullopt;
}

std::span<const std::string_view> video_engine_names() noexcept {
    return kEngineNames;
}

std::optional<VideoEngine> engine_of_directory(const std::filesystem::path& dir) {
    if (models::is_pipeline_dir(dir)) {
        return VideoEngine::Torch;
    }
    std::error_code code;
    if (std::filesystem::is_directory(dir, code) &&
        models::read_mlx_kind(dir).word == models::kMlxVideo) {
        return VideoEngine::Mlx;
    }
    return std::nullopt;
}

std::string choose_video_engine(std::string_view asked, VideoEngine directory,
                                std::string_view name, VideoEngine& out) {
    if (asked.empty() || asked == "auto") {
        out = directory;
        return {};
    }
    const std::optional<VideoEngine> engine = video_engine_from_string(asked);
    if (!engine.has_value()) {
        return "--engine takes auto, torch or mlx (got '" + std::string{asked} + "')";
    }
    if (*engine == directory) {
        out = directory;
        return {};
    }
    // The model says its engine: the other one is refused, saying how a
    // model for it is had.
    if (directory == VideoEngine::Mlx) {
        return "'" + std::string{name} +
               "' is an MLX video model, which runs on MLX (mlx-video), not PyTorch -- PyTorch "
               "runs a diffusers pipeline: 'apogee models pull <owner>/<repo>' stores one "
               "(Wan-AI's are its repositories ending -Diffusers), and 'apogee train setup "
               "--with video' installs what runs it";
    }
    return "'" + std::string{name} +
           "' is a diffusers pipeline, which runs on PyTorch, not MLX -- MLX runs an MLX video "
           "model: Wan's original layout converted with 'apogee models convert <set> --mlx', "
           "after '" +
           std::string{kMlxVideoSetupRemedy} + "'";
}

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
        case VideoRefusal::NotAppleSilicon:
            return "not apple silicon";
        case VideoRefusal::NoMlxVideo:
            return "no mlx-video";
        case VideoRefusal::NoTokenizer:
            return "no tokenizer";
        case VideoRefusal::NoMlxAudio:
            return "no mlx-audio";
    }
    return "unknown";
}

VideoHost VideoHost::at(const std::filesystem::path& home) {
    VideoHost host;
    host.venv = home / "training" / "venv";
    host.driver = home / harness::bundled_video_driver_relative_path();
    host.voice_driver = home / harness::bundled_voice_driver_relative_path();
    host.ffmpeg = platform::ffmpeg_path();
    host.mlx_driver = home / harness::bundled_video_mlx_driver_relative_path();
    host.target = platform::host_target();
    host.roots = models::StoreRoots::at(home / "models");
    host.voice_mlx_driver = home / harness::bundled_voice_mlx_driver_relative_path();
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

std::filesystem::path find_mlx_video_tokenizer(const models::StoreRoots& roots,
                                               const std::filesystem::path& dir,
                                               std::string& reason, std::string& remedy) {
    const std::string wanted = "mlx-video's Wan generator reads the T5 tokenizer (" +
                               std::string{kMlxVideoTokenizer} + ")";
    const std::optional<models::Snapshot> record = models::load_snapshot(dir);
    if (!record.has_value() || record->ref.empty()) {
        reason = wanted + ", which " + dir.string() +
                 " does not carry, and its record names no Wan set it was converted from";
        remedy = "convert its Wan set again: apogee models convert <set> --mlx";
        return {};
    }
    // The set it was converted from, by the handle its record names -- or,
    // converted from a path outside the store, that path.
    std::filesystem::path source{record->ref};
    std::string model;
    if (const std::optional<models::WeightsHandle> handle =
            models::parse_weights_handle(record->ref)) {
        source = models::find_weights_dir(roots, handle->format, handle->model, handle->id);
        model = handle->model;
    }
    std::error_code code;
    if (!std::filesystem::is_directory(source, code)) {
        reason = wanted + " from the Wan set it was converted from, " + record->ref +
                 ", which is not in the store";
        remedy = pull_of(model);
        return {};
    }
    const std::filesystem::path tokenizer = source / std::string{kMlxVideoTokenizer};
    // Every file the set's record lists under it, there -- else what the
    // tokenizer is read from at all.
    std::vector<std::string> missing;
    bool listed = false;
    const std::optional<models::Snapshot> stored = models::load_snapshot(source);
    if (stored.has_value()) {
        const std::string prefix = std::string{kMlxVideoTokenizer} + "/";
        for (const models::SnapshotFile& file : stored->files) {
            if (file.path.starts_with(prefix)) {
                listed = true;
                if (!std::filesystem::is_regular_file(source / file.path, code)) {
                    missing.push_back(file.path);
                }
            }
        }
    }
    if (missing.empty() &&
        !std::filesystem::is_regular_file(tokenizer / "tokenizer_config.json", code)) {
        missing.push_back(std::string{kMlxVideoTokenizer} + "/tokenizer_config.json");
    }
    if (missing.empty() && !std::filesystem::is_regular_file(tokenizer / "tokenizer.json", code) &&
        !std::filesystem::is_regular_file(tokenizer / "spiece.model", code)) {
        missing.push_back(std::string{kMlxVideoTokenizer} + "/tokenizer.json");
    }
    if (!missing.empty()) {
        std::string names;
        for (const std::string& name : missing) {
            names += (names.empty() ? "" : ", ") + name;
        }
        reason = wanted + " from the Wan set it was converted from, " + record->ref +
                 ", which lacks " + names;
        remedy = listed ? "apogee models repair " + record->ref
                        : (stored.has_value() && !stored->ref.empty()
                               ? "apogee models pull " + stored->ref + " --safetensors"
                               : pull_of(model));
        return {};
    }
    return tokenizer;
}

std::string mlx_video_mode(const std::filesystem::path& dir) {
    std::ifstream in{dir / "config.json", std::ios::binary};
    const nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
    if (!config.is_object()) {
        return {};
    }
    const auto found = config.find("model_type");
    return found != config.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

bool mlx_video_frames(int frames) noexcept {
    return frames >= 1 && (frames - 1) % 4 == 0;
}

VideoReadiness probe_video_mlx(const VideoHost& host, const std::filesystem::path& dir,
                               std::string_view handle) {
    VideoReadiness readiness;
    readiness.engine = VideoEngine::Mlx;
    if (host.target != kVideoMlxTarget) {
        return refused(std::move(readiness), VideoRefusal::NotAppleSilicon,
                       "MLX runs on Apple silicon alone, and this is " +
                           (host.target.empty() ? std::string{"another host"} : host.target),
                       "a diffusers pipeline runs here on PyTorch: apogee models pull "
                       "<owner>/<repo>");
    }
    const training::PythonEnv env{host.venv};
    if (!env.exists()) {
        return refused(std::move(readiness), VideoRefusal::NoEnvironment,
                       "there is no Python environment at " + host.venv.string() +
                           " -- video generation on MLX runs mlx-video there, never the system "
                           "Python",
                       std::string{kMlxVideoSetupRemedy});
    }
    readiness.interpreter = env.interpreter();
    if (training::find_mlx_video_generator(host.venv).empty()) {
        return refused(std::move(readiness), VideoRefusal::NoMlxVideo,
                       "mlx-video is not installed in the Python environment at " +
                           host.venv.string() + " (its Wan generator, " +
                           std::string{training::kMlxVideoGenerator} +
                           ", and the torch its set brings, read from their files)",
                       std::string{kMlxVideoSetupRemedy});
    }
    if (const std::filesystem::path package = training::find_site_package(host.venv, "mlx_video");
        !package.empty()) {
        readiness.mlx_video_version = training::installed_package_version(package);
    }
    if (const std::filesystem::path package = training::find_site_package(host.venv, "mlx");
        !package.empty()) {
        readiness.mlx_version = training::installed_package_version(package);
    }

    if (!dir.empty()) {
        const std::string name = handle.empty() ? dir.string() : std::string{handle};
        std::error_code code;
        if (!std::filesystem::is_directory(dir, code)) {
            return refused(std::move(readiness), VideoRefusal::NoPipeline,
                           "there is no MLX video model at " + dir.string(),
                           "apogee models convert <a Wan set> --mlx (mlx-video)");
        }
        const models::MlxKind kind = models::read_mlx_kind(dir);
        if (kind.word != models::kMlxVideo) {
            return refused(std::move(readiness), VideoRefusal::NotAPipeline,
                           name + " is an MLX " + kind.word + " model, not a video model -- " +
                               models::describe_mlx_kind(kind.word),
                           {});
        }
        const models::MlxInfo info = models::read_mlx_info(dir, models::kMlxVideo);
        if (!info.complete) {
            const std::optional<models::Snapshot> record = models::load_snapshot(dir);
            return refused(std::move(readiness), VideoRefusal::NotAPipeline,
                           name + " " + info.problem,
                           record.has_value() && !record->ref.empty() && !handle.empty()
                               ? "convert it again: apogee models delete " + std::string{handle} +
                                     ", then apogee models convert " + record->ref + " --mlx"
                               : std::string{"convert its Wan set again with --mlx"});
        }
        std::string reason;
        std::string remedy;
        readiness.tokenizer = find_mlx_video_tokenizer(host.roots, dir, reason, remedy);
        if (readiness.tokenizer.empty()) {
            return refused(std::move(readiness), VideoRefusal::NoTokenizer, std::move(reason),
                           std::move(remedy));
        }
    }

    std::error_code code;
    if (!std::filesystem::is_regular_file(host.mlx_driver, code)) {
        return refused(std::move(readiness), VideoRefusal::NoDriver,
                       "the MLX video driver is missing: " + host.mlx_driver.string(),
                       "apogee check --fix");
    }
    readiness.driver = host.mlx_driver;
    if (host.ffmpeg.empty()) {
        return refused(std::move(readiness), VideoRefusal::NoFfmpeg,
                       "the frames are assembled into an mp4 with ffmpeg, which is not on PATH",
                       "install ffmpeg");
    }
    readiness.ffmpeg = host.ffmpeg;
    return readiness;
}

VideoReadiness probe_video_engine(const VideoHost& host, VideoEngine engine,
                                  const std::filesystem::path& dir, std::string_view handle) {
    return engine == VideoEngine::Mlx ? probe_video_mlx(host, dir, handle)
                                      : probe_video(host, dir, handle);
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

VideoReadiness probe_voice_mlx(const VideoHost& host, const std::filesystem::path& model,
                               std::string_view handle) {
    VideoReadiness readiness;
    readiness.engine = VideoEngine::Mlx;
    if (host.target != kVideoMlxTarget) {
        return refused(std::move(readiness), VideoRefusal::NotAppleSilicon,
                       "MLX runs on Apple silicon alone, and this is " +
                           (host.target.empty() ? std::string{"another host"} : host.target),
                       "a voice model as Kokoro publishes it speaks here on PyTorch: apogee "
                       "models pull hexgrad/Kokoro-82M");
    }
    const training::PythonEnv env{host.venv};
    if (!env.exists()) {
        return refused(std::move(readiness), VideoRefusal::NoEnvironment,
                       "there is no Python environment at " + host.venv.string() +
                           " -- speech on MLX runs mlx-audio there, never the system Python",
                       std::string{kMlxAudioSetupRemedy});
    }
    readiness.interpreter = env.interpreter();
    if (training::find_mlx_audio_kokoro(host.venv).empty()) {
        return refused(std::move(readiness), VideoRefusal::NoMlxAudio,
                       "mlx-audio is not installed in the Python environment at " +
                           host.venv.string() + " (its Kokoro, " +
                           std::string{training::kMlxAudioKokoro} +
                           ", and the misaki it reads text through, read from their files)",
                       std::string{kMlxAudioSetupRemedy});
    }
    if (const std::filesystem::path package = training::find_site_package(host.venv, "mlx_audio");
        !package.empty()) {
        readiness.mlx_audio_version = training::installed_package_version(package);
    }
    if (const std::filesystem::path package = training::find_site_package(host.venv, "mlx");
        !package.empty()) {
        readiness.mlx_version = training::installed_package_version(package);
    }

    if (!model.empty()) {
        const std::string name = handle.empty() ? model.string() : std::string{handle};
        std::error_code code;
        if (!std::filesystem::is_directory(model, code)) {
            return refused(std::move(readiness), VideoRefusal::NoVoiceModel,
                           "there is no MLX voice model at " + model.string(),
                           "apogee models convert <a voice model> --mlx (mlx-audio)");
        }
        const models::MlxKind kind = models::read_mlx_kind(model);
        if (kind.word != models::kMlxVoice) {
            return refused(std::move(readiness), VideoRefusal::NotAVoiceModel,
                           name + " is an MLX " + kind.word + " model, not a voice model -- " +
                               models::describe_mlx_kind(kind.word),
                           {});
        }
        const models::MlxInfo info = models::read_mlx_info(model, models::kMlxVoice);
        if (!info.complete) {
            const std::optional<models::Snapshot> record = models::load_snapshot(model);
            return refused(std::move(readiness), VideoRefusal::NotAVoiceModel,
                           name + " " + info.problem,
                           record.has_value() && !record->ref.empty() && !handle.empty()
                               ? "convert it again: apogee models delete " + std::string{handle} +
                                     ", then apogee models convert " + record->ref + " --mlx"
                               : std::string{"convert its voice model again with --mlx"});
        }
    }

    std::error_code code;
    if (!std::filesystem::is_regular_file(host.voice_mlx_driver, code)) {
        return refused(std::move(readiness), VideoRefusal::NoDriver,
                       "the MLX voice driver is missing: " + host.voice_mlx_driver.string(),
                       "apogee check --fix");
    }
    readiness.driver = host.voice_mlx_driver;
    return readiness;
}

VideoReadiness probe_voice_engine(const VideoHost& host, VideoEngine engine,
                                  const std::filesystem::path& model, std::string_view handle) {
    return engine == VideoEngine::Mlx ? probe_voice_mlx(host, model, handle)
                                      : probe_voice(host, model, handle);
}

}  // namespace apogee::video
