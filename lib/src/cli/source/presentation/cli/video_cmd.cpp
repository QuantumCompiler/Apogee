#include "cli/video_cmd.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <random>
#include <string>

#include "cli/helpers.h"
#include "cli/interrupt.h"
#include "cli/video_generate.h"
#include "cli/video_runtime.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "machine/json_reporter.h"
#include "operations/renames.h"
#include "platform/ffmpeg.h"
#include "platform/platform.h"
#include "views/status_line.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message, int code = 1) {
    std::cerr << "apogee video: " << message << "\n";
    throw CLI::RuntimeError(code);
}

struct GenerateFlags {
    VideoGenerateOptions options;
    std::int64_t seed = 0;
    double duration = 0.0;
    int steps = 0;
    int fps = 0;
    ReadFormat format = ReadFormat::Text;
};

void video_generate(const RootContext& context, GenerateFlags flags, const CLI::App& verb) {
    const std::filesystem::path config_path = harness::resolve_config_path(context.config_path);
    harness::Config config;
    try {
        config = harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        fail(e.what());
    }
    VideoGenerateOptions options = flags.options;
    if (verb.count("--seed") > 0) {
        options.seed = flags.seed;
    }
    if (verb.count("--duration") > 0) {
        options.duration = flags.duration;
    }
    if (verb.count("--steps") > 0) {
        options.steps = flags.steps;
    }
    if (verb.count("--fps") > 0) {
        options.fps = flags.fps;
    }

    const bool json = flags.format == ReadFormat::Json;
    BusyLine busy{std::cerr, "loading the pipeline", busy_options(false)};
    const auto say = [&busy](const std::string& line) {
        busy.above([&line] { std::cerr << "[video] " << line << "\n"; });
    };
    int said_tenth = -1;

    VideoGenerateSeams seams;
    seams.ready = [&config](const ResolvedPipeline& pipeline) {
        // The environment and the set, asked for at a terminal; then the
        // whole ladder over this pipeline.
        const video::VideoReadiness runtime = ready_video_runtime(config, "video generate");
        if (!runtime.ready() && runtime.refusal != video::VideoRefusal::NoDriver &&
            runtime.refusal != video::VideoRefusal::NoFfmpeg) {
            return runtime;
        }
        return video::probe_video(video::VideoHost::current(), pipeline.dir, pipeline.handle);
    };
    seams.say = say;
    seams.on_event = [&](const video::VideoEvent& event) {
        if (event.kind == video::VideoEvent::Kind::Step) {
            if (busy.active()) {
                busy.report("denoising", static_cast<std::size_t>(event.step),
                            static_cast<std::size_t>(event.steps));
                return;
            }
            // On a pipe, a line at every tenth of the way, and the last.
            const int tenth = event.steps > 0 ? (event.step * 10) / event.steps : 0;
            if (tenth != said_tenth || event.step == event.steps) {
                said_tenth = tenth;
                say("step " + std::to_string(event.step) + "/" + std::to_string(event.steps));
            }
            return;
        }
        if (const std::optional<std::string> line = narrate_video_event(event)) {
            say(*line);
            busy.set(event.kind == video::VideoEvent::Kind::Assembling ? "assembling" : "working");
        }
    };
    seams.random_seed = [] {
        std::random_device device;
        return static_cast<std::int64_t>(device() & 0x7fffffffU);
    };
    seams.work_root = harness::video_work_dir();
    seams.last_frame = [](const std::filesystem::path& clip, const std::filesystem::path& png) {
        const harness::CancellationToken token = InterruptScope::token();
        platform::FfmpegLimits limits;
        limits.stopped = [token] { return token.stop_requested(); };
        std::string error;
        return platform::extract_last_frame(clip, png, limits, error) ? std::string{} : error;
    };
    seams.platform = platform::host_target();

    VideoGenerateResult result;
    {
        const InterruptScope interrupt;
        result =
            generate_clip(config, store_roots_of(config), options, seams, InterruptScope::token());
    }
    busy.finish();
    if (result.document.is_null()) {
        fail(result.error, result.exit_code);
    }
    if (json) {
        write_document(std::cout, result.document);
    } else {
        for (const std::string& line : result.lines) {
            std::cout << line << "\n";
        }
    }
    if (result.exit_code != 0) {
        fail(result.error, result.exit_code);
    }
}

}  // namespace

