#include "cli/video_cmd.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include "agentloop/reporter.h"
#include "agentloop/structured.h"
#include "backends/factory.h"
#include "cli/helpers.h"
#include "cli/interrupt.h"
#include "cli/job_stream.h"
#include "cli/video_compose.h"
#include "cli/video_generate.h"
#include "cli/video_runtime.h"
#include "cli/voice_say.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "harness/harness.h"
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
    JobFormat format = JobFormat::Text;
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

    const bool json = flags.format == JobFormat::Json;
    // The run's progress as events instead (52b): the driver's own, relayed.
    std::optional<JobStream> job;
    if (flags.format == JobFormat::StreamJson) {
        job.emplace("video generate");
    }
    BusyLine busy{std::cerr, "loading the pipeline", busy_options(job.has_value())};
    const auto say = [&busy, &job](const std::string& line) {
        if (job.has_value()) {
            job->events().on_notice(line);
            return;
        }
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
        if (job.has_value()) {
            relay_video_event(job->events(), event, 0);
            return;
        }
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
        if (job.has_value()) {
            job->failed(result.error);
        }
        fail(result.error, result.exit_code);
    }
    if (job.has_value()) {
        job->events().emit_document(result.document);
    } else if (json) {
        write_document(std::cout, result.document);
    } else {
        for (const std::string& line : result.lines) {
            std::cout << line << "\n";
        }
    }
    if (result.exit_code != 0) {
        if (job.has_value()) {
            job->failed(result.error);
        }
        fail(result.error, result.exit_code);
    }
}

struct ComposeFlags {
    VideoComposeOptions options;
    JobFormat format = JobFormat::Text;
};

/// The plan on `backend`: the system prompt and the user message as a
/// throwaway history (a side request), the model's thinking off, the answer
/// held to the schema by the structured-output path -- retried once,
/// validated always.
[[nodiscard]] video::PlanAnswer plan_on(const harness::Config& config,
                                        const std::filesystem::path& config_path,
                                        const std::string& backend, const std::string& system,
                                        const std::string& user, const std::string& schema) {
    video::PlanAnswer answer;
    harness::Harness harness{config};
    backends::BuildOptions build_options;
    build_options.config_path = config_path;
    const backends::BuildResult built = backends::build_providers(harness, build_options);
    for (const backends::BackendStatus& status : built.statuses) {
        if (status.name == backend && !status.constructed) {
            answer.error =
                "backend '" + backend + "' is configured but unavailable -- " + status.reason;
            return answer;
        }
    }
    std::vector<harness::ChatMessage> history{harness::ChatMessage::system(system),
                                              harness::ChatMessage::user(user)};
    agentloop::Options options;
    options.model = backend;
    options.max_tokens = 4096;
    options.stream_answer = false;
    options.side_request = true;
    options.thinking.mode = harness::ThinkingMode::Off;
    agentloop::NullReporter reporter;
    try {
        const agentloop::StructuredResult result = agentloop::run_structured(
            harness, history, options, reporter, std::string_view{schema});
        answer.json = result.json;
        answer.conforms = result.conforms;
        answer.attempts = result.attempts;
        for (const std::string& error : result.errors) {
            answer.error += (answer.error.empty() ? "" : "; ") + error;
        }
    } catch (const std::exception& e) {
        answer.error = e.what();
    }
    return answer;
}

