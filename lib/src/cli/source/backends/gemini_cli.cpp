#include "backends/gemini_cli.h"

#include <array>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

#include "backends/gemini_cli_events.h"
#include "backends/jsonl_framer.h"
#include "harness/errors.h"

namespace apogee::backends {
namespace {

/// A UUID v4, which is the form `--session-id` accepts.
///
/// Apogee generating the id is what makes this backend's continuity story
/// clean: there is nothing to capture from the stream and nothing to read off
/// disk, so the "never read the CLI's session files" rule costs nothing.
[[nodiscard]] std::string make_uuid() {
    std::random_device entropy;
    std::array<std::uint32_t, 4> words{entropy(), entropy(), entropy(), entropy()};

    // Version 4, variant 1 -- so the value is a well-formed UUID rather than
    // sixteen random bytes that happen to be the right length.
    words[1] = (words[1] & 0xFFFF0FFFU) | 0x00004000U;
    words[2] = (words[2] & 0x3FFFFFFFU) | 0x80000000U;

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    out << std::setw(8) << words[0] << "-";
    out << std::setw(4) << (words[1] >> 16U) << "-";
    out << std::setw(4) << (words[1] & 0xFFFFU) << "-";
    out << std::setw(4) << (words[2] >> 16U) << "-";
    out << std::setw(4) << (words[2] & 0xFFFFU);
    out << std::setw(8) << words[3];
    return out.str();
}

/// Renders the messages a turn needs to send.
///
/// On a resumed session the CLI already holds the conversation, so only the new
/// user text goes out. On a fresh session the whole history does.
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

GeminiCliProvider::GeminiCliProvider(Options options, Spawner spawner, IdFactory id_factory)
    : options_{std::move(options)},
      spawner_{std::move(spawner)},
      id_factory_{std::move(id_factory)} {}

std::unique_ptr<GeminiCliProvider> GeminiCliProvider::from_config(
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

    // `mode` is deliberately ignored, as on codex. The Claude backend
    // distinguishes subscription from bare because its CLI does; gemini has no
    // `--bare` analogue, and inventing a field that does nothing for the sake
    // of symmetry would be worse than not having one.
    if (!config.mode.empty() && config.mode != "subscription") {
        throw harness::ProviderError(
            backend_name, "the gemini CLI has no auth modes, so 'mode: " + config.mode +
                              "' means nothing here. Remove it -- authentication is whatever "
                              "'gemini' was logged into. For an API key, use a 'google' backend");
    }

    if (platform::find_on_path(options.binary).empty()) {
        throw harness::ProviderError(
            backend_name, "'" + options.binary +
                              "' was not found on PATH. Install the Gemini CLI and sign in with "
                              "your Google account, or set 'binary' on this backend to its full "
                              "path");
    }

    return std::make_unique<GeminiCliProvider>(
        std::move(options),
        [](const platform::ChildCommand& command, std::string& error) {
            return platform::start_child(command, error);
        },
        make_uuid);
}

std::vector<std::string> GeminiCliProvider::build_arguments(const Options& options,
                                                            Invocation invocation,
                                                            const std::string& session_id) {
    std::vector<std::string> arguments;

    // The typed event stream. Without it stdout is prose meant for a terminal.
    arguments.emplace_back("--output-format");
    arguments.emplace_back("stream-json");

    // PINNED, and not configurable. This CLI is an agent that runs tools while
    // answering; `plan` is its read-only mode, and it is the only setting under
    // which "answer this question" cannot become "modify this machine".
    arguments.emplace_back("--approval-mode");
    arguments.emplace_back("plan");

    // Load-bearing TWICE, which is why it is not optional:
    //   1. Without it a headless run in an untrusted directory refuses to start
    //      at all -- and Apogee spawns wherever the user's shell happens to be.
    //   2. It suppresses the workspace-trust override that was observed
    //      silently replacing `--approval-mode plan` with `default`. Pinning
    //      read-only without this flag pins nothing.
    arguments.emplace_back("--skip-trust");

    if (invocation == Invocation::Resume) {
        // Verified on 0.46.0: --resume takes the session UUID. (An earlier note
        // written before a login existed said it took only `latest` or an index
        // number; the recording disproved that.)
        arguments.emplace_back("--resume");
        arguments.push_back(session_id);
    } else {
        // Apogee's own id, so nothing is ever captured from the stream.
        arguments.emplace_back("--session-id");
        arguments.push_back(session_id);
    }

    if (!options.model.empty()) {
        arguments.emplace_back("--model");
        arguments.push_back(options.model);
    }

    // NOTE: --raw-output is deliberately never added. It disables sanitisation
    // of model output and the CLI itself warns that it is a security risk.
    return arguments;
}

std::string_view GeminiCliProvider::backend_name() const noexcept {
    return options_.backend_name;
}

const std::string& GeminiCliProvider::session_id() const noexcept {
    return session_id_;
}

int GeminiCliProvider::spawn_count() const noexcept {
    return spawns_;
}

void GeminiCliProvider::end_session() {
    session_id_.clear();
}

harness::StatusEvent GeminiCliProvider::model_status() const {
    harness::StatusEvent event;
    event.type = harness::StatusEvent::Type::ModelReady;
    event.phase = harness::StatusEvent::Phase::Done;
    event.name = options_.model.empty() ? std::string{"gemini"} : options_.model;
    return event;
}

GeminiCliProvider::Outcome GeminiCliProvider::run_turn(const std::string& prompt,
                                                       const harness::StreamOptions& options) {
    const Invocation invocation = session_id_.empty() ? Invocation::Fresh : Invocation::Resume;
    if (session_id_.empty()) {
        session_id_ = id_factory_();
    }

    platform::ChildCommand command;
    command.program = options_.binary;
    command.arguments = build_arguments(options_, invocation, session_id_);
    // The prompt is a flag value here, not a bare positional: the positional
    // `query` defaults to INTERACTIVE, so a backend that forgot -p would hang
    // waiting for a terminal that is not there.
    command.arguments.emplace_back("--prompt");
    command.arguments.push_back(prompt);

    ++spawns_;
    std::string error;
    std::unique_ptr<platform::ChildProcess> child = spawner_(command, error);
    if (child == nullptr) {
        throw harness::ProviderError(options_.backend_name,
                                     error.empty() ? "could not start the Gemini CLI" : error);
    }

    // The prompt rides in argv. Closing stdin stops the CLI waiting on input
    // that will never come.
    child->close_stdin();

    Outcome outcome;
    JsonlFramer framer;
    const auto deadline = std::chrono::steady_clock::now() + options_.turn_timeout;

    const auto handle = [&](std::string_view line) {
        std::optional<CliEvent> parsed = gemini_cli::parse_line(line);
        if (!parsed.has_value()) {
            return;
        }
        std::visit(
            [&](auto&& event) {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, TextDelta>) {
                    // Concatenated verbatim. A recorded delta boundary fell
                    // inside a two-digit number, so nothing here may assume a
                    // token, line, or character boundary.
                    outcome.answer += event.text;
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
                } else if constexpr (std::is_same_v<T, ToolOutcome>) {
                    if (options.on_status) {
                        harness::StatusEvent status;
                        status.type = harness::StatusEvent::Type::ToolCall;
                        status.phase = harness::StatusEvent::Phase::Done;
                        status.name = event.id;
                        options.on_status(status);
                    }
                } else if constexpr (std::is_same_v<T, TurnComplete>) {
                    outcome.input_tokens = event.input_tokens;
                    outcome.output_tokens = event.output_tokens;
                    if (!event.model.empty()) {
                        outcome.model = event.model;
                    }
                    if (event.is_error) {
                        outcome.is_error = true;
                        outcome.error_detail = event.error_subtype;
                    }
                }
                // A `session` Notice is deliberately not consulted: the id was
                // ours to begin with. Reading it back to "learn" the session
                // would be the disk-reading habit this family exists to avoid,
                // dressed up as a stream read.
            },
            std::move(*parsed));
    };

