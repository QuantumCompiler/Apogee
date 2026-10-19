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

/// Where a run came from, when it came from another (49d): `from` -- a
/// record run again, its settings taken and the flags' changes over them --
/// or `extend` -- a clip continued from its last frame. Each record names
/// its parent, so a final render traces back to its drafts.
struct VideoParent {
    /// `from` or `extend`; empty for a run of its own.
    std::string move{};
    /// The record read (`from`), or the continued clip's record when it had
    /// one (`extend`).
    std::filesystem::path document{};
    /// The clip whose record was read, or the clip continued.
    std::filesystem::path clip{};
    /// The clip whose last frame this run started from -- `extend`'s clip,
    /// or the one a record run again had continued -- so running it again
    /// continues the same clip. Empty for none.
    std::filesystem::path continued{};
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
    /// `given` (`--seed`), `held` (a record's, run again -- 49d) or
    /// `generated`.
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
    /// Where it came from (49d), and the preset it ran at (`draft`,
    /// `final`), or empty.
    VideoParent parent{};
    std::string preset{};
};

/// The `video.generation` document.
[[nodiscard]] nlohmann::json video_run_document(const VideoRun& run);

/// `document` read back into a record -- what `--from` runs again (49d).
/// Nullopt, with `error` saying why, for anything that is not a
/// `video.generation` record holding its model, seed and settings.
[[nodiscard]] std::optional<VideoRun> parse_video_run(const nlohmann::json& document,
                                                      std::string& error);

/// The record at `path`, read and parsed; nullopt with `error` naming the
/// file and why.
[[nodiscard]] std::optional<VideoRun> read_video_run(const std::filesystem::path& path,
                                                     std::string& error);

/// `<clip>.json`: where a clip's record sits -- beside it, its name the
/// clip's own with `.json` after, so it never takes a name the clip's
/// folder already gives something else.
[[nodiscard]] std::filesystem::path video_sidecar_path(const std::filesystem::path& clip);

}  // namespace apogee::harness