void video_compose(const RootContext& context, const ComposeFlags& flags) {
    const std::filesystem::path config_path = harness::resolve_config_path(context.config_path);
    harness::Config config;
    try {
        config = harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        fail(e.what());
    }
    const models::StoreRoots roots = store_roots_of(config);
    // The run's progress as events instead (52b): each scene begun, and its
    // drivers' own events, relayed with the scene they belong to.
    std::optional<JobStream> job;
    if (flags.format == JobFormat::StreamJson) {
        job.emplace("video compose");
    }
    BusyLine busy{std::cerr, "planning", busy_options(job.has_value())};
    const auto say = [&busy, &job](const std::string& line) {
        if (job.has_value()) {
            job->events().on_notice(line);
            return;
        }
        busy.above([&line] { std::cerr << "[compose] " << line << "\n"; });
    };
    std::string scene_label;
    int said_tenth = -1;
    std::size_t scene_now = 0;

    VideoComposeSeams seams;
    seams.ready_video = [&config](const ResolvedPipeline& pipeline) {
        const video::VideoReadiness runtime = ready_video_runtime(config, "video compose");
        if (!runtime.ready() && runtime.refusal != video::VideoRefusal::NoDriver &&
            runtime.refusal != video::VideoRefusal::NoFfmpeg) {
            return runtime;
        }
        return video::probe_video(video::VideoHost::current(), pipeline.dir, pipeline.handle);
    };
    seams.ready_voice = [&config](const ResolvedVoiceModel& model) {
        const video::VideoReadiness runtime = ready_voice_runtime(config, "video compose");
        if (!runtime.ready() && runtime.refusal != video::VideoRefusal::NoDriver) {
            return runtime;
        }
        return video::probe_voice(video::VideoHost::current(), model.dir, model.handle);
    };
    seams.plan = [&config, &config_path, &busy](const std::string& backend,
                                                const std::string& system, const std::string& user,
                                                const std::string& schema) {
        busy.set("planning");
        return plan_on(config, config_path, backend, system, user, schema);
    };
    seams.speak = [&](const ResolvedVoiceModel& model, const std::string& voice,
                      const std::string& text, const std::filesystem::path& wav) {
        busy.set("speaking");
        VoiceSayOptions options;
        options.text = text;
        options.model = model.handle;
        options.voice = voice;
        options.output = wav;
        VoiceSaySeams run;
        run.ready = [](const ResolvedVoiceModel& resolved) {
            return video::probe_voice(video::VideoHost::current(), resolved.dir, resolved.handle);
        };
        if (job.has_value()) {
            run.on_event = [&](const video::VoiceEvent& event) {
                relay_voice_event(job->events(), event, scene_now);
            };
        }
        run.work_root = harness::video_work_dir();
        run.platform = platform::host_target();
        return spoken_from(say_text(roots, options, run, InterruptScope::token()));
    };
    seams.render = [&](const ResolvedPipeline& pipeline, const video::ClipAsk& ask,
                       const std::filesystem::path& mp4) {
        scene_label = "scene " + std::to_string(ask.scene);
        said_tenth = -1;
        busy.set(scene_label + ": loading the pipeline");
        VideoGenerateOptions options;
        options.prompt = ask.prompt;
        options.model = pipeline.handle;
        options.seed = ask.seed;
        options.duration = ask.seconds;
        options.image = ask.image;
        options.output = mp4;
        VideoGenerateSeams run;
        run.ready = [](const ResolvedPipeline& resolved) {
            return video::probe_video(video::VideoHost::current(), resolved.dir, resolved.handle);
        };
        run.on_event = [&](const video::VideoEvent& event) {
            if (job.has_value()) {
                relay_video_event(job->events(), event, ask.scene);
                return;
            }
            if (event.kind == video::VideoEvent::Kind::Step) {
                if (busy.active()) {
                    busy.report(scene_label + ": denoising", static_cast<std::size_t>(event.step),
                                static_cast<std::size_t>(event.steps));
                    return;
                }
                const int tenth = event.steps > 0 ? (event.step * 10) / event.steps : 0;
                if (tenth != said_tenth || event.step == event.steps) {
                    said_tenth = tenth;
                    say(scene_label + ": step " + std::to_string(event.step) + "/" +
                        std::to_string(event.steps));
                }
            }
        };
        run.work_root = harness::video_work_dir();
        run.platform = platform::host_target();
        return rendered_from(generate_clip(config, roots, options, run, InterruptScope::token()));
    };
    seams.can_burn = [] { return platform::ffmpeg_has_filter("subtitles", {}); };
    seams.random_seed = [] {
        std::random_device device;
        return static_cast<std::int64_t>(device() & 0x7fffffffU);
    };
    seams.say = [&](const std::string& line) {
        say(line);
        busy.set("working");
    };
    seams.on_scene = [&](std::size_t scene, std::size_t scenes, std::string_view state) {
        scene_now = scene;
        if (job.has_value()) {
            job->events().emit_scene(scene, scenes, state);
        }
    };
    seams.platform = platform::host_target();

    VideoComposeResult result;
    {
        const InterruptScope interrupt;
        result = compose_video(config, roots, flags.options, seams, InterruptScope::token());
    }
    busy.finish();
    if (result.document.is_null()) {
        if (job.has_value()) {
            job->failed(result.error);
        }
        fail(result.error, result.exit_code);
    }
    if (job.has_value()) {
        job->events().emit_document(result.document);
    } else if (flags.format == JobFormat::Json) {
        write_document(std::cout, result.document);
    } else {
        for (const std::string& line : result.lines) {
            std::cout << line << "\n";
        }
    }
    if (result.exit_code != 0) {
        if (job.has_value()) {
            job->failed(result.error);
        }
        fail(result.error, result.exit_code);
    }
}

}  // namespace

