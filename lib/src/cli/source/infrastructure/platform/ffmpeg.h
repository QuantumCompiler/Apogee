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
    /// The first video and audio streams' codecs (`h264`, `aac`), the
    /// video's frame rate and the sound's rate and channels (50b): the facts
    /// that decide whether clips may be joined by copying. Empty or 0 when
    /// ffprobe states none.
    std::string video_codec{};
    std::string audio_codec{};
    double fps = 0.0;
    int sample_rate = 0;
    int channels = 0;
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

/// `directory`'s frames -- `f00001.png`, `f00002.png`, … -- assembled into
/// the mp4 `output` at `fps` frames a second (49b): H.264 in `yuv420p`, the
/// index at the front so a player starts at once, each side rounded down to
/// an even number as the format needs; an ffmpeg built without `libx264` is
/// asked again for MPEG-4 Part 2. The codec used (`h264`, `mpeg4`), or empty
/// with `error` filled.
[[nodiscard]] std::string encode_video(const std::filesystem::path& directory, int fps,
                                       const std::filesystem::path& output,
                                       const FfmpegLimits& limits, std::string& error);

/// `file`'s last frame, written as the PNG `output` (49d): what a clip is
/// continued from. Read from the last second -- every frame there decoded,
/// each written over the one before, so the last one stays -- and from the
/// whole clip when it is shorter than that. False with `error` filled.
[[nodiscard]] bool extract_last_frame(const std::filesystem::path& file,
                                      const std::filesystem::path& output,
                                      const FfmpegLimits& limits, std::string& error);

/// `length` seconds of `file`'s sound from `start` -- to its end when `length`
/// is 0 -- mixed to mono at `rate` Hz, as a WAV file's bytes. Empty with
/// `error` filled on failure; a span with no sound in it is empty with none.
[[nodiscard]] std::string decode_audio_wav(const std::filesystem::path& file, int rate,
                                           double start, double length, const FfmpegLimits& limits,
                                           std::string& error);

/// A WAV file holding 16-bit mono `pcm` at `rate` Hz.
[[nodiscard]] std::string wav_file(std::string_view pcm, int rate);

/// A WAV file's audio (50a): its rate, channels and sample width as its
/// header states them, and its sample bytes -- what a measured duration is
/// read from.
struct WavAudio {
    int rate = 0;
    int channels = 0;
    int bits = 0;
    std::string pcm;

    /// The samples' length in seconds: what the file holds, never an
    /// estimate.
    [[nodiscard]] double seconds() const noexcept;
};

/// `file` read as a PCM WAV; nullopt with `error` filled for anything else.
[[nodiscard]] std::optional<WavAudio> read_wav(const std::filesystem::path& file,
                                               std::string& error);

/// The `pts_time` of each `showinfo` line in `log`, in order.
[[nodiscard]] std::vector<double> showinfo_times(std::string_view log);

// ── Assembly (50b) ──────────────────────────────────────────────────────────
//
// Clips, tracks and captions into one video: three operations, each a pure
// builder of its command line -- tested argv-exact, no process -- and a thin
// executor that probes its inputs and runs the builder's argv through the one
// bounded child above. Paths in, paths out: nothing here knows a scene or a
// project.

/// One input as the builders take it: its path and what ffprobe read of it.
struct ClipFacts {
    std::filesystem::path file;
    MediaInfo info;
};

/// The final render's settings: one named profile, never a knob a run.
/// H.264 at a constant quality in `yuv420p` (what every player opens), the
/// index at the front, AAC sound; an ffmpeg without `libx264` is asked again
/// for MPEG-4 Part 2, as `encode_video` asks.
struct EncodeProfile {
    std::string_view name;
    /// The encoder asked for, and the codec it writes.
    std::string_view video_codec;
    std::string_view video_format;
    std::string_view preset;
    int crf = 0;
    std::string_view fallback_codec;
    int fallback_quality = 0;
    std::string_view audio_codec;
    std::string_view audio_bitrate;
    int sample_rate = 0;
    /// The frame rate when the first clip states none.
    double fps = 0.0;
};

