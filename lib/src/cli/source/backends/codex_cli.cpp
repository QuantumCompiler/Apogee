#include "backends/codex_cli.h"

#include <utility>

#include "backends/codex_cli_events.h"
#include "backends/jsonl_framer.h"
#include "harness/errors.h"

namespace apogee::backends {
namespace {

/// Renders the messages a turn needs to send.
///
/// On a resumed thread the CLI already holds the conversation, so only the new
/// user text goes out. On a fresh thread the whole history does.
[[nodiscard]] std::string prompt_for(const std::vector<harness::ChatMessage>& messages,
                                     bool resuming) {
    if (resuming) {
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->role == harness::Role::User) {
                return it->content.plain_text();
            }
        }
        return {};
    }

    std::string out;
    for (const harness::ChatMessage& message : messages) {
        const std::string text = message.content.plain_text();
        if (text.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += "\n\n";
        }
        switch (message.role) {
            case harness::Role::Assistant:
                out += "Assistant: ";
                break;
            case harness::Role::System:
                out += "System: ";
                break;
            default:
                out += "User: ";
                break;
        }
        out += text;
    }
    return out;
}

}  // namespace

CodexCliProvider::CodexCliProvider(Options options, Spawner spawner)
    : options_{std::move(options)}, spawner_{std::move(spawner)} {}

std::unique_ptr<CodexCliProvider> CodexCliProvider::from_config(
    const std::string& backend_name, const harness::BackendConfig& config) {
    if (!platform::supports_child_processes()) {
        throw harness::ProviderError(
            backend_name,
            "this platform cannot spawn child processes yet, so the vendor-CLI backends are "
            "unavailable here. Use a direct-API backend instead");
    }

    Options options;
    options.backend_name = backend_name;
    if (!config.binary.empty()) {
        options.binary = config.binary;
    }
    options.model = config.model;

    // `mode` is deliberately ignored. The Claude backend distinguishes
    // subscription from bare because its CLI does; codex has no `--bare`
    // analogue, and inventing a field that does nothing for the sake of
    // symmetry would be worse than not having one.
    if (!config.mode.empty() && config.mode != "subscription") {
        throw harness::ProviderError(backend_name,
                                     "the codex CLI has no auth modes, so 'mode: " + config.mode +
                                         "' means nothing here. Remove it -- authentication is "
                                         "whatever 'codex login' set up");
    }

    if (platform::find_on_path(options.binary).empty()) {
        throw harness::ProviderError(
            backend_name, "'" + options.binary +
                              "' was not found on PATH. Install the Codex CLI and run "
                              "'codex login', or set 'binary' on this backend to its full path");
    }

    return std::make_unique<CodexCliProvider>(
        std::move(options), [](const platform::ChildCommand& command, std::string& error) {
            return platform::start_child(command, error);
        });
}

std::vector<std::string> CodexCliProvider::build_arguments(const Options& options,
                                                           Invocation invocation,
                                                           const std::string& thread_id,
                                                           const std::string& schema_path) {
    std::vector<std::string> arguments{"exec"};

    if (invocation == Invocation::Resume) {
        arguments.emplace_back("resume");
        arguments.push_back(thread_id);
    }

    // The typed event stream. Without it stdout is prose meant for a terminal.
    arguments.emplace_back("--json");
    // The CLI expects a git repository and refuses outside one; a chat backend
    // has no business caring where the user's shell happens to be.
    arguments.emplace_back("--skip-git-repo-check");

    if (invocation == Invocation::Fresh) {
        // `resume` REJECTS both of these. Verified on 0.153.4: passing --color
        // to the subcommand fails with "unexpected argument". One argv builder
        // shared between the two forms works on turn one and breaks on turn
        // two, which is the worst possible place to find out.
        arguments.emplace_back("--color");
        arguments.emplace_back("never");

        // PINNED, and not configurable. `codex exec` is an agent that executes
        // model-generated shell commands; read-only is the only policy under
        // which "answer this question" cannot become "modify this machine".
        arguments.emplace_back("--sandbox");
        arguments.emplace_back("read-only");

        if (!options.model.empty()) {
            arguments.emplace_back("--model");
            arguments.push_back(options.model);
        }
    }

    if (!schema_path.empty()) {
        // A FILE, not inline JSON -- the opposite of Claude's --json-schema.
        arguments.emplace_back("--output-schema");
        arguments.push_back(schema_path);
    }
    return arguments;
}

std::string_view CodexCliProvider::backend_name() const noexcept {
    return options_.backend_name;
}

const std::string& CodexCliProvider::thread_id() const noexcept {
    return thread_id_;
}

int CodexCliProvider::spawn_count() const noexcept {
    return spawns_;
}

void CodexCliProvider::end_session() {
    thread_id_.clear();
}

harness::StatusEvent CodexCliProvider::model_status() const {
    harness::StatusEvent event;
    event.type = harness::StatusEvent::Type::ModelReady;
    event.phase = harness::StatusEvent::Phase::Done;
    event.name = options_.model.empty() ? std::string{"codex"} : options_.model;
    return event;
}

