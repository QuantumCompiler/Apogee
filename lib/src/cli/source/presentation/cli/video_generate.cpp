#include "cli/video_generate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <system_error>

#include "contracts/config_edit.h"
#include "modelstore/pipeline_info.h"
#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "version/version.h"

namespace apogee::commands {
namespace {

/// `text` read as a whole positive number, or nullopt.
[[nodiscard]] std::optional<int> whole_number(std::string_view text) {
    if (text.empty() || text.size() > 6) {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = (value * 10) + (c - '0');
    }
    return value;
}

/// `seconds` as a person reads it: `5`, `2.5`.
[[nodiscard]] std::string seconds_text(double seconds) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%g", seconds);
    return buffer.data();
}

[[nodiscard]] std::string side_refusal(int side) {
    if (side > kMaxVideoSide) {
        return "the cap is " + std::to_string(kMaxVideoSide) + " pixels a side";
    }
    if (side < kMinVideoSide) {
        return "the least is " + std::to_string(kMinVideoSide) + " pixels a side";
    }
    if (side % kVideoSideMultiple != 0) {
        return "each side must be a multiple of " + std::to_string(kVideoSideMultiple);
    }
    return {};
}

/// The handles of `pipelines`, one a line.
[[nodiscard]] std::string handles_of(const std::vector<models::StoredPipeline>& pipelines) {
    std::string out;
    for (const models::StoredPipeline& stored : pipelines) {
        out += "\n  " + models::weights_handle(stored.model, models::kPipelineFormat, stored.id);
    }
    return out;
}

[[nodiscard]] std::string expectation(std::string_view device) {
    if (device == "mps") {
        return "expect minutes per clip on Apple silicon";
    }
    if (device == "cuda") {
        return "expect a minute or more per clip on a GPU";
    }
    return "expect hours per clip on the CPU -- a GPU or Apple silicon is far faster";
}

}  // namespace

int video_frames(double duration, int fps) {
    const double frames = duration * static_cast<double>(fps);
    const auto k = static_cast<int>(std::lround((frames - 1.0) / 4.0));
    return std::max((4 * k) + 1, 5);
}

std::string resolve_video_settings(const VideoGenerateOptions& options,
                                   harness::VideoSettings& out) {
    out.duration = options.duration.value_or(kDefaultVideoSeconds);
    if (!(out.duration > 0.0)) {
        return "--duration must be above 0 seconds";
    }
    if (out.duration > kMaxVideoSeconds) {
        return "--duration " + seconds_text(out.duration) + " is over the cap of " +
               seconds_text(kMaxVideoSeconds) + " seconds a clip -- generate a shorter clip";
    }
    out.width = kDefaultVideoWidth;
    out.height = kDefaultVideoHeight;
    if (!options.resolution.empty()) {
        const std::size_t x = options.resolution.find('x');
        const std::optional<int> width =
            x == std::string::npos
                ? std::nullopt
                : whole_number(std::string_view{options.resolution}.substr(0, x));
        const std::optional<int> height =
            x == std::string::npos
                ? std::nullopt
                : whole_number(std::string_view{options.resolution}.substr(x + 1));
        if (!width.has_value() || !height.has_value()) {
            return "--resolution takes WIDTHxHEIGHT, like " + std::to_string(kDefaultVideoWidth) +
                   "x" + std::to_string(kDefaultVideoHeight) + " (got '" + options.resolution +
                   "')";
        }
        out.width = *width;
        out.height = *height;
        for (const int side : {out.width, out.height}) {
            if (std::string refused = side_refusal(side); !refused.empty()) {
                return "--resolution " + options.resolution + ": " + refused;
            }
        }
    }
    out.steps = options.steps.value_or(kDefaultVideoSteps);
    if (out.steps < 1) {
        return "--steps must be at least 1";
    }
    if (out.steps > kMaxVideoSteps) {
        return "--steps " + std::to_string(out.steps) + " is over the cap of " +
               std::to_string(kMaxVideoSteps) + " steps";
    }
    out.fps = options.fps.value_or(kDefaultVideoFps);
    if (out.fps < kMinVideoFps || out.fps > kMaxVideoFps) {
        return "--fps " + std::to_string(out.fps) + " is outside " + std::to_string(kMinVideoFps) +
               "-" + std::to_string(kMaxVideoFps) + " frames a second";
    }
    out.frames = video_frames(out.duration, out.fps);
    return {};
}