inline constexpr EncodeProfile kFinalRender{.name = "final",
                                            .video_codec = "libx264",
                                            .video_format = "h264",
                                            .preset = "medium",
                                            .crf = 20,
                                            .fallback_codec = "mpeg4",
                                            .fallback_quality = 2,
                                            .audio_codec = "aac",
                                            .audio_bitrate = "192k",
                                            .sample_rate = 48000,
                                            .fps = 24.0};

/// The crossfade between two clips, in seconds; 0 is a hard cut.
inline constexpr double kDefaultCrossfade = 0.5;

/// What an assembly run wrote, said -- an encode is never hidden.
struct Assembled {
    /// The video as written: `copy` when its stream was copied as it was,
    /// else the codec (`h264`, `mpeg4`).
    std::string codec;
    /// The settings in words, for the run's record.
    std::string settings;
};

/// Why `clips` cannot be joined with `crossfade`, or empty: none given, one
/// with no picture or no size, a negative crossfade, or one longer than half
/// of any clip (a clip's both ends may fade).
[[nodiscard]] std::string concat_problem(const std::vector<ClipFacts>& clips, double crossfade);

/// Whether `clips` may be joined by copying their streams: no transition to
/// draw (one clip, or a hard cut), and every clip's codec, size, frame rate
/// and sound alike -- decided by the probe's facts, never by hope.
[[nodiscard]] bool concat_can_copy(const std::vector<ClipFacts>& clips, double crossfade);

/// The concat demuxer's list for `clips`: `file '<path>'` a line, a quote in
/// a path closed, escaped and opened again.
[[nodiscard]] std::string concat_list(const std::vector<ClipFacts>& clips);

/// The filter graph that joins `clips` into `[vout]` (and `[aout]` when any
/// has sound): each picture scaled and padded into the first's even size at
/// its frame rate, each sound -- or silence as long as a clip without one --
/// at the profile's rate in stereo; then `xfade`/`acrossfade` at each
/// boundary, the offsets summed from the probed durations, or `concat` for a
/// hard cut.
[[nodiscard]] std::string concat_filter(const std::vector<ClipFacts>& clips, double crossfade,
                                        const EncodeProfile& profile = kFinalRender);

/// The re-encoding join's arguments; `fallback` asks for the profile's
/// fallback codec.
[[nodiscard]] std::vector<std::string> concat_args(const std::vector<ClipFacts>& clips,
                                                   double crossfade,
                                                   const std::filesystem::path& output,
                                                   bool fallback,
                                                   const EncodeProfile& profile = kFinalRender);

/// The copying join's arguments, reading the demuxer list at `list`.
[[nodiscard]] std::vector<std::string> concat_copy_args(const std::filesystem::path& list,
                                                        const std::filesystem::path& output);

/// `clips` joined, in order, into the mp4 `output`, `crossfade` seconds of
/// fade at each boundary: copied when `concat_can_copy` allows, else encoded
/// once by the profile. Nullopt with `error` filled, and nothing left at
/// `output`, on failure.
[[nodiscard]] std::optional<Assembled> concat_clips(const std::vector<std::filesystem::path>& clips,
                                                    double crossfade,
                                                    const std::filesystem::path& output,
                                                    const FfmpegLimits& limits, std::string& error);

/// A narration track laid `offset` seconds into the video.
struct NarrationPiece {
    std::filesystem::path file;
    double offset = 0.0;
};

/// What `mix_audio` lays under a video: narration pieces at their offsets,
/// and a music bed -- empty for none -- looped to the video's length, lowered
/// and ducked further under the narration.
struct AudioMix {
    std::vector<NarrationPiece> narration{};
    std::filesystem::path bed{};
};

