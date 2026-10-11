#include "cli/complete.h"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/media.h"
#include "agentloop/member_call.h"
#include "agentloop/rag.h"
#include "agentloop/reporter.h"
#include "agentloop/retriever.h"
#include "ansi/ansi.h"
#include "backends/factory.h"
#include "cli/chat_attachments.h"
#include "cli/embed.h"
#include "cli/helpers.h"
#include "cli/permissions.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "contracts/paths.h"
#include "harness/harness.h"
#include "harness/roles.h"
#include "logger/operational.h"
#include "machine/json_reporter.h"
#include "mcp/registry.h"
#include "operations/suites.h"
#include "platform/platform.h"
#include "tools/consult.h"
#include "views/ask_prompt.h"
#include "views/cli_reporter.h"
#include "views/terminal.h"

namespace apogee::commands {
namespace {

/// Flags, kept alive for the app's lifetime so CLI11 callbacks can read them.
struct CompleteFlags {
    std::string prompt;
    std::string model;
    std::string system_prompt;
    std::string context;
    std::vector<std::string> images;
    /// Files, folders and globs to attach (26d).
    std::vector<std::string> attach;
    /// The method those attaches take (27p): `code` or `off`; empty leaves
    /// it to the config's `attachments.graph`, else the one-shot's built-in,
    /// no graph (27n).
    std::string graph;
    double temperature = 0.0;
    std::int64_t max_tokens = 0;
    /// Whether the model thinks first, and for how long (26i).
    std::string think;
    std::int64_t think_budget = 0;
    /// The run's answers, given at invocation (26o): with nobody to ask, the
    /// only way a one-shot uses a destructive tool on purpose.
    std::vector<std::string> allow;
    std::vector<std::string> deny;
    std::vector<std::string> allow_hosts;
    bool quiet = false;
    bool verbose = false;
    bool all_backends = false;
    bool tools = false;
    bool search = false;
    /// The collection to retrieve from. Empty means no retrieval.
    std::string rag;
    int rag_limit = 4;
    /// `lexical` / `vector` / `hybrid` / `auto`; empty defers to the pin.
    std::string retriever;
    /// A backend to rerank with, `off`, or empty for the collection's pin.
    std::string rerank;
    bool no_color = false;
    bool raw = false;
    OutputFormat output_format = OutputFormat::Text;