std::string resolve_video_pipeline(const models::StoreRoots& roots, std::string_view given,
                                   std::string_view pointer, ResolvedPipeline& out) {
    const std::string_view name = given.empty() ? pointer : given;
    if (name.empty()) {
        return "no pipeline to run -- name one with --model, or point models.default_video at "
               "one ('apogee config set-default-video <pipeline>'); 'apogee models pull "
               "<owner>/<repo>' stores one";
    }
    out.source = given.empty() ? "models.default_video" : "--model";
    const models::StoreTarget target = models::resolve_store_target(roots, name);
    if (!target.error.empty()) {
        return target.error;
    }
    if (target.outside) {
        if (!models::is_pipeline_dir(target.path)) {
            return target.path.string() + " is not a pipeline (no model_index.json)";
        }
        out.handle = std::string{name};
        out.dir = target.path;
    } else if (!target.format.empty() && target.format != models::kPipelineFormat) {
        return "'" + std::string{name} + "' is " +
               (target.format == models::kGgufFormat ? std::string{"a GGUF"}
                                                     : "a " + target.format + " model") +
               ", not a pipeline";
    } else if (!target.id.empty()) {
        out.handle = models::weights_handle(target.model, models::kPipelineFormat, target.id);
        out.dir = target.path;
    } else {
        const std::vector<models::StoredPipeline> pipelines =
            models::list_store_pipelines(roots, target.model);
        if (pipelines.empty()) {
            return "'" + std::string{name} +
                   "' holds no pipeline -- 'apogee models list' shows "
                   "the store's";
        }
        if (pipelines.size() > 1) {
            return "'" + std::string{name} + "' holds " + std::to_string(pipelines.size()) +
                   " pipelines -- name one:" + handles_of(pipelines);
        }
        out.handle = models::weights_handle(pipelines.front().model, models::kPipelineFormat,
                                            pipelines.front().id);
        out.dir = pipelines.front().dir;
    }
    if (const std::optional<models::Snapshot> record = models::load_snapshot(out.dir)) {
        out.ref = record->ref;
        out.revision = record->revision;
    }
    const models::PipelineInfo info = models::read_pipeline_info(out.dir);
    out.pipeline_class = info.class_name;
    out.variant = info.variant();
    return {};
}

std::optional<std::string> narrate_video_event(const video::VideoEvent& event) {
    using Kind = video::VideoEvent::Kind;
    switch (event.kind) {
        case Kind::Message:
            return event.text;
        case Kind::Ready:
            return "running on " + event.ready.device +
                   (event.ready.dtype.empty() ? "" : " (" + event.ready.dtype + ")") +
                   (event.ready.mode == "i2v" ? ", from the seed image" : "") + " -- " +
                   expectation(event.ready.device);
        case Kind::Step:
            return std::nullopt;
        case Kind::Done:
            return std::to_string(event.frames) + " frames written, " +
                   std::to_string(event.width) + "x" + std::to_string(event.height);
        case Kind::Assembling:
            return "assembling " + std::to_string(event.frames) + " frames into the clip (ffmpeg)";
    }
    return std::nullopt;
}

