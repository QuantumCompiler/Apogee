#include "cli/video_generate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <random>
#include <system_error>
#include <utility>

#include "contracts/config_edit.h"
#include "modelstore/mlx_info.h"
#include "modelstore/pipeline_info.h"
#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "platform/platform.h"
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

/// `handles`, one a line.
[[nodiscard]] std::string handles_of(const std::vector<std::string>& handles) {
    std::string out;
    for (const std::string& handle : handles) {
        out += "\n  " + handle;
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

/// A file removed when the run is done with it, however it ends.
class RemovedAtEnd {
public:
    explicit RemovedAtEnd(std::filesystem::path path) : path_{std::move(path)} {}

    ~RemovedAtEnd() {
        if (!path_.empty()) {
            std::error_code code;
            std::filesystem::remove(path_, code);
        }
    }

    RemovedAtEnd(const RemovedAtEnd&) = delete;
    RemovedAtEnd& operator=(const RemovedAtEnd&) = delete;
    RemovedAtEnd(RemovedAtEnd&&) = delete;
    RemovedAtEnd& operator=(RemovedAtEnd&&) = delete;

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : ", ") + part;
    }
    return out;
}

/// What the driver is handed of `loras`: each file and its strength.
[[nodiscard]] std::vector<video::LoraFile> lora_files(
    const std::vector<harness::VideoLora>& loras) {
    std::vector<video::LoraFile> out;
    for (const harness::VideoLora& lora : loras) {
        out.push_back(video::LoraFile{.file = lora.path, .scale = lora.scale});
    }
    return out;
}

/// `count` LoRAs, in words.
[[nodiscard]] std::string lora_words(std::size_t count) {
    return std::to_string(count) + " LoRA" + (count == 1 ? "" : "s");
}

/// The LoRAs as the plan line says them: `style.safetensors x0.8`.
[[nodiscard]] std::string loras_said(const std::vector<harness::VideoLora>& loras) {
    std::string out;
    for (const harness::VideoLora& lora : loras) {
        std::array<char, 32> scale{};
        std::snprintf(scale.data(), scale.size(), "%g", lora.scale);
        out += (out.empty() ? "" : ", ") + lora.path.filename().string() + " x" + scale.data();
    }
    return out;
}

/// The record `--from` names: the file itself, or the one beside a clip.
[[nodiscard]] std::string read_from(const std::filesystem::path& given, harness::VideoRun& out,
                                    std::filesystem::path& document) {
    std::error_code code;
    const bool named = given.extension() == ".json";
    document = named ? given : harness::video_sidecar_path(given);
    if (!std::filesystem::is_regular_file(document, code)) {
        if (named) {
            return "no record at " + given.string();
        }
        return "no record beside " + given.string() + " (" + document.filename().string() +
               " is not there) -- settings are never guessed from pixels; name the record "
               "itself with --from <record.json>";
    }
    std::string error;
    std::optional<harness::VideoRun> run = harness::read_video_run(document, error);
    if (!run.has_value()) {
        return error;
    }
    out = std::move(*run);
    return {};
}

