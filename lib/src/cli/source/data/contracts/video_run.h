#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

/// A video generation's record (49c): what made one clip -- the pipeline,
/// the prompt, every setting, the seed, the device and the versions that
/// ran, the time it took -- as the `video.generation` document
/// `apogee video generate --output-format json` prints and, for a clip that
/// landed, the sidecar written beside it (`<clip>.json`).
///
/// **The reproducible record.** The same record on the same machine with
/// the same versions makes the same clip; across devices it is not promised,
/// and the record names its device so nobody has to guess. What
/// `--from` (49d) reads to make the next run.
namespace apogee::harness {

/// What the generation's settings were, as they ran.
struct VideoSettings {
    int width = 0;
    int height = 0;
    double duration = 0.0;
    int fps = 0;
    /// What `duration` at `fps` came to, rounded to the pipelines' `4k+1`.
    int frames = 0;
    int steps = 0;
    std::optional<double> guidance{};
};

struct VideoRun {
    /// `done`, `failed` or `cancelled`.
    std::string status;
    /// The clip, absolute; and its sidecar, when one was written.
    std::filesystem::path output;
    std::filesystem::path sidecar{};
    /// The pipeline: its store handle (or path), its directory, the class
    /// its index names, and where it came from.
    std::string model;
    std::filesystem::path model_path;
    std::string pipeline_class{};
    std::string model_ref{};
    std::string model_revision{};
    std::string variant{};
    std::string prompt;
    /// The seed image, for image-to-video; empty for text-to-video.
    std::filesystem::path image{};
    /// `t2v` or `i2v`.
    std::string mode{};
    std::int64_t seed = 0;
    /// `given` (`--seed`) or `generated`.
    std::string seed_source{};
    VideoSettings settings{};
    /// Where it ran: torch's device and type, and this build's target.
    std::string device{};
    std::string dtype{};
    std::string platform{};
    /// The versions that ran: torch, diffusers, Apogee.
    std::string torch{};
    std::string diffusers{};
    std::string apogee{};
    std::string codec{};
    /// Seconds, the whole run.
    double seconds = 0.0;
    std::string created_at{};
    /// Why it did not land, when it did not.
    std::string error{};
};

/// The `video.generation` document.
[[nodiscard]] nlohmann::json video_run_document(const VideoRun& run);

/// `<clip>.json`: where a clip's record sits -- beside it, its name the
/// clip's own with `.json` after, so it never takes a name the clip's
/// folder already gives something else.
[[nodiscard]] std::filesystem::path video_sidecar_path(const std::filesystem::path& clip);

}  // namespace apogee::harness
