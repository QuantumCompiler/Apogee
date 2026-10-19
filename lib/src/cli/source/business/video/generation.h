#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "contracts/cancellation.h"
#include "training/script_runner.h"
#include "video/runtime.h"

/// One clip, generated (49b): the request handed to the seeded driver, the
/// driver run over the one script runner with its progress said as it
/// comes, the frames assembled into an mp4 through the ffmpeg seam, and the
/// clip moved to where it was asked for -- only then.
///
/// **The output is touched once, at the end.** Frames and the assembled
/// clip live in a throwaway working directory under the cache
/// (`cache/video/run-<pid>-<random>/`); a failed or cancelled run removes it
/// and leaves the output path as it found it. A working directory a killed
/// run left behind is swept by the next run, by its owner's death.
namespace apogee::video {

/// One clip's settings -- what the driver takes, in the request it reads
/// (`request_document`). A JSON file rather than arguments, so a prompt is
/// never on a command line: not in a process listing, not bound by a
/// command line's length, its UTF-8 intact.
struct VideoRequest {
    std::filesystem::path pipeline;
    std::string prompt;
    std::string negative_prompt{};
    /// A seed image, for image-to-video; empty for text-to-video.
    std::filesystem::path image{};
    std::int64_t seed = 0;
    int width = 0;
    int height = 0;
    int frames = 0;
    int steps = 0;
    std::optional<double> guidance{};
    /// The weights variant to load (`fp16`), when the pipeline's are one.
    std::string variant{};
    /// Frames a second: the assembly's, never the driver's.
    int fps = 0;
};

/// The request the driver reads: every field of `request` it takes, and
/// where it writes its frames.
[[nodiscard]] nlohmann::json request_document(const VideoRequest& request,
                                              const std::filesystem::path& frames_dir);

/// What the driver found when it loaded the pipeline.
struct VideoReady {
    std::string device;
    std::string dtype;
    std::string pipeline;
    /// `t2v` or `i2v`.
    std::string mode;
    std::string torch;
    std::string diffusers;
};

/// One thing a generation said as it went.
struct VideoEvent {
    enum class Kind : std::uint8_t {
        /// A note from the driver, as it wrote it.
        Message,
        /// The pipeline is loaded: `ready` says on what.
        Ready,
        /// A denoising step finished: `step` of `steps`.
        Step,
        /// The driver wrote its `frames` frames, `width` x `height`.
        Done,
        /// The frames are being assembled into the clip.
        Assembling,
    };
    Kind kind = Kind::Message;
    std::string text{};
    VideoReady ready{};
    int step = 0;
    int steps = 0;
    int frames = 0;
    int width = 0;
    int height = 0;
};

using VideoEventSink = std::function<void(const VideoEvent&)>;

/// A protocol line the driver wrote, read: a `{"event": …}` record as its
/// event, a `{"message"}` as one; nullopt for a record that is no event.
[[nodiscard]] std::optional<VideoEvent> read_video_event(const training::ScriptEvent& line);

/// How the frames become a clip: `frames_dir`'s `f%05d.png` (from 1) at
/// `fps`, written to `out`. The codec used, or empty with `error` set.
using VideoAssembler = std::function<std::string(
    const std::filesystem::path& frames_dir, int fps, const std::filesystem::path& out,
    const harness::CancellationToken& cancellation, std::string& error)>;

/// `platform::encode_video` -- ffmpeg, a bounded captured child, stopped by
/// `cancellation`.
[[nodiscard]] VideoAssembler ffmpeg_assembler();

/// What a generation needs from outside, so a test hands in its own: how
/// the driver is spawned (a scripted child in place of Python), and how the
/// frames are assembled.
struct VideoSeams {
    training::Spawner spawn = training::default_spawner();
    VideoAssembler assemble = ffmpeg_assembler();
};

struct VideoOutcome {
    /// The clip is at `output`.
    bool ok = false;
    bool cancelled = false;
    /// Why not, the driver's stderr tail folded in.
    std::string error;
    VideoReady ready;
    int frames = 0;
    int width = 0;
    int height = 0;
    std::string codec;
    std::filesystem::path output;
    /// The whole run, from the request written to the clip in place.
    std::chrono::milliseconds elapsed{0};
};

/// Runs one generation on `readiness` (ready: `probe_video` passed):
/// a working directory made under `work_root` (and any a dead run left
/// there removed), the request written into it, the driver run with every
/// event to `on_event`, the frames assembled into the working directory,
/// and the clip moved to `output`. The working directory is removed on
/// every path; `output` is touched only when everything succeeded.
[[nodiscard]] VideoOutcome generate_video(const VideoReadiness& readiness,
                                          const VideoRequest& request,
                                          const std::filesystem::path& output,
                                          const std::filesystem::path& work_root,
                                          const VideoEventSink& on_event,
                                          const harness::CancellationToken& cancellation,
                                          const VideoSeams& seams = {});

}  // namespace apogee::video