    std::string chunk;
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw harness::ProviderError(options_.backend_name,
                                         "the Gemini CLI did not finish in time");
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

    // stderr is drained separately and never merged: this CLI writes a
    // 256-color warning and [STARTUP] lines there on every run, and merging
    // them into stdout would put non-JSON in front of the framer.
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

harness::ChatResponse GeminiCliProvider::chat(const harness::ChatRequest& request,
                                              const harness::CancellationToken& cancellation) {
    harness::StreamOptions options;
    options.cancellation = cancellation;
    return stream_chat(request, options);
}

harness::ChatResponse GeminiCliProvider::stream_chat(const harness::ChatRequest& request,
                                                     const harness::StreamOptions& options) {
    if (request.messages.empty()) {
        throw harness::ProviderError(options_.backend_name, "a request needs at least one message");
    }

    // A side request is not a turn of this conversation, so it must not join
    // the session -- the same rule the other backends apply.
    const bool side_request = request.transient.side_request;
    const std::string saved_session = session_id_;
    if (side_request) {
        session_id_.clear();
    }

    const std::string prompt = prompt_for(request.messages, !session_id_.empty());
    Outcome outcome = run_turn(prompt, options);

    if (side_request) {
        // Whatever session the side request created is discarded; the
        // conversation's own session is untouched.
        session_id_ = saved_session;
    }

    if (outcome.is_error) {
        throw harness::ProviderError(
            options_.backend_name,
            "the Gemini CLI reported an error" +
                (outcome.error_detail.empty() ? std::string{} : ": " + outcome.error_detail));
    }

    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(outcome.answer);
    // The CLI routes internally and names the models it used; reporting the one
    // that produced the answer beats reporting "auto", which is what `init`
    // says and what no user could act on.
    response.model = outcome.model.empty() ? options_.model : outcome.model;
    response.finish_reason = harness::FinishReason::Stop;
    response.usage.prompt_tokens = outcome.input_tokens;
    response.usage.completion_tokens = outcome.output_tokens;
    return response;
}

std::vector<harness::ModelInfo> GeminiCliProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    harness::ModelInfo info;
    info.id = options_.model.empty() ? "gemini" : options_.model;
    info.name = info.id;
    info.provider = "gemini-cli";
    info.backend = options_.backend_name;
    return {info};
}

}  // namespace apogee::backends
