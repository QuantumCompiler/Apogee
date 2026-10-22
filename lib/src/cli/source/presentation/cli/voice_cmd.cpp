#include "cli/voice_cmd.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "cli/helpers.h"
#include "cli/interrupt.h"
#include "cli/job_stream.h"
#include "cli/video_runtime.h"
#include "cli/voice_say.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "machine/json_reporter.h"
#include "operations/renames.h"
#include "platform/platform.h"
#include "views/status_line.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message, int code = 1) {
    std::cerr << "apogee voice: " << message << "\n";
    throw CLI::RuntimeError(code);
}

struct SayFlags {
    VoiceSayOptions options;
    double speed = 1.0;
    JobFormat format = JobFormat::Text;
};

void voice_say(const RootContext& context, SayFlags flags, const CLI::App& verb) {
    harness::Config config;
    try {
        config = harness::load_config(harness::resolve_config_path(context.config_path));
    } catch (const harness::ConfigError& e) {
        fail(e.what());
    }
    VoiceSayOptions options = flags.options;
    if (verb.count("--speed") > 0) {
        options.speed = flags.speed;
    }
    // The run's progress as events instead (52b): the driver's own, relayed.
    std::optional<JobStream> job;
    if (flags.format == JobFormat::StreamJson) {
        job.emplace("voice say");
    }
    BusyLine busy{std::cerr, "loading the voice model", busy_options(job.has_value())};
    const auto say = [&busy, &job](const std::string& line) {
        if (job.has_value()) {
            job->events().on_notice(line);
            return;
        }
        busy.above([&line] { std::cerr << "[voice] " << line << "\n"; });
    };
    VoiceSaySeams seams;
    seams.ready = [&config](const ResolvedVoiceModel& model) {
        return ready_voice_engine(config, model.dir, model.handle, model.engine, "voice say");
    };
    seams.say = say;
    seams.on_event = [&](const video::VoiceEvent& event) {
        if (job.has_value()) {
            relay_voice_event(job->events(), event, 0);
            return;
        }
        if (const std::optional<std::string> line = narrate_voice_event(event)) {
            say(*line);
            busy.set("speaking");
        }
    };
    seams.work_root = harness::video_work_dir();
    seams.platform = platform::host_target();

    VoiceSayResult result;
    {
        const InterruptScope interrupt;
        result = say_text(store_roots_of(config), options, seams, InterruptScope::token());
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

std::string_view VoiceCommand::name() const noexcept {
    return "voice";
}

std::string_view VoiceCommand::summary() const noexcept {
    return "Speak text with a voice model in the store, into a WAV";
}

void VoiceCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    auto flags = std::make_shared<SayFlags>();
    auto format = std::make_shared<JobFormat>(JobFormat::Text);
    CLI::App* say = cmd->add_subcommand(
        "say", "Speak text into one WAV -- each segment's length measured, its record beside it");
    say->add_option("text", flags->options.text, "What to say")->required();
    say->add_option("-o,--output", flags->options.output,
                    "The WAV to write (never over an existing file)")
        ->type_name(kPathValue)
        ->required();
    say->add_option("-m,--model", flags->options.model,
                    "The voice model, or its MLX build: a store handle, id or model, or a path "
                    "(default: the store's one)")
        ->type_name(kVoiceModelValue);
    say->add_option("--voice", flags->options.voice,
                    "One of the model's own voices (default: " + std::string{kDefaultVoice} +
                        " when it has it)")
        ->type_name(kVoiceValue);
    say->add_option("--speed", flags->speed, "How fast, 0.5-2.0 (default 1.0)");
    say->add_option("--engine", flags->options.engine,
                    "What it speaks on: torch (a voice model as Kokoro publishes it) or mlx (its "
                    "MLX build, through mlx-audio) -- the model's directory decides, so this only "
                    "names it or is refused (default: auto)")
        ->type_name(words_value(video::video_engine_names()));
    add_job_format(say, format, /*document=*/true);
    say->callback([&context, flags, format, say]() {
        flags->format = *format;
        voice_say(context, *flags, *say);
    });
}

}  // namespace apogee::commands
