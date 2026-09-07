#include "commands/complete.h"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "agent/fetch_url.h"
#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "ansi/ansi.h"
#include "backends/factory.h"
#include "backends/http_client.h"
#include "commands/ask_prompt.h"
#include "commands/cli_reporter.h"
#include "commands/helpers.h"
#include "commands/json_reporter.h"
#include "commands/terminal.h"
#include "harness/config.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "harness/paths.h"
#include "harness/roles.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

/// Flags, kept alive for the app's lifetime so CLI11 callbacks can read them.
struct CompleteFlags {
    std::string prompt;
    std::string model;
    std::string system_prompt;
    std::string context;
    std::vector<std::string> images;
    double temperature = 0.0;
    std::int64_t max_tokens = 0;
    bool quiet = false;
    bool verbose = false;
    bool all_backends = false;
    bool tools = false;
    bool search = false;
    bool no_color = false;
    OutputFormat output_format = OutputFormat::Text;

    CLI::Option* temperature_option = nullptr;
    CLI::Option* max_tokens_option = nullptr;
};

/// Reports a user error and sets the exit code.
[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee complete: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

[[noreturn]] void fail_backend(const std::string& message) {
    std::cerr << "apogee complete: " << message << "\n";
    throw CLI::RuntimeError(kBackendError);
}

/// Resolves the prompt from the argument or standard input.
std::string resolve_prompt(const CompleteFlags& flags) {
    if (!flags.prompt.empty()) {
        return flags.prompt;
    }
    if (!stdin_is_piped()) {
        // At an interactive prompt with no argument, print usage rather than
        // silently blocking on the user's keystrokes until they work out that
        // Ctrl-D is what it wants.
        fail_user("no prompt given. Pass one as an argument, or pipe it on stdin");
    }
    std::string piped = read_stdin();
    while (!piped.empty() && (piped.back() == '\n' || piped.back() == '\r')) {
        piped.pop_back();
    }
    if (piped.empty()) {
        fail_user("the piped prompt was empty");
    }
    return piped;
}

std::vector<harness::ContentPart> load_attachments(const CompleteFlags& flags) {
    std::vector<harness::ContentPart> attachments;
    attachments.reserve(flags.images.size());
    for (const std::string& image : flags.images) {
        try {
            attachments.push_back(load_image_part(std::filesystem::path{image}));
        } catch (const std::exception& e) {
            fail_user(e.what());
        }
    }
    return attachments;
}

/// Whether `model` names something the config actually defines.
///
/// Mirrors the router's first two rungs -- a backend key, or a backend entry's
/// `model:` field -- and deliberately NOT its third, the fallback to
/// `models.default`.
///
/// That fallback is right for an unspecified model and wrong for an explicit
/// one. `apogee complete -m sonnnet` is a typo, and silently answering from a
/// different backend is the worst possible response: the user gets a real
/// answer from a model they did not choose, with nothing to indicate it. So
/// `-m` is checked here before the router ever sees it.
bool names_a_configured_backend(const harness::Config& config, std::string_view model) {
    if (config.find_backend(model) != nullptr) {
        return true;
    }
    const std::string normalized = harness::normalize_route_key(model);
    for (const auto& [name, entry] : config.backends) {
        if (entry.model == model || harness::normalize_route_key(entry.model) == normalized) {
            return true;
        }
    }
    return false;
}

/// The built-in tool set for `--tools`.
///
/// `fetch_url` only, for now. Web search comes from the provider's own
/// server-side tool (`--search`), not a local one -- see the decision recorded
/// on this item. Native filesystem toolsets and MCP register here later.
agent::ToolRegistry built_in_tools() {
    agent::ToolRegistry registry;

    auto client =
        std::make_shared<backends::HttpClient>(std::make_unique<backends::CurlTransport>());

    registry.add(agent::make_fetch_url_tool([client](std::string_view url) {
        agent::FetchResult result;
        backends::HttpRequest request;
        request.method = "GET";
        request.url = std::string{url};
        request.timeout = std::chrono::seconds{30};
        try {
            const backends::HttpResponse response = client->send(request, {}, {});
            result.status = response.status;
            result.body = response.body;
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        return result;
    }));

    return registry;
}

/// Runs one prompt against one backend, streaming to stdout.
/// Returns the finish reason so a caller can note truncation.
harness::ChatResponse run_one(const harness::Harness& harness, const harness::Config& config,
                              const CompleteFlags& flags, const std::string& model,
                              const std::string& prompt,
                              const std::vector<harness::ContentPart>& attachments, bool decorate) {
    // A capability probe, not a type switch: the Harness asks the provider
    // itself, so this stays correct when a local backend gains vision without
    // this file learning that llamacpp exists.
    if (!attachments.empty() && !harness.accepts_images(model)) {
        fail_user("backend '" + model +
                  "' cannot accept images yet -- local (llamacpp) vision support has not "
                  "landed. Use a cloud backend for --image, or drop the flag");
    }

    harness::ChatRequest request;
    request.model = model;
    request.messages = build_messages(resolve_system_prompt(flags.system_prompt, config, model),
                                      flags.context, prompt, attachments);

    const std::optional<double> temperature =
        flags.temperature_option->count() > 0 ? std::optional<double>{flags.temperature}
                                              : resolve_temperature(std::nullopt, config, model);
    const std::optional<std::int64_t> max_tokens =
        flags.max_tokens_option->count() > 0 ? std::optional<std::int64_t>{flags.max_tokens}
                                             : resolve_max_tokens(std::nullopt, config, model);
    request.temperature = temperature;
    request.max_tokens = max_tokens;

    // Machine mode: the SAME loop, a different Reporter. Nothing below this
    // point knows which one is in use, which is the Reporter seam's whole
    // claim -- a GUI driving this over pipes sees every event a terminal user
    // sees, because there is one loop and it can only speak through one seam.
    if (flags.output_format == OutputFormat::StreamJson) {
        JsonReporter reporter{std::cout};
        reporter.begin_session(model);

        agentloop::Options machine_options;
        machine_options.model = model;
        machine_options.temperature = temperature;
        machine_options.max_tokens = max_tokens;
        machine_options.stream_answer = true;

        agent::ToolRegistry machine_registry;
        if (flags.tools) {
            machine_registry = built_in_tools();
            machine_options.tools = &machine_registry;
            // No AskFn: a one-shot driver has no way to answer a question
            // mid-turn. The loop's rule then applies unchanged -- ask_user is
            // never advertised, rather than advertised and unanswerable.
        }

        std::vector<harness::ChatMessage> machine_history = request.messages;
        try {
            const agentloop::RunResult result =
                agentloop::run(harness, machine_history, machine_options, reporter);

            harness::ChatResponse response;
            response.message = harness::ChatMessage::assistant(result.answer);
            response.model = model;
            reporter.emit_result(response);
            return response;
        } catch (const harness::CancelledError&) {
            reporter.emit_error("cancelled");
            throw CLI::RuntimeError(kCancelled);
        } catch (const harness::HarnessError& e) {
            // Both channels: the event so a driver need not scrape prose, and
            // stderr so a human tailing the log sees it too.
            reporter.emit_error(e.what());
            fail_backend(e.what());
        }
    }

    // One Reporter implementation for every surface. `complete` was retrofitted
    // onto it when the terminal UX layer landed, replacing an inline adapter --
    // two implementations would have drifted, which is the parity failure the
    // Reporter interface exists to prevent.
    TerminalWriter status_writer{std::cerr};

    CliReporter::Options reporter_options;
    reporter_options.answer_stream = &std::cout;
    reporter_options.decorate = decorate;
    reporter_options.verbosity = flags.verbose ? ansi::Verbosity::Verbose
                                 : flags.quiet ? ansi::Verbosity::Quiet
                                               : ansi::Verbosity::Line;
    reporter_options.style =
        ansi::Style::detect(flags.no_color ? ansi::ColorMode::Never : ansi::ColorMode::Auto);
    reporter_options.width = static_cast<std::size_t>(platform::terminal_width().value_or(80));

    CliReporter reporter{status_writer, reporter_options};

    if (flags.verbose) {
        // Startup speaks through the status line, never raw stderr.
        reporter.status().print_line(reporter_options.style.tag(ansi::Role::Apogee) + " " + model);
    }

    agentloop::Options loop_options;
    loop_options.model = model;
    loop_options.temperature = temperature;
    loop_options.max_tokens = max_tokens;
    loop_options.stream_answer = true;

    agent::ToolRegistry registry;
    if (flags.tools) {
        registry = built_in_tools();
        loop_options.tools = &registry;
        // Advertised only when there is a terminal to answer on. A null AskFn
        // means the tool never appears in the request at all.
        loop_options.ask = terminal_ask_fn(reporter.status(), reporter_options.style);
    }

    std::vector<harness::ChatMessage> history = request.messages;

    try {
        const agentloop::RunResult result =
            agentloop::run(harness, history, loop_options, reporter);
        if (result.hit_iteration_limit) {
            reporter.status().print_line(reporter_options.style.tag(ansi::Role::Warning) +
                                         " tool-call limit reached; answered without tools");
        }

        harness::ChatResponse response;
        response.message = harness::ChatMessage::assistant(result.answer);
        response.model = model;
        return response;
    } catch (const harness::CancelledError&) {
        throw CLI::RuntimeError(kCancelled);
    } catch (const harness::NoAvailableBackendError& e) {
        fail_user(e.what());
    } catch (const harness::HarnessError& e) {
        fail_backend(e.what());
    }
}

}  // namespace

std::string_view CompleteCommand::name() const noexcept {
    return "complete";
}

std::string_view CompleteCommand::summary() const noexcept {
    return "Get a one-shot completion for a prompt";
}

void CompleteCommand::bind(CLI::App& root, const RootContext& context) {
    auto flags = std::make_shared<CompleteFlags>();

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->add_option("prompt", flags->prompt, "The prompt. Read from stdin when omitted");
    cmd->add_option("-m,--model", flags->model,
                    "Backend or model to use (default: models.default from config)");
    cmd->add_option("-s,--system", flags->system_prompt, "System prompt for this turn");
    cmd->add_option("--context", flags->context, "Extra context injected before the prompt");
    // allow_extra_args(false) is load-bearing: a CLI11 vector option is GREEDY
    // by default, so `--image pic.png "my prompt"` would put BOTH into images
    // and leave the positional prompt empty -- after which the command blocks
    // reading a stdin that never arrives. One value per occurrence, repeatable.
    cmd->add_option("--image", flags->images, "Image file to attach (repeatable)")
        ->allow_extra_args(false);
    flags->temperature_option =
        cmd->add_option("-t,--temperature", flags->temperature, "Sampling temperature");
    flags->max_tokens_option =
        cmd->add_option("-n,--max-tokens", flags->max_tokens, "Maximum tokens to generate");
    cmd->add_flag("-q,--quiet", flags->quiet, "Suppress all output except the answer");
    cmd->add_flag("-v,--verbose", flags->verbose, "Print progress notes to stderr");
    cmd->add_flag("--all-backends", flags->all_backends,
                  "Run the prompt against every configured backend");
    cmd->add_flag("--tools", flags->tools,
                  "Let the model call tools (fetch_url; ask_user on a terminal)");
    cmd->add_flag("--no-color", flags->no_color, "Disable ANSI colour output");
    cmd->add_option_function<std::string>(
           "--output-format",
           [flags](const std::string& value) {
               const std::optional<OutputFormat> parsed = output_format_from_string(value);
               if (!parsed.has_value()) {
                   throw CLI::ValidationError("--output-format",
                                              "expected 'text' or 'stream-json'");
               }
               flags->output_format = *parsed;
           },
           "Output format: text (default) or stream-json for a machine driver")
        ->type_name("FORMAT");
    cmd->add_flag("--search", flags->search,
                  "Enable the provider's own server-side web search, where it has one");

    cmd->callback([&context, flags]() {
        // Decoration is gated on stdout being a terminal, never on a global
        // flag: `apogee complete x > out.txt` has a redirected stdout and a
        // terminal stderr, and only the first should go plain.
        const bool decorate = platform::is_terminal(platform::StandardStream::Out) && !flags->quiet;

        harness::Config config;
        const std::filesystem::path config_path = harness::resolve_config_path(context.config_path);
        try {
            config = harness::load_config(config_path);
        } catch (const harness::ConfigError& e) {
            fail_user(e.what());
        }

        harness::Harness harness{config};
        backends::BuildOptions build_options;
        build_options.web_search = flags->search;
        const backends::BuildResult built = backends::build_providers(harness, build_options);

        if (built.constructed_count() == 0) {
            std::string message = "no usable backend is configured";
            if (!built.skipped_summary().empty()) {
                message += " -- " + built.skipped_summary();
            } else {
                message += " (add one with 'apogee config add-backend')";
            }
            fail_user(message);
        }

        const std::string prompt = resolve_prompt(*flags);
        const std::vector<harness::ContentPart> attachments = load_attachments(*flags);

        if (!flags->all_backends) {
            if (!flags->model.empty() && !names_a_configured_backend(config, flags->model)) {
                std::string known;
                for (const std::string& name : config.backend_names()) {
                    known += known.empty() ? "" : ", ";
                    known += name;
                }
                fail_user("no backend named '" + flags->model + "'" +
                          (known.empty() ? "" : " (configured: " + known + ")"));
            }
            // A backend that IS configured but failed to construct is the same
            // hazard as a typo, one step later: without this, `-m local` on an
            // unbuildable local backend falls through to the default and the
            // user gets a real answer from a model they did not choose. Name
            // the backend's own reason instead.
            for (const backends::BackendStatus& status : built.statuses) {
                if (!status.constructed && status.name == flags->model) {
                    fail_user("backend '" + flags->model + "' is configured but unavailable -- " +
                              status.reason);
                }
            }

            const std::string model = harness::resolve_chat_backend(config, flags->model);
            (void)run_one(harness, config, *flags, model, prompt, attachments, decorate);
            return;
        }

        // --all-backends: one prompt, every configured backend. Headers are
        // printed even on a pipe, unlike every other decoration -- with several
        // answers concatenated, the labels are structure rather than ornament,
        // and without them the output cannot be attributed at all.
        bool any_succeeded = false;
        for (const backends::BackendStatus& status : built.statuses) {
            if (!status.constructed) {
                std::cerr << "[apogee] skipping " << status.name << ": " << status.reason << "\n";
                continue;
            }
            std::cout << "=== " << status.name << " ===\n";
            try {
                (void)run_one(harness, config, *flags, status.name, prompt, attachments, decorate);
                any_succeeded = true;
            } catch (const CLI::RuntimeError&) {
                // One backend failing must not abandon the rest -- comparing
                // backends is the entire point of this flag.
                continue;
            }
        }
        if (!any_succeeded) {
            fail_backend("every configured backend failed");
        }
    });
}

}  // namespace apogee::commands
