#include "commands/chat.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>

#include "agent/fetch_url.h"
#include "agentloop/content.h"
#include "agentloop/loop.h"
#include "ansi/ansi.h"
#include "backends/factory.h"
#include "backends/http_client.h"
#include "commands/ask_prompt.h"
#include "commands/chat_history.h"
#include "commands/cli_reporter.h"
#include "commands/helpers.h"
#include "commands/input_gate.h"
#include "commands/json_reporter.h"
#include "commands/line_reader.h"
#include "commands/terminal.h"
#include "harness/config.h"
#include "harness/context_windows.h"
#include "harness/errors.h"
#include "harness/paths.h"
#include "logger/operational.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee chat: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

std::string trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

/// The slash commands, in one place.
///
/// Completion and `/help` both read this, so a command cannot be offered on Tab
/// and then rejected -- or added and silently left uncompletable.
const std::vector<std::string>& slash_commands() {
    static const std::vector<std::string> commands{
        "/help",       "/model",   "/models", "/system", "/temperature",
        "/max-tokens", "/compact", "/title",  "/exit",   "/quit",
    };
    return commands;
}

struct ChatFlags {
    std::string model;
    std::string system_prompt;
    std::vector<std::string> images;
    double temperature = 0.0;
    std::int64_t max_tokens = 0;
    bool tools = false;
    bool search = false;
    bool no_color = false;
    OutputFormat output_format = OutputFormat::Text;
    std::optional<InputFormat> input_format;
    bool verbose = false;
    std::string resume;
    bool cont = false;

    CLI::Option* temperature_option = nullptr;
    CLI::Option* max_tokens_option = nullptr;
};

}  // namespace

std::optional<SlashCommand> parse_slash(std::string_view line) {
    if (line.empty() || line.front() != '/') {
        return std::nullopt;
    }
    const std::string_view rest = line.substr(1);
    const std::size_t space = rest.find_first_of(" \t");
    const std::string_view word = space == std::string_view::npos ? rest : rest.substr(0, space);

    // A command word contains no '/'. Otherwise `/usr/bin/env is a path` -- a
    // perfectly reasonable question -- would be swallowed as a command.
    if (word.empty() || word.find('/') != std::string_view::npos) {
        return std::nullopt;
    }

    SlashCommand command;
    command.name = std::string{word};
    if (space != std::string_view::npos) {
        command.argument = trim(rest.substr(space + 1));
    }
    return command;
}

ContextUsage measure_context(const harness::Harness& harness,
                             const std::vector<harness::ChatMessage>& messages,
                             const std::string& model) {
    ContextUsage usage;
    usage.window = harness.context_window_for_model(model);

    // Ask the provider first: a backend that owns a tokenizer (a local model
    // does) answers exactly, and the 80/90 thresholds are only as good as the
    // number they fire on. `std::nullopt` means "no tokenizer, or not cheaply
    // right now" -- never an error, and never a reason to skip the check.
    harness::ChatRequest probe;
    probe.messages = messages;
    probe.model = model;
    if (const std::optional<std::int64_t> exact = harness.count_prompt_tokens(model, probe)) {
        usage.used_tokens = *exact;
        usage.exact = true;
        return usage;
    }

    // The estimate path. `exact` is carried rather than assumed because a
    // warning that fires at the wrong point is worse than none, and the surface
    // says which number it has.
    const agentloop::TokenCount count = agentloop::estimate_prompt_tokens(messages);
    usage.used_tokens = count.tokens;
    usage.exact = !count.estimated;
    return usage;
}