std::string_view VideoCommand::name() const noexcept {
    return "video";
}

std::string_view VideoCommand::summary() const noexcept {
    return "Generate video locally from a diffusion pipeline in the store -- a clip from text "
           "or a seed image, or a narrated video composed from a brief";
}

void VideoCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    auto flags = std::make_shared<GenerateFlags>();
    auto format = std::make_shared<JobFormat>(JobFormat::Text);
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
    generate
        ->add_option("--lora", flags->options.loras,
                     "A LoRA to apply: a .safetensors file (or a directory holding one), its "
                     "strength after a colon (default 1); give it again for more, at most " +
                         std::to_string(kMaxVideoLoras) + " -- with --from, the record's replaced")
        ->type_name(kPathValue);
    generate->add_flag("--no-lora", flags->options.no_lora,
                       "No LoRAs -- with --from, the record's cleared");
    add_job_format(generate, format, /*document=*/true);
    generate->callback([&context, flags, format, generate]() {
        flags->format = *format;
        video_generate(context, *flags, *generate);
    });

    auto compose_flags = std::make_shared<ComposeFlags>();
    auto compose_format = std::make_shared<JobFormat>(JobFormat::Text);
    CLI::App* compose = cmd->add_subcommand(
        "compose",
        "Compose a narrated, captioned video from a brief -- the scenes planned and said first, "
        "each one clip and one narration, the whole kept as a project beside it");
    compose->add_option("brief", compose_flags->options.brief,
                        "What the video is about (none with --from: the project's own)");
    compose
        ->add_option("-o,--output", compose_flags->options.output,
                     "The video to write (an mp4; never over an existing file -- the same "
                     "command again resumes its project)")
        ->type_name(kPathValue)
        ->required();
    compose
        ->add_option("--music", compose_flags->options.music,
                     "A music bed under the narration, ducked while it speaks")
        ->type_name(kPathValue);
    compose
        ->add_option("--assets", compose_flags->options.assets,
                     "A directory of your own stills and clips, offered to the plan by name")
        ->type_name(kPathValue);
    compose->add_flag_callback(
        "--no-captions", [compose_flags] { compose_flags->options.captions = false; },
        "No captions (default: the narration's own sentences, timed from the voice)");
    compose
        ->add_option("-m,--model", compose_flags->options.model,
                     "The pipeline: a store handle, id or model, or a path (default: "
                     "models.default_video)")
        ->type_name(kVideoPipelineValue);
    compose
        ->add_option("--voice-model", compose_flags->options.voice_model,
                     "The voice model: a store handle, id or model, or a path (default: the "
                     "store's one)")
        ->type_name(kVoiceModelValue);
    compose
        ->add_option("--voice", compose_flags->options.voice,
                     "One of the voice model's own voices (default: " + std::string{kDefaultVoice} +
                         " when it has it)")
        ->type_name(kVoiceValue);
    compose
        ->add_option("--planner", compose_flags->options.planner,
                     "The backend that plans the scenes (default: the chat role)")
        ->type_name(kBackendValue);
    compose
        ->add_option("--from", compose_flags->options.from,
                     "Edit a composed video: it, or its project -- its brief and settings taken, "
                     "every scene not changed carried over as it is")
        ->type_name(kPathValue);
    compose->add_option(
        "--edit", compose_flags->options.edit,
        "With --from: an instruction one structured pass rewrites the plan by -- the diff said "
        "first, only the changed scenes rendered again, their seeds held");
    compose->add_option("--redo-scene", compose_flags->options.redo,
                        "With --from: render a scene (from 1) again on a fresh seed; give it "
                        "again for more");
    compose->add_flag("--dry-run", compose_flags->options.dry_run,
                      "Plan on paper: a brief's one planning call, its plan and spend said and "
                      "kept beside the video, nothing rendered; with --from, the diff said and "
                      "the edited project printed, nothing written");
    add_job_format(compose, compose_format, /*document=*/true);
    compose->callback([&context, compose_flags, compose_format]() {
        compose_flags->format = *compose_format;
        video_compose(context, *compose_flags);
    });
}

}  // namespace apogee::commands