/// The bed's level under the video, before any ducking.
inline constexpr double kBedVolume = 0.3;

/// The one ducking shape: the bed compressed hard while the narration speaks.
inline constexpr std::string_view kDuckFilter =
    "sidechaincompress=threshold=0.03:ratio=8:attack=20:release=400";

/// Why `mix` cannot be laid under `video`, or empty: no picture or no
/// length, nothing to mix, a piece before the start or past the end.
[[nodiscard]] std::string mix_problem(const ClipFacts& video, const AudioMix& mix);

/// The filter graph that writes `[aout]`: the video's own sound when it has
/// one, each piece delayed to its offset, and the bed ducked under them,
/// mixed at their own levels and padded to the video's length. Inputs: 0 the
/// video, 1..n the pieces, then the bed.
[[nodiscard]] std::string mix_filter(const ClipFacts& video, const AudioMix& mix,
                                     const EncodeProfile& profile = kFinalRender);

/// The mix's arguments: the picture copied as it is, the sound encoded by
/// the profile, cut at the video's length.
[[nodiscard]] std::vector<std::string> mix_args(const ClipFacts& video, const AudioMix& mix,
                                                const std::filesystem::path& output,
                                                const EncodeProfile& profile = kFinalRender);

/// `mix` laid under `video`, written as the mp4 `output`; every piece and
/// the bed probed for sound first. Nullopt with `error` filled, and nothing
/// left at `output`, on failure.
[[nodiscard]] std::optional<Assembled> mix_audio(const std::filesystem::path& video,
                                                 const AudioMix& mix,
                                                 const std::filesystem::path& output,
                                                 const FfmpegLimits& limits, std::string& error);

/// `path` as a filter option's value inside a filter graph: each character
/// the option parser reads (`\ ' :`, whitespace) escaped, then each the
/// graph parser reads (`\ ' [ ] , ;`, whitespace) escaped again.
[[nodiscard]] std::string filter_path(const std::filesystem::path& path);

/// The burn's arguments: the `subtitles` filter over the picture, encoded by
/// the profile (its fallback codec when `fallback`), the sound copied.
[[nodiscard]] std::vector<std::string> burn_args(const std::filesystem::path& video,
                                                 const std::filesystem::path& captions,
                                                 const std::filesystem::path& output, bool fallback,
                                                 const EncodeProfile& profile = kFinalRender);

/// `captions` -- an SRT file -- burned into `video`'s frames, written as the
/// mp4 `output`. An ffmpeg built without libass has no `subtitles` filter and
/// is refused by name. Nullopt with `error` filled, and nothing left at
/// `output`, on failure.
[[nodiscard]] std::optional<Assembled> burn_captions(const std::filesystem::path& video,
                                                     const std::filesystem::path& captions,
                                                     const std::filesystem::path& output,
                                                     const FfmpegLimits& limits,
                                                     std::string& error);

/// Whether `listing` -- `ffmpeg -filters` -- names the filter `name`.
[[nodiscard]] bool filter_listed(std::string_view listing, std::string_view name);

/// Whether the `ffmpeg` on PATH has the filter `name`: what a caller asks
/// before promising a burn (`subtitles` needs libass).
[[nodiscard]] bool ffmpeg_has_filter(std::string_view name, const FfmpegLimits& limits);

/// Whether `listing` -- `ffmpeg -encoders` -- names the encoder `name`
/// (`libx264`).
[[nodiscard]] bool encoder_listed(std::string_view listing, std::string_view name);

/// `ffmpeg -hide_banner <flag>`'s listing -- `-filters`, `-encoders` --
/// through the seam's one child (52e: what `check` reads), or nullopt with
/// `error` saying why ffmpeg could not be asked.
[[nodiscard]] std::optional<std::string> ffmpeg_listing(std::string_view flag,
                                                        const FfmpegLimits& limits,
                                                        std::string& error);

}  // namespace apogee::platform