/// `options` as a run seeded from a record (`--from`) or continuing a clip
/// (`--extend`) has them, the presets applied (49d): `effective` holds the
/// result, `parent` where it came from, `told` the lines saying what came
/// from where. The refusal, or empty.
[[nodiscard]] std::string seed_run(const VideoGenerateOptions& options, std::string_view platform,
                                   VideoGenerateOptions& effective, harness::VideoParent& parent,
                                   std::vector<std::string>& told,
                                   std::vector<harness::VideoLora>& held) {
    if (!options.from.empty() && !options.extend.empty()) {
        return "--from and --extend are two moves -- take one";
    }
    if (!options.extend.empty() && !options.image.empty()) {
        return "--extend continues from the clip's last frame; it takes no --image";
    }
    std::error_code code;
    std::vector<std::string> taken;
    std::vector<std::string> changed;
    // What a record supplies a run, each setting the flags left alone.
    const auto take = [&](const harness::VideoRun& record, bool whole) {
        if (options.model.empty()) {
            effective.model = record.model;
            taken.push_back("model " + record.model);
        } else if (whole) {
            changed.push_back("model " + options.model);
        }
        if (options.prompt.empty()) {
            effective.prompt = record.prompt;
            taken.push_back("the prompt");
        } else if (whole) {
            changed.push_back("the prompt");
        }
        const std::string size =
            std::to_string(record.settings.width) + "x" + std::to_string(record.settings.height);
        if (options.resolution.empty()) {
            effective.resolution = size;
            taken.push_back(size);
        } else if (whole) {
            changed.push_back(options.resolution);
        }
        if (!options.fps.has_value()) {
            effective.fps = record.settings.fps;
            taken.push_back(std::to_string(record.settings.fps) + " fps");
        } else if (whole) {
            changed.push_back(std::to_string(*options.fps) + " fps");
        }
        if (!whole) {
            return;
        }
        if (!options.duration.has_value()) {
            effective.duration = record.settings.duration;
            taken.push_back(seconds_text(record.settings.duration) + " s");
        } else {
            changed.push_back(seconds_text(*options.duration) + " s");
        }
        if (!options.steps.has_value()) {
            effective.steps = record.settings.steps;
            taken.push_back(std::to_string(record.settings.steps) + " steps");
        } else {
            changed.push_back(std::to_string(*options.steps) + " steps");
        }
        if (!options.seed.has_value()) {
            effective.seed = record.seed;
            taken.push_back("seed " + std::to_string(record.seed) + " (held)");
        } else {
            changed.push_back("seed " + std::to_string(*options.seed));
        }
        // The engine, held as the seed is (53l) while the model is the
        // record's: a run again runs where that one ran, or is refused.
        if (options.engine.empty() && options.model.empty()) {
            effective.engine = record.engine;
            if (record.engine != video::to_string(video::VideoEngine::Torch)) {
                taken.push_back("engine " + record.engine);
            }
        } else if (!options.engine.empty() && options.engine != "auto" &&
                   options.engine != record.engine) {
            changed.push_back("engine " + options.engine);
        }
        // The LoRAs, held as the seed is (49e): verified before the run.
        if (!options.loras.empty()) {
            changed.push_back(lora_words(options.loras.size()) + " given");
        } else if (options.no_lora) {
            if (!record.loras.empty()) {
                changed.push_back("no LoRAs");
            }
        } else if (!record.loras.empty()) {
            held = record.loras;
            taken.push_back(lora_words(record.loras.size()) + " (held)");
        }
    };

    if (!options.from.empty()) {
        harness::VideoRun record;
        std::filesystem::path document;
        if (std::string refused =
                read_from(std::filesystem::absolute(options.from, code), record, document);
            !refused.empty()) {
            return refused;
        }
        parent = harness::VideoParent{.move = "from", .document = document, .clip = record.output};
        take(record, true);
        if (!options.image.empty()) {
            changed.push_back("the seed image");
        } else if (!record.parent.continued.empty()) {
            // It continued a clip: run again, it continues that clip again.
            if (!std::filesystem::is_regular_file(record.parent.continued, code)) {
                return "the clip that run continued, " + record.parent.continued.string() +
                       ", is gone -- its last frame cannot be taken again; pass --image";
            }
            parent.continued = record.parent.continued;
            taken.push_back("the last frame of " + record.parent.continued.string());
        } else if (!record.image.empty()) {
            if (!std::filesystem::is_regular_file(record.image, code)) {
                return "the record's seed image " + record.image.string() +
                       " is gone -- pass --image";
            }
            effective.image = record.image;
            taken.push_back("the seed image");
        }
        told.push_back("from " + document.string() + ": " + joined(taken));
        if (!record.platform.empty() && !platform.empty() && record.platform != platform) {
            told.push_back("recorded on " + record.platform +
                           (record.device.empty() ? "" : " (" + record.device + ")") +
                           "; this is " + std::string{platform} +
                           " -- the same seed may not make the same clip here");
        }
    } else if (!options.extend.empty()) {
        const std::filesystem::path clip = std::filesystem::absolute(options.extend, code);
        if (!std::filesystem::is_regular_file(clip, code)) {
            return "--extend " + options.extend.string() + " is not a file";
        }
        parent = harness::VideoParent{.move = "extend", .clip = clip, .continued = clip};
        // Its record, when it has one: the continuation matches its frame,
        // its rate and its model.
        const std::filesystem::path document = harness::video_sidecar_path(clip);
        std::string ignored;
        if (const std::optional<harness::VideoRun> record =
                std::filesystem::is_regular_file(document, code)
                    ? harness::read_video_run(document, ignored)
                    : std::nullopt) {
            parent.document = document;
            take(*record, false);
        }
        told.push_back("extending " + clip.string() + " from its last frame" +
                       (taken.empty() ? "" : "; from its record: " + joined(taken)));
    }
    if (!options.preset.empty()) {
        const VideoPreset& preset =
            options.preset == kDraftPreset.name ? kDraftPreset : kFinalPreset;
        if (!options.steps.has_value()) {
            effective.steps = preset.steps;
            changed.push_back(std::to_string(preset.steps) + " steps (--" +
                              std::string{preset.name} + ")");
        }
    }
    if (!options.from.empty()) {
        told.push_back(changed.empty() ? "changed: nothing -- the same run again"
                                       : "changed: " + joined(changed));
    } else if (!options.preset.empty()) {
        told.push_back("preset: " + options.preset + " -- " +
                       std::to_string(effective.steps.value_or(kDefaultVideoSteps)) + " steps");
    }
    return {};
}

