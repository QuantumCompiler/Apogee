#include "cli/voice_say.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <system_error>

#include "contracts/config_edit.h"
#include "contracts/video_run.h"
#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "version/version.h"
#include "video/work_dir.h"

namespace apogee::commands {
namespace {

[[nodiscard]] std::string seconds_text(double seconds) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.2f", seconds);
    return buffer.data();
}

[[nodiscard]] std::string handles_of(const std::vector<models::StoredVoice>& voices) {
    std::string out;
    for (const models::StoredVoice& stored : voices) {
        out += "\n  " + models::weights_handle(stored.model, models::kVoiceFormat, stored.id);
    }
    return out;
}

}  // namespace

std::string resolve_voice_model(const models::StoreRoots& roots, std::string_view given,
                                ResolvedVoiceModel& out) {
    if (given.empty()) {
        const std::vector<models::StoredVoice> voices = models::list_store_voices(roots);
        if (voices.empty()) {
            return "no voice model in the store -- 'apogee models pull hexgrad/Kokoro-82M' "
                   "stores the reference one";
        }
        if (voices.size() > 1) {
            return "the store holds " + std::to_string(voices.size()) +
                   " voice models -- name one with --model:" + handles_of(voices);
        }
        out.handle =
            models::weights_handle(voices.front().model, models::kVoiceFormat, voices.front().id);
        out.dir = voices.front().dir;
    } else {
        const models::StoreTarget target = models::resolve_store_target(roots, given);
        if (!target.error.empty()) {
            return target.error;
        }
        if (target.outside) {
            if (!models::is_voice_dir(target.path)) {
                return target.path.string() +
                       " is not a voice model (config.json, a checkpoint and voices/)";
            }
            out.handle = std::string{given};
            out.dir = target.path;
        } else if (!target.format.empty() && target.format != models::kVoiceFormat) {
            return "'" + std::string{given} + "' is a " + target.format +
                   " model, not a voice model";
        } else if (!target.id.empty()) {
            out.handle = models::weights_handle(target.model, models::kVoiceFormat, target.id);
            out.dir = target.path;
        } else {
            const std::vector<models::StoredVoice> voices =
                models::list_store_voices(roots, target.model);
            if (voices.empty()) {
                return "'" + std::string{given} + "' holds no voice model";
            }
            if (voices.size() > 1) {
                return "'" + std::string{given} + "' holds " + std::to_string(voices.size()) +
                       " voice models -- name one:" + handles_of(voices);
            }
            out.handle = models::weights_handle(voices.front().model, models::kVoiceFormat,
                                                voices.front().id);
            out.dir = voices.front().dir;
        }
    }
    if (const std::optional<models::Snapshot> record = models::load_snapshot(out.dir)) {
        out.ref = record->ref;
        out.revision = record->revision;
    }
    out.info = models::read_voice_info(out.dir);
    return {};
}

std::string choose_voice(const models::VoiceInfo& info, std::string_view given, std::string& out) {
    if (!given.empty()) {
        if (!info.has_voice(given)) {
            std::string names;
            for (const models::VoicePack& voice : info.voices) {
                names += (names.empty() ? "" : ", ") + voice.name;
            }
            return "this model has no voice '" + std::string{given} +
                   "' -- its voices: " + (names.empty() ? std::string{"none"} : names);
        }
        out = std::string{given};
        return {};
    }
    if (info.voices.empty()) {
        return "this model has no voices";
    }
    out = info.has_voice(kDefaultVoice) ? std::string{kDefaultVoice} : info.voices.front().name;
    return {};
}

std::optional<std::string> narrate_voice_event(const video::VoiceEvent& event) {
    using Kind = video::VoiceEvent::Kind;
    switch (event.kind) {
        case Kind::Message:
            return event.text;
        case Kind::Ready:
            return "speaking on " + event.ready.device + " -- faster than real time";
        case Kind::Segment:
            return "segment " + std::to_string(event.index) + "/" + std::to_string(event.of) +
                   ": " + seconds_text(event.seconds) + " s";
        case Kind::Done:
            return std::nullopt;
    }
    return std::nullopt;
}

