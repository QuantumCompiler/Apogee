#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
#include "harness/harness.h"
#include "harness/types.h"

/// Images, audio and video, attached to a conversation (26e).
///
/// The same `/attach` that takes a document takes a picture, a recording or a
/// video, and each is given to whichever configured model can read it:
///
/// - **natively**, on the turn it is attached, when the chat model can -- an
///   image to a vision model, audio to one whose projector hears, a clip up to
///   a minute to a vision model as its frames;
/// - **as text**, always: a description from the `vision` role, a transcript
///   from the `transcription` role, and for a video a **timeline** -- frames
///   every five seconds and at each scene change, each described, merged in
///   time order with what was said. The text is indexed with the chat's
///   documents, and stands in for the pixels and samples on every later
///   turn, so a vision chat stops re-reading every image every turn.
///
/// A helper is used automatically (the user's call), falling back to the
/// chat's own model when no helper role is set and it can read the medium.
/// `ffmpeg` decodes audio and video, through `platform/ffmpeg`.
namespace apogee::agentloop {

/// A clip up to this long is read natively by a chat model that can; a longer
/// one only through its text (26e, default taken).
inline constexpr double kNativeSeconds = 60.0;
/// A transcript is made in windows this long.
inline constexpr double kTranscriptWindow = 30.0;
/// A timeline takes a frame at most this many seconds after the last...
inline constexpr double kFrameEvery = 5.0;
/// ...and at a scene change scoring over this...
inline constexpr double kSceneChange = 0.3;
/// ...and at most this many in all (26e, default taken).
inline constexpr std::size_t kMaxTimelineFrames = 240;
/// A native clip's frames: this many a second, at most this wide.
inline constexpr double kNativeFramesPerSecond = 1.0;
inline constexpr int kNativeFrameSide = 640;

/// The medium `path` is, by its extension; nullopt for text and documents.
[[nodiscard]] std::optional<harness::Medium> medium_of(const std::filesystem::path& path);

/// The media type an image is sent as, by its extension; empty for a format
/// the vendors do not take, which is converted to JPEG first.
[[nodiscard]] std::string image_media_type(const std::filesystem::path& path);

/// Base64, for the `data:` URIs and audio parts media are carried in.
[[nodiscard]] std::string base64_encode(std::string_view bytes);

/// `seconds` as a clock: `0:05`, `4:30`, `1:04:30`.
[[nodiscard]] std::string clock_time(double seconds);

/// The moments `question` names as clock times -- `4:30`, `1:04:30` -- in
/// seconds, in order. Empty when it names none.
[[nodiscard]] std::vector<double> moments_in(std::string_view question);

/// The first and last time a timeline or transcript span covers, read from
/// the `[m:ss]` and `[m:ss–m:ss]` stamps its lines start with; nullopt when
/// it has none. `text` is the whole file's text, `begin` and `end` the span.
struct TimeSpan {
    double start = 0.0;
    double end = 0.0;
};

[[nodiscard]] std::optional<TimeSpan> time_span(std::string_view text, std::size_t begin,
                                                std::size_t end);

/// Who reads a medium for a conversation on `chat`.
struct MediaReaders {
    /// The chat model reads it itself, on the turn it is attached (a clip
    /// only up to `kNativeSeconds`).
    bool native = false;
    /// The model that describes an image or a frame; empty when none can.
    std::string describer;
    /// The model that transcribes audio; empty when none can.
    std::string transcriber;
};

/// Who reads `medium` on a conversation on `chat`: the role's own model when
/// it can, else the chat's when it can (the default taken: the chat model
/// describes its own images when no `vision` role is set). Table-tested.
[[nodiscard]] MediaReaders media_readers(const harness::Harness& harness, const std::string& chat,
                                         harness::Medium medium);

/// The request that asks `backend` to describe an image. `context` -- "one
/// frame of a video, at 4:30" -- says what it is, when it is more than one.
[[nodiscard]] harness::ChatRequest describe_request(const std::string& backend,
                                                    const std::string& data_uri,
                                                    std::string_view context);

/// The request that asks `backend` to transcribe a WAV file's audio.
[[nodiscard]] harness::ChatRequest transcribe_request(const std::string& backend,
                                                      std::string_view wav);

/// Everything one media file's reading needs.
struct MediaJob {
    const harness::Harness* harness = nullptr;
    MediaReaders readers;
    /// A private folder for frames and conversions, removed after.
    std::filesystem::path scratch;
    /// What it is doing, for the status line: "describing frame 12 of 120".
    std::function<void(const std::string&)> status;
    harness::CancellationToken cancellation;
};

/// A medium's text form, as it is indexed.
struct MediaText {
    std::string text;
    /// `vision: <backend>`, `transcription: <backend>`, or `timeline: <the
    /// backends that built it>`.
    std::string reader;
    /// Why it was not read, when it was not.
    std::string reason;
    /// Worth saying: a video's sound left untranscribed, a frame that failed.
    std::vector<std::string> notes;
    /// The clip's length, when it has one.
    double duration = 0.0;
};

/// Reads `path` as `medium` into text: an image described, audio transcribed
/// in `kTranscriptWindow` windows, a video turned into a timeline.
[[nodiscard]] MediaText read_media(const std::filesystem::path& path, harness::Medium medium,
                                   const MediaJob& job);

/// `path` as the parts the chat model reads natively: the image; the audio as
/// WAV at `sample_rate` (16 kHz when 0); a clip's frames, one a second, with
/// their times between them, and its sound when `with_sound`. Empty with
/// `error` filled when it cannot be decoded.
[[nodiscard]] std::vector<harness::ContentPart> native_parts(
    const std::filesystem::path& path, harness::Medium medium, int sample_rate, bool with_sound,
    const std::filesystem::path& scratch, const harness::CancellationToken& cancellation,
    std::string& error);

/// How long a clip is, by ffprobe; 0 for a still image or when it cannot say.
[[nodiscard]] double media_duration(const std::filesystem::path& path,
                                    const harness::CancellationToken& cancellation);

}  // namespace apogee::agentloop
