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
#include "backends/factory.h"
#include "backends/http_client.h"
#include "commands/ask_prompt.h"
#include "commands/helpers.h"
#include "harness/config.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "harness/paths.h"
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

/// Whether the backend serving `model` can accept image parts.
///
/// Answered from the config entry's type rather than a capability probe. That
/// is a deliberate stopgap: the only image-incapable type is `llamacpp`, which
/// has no implementation yet, so there is no object to ask. When that backend
/// lands it should carry a `VisionCapable` capability and this function should
/// become a Harness probe like every other capability question -- a type switch
/// is exactly the shape the capability rule exists to avoid.
bool backend_accepts_images(const harness::Config& config, std::string_view model) {
    const harness::BackendConfig* entry = config.find_backend(model);
    if (entry == nullptr) {
        return true;  // unknown: let the provider decide rather than pre-refusing
    }
    return entry->type != harness::BackendType::LlamaCpp;
}

/// Adapts the loop's Reporter to a terminal.
///
/// The whole surface layer for `complete`: the answer goes to stdout, progress
/// goes to stderr and only on a terminal. That split is what keeps
/// `apogee complete --tools "..." | jq` working -- tool-status lines would
/// otherwise land in the piped output.
class CompleteReporter final : public agentloop::Reporter {
public:
    CompleteReporter(bool decorate, bool verbose) : decorate_{decorate}, verbose_{verbose} {}

    void on_tool_status(std::string_view detail) override {
        if (decorate_ || verbose_) {
            std::cerr << detail << "\n";
        }
    }

    void on_answer_token(std::string_view chunk) override {
        std::cout << chunk << std::flush;
        emitted_ = true;
    }

    void on_answer_end() override {
        if (emitted_) {
            std::cout << "\n";
        }
    }

    // Thinking is dropped: it is display metadata, `complete` has no display
    // layer yet, and it must never reach stdout -- a pipe receives exactly the
    // answer. The rich rendering arrives with chat-cli and retrofits here.

    [[nodiscard]] bool emitted() const noexcept {
        return emitted_;
    }

private:
    bool decorate_;
    bool verbose_;
    bool emitted_ = false;
};

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
    if (!attachments.empty() && !backend_accepts_images(config, model)) {
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

    if (decorate && flags.verbose) {
        std::cerr << "[apogee] " << model << "\n";
    }

    // With --tools the run goes through the shared agent loop; without it,
    // straight to the provider. Both paths are one call because the loop is
    // I/O-agnostic -- the surface is this adapter and nothing else.
    CompleteReporter reporter{decorate, flags.verbose};

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
        loop_options.ask = terminal_ask_fn();
    }

    std::vector<harness::ChatMessage> history = request.messages;

    try {
        const agentloop::RunResult result =
            agentloop::run(harness, history, loop_options, reporter);
        if (result.hit_iteration_limit && (decorate || flags.verbose)) {
            std::cerr << "[apogee] tool-call limit reached; answered without tools\n";
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
            const std::string model =
                flags->model.empty() ? config.models.default_backend : flags->model;
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
