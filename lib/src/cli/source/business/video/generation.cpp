#include "video/generation.h"

#include <fstream>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>

#include "modelstore/store.h"
#include "platform/ffmpeg.h"
#include "platform/platform.h"
#include "video/work_dir.h"

namespace apogee::video {
namespace {

[[nodiscard]] int int_at(const nlohmann::json& record, const char* key) {
    const auto found = record.find(key);
    return found != record.end() && found->is_number_integer() ? found->get<int>() : 0;
}

[[nodiscard]] std::string string_at(const nlohmann::json& record, const char* key) {
    const auto found = record.find(key);
    return found != record.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

}  // namespace

nlohmann::json request_document(const VideoRequest& request,
                                const std::filesystem::path& frames_dir) {
    nlohmann::json document{{"pipeline", request.pipeline.string()},
                            {"prompt", request.prompt},
                            {"seed", request.seed},
                            {"width", request.width},
                            {"height", request.height},
                            {"frames", request.frames},
                            {"steps", request.steps},
                            {"frames_dir", frames_dir.string()}};
    if (!request.negative_prompt.empty()) {
        document["negative_prompt"] = request.negative_prompt;
    }
    if (!request.image.empty()) {
        document["image"] = request.image.string();
    }
    if (request.guidance.has_value()) {
        document["guidance"] = *request.guidance;
    }
    if (!request.variant.empty()) {
        document["variant"] = request.variant;
    }
    // A run without LoRAs asks exactly what it asked before 49e.
    if (!request.loras.empty()) {
        nlohmann::json loras = nlohmann::json::array();
        for (const LoraFile& lora : request.loras) {
            loras.push_back({{"path", lora.file.string()}, {"scale", lora.scale}});
        }
        document["loras"] = std::move(loras);
    }
    // On MLX (53l): the engine named, the tokenizer from the store, and a
    // shift when one is asked. PyTorch's request is what it always was.
    if (request.engine == VideoEngine::Mlx) {
        document["engine"] = std::string{to_string(request.engine)};
        document["tokenizer"] = request.tokenizer.string();
        if (request.shift.has_value()) {
            document["shift"] = *request.shift;
        }
    }
    return document;
}

std::string video_request_refusal(const VideoRequest& request) {
    if (request.engine != VideoEngine::Mlx) {
        return {};
    }
    if (!mlx_video_frames(request.frames)) {
        return "mlx-video's Wan generator takes 4n+1 frames (5, 9, ... 81, 121) -- " +
               std::to_string(request.frames) + " is not one";
    }
    if (request.tokenizer.empty()) {
        return "no T5 tokenizer to point mlx-video's Wan generator at -- it is never fetched from "
               "the hub";
    }
    // The model's mode says whether a seed image is taken: Wan's TI2V takes
    // one or none, its I2V needs one, its T2V takes none.
    const std::string mode = mlx_video_mode(request.pipeline);
    if (mode == "t2v" && !request.image.empty()) {
        return "this MLX video model is text-to-video (its config.json says model_type t2v): it "
               "takes no seed image -- generate without --image";
    }
    if (mode == "i2v" && request.image.empty()) {
        return "this MLX video model is image-to-video (its config.json says model_type i2v): it "
               "starts from a seed image -- give it one with --image";
    }
    return {};
}

std::optional<VideoEvent> read_video_event(const training::ScriptEvent& line) {
    using Kind = training::ScriptEvent::Kind;
    if (line.kind == Kind::Message) {
        return VideoEvent{.kind = VideoEvent::Kind::Message, .text = line.text};
    }
    if (line.kind != Kind::Record || !line.record.is_object()) {
        return std::nullopt;
    }
    const std::string event = string_at(line.record, "event");
    VideoEvent out;
    if (event == "ready") {
        out.kind = VideoEvent::Kind::Ready;
        out.ready = VideoReady{.device = string_at(line.record, "device"),
                               .dtype = string_at(line.record, "dtype"),
                               .pipeline = string_at(line.record, "pipeline"),
                               .mode = string_at(line.record, "mode"),
                               .torch = string_at(line.record, "torch"),
                               .diffusers = string_at(line.record, "diffusers"),
                               .engine = string_at(line.record, "engine"),
                               .mlx = string_at(line.record, "mlx"),
                               .mlx_video = string_at(line.record, "mlx_video")};
        return out;
    }
    if (event == "step") {
        out.kind = VideoEvent::Kind::Step;
        out.step = int_at(line.record, "step");
        out.steps = int_at(line.record, "steps");
        return out;
    }
    if (event == "done") {
        out.kind = VideoEvent::Kind::Done;
        out.frames = int_at(line.record, "frames");
        out.width = int_at(line.record, "width");
        out.height = int_at(line.record, "height");
        return out;
    }
    return std::nullopt;
}

VideoAssembler ffmpeg_assembler() {
    return [](const std::filesystem::path& frames_dir, int fps, const std::filesystem::path& out,
              const harness::CancellationToken& cancellation, std::string& error) {
        platform::FfmpegLimits limits;
        limits.stopped = [cancellation] { return cancellation.stop_requested(); };
        return platform::encode_video(frames_dir, fps, out, limits, error);
    };
}

VideoOutcome generate_video(const VideoReadiness& readiness, const VideoRequest& request,
                            const std::filesystem::path& output,
                            const std::filesystem::path& work_root, const VideoEventSink& on_event,
                            const harness::CancellationToken& cancellation,
                            const VideoSeams& seams) {
    const auto started = std::chrono::steady_clock::now();
    VideoOutcome out;
    out.output = output;
    const auto finish = [&out, started](std::string error) {
        out.error = std::move(error);
        out.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        return out;
    };
    if (!readiness.ready()) {
        return finish(readiness.message());
    }
    // The one generation, either engine (53l): its own refusals up front,
    // and the driver the ladder found for this engine and no other.
    if (readiness.engine != request.engine) {
        return finish("the runtime was made ready for " + std::string{to_string(readiness.engine)} +
                      ", and the run asks for " + std::string{to_string(request.engine)});
    }
    if (std::string refused = video_request_refusal(request); !refused.empty()) {
        return finish(std::move(refused));
    }

    sweep_abandoned(work_root);
    std::string error;
    const std::filesystem::path work = make_work_dir(work_root, error);
    if (work.empty()) {
        return finish(error);
    }
    const WorkDir removed_at_end{work};
    const std::filesystem::path frames = work / "frames";
    const std::filesystem::path request_path = work / "request.json";
    {
        std::ofstream file{request_path, std::ios::binary};
        file << request_document(request, frames).dump(2) << "\n";
        if (!file) {
            return finish("could not write the request into " + work.string());
        }
    }

    bool done = false;
    const training::ScriptRequest script{
        .interpreter = readiness.interpreter,
        .script = readiness.driver,
        .arguments = {"--request", request_path.string()},
        .environment = {{"PYTHONDONTWRITEBYTECODE", "1"}},
    };
    const training::ScriptOutcome ran = training::run_script(
        script,
        [&](const training::ScriptEvent& line) {
            std::optional<VideoEvent> event = read_video_event(line);
            if (!event.has_value()) {
                return;
            }
            if (event->kind == VideoEvent::Kind::Ready) {
                // The engine the run takes, said from the moment it is ready
                // (M24): the request's, as its record says it.
                event->ready.engine = std::string{to_string(request.engine)};
                out.ready = event->ready;
            } else if (event->kind == VideoEvent::Kind::Done) {
                done = true;
                out.frames = event->frames;
                out.width = event->width;
                out.height = event->height;
            }
            if (on_event) {
                on_event(*event);
            }
        },
        cancellation, seams.spawn);
    if (ran.cancelled || cancellation.stop_requested()) {
        out.cancelled = true;
        return finish("cancelled");
    }
    if (!ran.ok) {
        return finish(ran.describe());
    }
    std::error_code code;
    if (!done || out.frames <= 0 ||
        !std::filesystem::is_regular_file(frames / "f00001.png", code)) {
        return finish("the driver ended without writing its frames");
    }

    if (on_event) {
        on_event(VideoEvent{.kind = VideoEvent::Kind::Assembling, .frames = out.frames});
    }
    const std::filesystem::path clip = work / "clip.mp4";
    std::string assembled;
    out.codec = seams.assemble(frames, request.fps, clip, cancellation, assembled);
    if (cancellation.stop_requested()) {
        out.cancelled = true;
        return finish("cancelled");
    }
    if (out.codec.empty() || !std::filesystem::is_regular_file(clip, code)) {
        return finish(assembled.empty() ? "ffmpeg wrote no clip" : assembled);
    }
    if (const std::string moved = models::move_path(clip, output); !moved.empty()) {
        return finish("could not put the clip at " + output.string() + ": " + moved);
    }
    out.ok = true;
    return finish({});
}

}  // namespace apogee::video