VoiceSayResult say_text(const models::StoreRoots& roots, const VoiceSayOptions& options,
                        const VoiceSaySeams& seams,
                        const harness::CancellationToken& cancellation) {
    VoiceSayResult result;
    const auto refuse = [&result](std::string why) {
        result.exit_code = 1;
        result.error = std::move(why);
        return result;
    };
    if (options.text.find_first_not_of(" \t\r\n") == std::string::npos) {
        return refuse("there is nothing to say -- give the text to speak");
    }
    if (options.text.size() > kMaxVoiceCharacters) {
        return refuse("the text is " + std::to_string(options.text.size()) +
                      " characters, over the cap of " + std::to_string(kMaxVoiceCharacters) +
                      " a run -- speak it in parts");
    }
    const double speed = options.speed.value_or(1.0);
    if (speed < kMinVoiceSpeed || speed > kMaxVoiceSpeed) {
        std::array<char, 64> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "--speed %g is outside %g-%g", speed,
                      kMinVoiceSpeed, kMaxVoiceSpeed);
        return refuse(buffer.data());
    }
    std::error_code code;
    if (options.output.empty()) {
        return refuse("-o names the WAV to write");
    }
    const std::filesystem::path output = std::filesystem::absolute(options.output, code);
    if (std::filesystem::exists(output, code)) {
        return refuse(output.string() +
                      " exists -- a run never writes over a file; name another with -o");
    }
    if (!std::filesystem::is_directory(output.parent_path(), code)) {
        return refuse("no directory " + output.parent_path().string() + " for the file");
    }
    ResolvedVoiceModel model;
    if (std::string refused = resolve_voice_model(roots, options.model, model); !refused.empty()) {
        return refuse(std::move(refused));
    }
    std::string voice;
    if (std::string refused = choose_voice(model.info, options.voice, voice); !refused.empty()) {
        return refuse(std::move(refused));
    }
    const video::VideoReadiness ready = seams.ready(model);
    if (!ready.ready()) {
        return refuse(ready.message());
    }
    if (seams.say) {
        seams.say(model.handle + ": voice " + voice + " (" + models::voice_language(voice) +
                  "), speed " + seconds_text(speed));
    }

    // The segments land in a directory of the run's own, joined into the
    // output only when every one did.
    std::string error;
    const std::filesystem::path segments_dir = video::make_work_dir(seams.work_root, error);
    if (segments_dir.empty()) {
        return refuse(error);
    }
    const video::WorkDir removed_at_end{segments_dir};
    const video::VoiceRequest request{
        .model = model.dir, .voice = voice, .speed = speed, .segments = {options.text}};
    const video::VoiceOutcome outcome =
        video::speak(ready, request, segments_dir / "kept", seams.work_root, seams.on_event,
                     cancellation, seams.spawn);
    std::string joined;
    if (outcome.ok) {
        joined = video::join_segments(outcome.segments, output);
    }

    const bool landed = outcome.ok && joined.empty();
    nlohmann::json segments = nlohmann::json::array();
    double offset = 0.0;
    for (const video::SpokenSegment& segment : outcome.segments) {
        nlohmann::json sentences = nlohmann::json::array();
        double at = offset;
        for (const video::SpokenSentence& sentence : segment.sentences) {
            sentences.push_back(
                {{"text", sentence.text}, {"seconds", sentence.seconds}, {"offset", at}});
            at += sentence.seconds;
        }
        segments.push_back({{"text", segment.text},
                            {"seconds", segment.seconds},
                            {"offset", offset},
                            {"sentences", std::move(sentences)}});
        offset += segment.seconds;
    }
    const std::filesystem::path sidecar = harness::video_sidecar_path(output);
    result.document = nlohmann::json{
        {"object", "voice.speech"},
        {"status", landed ? "done" : (outcome.cancelled ? "cancelled" : "failed")},
        {"output", output.string()},
        {"sidecar", landed ? nlohmann::json(sidecar.string()) : nlohmann::json(nullptr)},
        {"model",
         {{"name", model.handle},
          {"path", model.dir.string()},
          {"ref", model.ref},
          {"revision", model.revision},
          {"checkpoint", model.info.checkpoint}}},
        {"voice", voice},
        {"language", models::voice_language(voice)},
        {"speed", speed},
        {"text", options.text},
        {"segments", std::move(segments)},
        {"seconds", outcome.seconds},
        {"sample_rate", outcome.sample_rate},
        {"device", {{"device", outcome.ready.device}, {"platform", seams.platform}}},
        {"versions",
         {{"torch", outcome.ready.torch.empty() ? ready.torch_version : outcome.ready.torch},
          {"kokoro", outcome.ready.kokoro.empty() ? ready.kokoro_version : outcome.ready.kokoro},
          {"apogee", std::string{version::semantic()}}}},
        {"run_seconds", static_cast<double>(outcome.elapsed.count()) / 1000.0},
        {"created_at", models::now_rfc3339()},
    };
    if (!landed) {
        result.document["error"] = outcome.ok ? joined : outcome.error;
        result.exit_code = outcome.cancelled ? 130 : 1;
        result.error = outcome.cancelled
                           ? "cancelled -- nothing was written (" + output.string() + " untouched)"
                           : result.document["error"].get<std::string>() + " -- " +
                                 output.string() + " was not written";
        return result;
    }
    try {
        harness::write_file_atomically(sidecar, result.document.dump(2) + "\n");
    } catch (const std::exception& e) {
        result.exit_code = 1;
        result.error = "the speech is at " + output.string() +
                       ", but its record could not be written: " + e.what();
        return result;
    }
    result.lines = {output.string(),
                    seconds_text(outcome.seconds) + " s of speech, voice " + voice + ", " +
                        std::to_string(outcome.sample_rate) + " Hz -- on " +
                        (outcome.ready.device.empty() ? std::string{"an unstated device"}
                                                      : outcome.ready.device),
                    "record: " + sidecar.string()};
    return result;
}

}  // namespace apogee::commands
