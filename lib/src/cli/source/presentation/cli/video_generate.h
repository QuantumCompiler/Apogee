#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "contracts/config.h"
#include "contracts/video_run.h"
#include "modelstore/store.h"
#include "video/generation.h"
#include "video/runtime.h"

/// `apogee video generate` composed (49c): the settings checked against the
/// caps, the pipeline resolved from `--model` or `models.default_video`, the
/// runtime made ready, one generation run, and its record -- the
/// `video.generation` document, printed under `--output-format json` and
/// written beside a clip that landed as its sidecar.
///
/// Every outside thing arrives as a seam -- the ladder (with its consent),
/// the driver's spawn and the assembly, the narration, the seed -- so the
/// command wires the real ones and a test hands in its own.
namespace apogee::commands {

// --- the caps, quoted in every refusal that meets one ----------------------------------

/// The longest clip one run makes: minutes per second of video on Apple
/// silicon, and every `4k+1` frames a pipeline denoises at once.
inline constexpr double kMaxVideoSeconds = 10.0;
/// The longest side a frame may have, and the shortest.
inline constexpr int kMaxVideoSide = 1920;
inline constexpr int kMinVideoSide = 128;
/// Each side a multiple of this: the pipelines' VAEs and patching divide
/// a frame by it.
inline constexpr int kVideoSideMultiple = 16;
/// The most denoising steps a run takes.
inline constexpr int kMaxVideoSteps = 100;
/// The frame rates a clip may be assembled at.
inline constexpr int kMinVideoFps = 4;
inline constexpr int kMaxVideoFps = 60;

/// The defaults: the reference pipeline's own (Wan 2.2 TI2V-5B) -- 1280x704,
/// five seconds at 24 frames a second (121 frames), 50 steps.
inline constexpr int kDefaultVideoWidth = 1280;
inline constexpr int kDefaultVideoHeight = 704;
inline constexpr double kDefaultVideoSeconds = 5.0;
inline constexpr int kDefaultVideoFps = 24;
inline constexpr int kDefaultVideoSteps = 50;

/// The preview loop's presets (49d), named beside the caps: a draft at a
/// few steps, the final at the defaults'. Steps alone: under one seed only
/// the same frame size and frame count draw the same starting noise, so a
/// smaller draft would be another clip rather than a preview of this one --
/// a draft that moves right becomes the final of *that* clip.
struct VideoPreset {
    std::string_view name;
    int steps = 0;
};

/// The most LoRAs one run applies, and the strengths one may have (49e).
inline constexpr std::size_t kMaxVideoLoras = 4;
inline constexpr double kMaxLoraScale = 2.0;

inline constexpr VideoPreset kDraftPreset{.name = "draft", .steps = 16};
inline constexpr VideoPreset kFinalPreset{.name = "final", .steps = kDefaultVideoSteps};

/// What `video generate` was asked for. A setting not given is empty, so a
/// run seeded from a record (49d) knows which ones the flags changed.
struct VideoGenerateOptions {
    std::string prompt;
    std::filesystem::path image{};
    /// `--model`: a stored pipeline by handle, id or model name, or a path.
    std::string model{};
    std::optional<std::int64_t> seed{};
    std::optional<double> duration{};
    /// `WIDTHxHEIGHT`.
    std::string resolution{};
    std::optional<int> steps{};
    std::optional<int> fps{};
    std::filesystem::path output{};
    /// `--from` (49d): a run's record, or a clip whose record sits beside it
    /// -- every setting the flags leave alone taken from it, the seed held.
    std::filesystem::path from{};
    /// `--extend` (49d): a clip continued from its last frame, through the
    /// image-to-video path.
    std::filesystem::path extend{};
    /// `--draft` or `--final` (49d): `draft`, `final`, or empty.
    std::string preset{};
    /// `--lora <path>[:<scale>]` (49e), as given, in order: on a run from a
    /// record, the whole set replaced. `--no-lora`: none, a record's
    /// cleared.
    std::vector<std::string> loras{};
    bool no_lora = false;
    /// `--engine` (53l): `auto`, `torch` or `mlx`, or empty when not given.
    /// It only names the engine the model's directory says, or is refused.
    std::string engine{};
};

/// `given` -- `<path>[:<scale>]` -- read: the path, and the strength after
/// its last colon when that reads as a number (1 when there is none). The
/// refusal, quoting the range, or empty.
[[nodiscard]] std::string parse_lora(std::string_view given, std::filesystem::path& path,
                                     double& scale);

/// The settings `options` come to, the defaults filling what was not given
/// -- or the refusal, quoting the cap it met.
[[nodiscard]] std::string resolve_video_settings(const VideoGenerateOptions& options,
                                                 harness::VideoSettings& out);

/// `duration` seconds at `fps`, as frames a pipeline takes: the nearest
/// `4k+1` (the video VAEs compress time four frames to one), at least 5.
[[nodiscard]] int video_frames(double duration, int fps);

/// The model a run uses, resolved: a diffusers pipeline, or since 53l an
/// MLX video model -- mlx-video's build of Wan's original layout.
struct ResolvedPipeline {
    /// The name it was given by: a store handle (`<model>/pipeline/<id>`,
    /// `<model>/mlx/<id>`), or the path a model outside the store was named
    /// by.
    std::string handle;
    std::filesystem::path dir;
    /// `--model`, or `models.default_video`.
    std::string source;
    /// From its record and its index -- for an MLX video model, its record
    /// (the set it was converted from) and `Wan <mode>` from its config.
    std::string ref{};
    std::string revision{};
    std::string pipeline_class{};
    std::string variant{};
    /// What its directory runs on (53l, `video::engine_of_directory`).
    video::VideoEngine engine = video::VideoEngine::Torch;
};

/// `given` (`--model`), else `pointer` (`models.default_video`), as one
/// model a clip runs on: a handle, a bare id, a model holding exactly one
/// pipeline or MLX video model, or a path to either's directory. The
/// refusal names what to do instead; empty `error` means `out` is set.
[[nodiscard]] std::string resolve_video_pipeline(const models::StoreRoots& roots,
                                                 std::string_view given, std::string_view pointer,
                                                 ResolvedPipeline& out);

/// What a run needs from outside.
struct VideoGenerateSeams {
    /// The ladder for the resolved model on its engine -- in the command,
    /// on PyTorch the consent to install the runtime first
    /// (`ready_video_runtime`); on MLX the ladder alone (53l).
    std::function<video::VideoReadiness(const ResolvedPipeline&)> ready;
    /// The driver's spawn and the assembly.
    video::VideoSeams run{};
    /// Each event as the run says it, for the narration.
    video::VideoEventSink on_event{};
    /// A line the run says before it starts.
    std::function<void(const std::string&)> say{};
    /// A seed when none was given.
    std::function<std::int64_t()> random_seed{};
    /// `cache/video`.
    std::filesystem::path work_root{};
    /// This build's target, for the record.
    std::string platform{};
    /// A clip's last frame written as a PNG (49d): the error, or empty.
    std::function<std::string(const std::filesystem::path& clip, const std::filesystem::path& png)>
        last_frame{};
};

struct VideoGenerateResult {
    int exit_code = 0;
    /// The `video.generation` document -- null when the run was refused
    /// before it started.
    nlohmann::json document;
    /// What the text form prints on stdout.
    std::vector<std::string> lines;
    /// The refusal or the failure, for stderr.
    std::string error;
};

/// One `video generate`, composed: refused before anything runs when a
/// setting meets a cap, the output exists or its directory does not, the
/// seed image is missing, no model resolves, an engine asked for -- by
/// `--engine`, or held from a record -- is not the model's (53l), or the
/// ladder refuses; else one generation on the model's engine, its record
/// written beside the clip on success.
[[nodiscard]] VideoGenerateResult generate_clip(const harness::Config& config,
                                                const models::StoreRoots& roots,
                                                const VideoGenerateOptions& options,
                                                const VideoGenerateSeams& seams,
                                                const harness::CancellationToken& cancellation);

/// The line the narration says for `event` -- a step is the caller's
/// progress, so none for it.
[[nodiscard]] std::optional<std::string> narrate_video_event(const video::VideoEvent& event);

}  // namespace apogee::commands