namespace {

/// Everything one chat turn does, for every surface that drives one.
///
/// Extracted when machine mode landed. The terminal REPL and a JSONL-driven
/// child differ entirely in how they READ input -- a line editor with slash
/// commands versus one JSON object per line -- and not at all in what a turn
/// *is*: measure the context, compact or warn, append, run the loop, persist,
/// and title the conversation once.
///
/// Duplicating that for the driver would have been the parity failure the
/// Reporter seam exists to prevent, one level up: context monitoring or the
/// per-turn save would have reached one surface and not the other, and nobody
/// would notice until a GUI user lost a conversation.
///
/// `notice` is the one genuinely surface-specific part. The terminal prints
/// warnings to its status line; machine mode sends them to **stderr**, because
/// stdout carries only protocol events and a context warning is a diagnostic
/// rather than a Reporter event. Inventing an event type for it would grow a
/// second vocabulary out of the first.
void run_chat_turn(const harness::Harness& harness, logger::Session& session,
                   const std::string& input, std::vector<harness::ContentPart>& attachments,
                   agent::ToolRegistry* tools, const agentloop::AskFn& ask,
                   agentloop::Reporter& reporter,
                   const std::function<void(const std::string&)>& notice) {
    std::vector<harness::ContentPart> turn_attachments;
    turn_attachments.swap(attachments);  // first message only

    const std::vector<harness::ChatMessage> incoming =
        build_messages({}, {}, input, turn_attachments);

    // Context is measured against what is ABOUT TO BE SENT -- the saved history
    // plus this turn -- not the history alone. Measuring before appending means
    // the first turn always reads as empty, and a single large prompt never
    // trips the threshold it should.
    std::vector<harness::ChatMessage> prospective = session.messages;
    prospective.insert(prospective.end(), incoming.begin(), incoming.end());
    const ContextUsage usage = measure_context(harness, prospective, session.backend);

    if (usage.should_compact()) {
        notice("context " + std::to_string(static_cast<int>(usage.fraction() * 100)) +
               "% full -- compacting");
        // Compacts the PRIOR history only: folding the message the user just
        // typed into a summary of the conversation so far would summarise away
        // the question being asked.
        session.messages = agentloop::compact_history(harness, session.messages, session.backend);
        ++session.compactions;
    } else if (usage.should_warn()) {
        notice("context " + std::to_string(static_cast<int>(usage.fraction() * 100)) + "% full" +
               (usage.exact ? "" : " (estimated)"));
    }

    for (const harness::ChatMessage& message : incoming) {
        session.messages.push_back(message);
    }

    agentloop::Options loop_options;
    loop_options.model = session.backend;
    loop_options.temperature = session.params.temperature;
    loop_options.max_tokens = session.params.max_tokens;
    loop_options.stream_answer = true;
    if (tools != nullptr) {
        loop_options.tools = tools;
        loop_options.ask = ask;
    }

    try {
        const agentloop::RunResult result =
            agentloop::run(harness, session.messages, loop_options, reporter);
        ++session.turns;
        if (result.hit_iteration_limit) {
            notice("tool-call limit reached");
        }
    } catch (const harness::CancelledError&) {
        notice("cancelled");
    } catch (const harness::HarnessError& e) {
        logger::log(logger::Level::Error, "chat", e.what());
        notice(e.what());
    }

    // Persist after EVERY turn. A kill -9 mid-conversation must leave every
    // completed turn on disk, and that is a property of writing here rather
    // than at exit.
    logger::save(session);

    // Auto-titling rides the first completed exchange. A side request so it
    // never enters the conversation's own history.
    if (session.title.empty() && session.custom_name.empty() && session.turns >= 1) {
        harness::ChatRequest title_request;
        title_request.model = session.backend;
        title_request.messages = session.messages;
        title_request.messages.push_back(harness::ChatMessage::user(title_prompt()));
        title_request.transient.side_request = true;
        try {
            session.title =
                sanitize_title(harness.chat(title_request).message.content.plain_text());
            logger::save(session);
        } catch (const harness::HarnessError&) {
            // A failed title is cosmetic. It must never cost a turn.
        }
    }
}

}  // namespace

std::string_view ChatCommand::name() const noexcept {
    return "chat";
}

std::string_view ChatCommand::summary() const noexcept {
    return "Start or resume an interactive conversation";
}

