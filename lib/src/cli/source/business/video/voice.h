#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "training/script_runner.h"
#include "video/runtime.h"

/// Speech (50a): text segments spoken by a voice model from the store
/// through the seeded `voice_say.py`, over the one script runner -- each
/// segment one 16-bit WAV, its duration **measured from the file's own
/// header**, never estimated, because a caption timed from it (50c) is only
/// as true as that number. The video runtime's template, applied to a voice.
///
/// **Two engines, one speech** (53m), as video has (53l): a voice model as
/// Kokoro publishes it speaks on PyTorch through `voice_say.py`, and
/// mlx-audio's MLX build of one on MLX through `voice_say_mlx.py`, which
/// speaks the same protocol -- the `ready` event, the segments, their WAVs.
/// The model's directory says which (`voice_engine_of_directory`); the
/// request, the working directory, the measuring and the record are the one
/// speech's.
namespace apogee::video {

/// What the voice model at `dir` speaks on, from its files (53m): a voice
/// model as Kokoro publishes it (`models::is_voice_dir`: `config.json`, a
/// checkpoint and `voices/`) on PyTorch, an MLX directory whose kind is
/// `voice` (53e's reader, `read_mlx_kind`) on MLX; nullopt for anything else.
/// The model says its engine: a person picks the model.
[[nodiscard]] std::optional<VideoEngine> voice_engine_of_directory(
    const std::filesystem::path& dir);

/// The engine a speech runs on (53m), `choose_video_engine`'s rule: the
/// model's, `directory`; `asked` -- `voice say`'s `--engine` -- only names it
/// or refuses it: empty or `auto` takes the model's, the model's own is it,
/// and the other engine is refused, saying what `name` is, what runs it, and
/// how a model for the asked engine is had; any other word is refused naming
/// the words. Empty means `out` is set.
[[nodiscard]] std::string choose_voice_engine(std::string_view asked, VideoEngine directory,
                                              std::string_view name, VideoEngine& out);

/// What to speak, and with what.
struct VoiceRequest {
    std::filesystem::path model;
    std::string voice;
    double speed = 1.0;
    /// Each one file; each split into sentences by the driver's plain rule.
    std::vector<std::string> segments;
    /// What it speaks on (53m): the readiness must have been made for it.
    VideoEngine engine = VideoEngine::Torch;
};

/// The request the driver reads: the model, the voice, the speed, the
/// segments, and where it writes its files -- and on MLX its engine named
/// (53m); the PyTorch driver's request is exactly what it was.
[[nodiscard]] nlohmann::json voice_request_document(const VoiceRequest& request,
                                                    const std::filesystem::path& out_dir);

/// What the driver found when it loaded the model.
struct VoiceReady {
    std::string device;
    std::string torch;
    std::string kokoro;
    int sample_rate = 0;
    /// The engine the driver says it runs (53m) -- `mlx` from the MLX
    /// driver, empty from the PyTorch one, which predates the word -- and on
    /// MLX the versions that ran. A speech's own events say the engine its
    /// request takes, `torch` or `mlx`, whatever the driver wrote (M24).
    std::string engine{};
    std::string mlx{};
    std::string mlx_audio{};
};

/// One sentence of a segment, and the seconds its samples last.
struct SpokenSentence {
    std::string text;
    double seconds = 0.0;
};

/// One segment spoken: its text, its file, and how long it lasts -- the
/// file's own length -- with its sentences'.
struct SpokenSegment {
    std::string text;
    std::filesystem::path file;
    double seconds = 0.0;
    std::vector<SpokenSentence> sentences;
};

struct VoiceEvent {
    enum class Kind : std::uint8_t { Message, Ready, Segment, Done };
    Kind kind = Kind::Message;
    std::string text{};
    VoiceReady ready{};
    /// A segment: which of how many, and its seconds as the driver counted.
    int index = 0;
    int of = 0;
    double seconds = 0.0;
    std::string file{};
    std::vector<SpokenSentence> sentences{};
};

using VoiceEventSink = std::function<void(const VoiceEvent&)>;

/// A protocol line the driver wrote, read: an `{"event": …}` record as its
/// event, a message as one; nullopt for anything else.
[[nodiscard]] std::optional<VoiceEvent> read_voice_event(const training::ScriptEvent& line);

struct VoiceOutcome {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    VoiceReady ready;
    /// Each segment, its file in the caller's directory.
    std::vector<SpokenSegment> segments;
    /// The segments' seconds together, and the rate they share.
    double seconds = 0.0;
    int sample_rate = 0;
    std::chrono::milliseconds elapsed{0};
};

/// Speaks `request` on `readiness` (ready: `probe_voice_engine` passed for
/// the request's engine): the request written into a working directory under
/// `work_root`, the driver the ladder found run with every event to
/// `on_event`, each segment's file read back and measured, and moved into
/// `segments_dir` as `s00001.wav`, … . The working directory is removed on
/// every path; nothing lands in `segments_dir` unless every segment did.
[[nodiscard]] VoiceOutcome speak(const VideoReadiness& readiness, const VoiceRequest& request,
                                 const std::filesystem::path& segments_dir,
                                 const std::filesystem::path& work_root,
                                 const VoiceEventSink& on_event,
                                 const harness::CancellationToken& cancellation,
                                 const training::Spawner& spawn = training::default_spawner());

/// The segments' samples, one after another, written as one WAV at
/// `output` -- through a file beside it, renamed into place. Every segment
/// must share one rate and the 16-bit mono form. The error, or empty.
[[nodiscard]] std::string join_segments(const std::vector<SpokenSegment>& segments,
                                        const std::filesystem::path& output);

}  // namespace apogee::video
