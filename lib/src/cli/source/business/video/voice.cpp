#include "video/voice.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>

#include "modelstore/mlx_info.h"
#include "modelstore/store.h"
#include "modelstore/voice_info.h"
#include "platform/ffmpeg.h"
#include "video/work_dir.h"

namespace apogee::video {
namespace {

[[nodiscard]] std::string string_at(const nlohmann::json& record, const char* key) {
    const auto found = record.find(key);
    return found != record.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

[[nodiscard]] double number_at(const nlohmann::json& record, const char* key) {
    const auto found = record.find(key);
    return found != record.end() && found->is_number() ? found->get<double>() : 0.0;
}

}  // namespace

std::optional<VideoEngine> voice_engine_of_directory(const std::filesystem::path& dir) {
    if (models::is_voice_dir(dir)) {
        return VideoEngine::Torch;
    }
    std::error_code code;
    if (std::filesystem::is_directory(dir, code) &&
        models::read_mlx_kind(dir).word == models::kMlxVoice) {
        return VideoEngine::Mlx;
    }
    return std::nullopt;
}

std::string choose_voice_engine(std::string_view asked, VideoEngine directory,
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
               "' is an MLX voice model, which speaks on MLX (mlx-audio), not PyTorch -- "
               "PyTorch speaks with a voice model as Kokoro publishes it: 'apogee models pull "
               "hexgrad/Kokoro-82M' stores one, and 'apogee train setup --with voice' installs "
               "what runs it";
    }
    return "'" + std::string{name} +
           "' is a voice model as Kokoro publishes it, which speaks on PyTorch (kokoro), not "
           "MLX -- MLX speaks with its MLX build: 'apogee models convert " +
           std::string{name} + " --mlx' makes one, after '" + std::string{kMlxAudioSetupRemedy} +
           "'";
}

nlohmann::json voice_request_document(const VoiceRequest& request,
                                      const std::filesystem::path& out_dir) {
    nlohmann::json document{{"model_dir", request.model.string()},
                            {"voice", request.voice},
                            {"speed", request.speed},
                            {"segments", request.segments},
                            {"out_dir", out_dir.string()}};
    // On MLX the engine named (53m), as a video request names it.
    if (request.engine == VideoEngine::Mlx) {
        document["engine"] = std::string{to_string(request.engine)};
    }
    return document;
}

std::optional<VoiceEvent> read_voice_event(const training::ScriptEvent& line) {
    using Kind = training::ScriptEvent::Kind;
    if (line.kind == Kind::Message) {
        return VoiceEvent{.kind = VoiceEvent::Kind::Message, .text = line.text};
    }
    if (line.kind != Kind::Record || !line.record.is_object()) {
        return std::nullopt;
    }
    const nlohmann::json& record = line.record;
    const std::string event = string_at(record, "event");
    VoiceEvent out;
    if (event == "ready") {
        out.kind = VoiceEvent::Kind::Ready;
        out.ready = VoiceReady{.device = string_at(record, "device"),
                               .torch = string_at(record, "torch"),
                               .kokoro = string_at(record, "kokoro"),
                               .sample_rate = static_cast<int>(number_at(record, "sample_rate")),
                               .engine = string_at(record, "engine"),
                               .mlx = string_at(record, "mlx"),
                               .mlx_audio = string_at(record, "mlx_audio")};
        return out;
    }
    if (event == "segment") {
        out.kind = VoiceEvent::Kind::Segment;
        out.index = static_cast<int>(number_at(record, "index"));
        out.of = static_cast<int>(number_at(record, "of"));
        out.seconds = number_at(record, "seconds");
        out.file = string_at(record, "file");
        if (const auto sentences = record.find("sentences");
            sentences != record.end() && sentences->is_array()) {
            for (const nlohmann::json& sentence : *sentences) {
                if (sentence.is_object()) {
                    out.sentences.push_back({.text = string_at(sentence, "text"),
                                             .seconds = number_at(sentence, "seconds")});
                }
            }
        }
        return out;
    }
    if (event == "done") {
        out.kind = VoiceEvent::Kind::Done;
        out.of = static_cast<int>(number_at(record, "segments"));
        out.seconds = number_at(record, "seconds");
        return out;
    }
    return std::nullopt;
}

VoiceOutcome speak(const VideoReadiness& readiness, const VoiceRequest& request,
                   const std::filesystem::path& segments_dir,
                   const std::filesystem::path& work_root, const VoiceEventSink& on_event,
                   const harness::CancellationToken& cancellation, const training::Spawner& spawn) {
    const auto started = std::chrono::steady_clock::now();
    VoiceOutcome out;
    const auto finish = [&out, started](std::string error) {
        out.error = std::move(error);
        out.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        return out;
    };
    if (!readiness.ready()) {
        return finish(readiness.message());
    }
    if (request.segments.empty()) {
        return finish("nothing to speak");
    }
    // The one speech, either engine (53m): the driver the ladder found for
    // this engine and no other.
    if (readiness.engine != request.engine) {
        return finish("the runtime was made ready for " + std::string{to_string(readiness.engine)} +
                      ", and the speech asks for " + std::string{to_string(request.engine)});
    }
    sweep_abandoned(work_root);
    std::string error;
    const std::filesystem::path work = make_work_dir(work_root, error);
    if (work.empty()) {
        return finish(error);
    }
    const WorkDir removed_at_end{work};
    const std::filesystem::path written = work / "segments";
    const std::filesystem::path request_path = work / "request.json";
    {
        std::ofstream file{request_path, std::ios::binary};
        file << voice_request_document(request, written).dump(2) << "\n";
        if (!file) {
            return finish("could not write the request into " + work.string());
        }
    }

    std::vector<VoiceEvent> segments;
    const training::ScriptRequest script{
        .interpreter = readiness.interpreter,
        .script = readiness.driver,
        .arguments = {"--request", request_path.string()},
        .environment = {{"PYTHONDONTWRITEBYTECODE", "1"}},
    };
    const training::ScriptOutcome ran = training::run_script(
        script,
        [&](const training::ScriptEvent& line) {
            const std::optional<VoiceEvent> event = read_voice_event(line);
            if (!event.has_value()) {
                return;
            }
            if (event->kind == VoiceEvent::Kind::Ready) {
                out.ready = event->ready;
            } else if (event->kind == VoiceEvent::Kind::Segment) {
                segments.push_back(*event);
            }
            if (on_event) {
                on_event(*event);
            }
        },
        cancellation, spawn);
    if (ran.cancelled || cancellation.stop_requested()) {
        out.cancelled = true;
        return finish("cancelled");
    }
    if (!ran.ok) {
        return finish(ran.describe());
    }
    if (segments.size() != request.segments.size()) {
        return finish("the driver spoke " + std::to_string(segments.size()) + " of " +
                      std::to_string(request.segments.size()) + " segment(s)");
    }

    // Every file read back and measured before any lands where it is kept.
    std::vector<SpokenSegment> spoken;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const VoiceEvent& event = segments.at(index);
        std::string bad;
        const std::optional<platform::WavAudio> audio = platform::read_wav(event.file, bad);
        if (!audio.has_value()) {
            return finish("segment " + std::to_string(index + 1) + ": " + bad);
        }
        if (out.sample_rate == 0) {
            out.sample_rate = audio->rate;
        }
        spoken.push_back({.text = request.segments.at(index),
                          .file = event.file,
                          .seconds = audio->seconds(),
                          .sentences = event.sentences});
        out.seconds += audio->seconds();
    }
    std::error_code code;
    std::filesystem::create_directories(segments_dir, code);
    for (std::size_t index = 0; index < spoken.size(); ++index) {
        std::array<char, 16> name{};
        std::snprintf(name.data(), name.size(), "s%05zu.wav", index + 1);
        const std::filesystem::path kept = segments_dir / name.data();
        if (const std::string moved = models::move_path(spoken.at(index).file, kept);
            !moved.empty()) {
            return finish("could not keep " + kept.string() + ": " + moved);
        }
        spoken.at(index).file = kept;
    }
    out.segments = std::move(spoken);
    out.ok = true;
    return finish({});
}

std::string join_segments(const std::vector<SpokenSegment>& segments,
                          const std::filesystem::path& output) {
    std::string pcm;
    int rate = 0;
    for (const SpokenSegment& segment : segments) {
        std::string error;
        const std::optional<platform::WavAudio> audio = platform::read_wav(segment.file, error);
        if (!audio.has_value()) {
            return error;
        }
        if (audio->channels != 1 || audio->bits != 16 || (rate != 0 && audio->rate != rate)) {
            return segment.file.string() + " is not the 16-bit mono at one rate the others are";
        }
        rate = audio->rate;
        pcm += audio->pcm;
    }
    const std::filesystem::path partial{output.string() + ".partial"};
    {
        std::ofstream file{partial, std::ios::binary};
        file << platform::wav_file(pcm, rate);
        if (!file) {
            return "could not write " + partial.string();
        }
    }
    if (const std::string moved = models::move_path(partial, output); !moved.empty()) {
        std::error_code code;
        std::filesystem::remove(partial, code);
        return moved;
    }
    return {};
}

}  // namespace apogee::video