void ChatCommand::bind(CLI::App& root, const RootContext& context) {
    auto flags = std::make_shared<ChatFlags>();

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->add_option("-m,--model", flags->model, "Backend or model to use");
    cmd->add_option("-s,--system", flags->system_prompt, "System prompt for the session");
    cmd->add_option("--image", flags->images, "Image to attach to the first message (repeatable)")
        ->allow_extra_args(false);
    flags->temperature_option =
        cmd->add_option("-t,--temperature", flags->temperature, "Sampling temperature");
    flags->max_tokens_option =
        cmd->add_option("-n,--max-tokens", flags->max_tokens, "Maximum tokens per reply");
    cmd->add_flag("--tools", flags->tools, "Let the model call tools");
    cmd->add_flag("--search", flags->search, "Enable the provider's server-side web search");
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
    cmd->add_option_function<std::string>(
           "--input-format",
           [flags](const std::string& value) {
               const std::optional<InputFormat> parsed = input_format_from_string(value);
               if (!parsed.has_value()) {
                   throw CLI::ValidationError("--input-format", "expected 'text' or 'stream-json'");
               }
               flags->input_format = *parsed;
           },
           "Input format: text (default) or stream-json; follows --output-format if unset")
        ->type_name("FORMAT");
    cmd->add_flag("-v,--verbose", flags->verbose, "Print progress notes");
    cmd->add_option("--resume", flags->resume, "Resume a saved conversation by id or name");
    cmd->add_flag("-c,--continue", flags->cont, "Resume the most recent conversation");

    cmd->callback([&context, flags]() {
        const bool decorate = platform::is_terminal(platform::StandardStream::Out);

        harness::Config config;
        try {
            config = harness::load_config(harness::resolve_config_path(context.config_path));
        } catch (const harness::ConfigError& e) {
            fail_user(e.what());
        }

        // Every configured backend is constructed up front, which is what makes
        // /model an instant switch rather than a reconstruction -- and why
        // history has to be neutral IR rather than a vendor transcript.
        harness::Harness harness{config};
        backends::BuildOptions build_options;
        build_options.web_search = flags->search;
        const backends::BuildResult built = backends::build_providers(harness, build_options);

        if (built.constructed_count() == 0) {
            std::string message = "no usable backend is configured";
            if (!built.skipped_summary().empty()) {
                message += " -- " + built.skipped_summary();
            }
            fail_user(message);
        }

        TerminalWriter status_writer{std::cerr};
        CliReporter::Options reporter_options;
        reporter_options.answer_stream = &std::cout;
        reporter_options.decorate = decorate;
        reporter_options.verbosity =
            flags->verbose ? ansi::Verbosity::Verbose : ansi::Verbosity::Line;
        reporter_options.style =
            ansi::Style::detect(flags->no_color ? ansi::ColorMode::Never : ansi::ColorMode::Auto);
        reporter_options.width = static_cast<std::size_t>(platform::terminal_width().value_or(80));
        CliReporter reporter{status_writer, reporter_options};
        const ansi::Style& style = reporter_options.style;

        // --- resume ---------------------------------------------------------
        logger::Session session;
        logger::KnownDependencies known;
        known.backends = config.backend_names();

        if (!flags->resume.empty() || flags->cont) {
            try {
                logger::LoadedSession loaded = flags->cont ? [&]() {
                    const std::optional<logger::Session> recent = logger::most_recent();
                    if (!recent.has_value()) {
                        fail_user("no saved conversation to continue");
                    }
                    return logger::load(recent->chat_id, known);
                }()
                                                           : logger::load(flags->resume, known);

                session = std::move(loaded.session);
                // Resume degrades, never fails: every problem is a note.
                for (const logger::ResumeWarning& warning : loaded.warnings) {
                    reporter.status().print_line(style.tag(ansi::Role::Warning) + " [resume] " +
                                                 warning.message);
                    if (warning.kind == logger::WarningKind::BackendMissing) {
                        session.backend.clear();  // fall back to the default
                    }
                }
            } catch (const CLI::RuntimeError&) {
                throw;
            } catch (const std::exception& e) {
                fail_user(e.what());
            }
        } else {
            session.chat_id = logger::new_chat_id();
        }

        // Precedence: an explicit flag beats the saved value beats the default.
        const std::string model = !flags->model.empty()      ? flags->model
                                  : !session.backend.empty() ? session.backend
                                                             : config.models.default_backend;
        session.backend = model;

        if (flags->temperature_option->count() > 0) {
            session.params.temperature = flags->temperature;
        }
        if (flags->max_tokens_option->count() > 0) {
            session.params.max_tokens = flags->max_tokens;
        }
        if (!flags->system_prompt.empty()) {
            session.params.system_prompt = flags->system_prompt;
        }

        if (session.messages.empty() && !session.params.system_prompt.empty()) {
            session.messages.push_back(harness::ChatMessage::system(session.params.system_prompt));
        }

        logger::log(logger::Level::Info, "chat",
                    "session " + session.chat_id + " on backend " + model);

        // --- tools ----------------------------------------------------------
        agent::ToolRegistry registry;
        if (flags->tools) {
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
        }

        // --- attachments, for the first message only -------------------------
        std::vector<harness::ContentPart> attachments;
        for (const std::string& image : flags->images) {
            try {
                attachments.push_back(load_image_part(std::filesystem::path{image}));
            } catch (const std::exception& e) {
                fail_user(e.what());
            }
        }

        if (decorate) {
            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " " + model +
                                         "  ·  chat " + session.chat_id +
                                         "  ·  /help for commands");
        }

        // --- machine mode ----------------------------------------------------
        //
        // A driven child: one JSON object per line in, the same typed event
        // stream out, many turns on one process. It shares `run_chat_turn` with
        // the REPL below, so context monitoring, compaction, the per-turn save
        // and auto-titling all reach a GUI-driven session too -- they were
        // never surface concerns, only the input was.
        //
        // No line editor, no slash commands, no typeahead flush: those are all
        // terminal affordances, and a driver has its own UI for every one of
        // them.
        // Unset follows --output-format: a driver asking for machine output is
        // a machine, and a human asking for text is a human, so the two
        // ordinary cases need one flag rather than two.
        const InputFormat input_format = flags->input_format.value_or(
            flags->output_format == OutputFormat::StreamJson ? InputFormat::StreamJson
                                                             : InputFormat::Text);

        // On `chat` the two directions must agree. A JSONL-emitting REPL has no
        // coherent meaning -- slash commands print through the terminal
        // reporter and have no protocol event -- and a driven session that
        // renders prose gives its driver nothing to parse. Saying so is the
        // point: the alternative is accepting the flag and quietly ignoring it,
        // which is how a driver ends up debugging output it never asked for.
        const bool machine_in = input_format == InputFormat::StreamJson;
        const bool machine_out = flags->output_format == OutputFormat::StreamJson;
        if (machine_in != machine_out) {
            fail_user(std::string{"--input-format "} + std::string{to_string(input_format)} +
                      " cannot be combined with --output-format " +
                      std::string{to_string(flags->output_format)} +
                      "; a driven chat session speaks the protocol in both directions");
        }

        if (input_format == InputFormat::StreamJson) {
            JsonReporter machine_reporter{std::cout};
            machine_reporter.begin_session(session.backend);

            const auto machine_notice = [](const std::string& message) {
                // stdout carries ONLY protocol events, so a diagnostic goes to
                // stderr -- the same discipline Apogee demands of the vendor
                // CLIs it drives, having been the consumer on the other side.
                std::cerr << "apogee: " << message << "\n";
            };

            const agentloop::AskFn driver_ask =
                flags->tools ? make_driver_ask_fn(machine_reporter, std::cin) : agentloop::AskFn{};

            std::string line;
            while (std::getline(std::cin, line)) {
                const DriverMessage message = parse_driver_line(line);
                if (message.kind != DriverMessage::Kind::User || message.text.empty()) {
                    // Unknown types are ignored rather than fatal: the same
                    // tolerance this protocol asks of its own drivers.
                    continue;
                }

                // The driver reads structured input, so there IS someone to
                // answer a question -- the loop's "nil AskFn <=> never
                // advertised" rule is satisfied rather than sidestepped.
                run_chat_turn(harness, session, message.text, attachments,
                              flags->tools ? &registry : nullptr, driver_ask, machine_reporter,
                              machine_notice);

                harness::ChatResponse response;
                response.message = session.messages.empty() ? harness::ChatMessage::assistant("")
                                                            : session.messages.back();
                response.model = session.backend;
                machine_reporter.emit_result(response);
            }

            // stdin closed: the driver is done. Everything is already persisted
            // by the per-turn save, so exiting is clean by construction.
            logger::save(session);
            return;
        }

        // Once, immediately before the first prompt -- never between turns.
        discard_startup_typeahead();

        EditingLineReader::Options reader_options;
        reader_options.history_path = default_history_path();
        reader_options.completions = slash_commands();
        // Backend names complete too: `/model cla<Tab>` is the common case.
        for (const std::string& backend : config.backend_names()) {
            reader_options.completions.push_back(backend);
        }
        const std::unique_ptr<LineReader> reader =
            make_line_reader(std::move(reader_options), std::cin);

        // --- the REPL --------------------------------------------------------
        bool running = true;
        while (running) {
            // The editor draws its own prompt; the plain reader ignores it and
            // the prompt goes to stderr so a piped run's stdout stays clean.
            std::optional<std::string> line;
            if (reader->interactive()) {
                line = reader->read(style.colorize("You: ", ansi::Color::Green));
            } else {
                if (decorate) {
                    std::cerr << style.colorize("You: ", ansi::Color::Green) << std::flush;
                }
                line = reader->read({});
            }
            if (!line.has_value()) {
                break;  // EOF, or Ctrl-D / Ctrl-C at the editor
            }

            const std::string input = trim(*line);
            if (input.empty()) {
                continue;
            }
            // Only non-empty lines enter the history, so recall is not padded
            // with blanks.
            reader->remember(input);

            if (const std::optional<SlashCommand> command = parse_slash(input);
                command.has_value()) {
                const std::string& verb = command->name;
                const std::string& argument = command->argument;

                if (verb == "exit" || verb == "quit") {
                    running = false;
                } else if (verb == "help") {
                    std::string help;
                    for (const std::string& entry : slash_commands()) {
                        help += help.empty() ? "" : "  ";
                        help += entry;
                    }
                    reporter.status().print_line(help);
                } else if (verb == "models") {
                    for (const std::string& backend : config.backend_names()) {
                        reporter.status().print_line((backend == session.backend ? "* " : "  ") +
                                                     backend);
                    }
                } else if (verb == "model") {
                    if (argument.empty()) {
                        reporter.status().print_line(session.backend);
                    } else if (config.find_backend(argument) == nullptr) {
                        reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                     " no backend named '" + argument + "'");
                    } else {
                        // Instant, and history carries over: every backend was
                        // constructed up front and history is neutral IR.
                        session.backend = argument;
                        reporter.status().print_line(style.tag(ansi::Role::Apogee) +
                                                     " switched to " + argument);
                    }
                } else if (verb == "system") {
                    session.params.system_prompt = argument;
                    reporter.status().print_line(style.tag(ansi::Role::Apogee) +
                                                 " system prompt updated");
                } else if (verb == "temperature") {
                    try {
                        session.params.temperature = std::stod(argument);
                    } catch (const std::exception&) {
                        reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                     " not a number: '" + argument + "'");
                    }
                } else if (verb == "max-tokens") {
                    try {
                        session.params.max_tokens = std::stoll(argument);
                    } catch (const std::exception&) {
                        reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                     " not a number: '" + argument + "'");
                    }
                } else if (verb == "title") {
                    session.custom_name = argument;
                    logger::save(session);
                    reporter.status().print_line(style.tag(ansi::Role::Apogee) + " renamed");
                } else if (verb == "compact") {
                    session.messages =
                        agentloop::compact_history(harness, session.messages, session.backend);
                    ++session.compactions;
                    logger::save(session);
                    reporter.status().print_line(style.tag(ansi::Role::Apogee) +
                                                 " history compacted");
                } else {
                    reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                 " unknown command '/" + verb +
                                                 "' -- /help lists them");
                }
                continue;
            }

            // --- the turn ------------------------------------------------------
            run_chat_turn(
                harness, session, input, attachments, flags->tools ? &registry : nullptr,
                flags->tools ? terminal_ask_fn(reporter.status(), style) : agentloop::AskFn{},
                reporter, [&reporter, &style](const std::string& message) {
                    reporter.status().print_line(style.tag(ansi::Role::Warning) + " " + message);
                });
        }

        logger::save(session);
        if (decorate) {
            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " saved " +
                                         session.chat_id);
        }
    });
}

}  // namespace apogee::commands
