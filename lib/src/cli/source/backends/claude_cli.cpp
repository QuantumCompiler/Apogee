#include "backends/claude_cli.h"

#include <algorithm>
#include <utility>

#include "backends/claude_cli_events.h"
#include "harness/errors.h"

namespace apogee::backends {
namespace {

/// A short, stable fingerprint of a message, so a diverged history is noticed.
[[nodiscard]] std::string digest_of(const harness::ChatMessage& message) {
    return std::string{harness::to_string(message.role)} + ":" +
           std::to_string(std::hash<std::string>{}(message.content.plain_text()));
}

/// Renders the messages a fresh child needs in order to have the conversation.
///
/// The transcript-replay fallback: when a resume is refused, the new child
/// knows nothing, so the history goes in as context ahead of the live question.
[[nodiscard]] std::string replay_text(const std::vector<harness::ChatMessage>& messages) {
    std::string out;
    for (const harness::ChatMessage& message : messages) {
        if (message.role == harness::Role::System) {
            continue;  // carried as --system-prompt, not as a turn
        }
        out += message.role == harness::Role::Assistant ? "Assistant: " : "User: ";
        out += message.content.plain_text();
        out += "\n\n";
    }
    return out;
}

[[nodiscard]] std::string system_prompt_of(const harness::ChatRequest& request) {
    std::string prompt;
    for (const harness::ChatMessage& message : request.messages) {
        if (message.role != harness::Role::System) {
            continue;
        }
        if (!prompt.empty()) {
            prompt += "\n\n";
        }
        prompt += message.content.plain_text();
    }
    return prompt;
}

}  // namespace

std::string_view to_string(ClaudeCliMode mode) noexcept {
    return mode == ClaudeCliMode::Bare ? "bare" : "subscription";
}

std::optional<ClaudeCliMode> claude_cli_mode_from_string(std::string_view name) noexcept {
    if (name == "subscription" || name.empty()) {
        return ClaudeCliMode::Subscription;
    }
    if (name == "bare") {
        return ClaudeCliMode::Bare;
    }
    return std::nullopt;
}

std::vector<std::string> build_arguments(const ClaudeCliInvocation& invocation) {
    std::vector<std::string> arguments{"-p"};

    if (invocation.streaming_stdin) {
        // Makes stdin accept JSONL user messages, which is what lets ONE
        // process serve many turns.
        arguments.emplace_back("--input-format");
        arguments.emplace_back("stream-json");
    }

    arguments.emplace_back("--output-format");
    arguments.emplace_back("stream-json");
    // Required by stream-json; the CLI rejects the combination without it.
    arguments.emplace_back("--verbose");
    // The flag that produces token deltas at all. Without it the stream is
    // message-granular, which is the choppiness this design exists to remove.
    arguments.emplace_back("--include-partial-messages");

    if (!invocation.model.empty()) {
        arguments.emplace_back("--model");
        arguments.push_back(invocation.model);
    }
    if (!invocation.system_prompt.empty()) {
        arguments.emplace_back("--system-prompt");
        arguments.push_back(invocation.system_prompt);
    }
    if (!invocation.json_schema.empty()) {
        // Inline JSON is accepted -- no temp file needed.
        arguments.emplace_back("--json-schema");
        arguments.push_back(invocation.json_schema);
    }
    if (!invocation.resume_session_id.empty()) {
        arguments.emplace_back("--resume");
        arguments.push_back(invocation.resume_session_id);
    }
    if (invocation.mode == ClaudeCliMode::Bare) {
        arguments.emplace_back("--bare");
    }
    return arguments;
}

ClaudeCliProvider::ClaudeCliProvider(Options options, Spawner spawner)
    : options_{std::move(options)}, spawner_{std::move(spawner)} {}

ClaudeCliProvider::~ClaudeCliProvider() {
    end_session();
}

std::unique_ptr<ClaudeCliProvider> ClaudeCliProvider::from_config(
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

    const std::optional<ClaudeCliMode> mode = claude_cli_mode_from_string(config.mode);
    if (!mode.has_value()) {
        throw harness::ProviderError(
            backend_name, "unknown mode '" + config.mode + "' (accepted: subscription, bare)");
    }
    options.mode = *mode;

    if (platform::find_on_path(options.binary).empty()) {
        // Named, so the fix is obvious. Apogee never installs or bundles the
        // vendor binary -- the user installs and logs in themselves.
        throw harness::ProviderError(
            backend_name, "'" + options.binary +
                              "' was not found on PATH. Install the Claude CLI and log in, or set "
                              "'binary' on this backend to its full path");
    }

    return std::make_unique<ClaudeCliProvider>(
        std::move(options), [](const platform::ChildCommand& command, std::string& error) {
            return platform::start_child(command, error);
        });
}

std::string_view ClaudeCliProvider::backend_name() const noexcept {
    return options_.backend_name;
}

int ClaudeCliProvider::spawn_count() const noexcept {
    return spawns_;
}

const std::string& ClaudeCliProvider::session_id() const noexcept {
    return session_id_;
}

bool ClaudeCliProvider::has_live_child() const noexcept {
    return child_ != nullptr;
}

void ClaudeCliProvider::end_session() {
    if (child_ == nullptr) {
        return;
    }
    // Closing stdin, not killing: a clean close lets the child emit its
    // terminal event, which is where the cost accounting lives. The CLI then
    // drains its backlog rather than truncating.
    child_->close_stdin();
    (void)child_->wait_for_exit(options_.shutdown_timeout);
    child_.reset();
    framer_.reset();
    pending_.clear();
    delivered_ = 0;
    delivered_digest_.clear();
}

harness::StatusEvent ClaudeCliProvider::model_status() const {
    harness::StatusEvent event;
    event.type = child_ == nullptr ? harness::StatusEvent::Type::ModelLoading
                                   : harness::StatusEvent::Type::ModelReady;
    event.phase = harness::StatusEvent::Phase::Done;
    event.name = options_.model.empty() ? std::string{"claude"} : options_.model;
    return event;
}

bool ClaudeCliProvider::ensure_child(std::string& error) {
    if (child_ != nullptr && !child_->exited()) {
        return true;
    }
    if (child_ != nullptr) {
        // It died. Everything it knew is gone with it, so the delivered
        // bookkeeping resets too -- a resumed child is re-told nothing, and a
        // fresh one is told everything.
        child_.reset();
        framer_.reset();
        pending_.clear();
        delivered_ = 0;
        delivered_digest_.clear();
    }

    ClaudeCliInvocation invocation;
    invocation.mode = options_.mode;
    invocation.model = options_.model;
    invocation.resume_session_id = session_id_;

    platform::ChildCommand command;
    command.program = options_.binary;
    command.arguments = build_arguments(invocation);

    ++spawns_;
    child_ = spawner_(command, error);
    if (child_ == nullptr && !session_id_.empty()) {
        // The resume was refused -- a stale id, a session the CLI has expired.
        // Falling back to a fresh child is the whole point of keeping our own
        // transcript authoritative: a conversation must not die because an
        // optimisation went stale.
        session_id_.clear();
        invocation.resume_session_id.clear();
        command.arguments = build_arguments(invocation);
        ++spawns_;
        child_ = spawner_(command, error);
    }
    return child_ != nullptr;
}

ClaudeCliProvider::TurnOutcome ClaudeCliProvider::pump_turn(platform::ChildProcess& child,
                                                            const harness::StreamOptions& options,
                                                            bool suppress_text) {
    TurnOutcome outcome;
    const auto deadline = std::chrono::steady_clock::now() + options_.turn_timeout;

    std::string chunk;
    bool done = false;

    // Parsing only. Consumption happens below, so a feed that carries past the
    // terminal event leaves the remainder queued instead of misattributing it.
    const auto handle_line = [this](std::string_view line) {
        if (std::optional<CliEvent> parsed = claude_cli::parse_line(line)) {
            pending_.push_back(std::move(*parsed));
        }
    };

    const auto consume = [&](CliEvent&& event) {
        std::visit(
            [&](auto&& event) {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, TextDelta>) {
                    outcome.answer += event.text;
                    // Suppressed in schema mode: otherwise the user watches a
                    // paragraph stream only to be replaced by a JSON object.
                    if (!suppress_text && options.on_token && !event.text.empty()) {
                        options.on_token(event.text);
                    }
                } else if constexpr (std::is_same_v<T, ThinkingDelta>) {
                    // Forwarded even in schema mode -- reasoning is display,
                    // and an empty payload is the redacted-thinking case.
                    if (options.on_thinking && !event.text.empty()) {
                        options.on_thinking(event.text);
                    }
                } else if constexpr (std::is_same_v<T, ThinkingTokens>) {
                    if (options.on_status) {
                        harness::StatusEvent status;
                        status.type = harness::StatusEvent::Type::Thinking;
                        status.phase = harness::StatusEvent::Phase::Start;
                        status.tokens = event.estimated;
                        options.on_status(status);
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
                    outcome.complete = event;
                    if (!event.session_id.empty()) {
                        session_id_ = event.session_id;
                    }
                    done = true;
                } else if constexpr (std::is_same_v<T, Notice>) {
                    if (options.on_status) {
                        harness::StatusEvent status;
                        status.type = harness::StatusEvent::Type::ModelReady;
                        status.phase = harness::StatusEvent::Phase::Done;
                        status.name = event.kind;
                        status.detail = event.detail;
                        options.on_status(status);
                    }
                }
            },
            std::move(event));
    };

    const auto drain_pending = [&] {
        while (!done && !pending_.empty()) {
            CliEvent event = std::move(pending_.front());
            pending_.pop_front();
            consume(std::move(event));
        }
    };

    // Anything left over from the previous turn belongs to this one.
    drain_pending();

    while (!done) {
        if (std::chrono::steady_clock::now() >= deadline) {
            outcome.timed_out = true;
            break;
        }
        // Between chunks, not merely at entry -- a long answer is exactly when
        // a user reaches for Ctrl-C.
        options.cancellation.throw_if_cancelled();

        const platform::ReadStatus status =
            child.read_stdout(chunk, std::chrono::milliseconds{200});
        if (status == platform::ReadStatus::Data) {
            framer_.feed(chunk, handle_line);
            drain_pending();
            continue;
        }
        if (status == platform::ReadStatus::Timeout) {
            continue;
        }
        // EOF or error: the child is finished or gone.
        framer_.flush(handle_line);
        drain_pending();
        if (!done) {
            outcome.child_died = true;
        }
        break;
    }

    // Whatever the child said on stderr, kept bounded, so a failure can be
    // reported with the child's own words rather than an exit code alone.
    std::string errors;
    while (child.read_stderr(errors, std::chrono::milliseconds{0}) == platform::ReadStatus::Data) {
        stderr_tail_ += errors;
        if (stderr_tail_.size() > 8192) {
            stderr_tail_.erase(0, stderr_tail_.size() - 8192);
        }
    }

    return outcome;
}

ClaudeCliProvider::TurnOutcome ClaudeCliProvider::run_one_shot(
    const ClaudeCliInvocation& invocation, std::string_view prompt,
    const harness::StreamOptions& options, bool suppress_text, std::string& error) {
    platform::ChildCommand command;
    command.program = options_.binary;
    command.arguments = build_arguments(invocation);
    command.arguments.emplace_back(prompt);

    ++spawns_;
    std::unique_ptr<platform::ChildProcess> child = spawner_(command, error);
    if (child == nullptr) {
        TurnOutcome outcome;
        outcome.child_died = true;
        return outcome;
    }

    child->close_stdin();

    // A one-shot gets its own framer: a half-line from this child must never
    // prefix the session child's next event.
    JsonlFramer saved_framer = std::move(framer_);
    std::deque<CliEvent> saved_pending = std::move(pending_);
    framer_ = JsonlFramer{};
    pending_.clear();
    TurnOutcome outcome = pump_turn(*child, options, suppress_text);
    framer_ = std::move(saved_framer);
    pending_ = std::move(saved_pending);

    (void)child->wait_for_exit(options_.shutdown_timeout);
    return outcome;
}

harness::ChatResponse ClaudeCliProvider::chat(const harness::ChatRequest& request,
                                              const harness::CancellationToken& cancellation) {
    harness::StreamOptions options;
    options.cancellation = cancellation;
    return stream_chat(request, options);
}

harness::ChatResponse ClaudeCliProvider::stream_chat(const harness::ChatRequest& request,
                                                     const harness::StreamOptions& options) {
    if (request.messages.empty()) {
        throw harness::ProviderError(options_.backend_name, "a request needs at least one message");
    }

    // A side request -- a background title, a clerk call -- is not a turn of
    // this conversation. It runs on its own short-lived child so the session's
    // state is untouched: the same rule the local backend applies to its KV
    // cache, for the same reason.
    if (request.transient.side_request) {
        ClaudeCliInvocation invocation;
        invocation.mode = options_.mode;
        invocation.model = options_.model;
        invocation.system_prompt = system_prompt_of(request);
        invocation.streaming_stdin = false;

        std::string error;
        const TurnOutcome outcome =
            run_one_shot(invocation, replay_text(request.messages), options, false, error);
        if (outcome.child_died && outcome.answer.empty()) {
            throw harness::ProviderError(
                options_.backend_name,
                error.empty() ? "the side request produced no result" : error);
        }

        harness::ChatResponse response;
        response.message = harness::ChatMessage::assistant(
            outcome.complete.final_text.empty() ? outcome.answer : outcome.complete.final_text);
        response.model = options_.model;
        return response;
    }

    // Does the caller's history still extend what this child was told? A
    // compaction or an edit rewrites the past, and a child fed a contradiction
    // would answer against a conversation that no longer exists.
    bool extends = delivered_ <= request.messages.size();
    if (extends) {
        for (std::size_t index = 0; index < delivered_ && index < delivered_digest_.size();
             ++index) {
            if (delivered_digest_[index] != digest_of(request.messages[index])) {
                extends = false;
                break;
            }
        }
    }
    if (!extends) {
        end_session();
        session_id_.clear();  // the CLI's copy of the old history is worthless now
    }

    std::string error;
    if (!ensure_child(error)) {
        throw harness::ProviderError(
            options_.backend_name,
            error.empty() ? "could not start the Claude CLI"
                          : error + (stderr_tail_.empty() ? "" : " -- " + stderr_tail_));
    }

    // Send only what is new. The CLI keeps the conversation, so re-sending
    // history every turn would duplicate it -- and re-sending nothing on a
    // fresh child would ask a question with no context.
    for (std::size_t index = delivered_; index < request.messages.size(); ++index) {
        const harness::ChatMessage& message = request.messages[index];
        if (message.role == harness::Role::System) {
            continue;  // a process-start flag, not a turn
        }
        if (message.role == harness::Role::Assistant) {
            continue;  // the CLI produced it; echoing it back would duplicate
        }
        if (!child_->write_stdin(claude_cli::user_message_line(message.content.plain_text()) +
                                 "\n")) {
            throw harness::ProviderError(options_.backend_name,
                                         "the Claude CLI closed its input mid-turn");
        }
    }

    delivered_digest_.clear();
    delivered_digest_.reserve(request.messages.size());
    for (const harness::ChatMessage& message : request.messages) {
        delivered_digest_.push_back(digest_of(message));
    }
    delivered_ = request.messages.size();

    TurnOutcome outcome = pump_turn(*child_, options, false);

    if (outcome.child_died && outcome.answer.empty()) {
        // It died before saying anything. Resume once -- this is the recovery
        // path the whole session-id capture exists for.
        if (ensure_child(error)) {
            for (const harness::ChatMessage& message : request.messages) {
                if (message.role != harness::Role::User) {
                    continue;
                }
                (void)child_->write_stdin(
                    claude_cli::user_message_line(message.content.plain_text()) + "\n");
            }
            outcome = pump_turn(*child_, options, false);
        }
    }

    if (outcome.timed_out) {
        throw harness::ProviderError(options_.backend_name,
                                     "the Claude CLI did not finish in time");
    }
    if (outcome.complete.is_error) {
        throw harness::ProviderError(
            options_.backend_name,
            "the Claude CLI reported " + (outcome.complete.error_subtype.empty()
                                              ? std::string{"an error"}
                                              : outcome.complete.error_subtype));
    }

    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(
        outcome.answer.empty() ? outcome.complete.final_text : outcome.answer);
    response.model = options_.model;
    response.finish_reason = harness::FinishReason::Stop;
    response.usage.prompt_tokens = outcome.complete.input_tokens;
    response.usage.completion_tokens = outcome.complete.output_tokens;

    // The assistant turn the CLI just produced is part of the conversation it
    // remembers, so it counts as delivered.
    delivered_digest_.push_back(digest_of(response.message));
    ++delivered_;
    return response;
}

std::string ClaudeCliProvider::complete_structured(std::string_view prompt,
                                                   std::string_view json_schema,
                                                   const harness::CancellationToken& cancellation) {
    ClaudeCliInvocation invocation;
    invocation.mode = options_.mode;
    invocation.model = options_.model;
    invocation.json_schema = std::string{json_schema};
    invocation.streaming_stdin = false;

    harness::StreamOptions options;
    options.cancellation = cancellation;

    std::string error;
    // suppress_text: the prose pre-answer is held, not shown.
    const TurnOutcome outcome = run_one_shot(invocation, prompt, options, true, error);

    if (!outcome.complete.structured_output.empty()) {
        return outcome.complete.structured_output;
    }
    // The CLI did not populate the field. Degrading to the held prose keeps a
    // clerk working through a schema regression instead of returning nothing.
    if (!outcome.complete.final_text.empty()) {
        return outcome.complete.final_text;
    }
    if (!outcome.answer.empty()) {
        return outcome.answer;
    }
    throw harness::ProviderError(options_.backend_name,
                                 error.empty() ? "the schema request produced no result" : error);
}

std::vector<harness::ModelInfo> ClaudeCliProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    harness::ModelInfo info;
    info.id = options_.model.empty() ? "claude" : options_.model;
    info.name = info.id;
    info.provider = "claude-cli";
    info.backend = options_.backend_name;
    return {info};
}

}  // namespace apogee::backends
