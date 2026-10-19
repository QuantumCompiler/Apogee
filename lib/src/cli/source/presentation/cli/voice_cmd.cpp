#include "cli/voice_cmd.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <string>

#include "cli/helpers.h"
#include "cli/interrupt.h"
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
    ReadFormat format = ReadFormat::Text;
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
    BusyLine busy{std::cerr, "loading the voice model", busy_options(false)};
    const auto say = [&busy](const std::string& line) {
        busy.above([&line] { std::cerr << "[voice] " << line << "\n"; });
    };
    VoiceSaySeams seams;
    seams.ready = [&config](const ResolvedVoiceModel& model) {
        const video::VideoReadiness runtime = ready_voice_runtime(config, "voice say");
        if (!runtime.ready() && runtime.refusal != video::VideoRefusal::NoDriver) {
            return runtime;
        }
        return video::probe_voice(video::VideoHost::current(), model.dir, model.handle);
    };
    seams.say = say;
    seams.on_event = [&](const video::VoiceEvent& event) {
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
        fail(result.error, result.exit_code);
    }
    if (flags.format == ReadFormat::Json) {
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
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* say = cmd->add_subcommand(
        "say", "Speak text into one WAV -- each segment's length measured, its record beside it");
    say->add_option("text", flags->options.text, "What to say")->required();
    say->add_option("-o,--output", flags->options.output,
                    "The WAV to write (never over an existing file)")
        ->type_name(kPathValue)
        ->required();
    say->add_option("-m,--model", flags->options.model,
                    "The voice model: a store handle, id or model, or a path (default: the "
                    "store's one)")
        ->type_name(kVoiceModelValue);
    say->add_option("--voice", flags->options.voice,
                    "One of the model's own voices (default: " + std::string{kDefaultVoice} +
                        " when it has it)")
        ->type_name(kVoiceValue);
    say->add_option("--speed", flags->speed, "How fast, 0.5-2.0 (default 1.0)");
    add_read_format(say, format);
    say->callback([&context, flags, format, say]() {
        flags->format = *format;
        voice_say(context, *flags, *say);
    });
}

}  // namespace apogee::commands
