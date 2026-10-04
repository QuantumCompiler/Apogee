#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Running `ffmpeg` and `ffprobe` for media attachments (26e).
///
/// Audio and video reach a model as samples and frames, and `ffmpeg` -- found
/// on `PATH`, never bundled (the user's call) -- is what decodes them. Apogee
/// runs it itself, through `platform::start_child`, so its stderr is a pipe
/// Apogee owns: drained, kept as a bounded tail for an error message, never
/// the terminal's. mtmd's own video helper would spawn it from code whose
/// output Apogee does not own, which is why it is not used.
///
/// Every run is bounded: a deadline, a cap on what it may write to stdout,
/// and a stop callback polled while it runs, so Ctrl-C ends a long decode.
namespace apogee::platform {

/// What a media file holds, as ffprobe reads it.
struct MediaInfo {
    /// Seconds; 0 when it has none (a still image).
    double duration = 0.0;
    bool video = false;
    bool audio = false;
    int width = 0;
    int height = 0;
};

/// Bounds on one run.
struct FfmpegLimits {
    std::chrono::milliseconds timeout{std::chrono::minutes{10}};
    /// The most stdout a run may produce; past it the child is stopped and the
    /// run fails, rather than holding an unbounded decode in memory.
    std::uint64_t max_output = std::uint64_t{256} * 1024 * 1024;
    /// Polled while it runs; true stops the child.
    std::function<bool()> stopped;
};

/// The `ffmpeg` and `ffprobe` on `PATH`, or empty.
[[nodiscard]] std::string ffmpeg_path();
[[nodiscard]] std::string ffprobe_path();

/// What `file` holds, or nullopt with `error` filled.
[[nodiscard]] std::optional<MediaInfo> probe_media(const std::filesystem::path& file,
                                                   const FfmpegLimits& limits, std::string& error);

/// Which frames to take from a video.
struct FrameOptions {
    /// A frame at most this many seconds after the last one taken.
    double every = 5.0;
    /// A scene change scoring over this takes one too; 0 for none.
    double scene = 0.3;
    /// Above 0, a steady rate instead: this many frames a second.
    double fps = 0.0;
    /// The longer side of a frame, at most; a smaller frame is kept as it is.
    int max_side = 1280;
    /// The most frames written; 0 for no cap.
    std::size_t max_frames = 0;
    /// Where to start and how much to read, in seconds; 0 for all of it.
    double start = 0.0;
    double length = 0.0;
};

/// One frame written to disk, and its time in the video.
struct ExtractedFrame {
    double seconds = 0.0;
    std::filesystem::path file;
};

/// Writes `file`'s frames as JPEGs into `directory`, in time order, each with
/// its timestamp (from ffmpeg's `showinfo`). Empty with `error` filled on
/// failure.
[[nodiscard]] std::vector<ExtractedFrame> extract_frames(const std::filesystem::path& file,
                                                         const FrameOptions& options,
                                                         const std::filesystem::path& directory,
                                                         const FfmpegLimits& limits,
                                                         std::string& error);

/// `length` seconds of `file`'s sound from `start` -- to its end when `length`
/// is 0 -- mixed to mono at `rate` Hz, as a WAV file's bytes. Empty with
/// `error` filled on failure; a span with no sound in it is empty with none.
[[nodiscard]] std::string decode_audio_wav(const std::filesystem::path& file, int rate,
                                           double start, double length, const FfmpegLimits& limits,
                                           std::string& error);

/// A WAV file holding 16-bit mono `pcm` at `rate` Hz.
[[nodiscard]] std::string wav_file(std::string_view pcm, int rate);

/// The `pts_time` of each `showinfo` line in `log`, in order.
[[nodiscard]] std::vector<double> showinfo_times(std::string_view log);

}  // namespace apogee::platform