/// The refusal of Wan's original layout handed to a run (M24) -- the set at
/// `dir`, `handle` as `models convert` takes it, `subject` what was given
/// with its verb -- or empty when the set there is none.
[[nodiscard]] std::string wan_refusal(const models::StoreRoots& roots, std::string_view subject,
                                      std::string_view handle, const std::filesystem::path& dir) {
    const std::optional<models::DiffusionModel> diffusion =
        models::stored_diffusion_model(roots, dir);
    return diffusion.has_value() ? models::wan_layout_run_refusal(subject, handle, *diffusion)
                                 : std::string{};
}

/// The same for a model's SafeTensors set named in the store: the one its
/// id names, else its newest -- the one `models convert` reads.
[[nodiscard]] std::string stored_wan_refusal(const models::StoreRoots& roots,
                                             std::string_view subject,
                                             const models::StoreTarget& target) {
    if (!target.id.empty()) {
        return wan_refusal(
            roots, subject,
            models::weights_handle(target.model, models::kSafetensorsFormat, target.id),
            target.path);
    }
    const std::optional<models::StoredSnapshot> newest =
        models::newest_snapshot(roots, target.model);
    return newest.has_value()
               ? wan_refusal(
                     roots, subject,
                     models::weights_handle(newest->model, models::kSafetensorsFormat, newest->id),
                     newest->dir)
               : std::string{};
}

}  // namespace