std::string_view VideoCommand::name() const noexcept {
    return "video";
}

std::string_view VideoCommand::summary() const noexcept {
    return "Generate video locally from a diffusion pipeline in the store -- from text or a "
           "seed image";
}

void VideoCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    auto flags = std::make_shared<GenerateFlags>();
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* generate = cmd->add_subcommand(
        "generate",
        "Generate one clip -- narrated step by step, written only when whole, its record beside "
        "it");
    generate->add_option("prompt", flags->options.prompt,
                         "What the clip shows (with --from, a new prompt over the recorded one)");
    generate
        ->add_option("-o,--output", flags->options.output,
                     "The clip to write (an mp4; never over an existing file)")
        ->type_name(kPathValue)
        ->required();
    generate
        ->add_option("--image", flags->options.image,
                     "A seed image: the clip starts from it (image-to-video)")
        ->type_name(kPathValue);
    generate
        ->add_option("-m,--model", flags->options.model,
                     "The pipeline: a store handle, id or model, or a path (default: "
                     "models.default_video)")
        ->type_name(kVideoPipelineValue);
    generate->add_option("--seed", flags->seed,
                         "The seed (default: one drawn, and kept in the record)");
    generate->add_option("--duration", flags->duration,
                         "Seconds of video, at most " +
                             std::to_string(static_cast<int>(kMaxVideoSeconds)) + " (default " +
                             std::to_string(static_cast<int>(kDefaultVideoSeconds)) + ")");
    generate->add_option("--resolution", flags->options.resolution,
                         "WIDTHxHEIGHT, each a multiple of " + std::to_string(kVideoSideMultiple) +
                             " up to " + std::to_string(kMaxVideoSide) + " (default " +
                             std::to_string(kDefaultVideoWidth) + "x" +
                             std::to_string(kDefaultVideoHeight) + ")");
    generate->add_option("--steps", flags->steps,
                         "Denoising steps, at most " + std::to_string(kMaxVideoSteps) +
                             " (default " + std::to_string(kDefaultVideoSteps) + ")");
    generate->add_option("--fps", flags->fps,
                         "Frames a second, " + std::to_string(kMinVideoFps) + "-" +
                             std::to_string(kMaxVideoFps) + " (default " +
                             std::to_string(kDefaultVideoFps) + ")");
    generate
        ->add_option("--from", flags->options.from,
                     "Run a recorded run again: its record, or a clip with its record beside it "
                     "-- every setting not given here taken from it, the seed held")
        ->type_name(kPathValue);
    generate
        ->add_option("--extend", flags->options.extend,
                     "Continue a clip from its last frame (image-to-video), matching its record "
                     "when it has one")
        ->type_name(kPathValue);
    CLI::Option* draft = generate->add_flag_callback(
        "--draft", [flags] { flags->options.preset = std::string{kDraftPreset.name}; },
        "A cheap preview: " + std::to_string(kDraftPreset.steps) +
            " steps at the same frame and seed, so the final of the same seed is this clip");
    CLI::Option* final_preset = generate->add_flag_callback(
        "--final", [flags] { flags->options.preset = std::string{kFinalPreset.name}; },
        "The full render: " + std::to_string(kFinalPreset.steps) +
            " steps -- with --from a draft, that draft's clip at full quality");
    draft->excludes(final_preset);
    add_read_format(generate, format);
    generate->callback([&context, flags, format, generate]() {
        flags->format = *format;
        video_generate(context, *flags, *generate);
    });
}

}  // namespace apogee::commands