CodexCliProvider::Outcome CodexCliProvider::run_turn(const std::string& prompt,
                                                     const std::string& schema_path,
                                                     const harness::StreamOptions& options) {
    const Invocation invocation = thread_id_.empty() ? Invocation::Fresh : Invocation::Resume;

    platform::ChildCommand command;
    command.program = options_.binary;
    command.arguments = build_arguments(options_, invocation, thread_id_, schema_path);
    command.arguments.push_back(prompt);

    ++spawns_;
    std::string error;
    std::unique_ptr<platform::ChildProcess> child = spawner_(command, error);
    if (child == nullptr) {
        throw harness::ProviderError(options_.backend_name,
                                     error.empty() ? "could not start the Codex CLI" : error);
    }

    // The prompt rides in argv. Closing stdin stops the CLI waiting on input
    // that will never come -- it reads stdin when a prompt is absent.
    child->close_stdin();

    Outcome outcome;
    JsonlFramer framer;
    const auto deadline = std::chrono::steady_clock::now() + options_.turn_timeout;

    const auto handle = [&](std::string_view line) {
        std::optional<CliEvent> parsed = codex_cli::parse_line(line);
        if (!parsed.has_value()) {
            return;
        }
        std::visit(
            [&](auto&& event) {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, TextDelta>) {
                    outcome.answer += event.text;
                    // "Delta" is the union's word, not this CLI's: the whole
                    // message arrives at once, so a surface sees one chunk.
                    if (options.on_token && !event.text.empty()) {
                        options.on_token(event.text);
                    }
                } else if constexpr (std::is_same_v<T, ToolUseStart>) {
                    if (options.on_status) {
                        harness::StatusEvent status;
                        status.type = harness::StatusEvent::Type::ToolCall;
                        status.phase = harness::StatusEvent::Phase::Start;
                        status.name = event.name;
                        options.on_status(status);
                    }
                } else if constexpr (std::is_same_v<T, TurnComplete>) {
                    outcome.input_tokens = event.input_tokens;
                    outcome.output_tokens = event.output_tokens;
                    if (event.is_error) {
                        outcome.is_error = true;
                        outcome.error_detail = event.error_subtype;
                    }
                } else if constexpr (std::is_same_v<T, Notice>) {
                    // The resume handle, captured and stored by US -- which is
                    // why nothing here ever reads the CLI's session files.
                    if (event.kind == "thread" && !event.detail.empty()) {
                        thread_id_ = event.detail;
                    }
                }
            },
            std::move(*parsed));
    };

    std::string chunk;
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw harness::ProviderError(options_.backend_name,
                                         "the Codex CLI did not finish in time");
        }
        options.cancellation.throw_if_cancelled();

        const platform::ReadStatus status =
            child->read_stdout(chunk, std::chrono::milliseconds{200});
        if (status == platform::ReadStatus::Data) {
            framer.feed(chunk, handle);
            continue;
        }
        if (status == platform::ReadStatus::Timeout) {
            continue;
        }
        framer.flush(handle);
        break;
    }

    std::string diagnostics;
    std::string piece;
    while (child->read_stderr(piece, std::chrono::milliseconds{0}) == platform::ReadStatus::Data) {
        diagnostics += piece;
        if (diagnostics.size() > 8192) {
            diagnostics.erase(0, diagnostics.size() - 8192);
        }
    }

    const std::optional<int> status = child->wait_for_exit(std::chrono::seconds{5});
    if (status.has_value() && *status != 0 && outcome.answer.empty()) {
        outcome.is_error = true;
        if (outcome.error_detail.empty()) {
            outcome.error_detail = "exited with status " + std::to_string(*status) +
                                   (diagnostics.empty() ? "" : ": " + diagnostics);
        }
    }
    return outcome;
}

harness::ChatResponse CodexCliProvider::chat(const harness::ChatRequest& request,
                                             const harness::CancellationToken& cancellation) {
    harness::StreamOptions options;
    options.cancellation = cancellation;
    return stream_chat(request, options);
}

harness::ChatResponse CodexCliProvider::stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) {
    if (request.messages.empty()) {
        throw harness::ProviderError(options_.backend_name, "a request needs at least one message");
    }

    // A side request is not a turn of this conversation, so it must not join
    // the thread -- the same rule the other backends apply. Running it against
    // a fresh thread is how that is achieved when the session IS the thread id.
    const bool side_request = request.transient.side_request;
    const std::string saved_thread = thread_id_;
    if (side_request) {
        thread_id_.clear();
    }

    const std::string prompt = prompt_for(request.messages, !thread_id_.empty());
    Outcome outcome = run_turn(prompt, {}, options);

    if (side_request) {
        // Whatever thread the side request created is discarded; the
        // conversation's own thread is untouched.
        thread_id_ = saved_thread;
    }

    if (outcome.is_error) {
        throw harness::ProviderError(
            options_.backend_name,
            "the Codex CLI reported an error" +
                (outcome.error_detail.empty() ? std::string{} : ": " + outcome.error_detail));
    }

    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(outcome.answer);
    response.model = options_.model;
    response.finish_reason = harness::FinishReason::Stop;
    response.usage.prompt_tokens = outcome.input_tokens;
    response.usage.completion_tokens = outcome.output_tokens;
    return response;
}

std::vector<harness::ModelInfo> CodexCliProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    harness::ModelInfo info;
    info.id = options_.model.empty() ? "codex" : options_.model;
    info.name = info.id;
    info.provider = "codex-cli";
    info.backend = options_.backend_name;
    return {info};
}

}  // namespace apogee::backends