VideoGenerateResult generate_clip(const harness::Config& config, const models::StoreRoots& roots,
                                  const VideoGenerateOptions& options,
                                  const VideoGenerateSeams& seams,
                                  const harness::CancellationToken& cancellation) {
    VideoGenerateResult result;
    const auto refuse = [&result](std::string why) {
        result.exit_code = 1;
        result.error = std::move(why);
        return result;
    };

    // Every refusal before anything runs.
    if (options.prompt.empty()) {
        return refuse("a prompt is required -- say what the clip shows");
    }
    harness::VideoSettings settings;
    if (std::string refused = resolve_video_settings(options, settings); !refused.empty()) {
        return refuse(std::move(refused));
    }
    std::error_code code;
    if (options.output.empty()) {
        return refuse("-o names the clip to write");
    }
    const std::filesystem::path output = std::filesystem::absolute(options.output, code);
    if (std::filesystem::exists(output, code)) {
        return refuse(output.string() +
                      " exists -- a generation never writes over a file; name another with -o");
    }
    if (!std::filesystem::is_directory(output.parent_path(), code)) {
        return refuse("no directory " + output.parent_path().string() + " for the clip");
    }
    std::filesystem::path image;
    if (!options.image.empty()) {
        image = std::filesystem::absolute(options.image, code);
        if (!std::filesystem::is_regular_file(image, code)) {
            return refuse("--image " + options.image.string() + " is not a file");
        }
    }
    ResolvedPipeline pipeline;
    if (std::string refused =
            resolve_video_pipeline(roots, options.model, config.models.default_video, pipeline);
        !refused.empty()) {
        return refuse(std::move(refused));
    }
    const video::VideoReadiness ready = seams.ready(pipeline);
    if (!ready.ready()) {
        return refuse(ready.message());
    }

    harness::VideoRun run;
    run.output = output;
    run.model = pipeline.handle;
    run.model_path = pipeline.dir;
    run.pipeline_class = pipeline.pipeline_class;
    run.model_ref = pipeline.ref;
    run.model_revision = pipeline.revision;
    run.variant = pipeline.variant;
    run.prompt = options.prompt;
    run.image = image;
    run.seed_source = options.seed.has_value() ? "given" : "generated";
    run.seed =
        options.seed.has_value() ? *options.seed : (seams.random_seed ? seams.random_seed() : 0);
    run.settings = settings;
    run.platform = seams.platform;
    run.apogee = std::string{version::semantic()};
    run.torch = ready.torch_version;
    run.diffusers = ready.diffusers_version;
    run.created_at = models::now_rfc3339();

    if (seams.say) {
        seams.say(pipeline.handle + " (" + pipeline.source + "): " +
                  std::to_string(settings.width) + "x" + std::to_string(settings.height) + ", " +
                  std::to_string(settings.frames) + " frames (" + seconds_text(settings.duration) +
                  " s at " + std::to_string(settings.fps) + " fps), " +
                  std::to_string(settings.steps) + " steps, seed " + std::to_string(run.seed));
    }
    const video::VideoRequest request{.pipeline = pipeline.dir,
                                      .prompt = options.prompt,
                                      .image = image,
                                      .seed = run.seed,
                                      .width = settings.width,
                                      .height = settings.height,
                                      .frames = settings.frames,
                                      .steps = settings.steps,
                                      .guidance = settings.guidance,
                                      .variant = pipeline.variant,
                                      .fps = settings.fps};
    const video::VideoOutcome outcome = video::generate_video(
        ready, request, output, seams.work_root, seams.on_event, cancellation, seams.run);
    run.mode = outcome.ready.mode.empty() ? (image.empty() ? "t2v" : "i2v") : outcome.ready.mode;
    run.device = outcome.ready.device;
    run.dtype = outcome.ready.dtype;
    if (!outcome.ready.torch.empty()) {
        run.torch = outcome.ready.torch;
    }
    if (!outcome.ready.diffusers.empty()) {
        run.diffusers = outcome.ready.diffusers;
    }
    run.codec = outcome.codec;
    run.seconds = static_cast<double>(outcome.elapsed.count()) / 1000.0;

    if (!outcome.ok) {
        run.status = outcome.cancelled ? "cancelled" : "failed";
        run.error = outcome.error;
        result.document = harness::video_run_document(run);
        result.exit_code = outcome.cancelled ? 130 : 1;
        result.error = outcome.cancelled
                           ? "cancelled -- nothing was written (" + output.string() + " untouched)"
                           : outcome.error + " -- " + output.string() + " was not written";
        return result;
    }

    // The clip landed: its record beside it, the document it is.
    run.status = "done";
    run.sidecar = harness::video_sidecar_path(output);
    result.document = harness::video_run_document(run);
    try {
        harness::write_file_atomically(run.sidecar, result.document.dump(2) + "\n");
    } catch (const std::exception& e) {
        result.exit_code = 1;
        result.error = "the clip is at " + output.string() +
                       ", but its record could not be "
                       "written: " +
                       e.what();
        return result;
    }
    result.lines = {output.string(),
                    std::to_string(settings.width) + "x" + std::to_string(settings.height) + ", " +
                        std::to_string(outcome.frames) + " frames at " +
                        std::to_string(settings.fps) + " fps (" + seconds_text(settings.duration) +
                        " s), " + std::to_string(settings.steps) + " steps, seed " +
                        std::to_string(run.seed) + " -- " + seconds_text(std::round(run.seconds)) +
                        " s on " +
                        (run.device.empty() ? std::string{"an unstated device"} : run.device),
                    "record: " + run.sidecar.string()};
    return result;
}

}  // namespace apogee::commands