    CLI::Option* temperature_option = nullptr;
    CLI::Option* max_tokens_option = nullptr;
    CLI::Option* think_budget_option = nullptr;
    /// Kept so an explicit `--rag ""` can be told from no flag at all.
    CLI::Option* rag_option = nullptr;
};

/// Reports a user error and sets the exit code.
[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee complete: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

/// The run's answers, given at invocation (26o), checked against the tools
/// that ask. A name that is neither a gated tool nor a website fails the run:
/// a typo must not quietly grant nothing.
std::shared_ptr<SessionApprovals> seeded_approvals(const CompleteFlags& flags,
                                                   const agent::ToolRegistry& registry) {
    auto approvals = std::make_shared<SessionApprovals>();
    if (const std::string refused = seed_approvals(
            *approvals,
            PermissionPresets{
                .allow = flags.allow, .deny = flags.deny, .allow_hosts = flags.allow_hosts},
            gated_tools(registry));
        !refused.empty()) {
        fail_user(refused);
    }
    return approvals;
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
    // A driver's `hello` before the prompt (28d): recorded, and never part of
    // what the model is asked.
    if (flags.output_format == OutputFormat::StreamJson) {
        const std::size_t end = piped.find('\n');
        const DriverMessage first = parse_driver_line(std::string_view{piped}.substr(0, end));
        if (first.kind == DriverMessage::Kind::Hello) {
            logger::log(logger::Level::Info, "complete", describe_hello(first));
            piped.erase(0, end == std::string::npos ? piped.size() : end + 1);
        }
    }
    while (!piped.empty() && (piped.back() == '\n' || piped.back() == '\r')) {
        piped.pop_back();
    }
    if (piped.empty()) {
        fail_user("the piped prompt was empty");
    }
    return piped;
}

/// Fails unless each `--image` is an image file that exists: a one-shot whose
/// picture is missing has nothing to answer about.
void check_images(const CompleteFlags& flags) {
    for (const std::string& image : flags.images) {
        const std::filesystem::path path{image};
        std::error_code code;
        if (!std::filesystem::is_regular_file(path, code)) {
            fail_user(image + ": cannot open file");
        }
        if (agentloop::medium_of(path) != harness::Medium::Image) {
            fail_user(image +
                      ": unsupported image type (accepted: png, jpg, jpeg, gif, webp, bmp, tif, "
                      "tiff, heic)");
        }
    }
}

/// A one-shot's attachments (26d): indexed into a temporary store of its own,
/// removed at exit -- there is no chat to keep them with -- with the other
/// chats' indexes still searched first, so nothing already embedded is
/// embedded again.
class OneShotAttachments {
public:
    OneShotAttachments(const harness::Harness& harness, const std::string& model,
                       std::optional<std::int64_t> max_tokens, ChatAttachments::Hooks hooks)
        : folder_{std::filesystem::temp_directory_path() /
                  ("apogee-attach-" + std::to_string(std::random_device{}()))} {
        session_.backend = model;
        session_.params.max_tokens = max_tokens;
        hooks.save = false;
        attached_ = std::make_unique<ChatAttachments>(harness, session_, folder_ / "index.db",
                                                      std::move(hooks));
    }

    ~OneShotAttachments() {
        attached_.reset();
        std::error_code code;
        std::filesystem::remove_all(folder_, code);
    }

    OneShotAttachments(const OneShotAttachments&) = delete;
    OneShotAttachments& operator=(const OneShotAttachments&) = delete;
    OneShotAttachments(OneShotAttachments&&) = delete;
    OneShotAttachments& operator=(OneShotAttachments&&) = delete;

    [[nodiscard]] ChatAttachments& attached() noexcept {
        return *attached_;
    }

private:
    std::filesystem::path folder_;
    logger::Session session_;
    std::unique_ptr<ChatAttachments> attached_;
};

/// The prompt's attachments, indexed and settled, and what the one request
/// sends of them: `user_message` is where the prompt sits among the messages.
ChatAttachments::Turn attach_for_prompt(OneShotAttachments& attachments, const CompleteFlags& flags,
                                        const std::string& prompt, std::size_t user_message,
                                        const agentloop::TurnBudget& budget) {
    std::error_code code;
    const std::filesystem::path working_directory = std::filesystem::current_path(code);
    // `--image` is an attachment like any other since 26e -- described for a
    // model that cannot see it -- but a one-shot whose picture nothing can
    // read fails rather than answer without it.
    for (const std::string& image : flags.images) {
        if (!attachments.attached().attach(image, working_directory)) {
            fail_user(image + " was not attached, so there is nothing to ask about");
        }
    }
    // `--graph` for this invocation's attaches, over the config's default
    // and the one-shot's built-in (27p).
    const GraphMethod method = attachments.attached().graph_method(
        harness::attachment_graph_method_from_string(flags.graph));
    for (const std::string& spec : flags.attach) {
        (void)attachments.attached().attach(spec, working_directory, method);
    }
    attachments.attached().settle();
    return attachments.attached().for_turn(user_message, prompt, budget, flags.rag_limit, {});
}

/// Runs one prompt against one backend, streaming to stdout.
/// Returns the finish reason so a caller can note truncation.
harness::ChatResponse run_one(const harness::Harness& harness, const harness::Config& config,
                              const std::filesystem::path& config_path, const CompleteFlags& flags,
                              const std::string& model, const std::string& prompt, bool decorate) {
    harness::ChatRequest request;
    request.model = model;
    request.messages = build_messages(resolve_system_prompt(flags.system_prompt, config, model),
                                      flags.context, prompt, {});
    const bool attaching = !flags.attach.empty() || !flags.images.empty();

    const std::optional<double> temperature =
        flags.temperature_option->count() > 0 ? std::optional<double>{flags.temperature}
                                              : resolve_temperature(std::nullopt, config, model);
    const std::optional<std::int64_t> max_tokens =
        flags.max_tokens_option->count() > 0 ? std::optional<std::int64_t>{flags.max_tokens}
                                             : resolve_max_tokens(std::nullopt, config, model);
    request.temperature = temperature;
    request.max_tokens = max_tokens;
    if (!flags.tools &&
        !PermissionPresets{
            .allow = flags.allow, .deny = flags.deny, .allow_hosts = flags.allow_hosts}
             .empty()) {
        fail_user("--allow, --deny and --allow-host need --tools: without tools, nothing is asked");
    }
    // Whether the model thinks first (26i): the flags, else the backend's.
    const harness::Thinking thinking = resolve_thinking(
        flags.think.empty() ? std::nullopt : harness::thinking_mode_from_string(flags.think),
        flags.think_budget_option->count() > 0 ? std::optional<std::int64_t>{flags.think_budget}
                                               : std::nullopt,
        config, model);

    // Machine mode: the SAME loop, a different Reporter. Nothing below this
    // point knows which one is in use, which is the Reporter seam's whole
    // claim -- a GUI driving this over pipes sees every event a terminal user
    // sees, because there is one loop and it can only speak through one seam.
    if (flags.output_format == OutputFormat::StreamJson) {
        JsonReporter reporter{std::cout};
        // One-shot: nobody can be asked, and the only line it may read is a
        // `hello` before a prompt piped on stdin (28d).
        MachineCapabilities capabilities;
        if (flags.prompt.empty()) {
            capabilities.accepts.emplace_back("hello");
        }
        capabilities.tools = flags.tools;
        reporter.begin_session(model, capabilities);

        agentloop::Options machine_options;
        machine_options.model = model;
        machine_options.temperature = temperature;
        machine_options.max_tokens = max_tokens;
        machine_options.thinking = thinking;
        machine_options.thinking_judge = named_utility(config);
        machine_options.stream_answer = true;
        machine_options.summary_model = named_utility(config);
        std::optional<OneShotAttachments> machine_attachments;
        if (attaching) {
            machine_attachments.emplace(
                harness, model, max_tokens,
                ChatAttachments::Hooks{
                    .say = [&reporter](const std::string& line,
                                       bool /*warning*/) { reporter.on_notice(line); },
                    .progress = {},
                    .confirm_large = {},
                    .save = false,
                    // No graph for a one-shot by default (27n): no follow-up
                    // walks it -- unless the config or --graph asks (27p).
                    .built_in_graph = harness::AttachmentGraphMethod::Off});
            ChatAttachments::Turn turn =
                attach_for_prompt(*machine_attachments, flags, prompt, request.messages.size() - 1,
                                  agentloop::turn_budget(harness, model, max_tokens));
            machine_options.inline_attachments = std::move(turn.inlined);
            if (turn.retrieved.has_value() && turn.retrieved->error.empty()) {
                machine_options.transient_prefix = turn.retrieved->prefix;
            }
        }

        agent::ToolRegistry machine_registry;
        std::unique_ptr<agentloop::ToolSelection> machine_selection;
        const auto machine_mcp = std::make_shared<mcp::Registry>();
        const auto machine_consults = std::make_shared<agentloop::MemberCalls>(harness);
        // The run's member calls (27f), with tools or without: the suite's
        // validation (27g) spends from the same count.
        machine_options.member_calls = machine_consults.get();
        if (flags.tools) {
            // stdout is the protocol: connection notes go to stderr.
            // The toolset the active suite pins on this backend, if any (27d).
            machine_registry = pin_toolset(
                make_built_in_tools(BuiltInToolOptions{
                    .config = &config,
                    .harness = &harness,
                    .mcp = machine_mcp,
                    .mcp_status = [](std::string_view line) { std::cerr << line << "\n"; }}),
                config, model);
            // The consult tool when the suite designates members (27f), after
            // the pin: `consultable:` is the suite's own switch.
            for (const std::string& note :
                 tools::register_consult_tool(machine_registry, harness, machine_consults).notes) {
                std::cerr << "apogee: " << note << "\n";
            }
            machine_options.tools = &machine_registry;
            // Past a dozen and a half tools, the ones the question needs (26g).
            std::string ranked_by;
            machine_selection =
                make_tool_selection(harness, config, machine_registry, config_path, ranked_by);
            machine_options.tool_selection = machine_selection.get();
            // The config's levels and the run's own answers (26o): a one-shot
            // driver cannot be asked, so anything else that asks is a deny,
            // exactly as on a pipe.
            machine_options.permission =
                make_permission_checker(config, seeded_approvals(flags, machine_registry));
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
    reporter_options.markdown = !flags.raw && config.ui.markdown;
    reporter_options.hyperlinks = ansi::hyperlinks_supported();

    CliReporter reporter{status_writer, reporter_options};

    if (flags.verbose) {
        // Startup speaks through the status line, never raw stderr.
        reporter.status().print_line(reporter_options.style.tag(ansi::Role::Apogee) + " " + model);
    }

    agentloop::Options loop_options;
    loop_options.model = model;
    loop_options.temperature = temperature;
    loop_options.max_tokens = max_tokens;
    loop_options.thinking = thinking;
    loop_options.thinking_judge = named_utility(config);
    loop_options.stream_answer = true;
    // A large tool result is summarised by the utility model, when one is
    // set, before the model reads it (26b).
    loop_options.summary_model = named_utility(config);

    // Retrieval, spliced into the OUTGOING request only. `transient_prefix` is
    // the seam the agent loop already test-locks as never reaching persisted
    // history -- which is what keeps a transcript readable and keeps turn one's
    // chunks from competing with turn two's question.
    //
    // The collection comes from the flag, else the config's `auto_rag` -- one
    // shared decision, so this surface and chat cannot disagree about which
    // wins. Either way the status line says what was injected and from where.
    const agentloop::TurnBudget budget = agentloop::turn_budget(harness, model, max_tokens);
    const ansi::Style& style = reporter_options.style;
    std::optional<OneShotAttachments> attached;
    std::int64_t share_used = 0;
    if (attaching) {
        // The same core as chat's: an attachment that fits rides the prompt
        // whole, and the others' excerpts are retrieved for it (26d).
        attached.emplace(
            harness, model, max_tokens,
            ChatAttachments::Hooks{
                .say =
                    [&reporter, &style](const std::string& line, bool warning) {
                        reporter.status().print_line(
                            style.tag(warning ? ansi::Role::Warning : ansi::Role::Apogee) + " " +
                            line);
                    },
                .progress =
                    [&reporter, &style](const std::string& line) {
                        if (line.empty()) {
                            reporter.status().clear();
                        } else {
                            reporter.status().set(style.tag(ansi::Role::Apogee) + " " + line);
                        }
                    },
                .confirm_large = {},
                .save = false,
                // No graph for a one-shot by default (27n): no follow-up
                // walks it -- unless the config or --graph asks (27p).
                .built_in_graph = harness::AttachmentGraphMethod::Off});
        ChatAttachments::Turn turn =
            attach_for_prompt(*attached, flags, prompt, request.messages.size() - 1, budget);
        loop_options.inline_attachments = std::move(turn.inlined);
        if (turn.retrieved.has_value()) {
            if (turn.retrieved->error.empty() && !turn.retrieved->prefix.empty()) {
                loop_options.transient_prefix = turn.retrieved->prefix;
                share_used = turn.retrieved->tokens;
            }
            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " " +
                                         describe_attachment_retrieval(*turn.retrieved));
        }
    }

    const RagChoice rag_choice =
        choose_rag_collection(flags.rag_option->count() > 0, flags.rag, config.auto_rag);
    if (rag_choice.active()) {
        const agentloop::RagResult rag =
            retrieve_for_collection(harness, config, rag_choice.collection, prompt, flags.rag_limit,
                                    flags.retriever, flags.rerank, {}, model, budget, share_used);
        // Explicitly asked for and impossible -- an `--retriever vector` with
        // no vectors -- is the user's request failing, not a fallback.
        if (!rag.error.empty() && !flags.retriever.empty()) {
            fail_user(rag.error);
        }
        // Otherwise a missing collection is reported, not fatal: answering
        // without retrieved context beats refusing to answer.
        const ansi::Role role = rag.error.empty() ? ansi::Role::Apogee : ansi::Role::Warning;
        // Whenever there is a prefix: the chunks, or the graph context that
        // survived a judge dropping every chunk.
        if (rag.error.empty() && !rag.prefix.empty()) {
            loop_options.transient_prefix.insert(loop_options.transient_prefix.end(),
                                                 rag.prefix.begin(), rag.prefix.end());
        }
        reporter.status().print_line(reporter_options.style.tag(role) + " " +
                                     describe_retrieval(rag_choice, rag));
    }

    agent::ToolRegistry registry;
    std::unique_ptr<agentloop::ToolSelection> selection;
    const auto mcp_registry = std::make_shared<mcp::Registry>();
    const auto consults = std::make_shared<agentloop::MemberCalls>(harness);
    // The run's member calls (27f), with tools or without: the suite's
    // validation (27g) spends from the same count.
    loop_options.member_calls = consults.get();
    if (flags.tools) {
        // The toolset the active suite pins on this backend, if any (27d).
        registry = pin_toolset(
            make_built_in_tools(BuiltInToolOptions{
                .config = &config,
                .harness = &harness,
                .mcp = mcp_registry,
                .mcp_status = mcp_status_line(reporter.status()),
                .mcp_server_log = flags.verbose ? mcp::StderrTail::Sink{[](std::string_view bytes) {
                    std::cerr << bytes << std::flush;
                }}
                                                : mcp::StderrTail::Sink{}}),
            config, model);
        // The consult tool when the suite designates members (27f), after
        // the pin: `consultable:` is the suite's own switch.
        for (const std::string& note :
             tools::register_consult_tool(registry, harness, consults).notes) {
            reporter.status().print_line(reporter_options.style.tag(ansi::Role::Warning) + " " +
                                         note);
        }
        loop_options.tools = &registry;
        // Past a dozen and a half tools, the ones the question needs (26g).
        std::string ranked_by;
        selection = make_tool_selection(harness, config, registry, config_path, ranked_by);
        if (selection != nullptr && flags.verbose) {
            reporter.status().print_line("[tools] " + std::to_string(registry.size()) +
                                         " registered: the turn offers the ones its question "
                                         "needs, ranked by " +
                                         ranked_by);
        }
        loop_options.tool_selection = selection.get();
        // Advertised only when there is a terminal to answer on. A null AskFn
        // means the tool never appears in the request at all -- and a null
        // ConfirmFn, on a pipe, means a destructive tool's `ask` is a deny.
        loop_options.ask = terminal_ask_fn(reporter.status(), reporter_options.style);
        const std::shared_ptr<SessionApprovals> approvals = seeded_approvals(flags, registry);
        loop_options.permission = make_permission_checker(config, approvals);
        loop_options.confirm =
            terminal_confirm_fn(reporter.status(), reporter_options.style, config_path, approvals);
        if (is_base_model(harness, model)) {
            reporter.status().print_line(reporter_options.style.tag(ansi::Role::Warning) + " " +
                                         base_model_tools_note(model));
        }
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
                    "Backend or model to use (default: models.default from config)")
        ->type_name(kBackendValue);
    cmd->add_option("-s,--system", flags->system_prompt, "System prompt for this turn");
    cmd->add_option("--context", flags->context, "Extra context injected before the prompt");
    // allow_extra_args(false) is load-bearing: a CLI11 vector option is GREEDY
    // by default, so `--image pic.png "my prompt"` would put BOTH into images
    // and leave the positional prompt empty -- after which the command blocks
    // reading a stdin that never arrives. One value per occurrence, repeatable.
    flags->rag_option =
        cmd->add_option("--rag", flags->rag,
                        "Retrieve context from this collection (see 'apogee embed'); "
                        "\"\" switches off the config's auto_rag for this run")
            ->type_name(kCollectionValue);
    cmd->add_option("--rag-limit", flags->rag_limit, "How many chunks to inject (default 4)");
    cmd->add_option("--retriever", flags->retriever,
                    "How to search the collection: lexical, vector, hybrid, or auto")
        ->type_name(words_value(agentloop::retriever_names()))
        ->check([](const std::string& value) {
            return agentloop::valid_retriever(value)
                       ? std::string{}
                       : agentloop::retriever_values_message("", value);
        });
    cmd->add_option(
           "--rerank", flags->rerank,
           "Backend that reorders retrieved chunks with one generation call, on (the utility "
           "model), or off")
        ->type_name(kBackendValue);
    cmd->add_option("--image", flags->images,
                    "An image to attach: seen as it is by a model that can, described for one "
                    "that cannot (repeatable)")
        ->type_name(kPathValue)
        ->allow_extra_args(false);
    cmd->add_option("--attach", flags->attach,
                    "A file, folder or glob to attach: inlined when it fits, its excerpts "
                    "retrieved when not (repeatable)")
        ->type_name(kPathValue)
        ->allow_extra_args(false);
    cmd->add_option("--graph", flags->graph,
                    "For this run's --attach: code builds a folder's code graph, off indexes its "
                    "chunks alone (default: the config's attachments.graph, else off)")
        ->type_name(words_value(harness::attachment_graph_method_names()))
        ->check([](const std::string& value) {
            return harness::attachment_graph_method_from_string(value).has_value()
                       ? std::string{}
                       : harness::attachment_graph_values_message("", value);
        });
    flags->temperature_option =
        cmd->add_option("-t,--temperature", flags->temperature, "Sampling temperature");
    flags->max_tokens_option =
        cmd->add_option("-n,--max-tokens", flags->max_tokens, "Maximum tokens to generate");
    cmd->add_option("--think", flags->think,
                    "Whether a reasoning model thinks first: on, off, or auto (for this question)")
        ->check(CLI::IsMember({"on", "off", "auto"}));
    flags->think_budget_option =
        cmd->add_option("--think-budget", flags->think_budget,
                        "The most tokens a reasoning model may think for before it answers")
            ->check(CLI::Range(std::int64_t{0}, harness::kMaxThinkingBudget));
    cmd->add_flag("-q,--quiet", flags->quiet, "Suppress all output except the answer");
    cmd->add_flag("-v,--verbose", flags->verbose, "Print progress notes to stderr");
    cmd->add_flag("--all-backends", flags->all_backends,
                  "Run the prompt against every configured backend");
    cmd->add_flag("--tools", flags->tools,
                  "Let the model call tools (fetch_url; ask_user on a terminal)");
    cmd->add_option("--allow", flags->allow,
                    "Allow a tool for this run without asking (repeatable, with --tools)")
        ->type_name(kToolValue)
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    cmd->add_option("--deny", flags->deny,
                    "Refuse a tool or website for this run without asking (repeatable)")
        ->type_name(kToolValue)
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    cmd->add_option("--allow-host", flags->allow_hosts,
                    "Allow fetching from a website for this run without asking (repeatable)")
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    cmd->add_flag("--no-color", flags->no_color, "Disable ANSI colour output");
    cmd->add_flag("--raw", flags->raw,
                  "Show the answer's Markdown as written instead of rendering it on the terminal");
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
        ->type_name(words_value(format_names()));
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

        // A model that is neither a backend key nor an entry's `model:` may be a
        // vendor roster's (M13), resolved as chat's `-m` and `/model` resolve it
        // (33): a sole configured owner pins that entry's model for this run, in
        // memory only -- the config file never changes; two owners refuse
        // naming both; none falls through to the existing refusal.
        if (const SessionModel named = resolve_session_model(config, flags->model);
            !named.refusal.empty()) {
            fail_user(named.refusal);
        } else if (!named.pinned.empty()) {
            config.backends.at(named.backend).model = named.pinned;
            std::cerr << "model '" << named.pinned << "' -- " << roster_pin_note(named) << "\n";
            flags->model = named.backend;
        }
        harness::Harness harness{config};
        backends::BuildOptions build_options;
        build_options.web_search = flags->search;
        build_options.config_path = config_path;
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

        // A suite member naming nothing is refused, never routed around (27d).
        if (const std::string refused = validate_active_suite(config); !refused.empty()) {
            fail_user(refused);
        }
        // `--graph` is how this run's attaches are indexed (27p): with none,
        // it would say nothing.
        if (!flags->graph.empty() && flags->attach.empty()) {
            fail_user(
                "--graph applies to --attach: it says whether an attached folder's code "
                "graph is built");
        }
        const std::string prompt = resolve_prompt(*flags);
        check_images(*flags);

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
            (void)run_one(harness, config, config_path, *flags, model, prompt, decorate);
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
                (void)run_one(harness, config, config_path, *flags, status.name, prompt, decorate);
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