std::string parse_lora(std::string_view given, std::filesystem::path& path, double& scale) {
    scale = 1.0;
    std::string_view file = given;
    if (const std::size_t colon = given.rfind(':'); colon != std::string_view::npos) {
        const std::string tail{given.substr(colon + 1)};
        char* end = nullptr;
        const double value = tail.empty() ? 0.0 : std::strtod(tail.c_str(), &end);
        // Only a number after the last colon is a strength: `C:\lora` and a
        // name holding a colon stay paths.
        if (!tail.empty() && end == tail.c_str() + tail.size()) {
            if (!std::isfinite(value) || value <= 0.0 || value > kMaxLoraScale) {
                return "--lora " + std::string{given} + ": its strength " + tail +
                       " is outside (0, " + seconds_text(kMaxLoraScale) + "]";
            }
            scale = value;
            file = given.substr(0, colon);
        }
    }
    if (file.empty()) {
        return "--lora takes <path>[:<scale>] -- name the LoRA's file";
    }
    path = std::filesystem::path{std::string{file}};
    return {};
}

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
        // A directory outside the store runs on what it is (53l).
        const std::optional<video::VideoEngine> engine = video::engine_of_directory(target.path);
        if (!engine.has_value()) {
            // Wan's original layout is said with what runs it (M24).
            if (std::string wan = wan_refusal(roots, target.path.string() + " is",
                                              target.path.string(), target.path);
                !wan.empty()) {
                return wan;
            }
            return target.path.string() +
                   " is not a pipeline (no model_index.json), nor an MLX video model";
        }
        out.handle = std::string{name};
        out.dir = target.path;
        out.engine = *engine;
    } else if (target.format == models::kMlxFormat && !target.id.empty()) {
        // An MLX directory runs here only as a video model (53l).
        const models::MlxKind kind = models::read_mlx_kind(target.path);
        if (kind.word != models::kMlxVideo) {
            return "'" + std::string{name} + "' is an MLX " + kind.word +
                   " model, not a pipeline or an MLX video model -- " +
                   models::describe_mlx_kind(kind.word);
        }
        out.handle = models::weights_handle(target.model, models::kMlxFormat, target.id);
        out.dir = target.path;
        out.engine = video::VideoEngine::Mlx;
    } else if (!target.format.empty() && target.format != models::kPipelineFormat &&
               target.format != models::kMlxFormat) {
        // Wan's original layout (53j) is said with what runs it (M24).
        if (target.format == models::kSafetensorsFormat) {
            if (std::string wan =
                    stored_wan_refusal(roots, "'" + std::string{name} + "' is", target);
                !wan.empty()) {
                return wan;
            }
        }
        return "'" + std::string{name} + "' is " +
               (target.format == models::kGgufFormat ? std::string{"a GGUF"}
                                                     : "a " + target.format + " model") +
               ", not a pipeline or an MLX video model";
    } else if (!target.id.empty()) {
        out.handle = models::weights_handle(target.model, models::kPipelineFormat, target.id);
        out.dir = target.path;
    } else {
        // A model by name: the one video model it holds -- a pipeline, or an
        // MLX video model (53l).
        std::vector<std::pair<std::string, std::filesystem::path>> found;
        for (const models::StoredPipeline& stored :
             models::list_store_pipelines(roots, target.model)) {
            found.emplace_back(
                models::weights_handle(stored.model, models::kPipelineFormat, stored.id),
                stored.dir);
        }
        for (const models::StoredMlx& stored : models::list_store_mlx(roots, target.model)) {
            if (models::read_mlx_kind(stored.dir).word == models::kMlxVideo) {
                found.emplace_back(
                    models::weights_handle(stored.model, models::kMlxFormat, stored.id),
                    stored.dir);
            }
        }
        if (found.empty()) {
            // Its Wan set, as Wan publishes it beside the pipeline, said with
            // what runs it (M24).
            if (std::string wan =
                    stored_wan_refusal(roots, "'" + std::string{name} + "' holds", target);
                !wan.empty()) {
                return wan;
            }
            return "'" + std::string{name} +
                   "' holds no pipeline and no MLX video model -- 'apogee models list' shows "
                   "the store's";
        }
        if (found.size() > 1) {
            std::vector<std::string> handles;
            for (const auto& each : found) {
                handles.push_back(each.first);
            }
            std::ranges::sort(handles);
            return "'" + std::string{name} + "' holds " + std::to_string(found.size()) +
                   " video models -- name one:" + handles_of(handles);
        }
        out.handle = found.front().first;
        out.dir = found.front().second;
        out.engine = video::engine_of_directory(out.dir).value_or(video::VideoEngine::Torch);
    }
    if (const std::optional<models::Snapshot> record = models::load_snapshot(out.dir)) {
        out.ref = record->ref;
        out.revision = record->revision;
    }
    if (out.engine == video::VideoEngine::Mlx) {
        // mlx-video's Wan, by the mode its config names.
        const std::string mode = video::mlx_video_mode(out.dir);
        out.pipeline_class = mode.empty() ? std::string{"Wan"} : "Wan " + mode;
        return {};
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
            // MLX runs on Apple silicon alone, its device MLX's own (53l).
            if (event.ready.engine == video::to_string(video::VideoEngine::Mlx)) {
                return "running on MLX (" + event.ready.device +
                       (event.ready.dtype.empty() ? "" : ", " + event.ready.dtype) + ")" +
                       (event.ready.mode == "i2v" ? ", from the seed image" : "") +
                       " -- expect minutes per clip on Apple silicon";
            }
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

    // A run seeded from a record, or continuing a clip (49d): the settings
    // the flags left alone filled from it, and what came from where said.
    VideoGenerateOptions effective = options;
    harness::VideoParent parent;
    std::vector<std::string> told;
    std::vector<harness::VideoLora> held;
    if (options.no_lora && !options.loras.empty()) {
        return refuse("--lora and --no-lora are two sets -- take one");
    }
    if (std::string refused = seed_run(options, seams.platform, effective, parent, told, held);
        !refused.empty()) {
        return refuse(std::move(refused));
    }

    // Every refusal before anything runs.
    if (effective.prompt.empty()) {
        return refuse("a prompt is required -- say what the clip shows");
    }
    harness::VideoSettings settings;
    if (std::string refused = resolve_video_settings(effective, settings); !refused.empty()) {
        return refuse(std::move(refused));
    }
    std::error_code code;
    if (effective.output.empty()) {
        return refuse("-o names the clip to write");
    }
    const std::filesystem::path output = std::filesystem::absolute(effective.output, code);
    if (std::filesystem::exists(output, code)) {
        return refuse(output.string() +
                      " exists -- a generation never writes over a file; name another with -o");
    }
    if (!std::filesystem::is_directory(output.parent_path(), code)) {
        return refuse("no directory " + output.parent_path().string() + " for the clip");
    }
    std::filesystem::path image;
    if (!effective.image.empty()) {
        image = std::filesystem::absolute(effective.image, code);
        if (!std::filesystem::is_regular_file(image, code)) {
            return refuse("--image " + effective.image.string() + " is not a file");
        }
    }
    // The LoRAs (49e): file facts before anything runs, each hashed for the
    // record; a record's, verified against the hashes it kept.
    if (options.loras.size() > kMaxVideoLoras) {
        return refuse(std::to_string(options.loras.size()) + " LoRAs is over the cap of " +
                      std::to_string(kMaxVideoLoras) + " a run");
    }
    std::vector<harness::VideoLora> loras;
    for (const std::string& given : options.loras) {
        std::filesystem::path named;
        double scale = 1.0;
        if (std::string refused = parse_lora(given, named, scale); !refused.empty()) {
            return refuse(std::move(refused));
        }
        std::filesystem::path file;
        if (std::string refused = video::lora_file(std::filesystem::absolute(named, code), file);
            !refused.empty()) {
            return refuse(std::move(refused));
        }
        loras.push_back(
            harness::VideoLora{.path = file, .sha256 = models::file_sha256(file), .scale = scale});
    }
    for (const harness::VideoLora& lora : held) {
        if (!std::filesystem::is_regular_file(lora.path, code)) {
            return refuse("the record's LoRA " + lora.path.string() +
                          " is gone -- --lora names the set to run with, --no-lora runs "
                          "without");
        }
        if (models::file_sha256(lora.path) != lora.sha256) {
            return refuse("the record's LoRA " + lora.path.string() +
                          " has changed since that run (its sha256 is not the recorded one) -- "
                          "--lora runs it as it is now, --no-lora runs without");
        }
        loras.push_back(lora);
    }
    ResolvedPipeline pipeline;
    if (std::string refused =
            resolve_video_pipeline(roots, effective.model, config.models.default_video, pipeline);
        !refused.empty()) {
        return refuse(std::move(refused));
    }
    if (options.model.empty() && !effective.model.empty()) {
        pipeline.source = parent.move == "extend" ? "the continued clip's record" : "the record";
    }
    // The engine (53l): the model's directory says it; `--engine`, or the
    // engine a record ran on, only names it or is refused -- before the
    // ladder, so nothing is asked or spawned for an engine that cannot run.
    video::VideoEngine engine = pipeline.engine;
    if (std::string refused =
            video::choose_video_engine(effective.engine, pipeline.engine, pipeline.handle, engine);
        !refused.empty()) {
        if (video::video_engine_from_string(effective.engine).has_value()) {
            refused = (options.engine.empty() ? "the record ran on " + effective.engine
                                              : "--engine " + options.engine) +
                      ": " + refused;
        }
        return refuse(std::move(refused));
    }
    pipeline.engine = engine;
    const video::VideoReadiness ready = seams.ready(pipeline);
    if (!ready.ready()) {
        return refuse(ready.message());
    }
    // diffusers loads a LoRA through peft; mlx-video through its own loader.
    if (!loras.empty() && engine == video::VideoEngine::Torch && !ready.peft) {
        return refuse("a LoRA loads through peft, which the video runtime does not have yet -- " +
                      std::string{video::kVideoSetupRemedy});
    }
    // A continuation's seed image: the clip's last frame, taken now and
    // gone when the run is.
    std::filesystem::path frame;
    if (!parent.continued.empty()) {
        std::filesystem::create_directories(seams.work_root, code);
        frame = seams.work_root / ("frame-" + std::to_string(platform::current_process_id()) + "-" +
                                   std::to_string(std::random_device{}()) + ".png");
        const std::string taken =
            seams.last_frame ? seams.last_frame(parent.continued, frame) : "no way to take a frame";
        if (!taken.empty()) {
            std::filesystem::remove(frame, code);
            return refuse("could not take the last frame of " + parent.continued.string() + ": " +
                          taken);
        }
        image = frame;
    }
    const RemovedAtEnd frame_removed{frame};

    harness::VideoRun run;
    run.output = output;
    run.model = pipeline.handle;
    run.model_path = pipeline.dir;
    run.pipeline_class = pipeline.pipeline_class;
    run.model_ref = pipeline.ref;
    run.model_revision = pipeline.revision;
    run.variant = pipeline.variant;
    run.prompt = effective.prompt;
    // A continuation's image is its parent clip's last frame, not a file
    // that outlives the run: the parent says it.
    run.image = parent.continued.empty() ? image : std::filesystem::path{};
    run.parent = parent;
    run.preset = effective.preset;
    // `--seed`, a record's seed run again (49d), or one drawn now.
    run.seed_source = options.seed.has_value()     ? "given"
                      : effective.seed.has_value() ? "held"
                                                   : "generated";
    run.seed = effective.seed.has_value() ? *effective.seed
                                          : (seams.random_seed ? seams.random_seed() : 0);
    run.settings = settings;
    run.platform = seams.platform;
    run.apogee = std::string{version::semantic()};
    run.torch = ready.torch_version;
    run.diffusers = ready.diffusers_version;
    run.created_at = models::now_rfc3339();
    run.loras = loras;
    if (!loras.empty() && engine == video::VideoEngine::Torch) {
        run.peft = ready.peft_version;
    }
    run.engine = std::string{video::to_string(engine)};
    if (engine == video::VideoEngine::Mlx) {
        run.mlx = ready.mlx_version;
        run.mlx_video = ready.mlx_video_version;
    }

    if (seams.say) {
        for (const std::string& line : told) {
            seams.say(line);
        }
        seams.say(pipeline.handle + " (" + pipeline.source + "): " +
                  std::to_string(settings.width) + "x" + std::to_string(settings.height) + ", " +
                  std::to_string(settings.frames) + " frames (" + seconds_text(settings.duration) +
                  " s at " + std::to_string(settings.fps) + " fps), " +
                  std::to_string(settings.steps) + " steps, seed " + std::to_string(run.seed) +
                  (loras.empty() ? std::string{} : ", LoRAs: " + loras_said(loras)) +
                  (engine == video::VideoEngine::Mlx ? ", on MLX (mlx-video)" : ""));
    }
    const video::VideoRequest request{.pipeline = pipeline.dir,
                                      .prompt = effective.prompt,
                                      .image = image,
                                      .seed = run.seed,
                                      .width = settings.width,
                                      .height = settings.height,
                                      .frames = settings.frames,
                                      .steps = settings.steps,
                                      .guidance = settings.guidance,
                                      .variant = pipeline.variant,
                                      .fps = settings.fps,
                                      .loras = lora_files(loras),
                                      .engine = engine,
                                      .tokenizer = ready.tokenizer};
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
    if (!outcome.ready.mlx.empty()) {
        run.mlx = outcome.ready.mlx;
    }
    if (!outcome.ready.mlx_video.empty()) {
        run.mlx_video = outcome.ready.mlx_video;
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
    result.lines = {
        output.string(),
        std::to_string(settings.width) + "x" + std::to_string(settings.height) + ", " +
            std::to_string(outcome.frames) + " frames at " + std::to_string(settings.fps) +
            " fps (" + seconds_text(settings.duration) + " s), " + std::to_string(settings.steps) +
            " steps, seed " + std::to_string(run.seed) + " -- " +
            seconds_text(std::round(run.seconds)) + " s on " +
            (engine == video::VideoEngine::Mlx
                 ? "MLX" + (run.device.empty() ? std::string{} : " (" + run.device + ")")
             : run.device.empty() ? std::string{"an unstated device"}
                                  : run.device),
        "record: " + run.sidecar.string()};
    return result;
}

}  // namespace apogee::commands
