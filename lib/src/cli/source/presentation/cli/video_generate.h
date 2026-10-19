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
};

/// The settings `options` come to, the defaults filling what was not given
/// -- or the refusal, quoting the cap it met.
[[nodiscard]] std::string resolve_video_settings(const VideoGenerateOptions& options,
                                                 harness::VideoSettings& out);

/// `duration` seconds at `fps`, as frames a pipeline takes: the nearest
/// `4k+1` (the video VAEs compress time four frames to one), at least 5.
[[nodiscard]] int video_frames(double duration, int fps);

/// The pipeline a run uses, resolved.
struct ResolvedPipeline {
    /// The name it was given by: a store handle (`<model>/pipeline/<id>`),
    /// or the path a pipeline outside the store was named by.
    std::string handle;
    std::filesystem::path dir;
    /// `--model`, or `models.default_video`.
    std::string source;
    /// From its record and its index.
    std::string ref{};
    std::string revision{};
    std::string pipeline_class{};
    std::string variant{};
};

/// `given` (`--model`), else `pointer` (`models.default_video`), as one
/// pipeline: a handle, a bare id, a model holding exactly one pipeline, or
/// a path to a pipeline's directory. The refusal names what to do instead;
/// empty `error` means `out` is set.
[[nodiscard]] std::string resolve_video_pipeline(const models::StoreRoots& roots,
                                                 std::string_view given, std::string_view pointer,
                                                 ResolvedPipeline& out);

/// What a run needs from outside.
struct VideoGenerateSeams {
    /// The ladder for the resolved pipeline -- in the command, the consent
    /// to install the runtime first (`ready_video_runtime`).
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
/// seed image is missing, no pipeline resolves or the ladder refuses; else
/// one generation, its record written beside the clip on success.
[[nodiscard]] VideoGenerateResult generate_clip(const harness::Config& config,
                                                const models::StoreRoots& roots,
                                                const VideoGenerateOptions& options,
                                                const VideoGenerateSeams& seams,
                                                const harness::CancellationToken& cancellation);

/// The line the narration says for `event` -- a step is the caller's
/// progress, so none for it.
[[nodiscard]] std::optional<std::string> narrate_video_event(const video::VideoEvent& event);

}  // namespace apogee::commands
