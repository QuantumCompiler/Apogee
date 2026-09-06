#include "backends/ollama_cli.h"

#include <utility>

#include "backends/http_client.h"
#include "harness/errors.h"

namespace apogee::backends {
namespace {

/// How a role is labelled when history is flattened into one prompt.
[[nodiscard]] std::string_view speaker(harness::Role role) noexcept {
    switch (role) {
        case harness::Role::Assistant:
            return "Assistant";
        case harness::Role::System:
            return "System";
        case harness::Role::User:
        case harness::Role::Tool:
            break;
    }
    return "User";
}

}  // namespace

bool ollama_server_reachable(const std::string& host) {
    // An outbound GET. This does not open a listening socket, so it is safe
    // under the interactive-never-listens invariant -- and it is the only way
    // to tell "the user is running Ollama" from "we are about to make Ollama
    // run", which is the distinction the whole rule turns on.
    // ONE attempt, deliberately. The shared client retries with backoff by
    // default, which is right for a real request over a flaky network and wrong
    // here: a refused connection to localhost is an immediate, certain answer,
    // and retrying it three more times only makes the refusal take four times
    // as long to arrive. Measured: 3.5s with the default policy, versus a
    // connect refusal that returns instantly.
    RetryPolicy once;
    once.max_attempts = 1;
    HttpClient client{std::make_unique<CurlTransport>(), once};

    HttpRequest request;
    request.method = "GET";
    request.url = "http://" + host + "/api/version";
    // Short: this runs before every turn, and a hung probe would be worse than
    // the failure it is checking for.
    request.timeout = std::chrono::seconds{2};

    try {
        const HttpResponse response = client.send(request, {}, {});
        return response.status >= 200 && response.status < 500;
    } catch (const std::exception&) {
        return false;
    }
}

OllamaCliProvider::OllamaCliProvider(Options options, Spawner spawner, ServerProbe probe)
    : options_{std::move(options)}, spawner_{std::move(spawner)}, probe_{std::move(probe)} {}

std::unique_ptr<OllamaCliProvider> OllamaCliProvider::from_config(
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
    if (config.model.empty()) {
        throw harness::ProviderError(
            backend_name,
            "no model configured. Set model on this backend to an Ollama cloud model, "
            "e.g. gpt-oss:20b-cloud");
    }
    options.model = config.model;
    if (!config.host.empty()) {
        options.host = config.host;
    }

    if (platform::find_on_path(options.binary).empty()) {
        throw harness::ProviderError(
            backend_name, "'" + options.binary +
                              "' was not found on PATH. Install Ollama and run 'ollama signin', or "
                              "set 'binary' on this backend to its full path");
    }

    return std::make_unique<OllamaCliProvider>(
        std::move(options),
        [](const platform::ChildCommand& command, std::string& error) {
            return platform::start_child(command, error);
        },
        [](const std::string& host) { return ollama_server_reachable(host); });
}

std::vector<std::string> OllamaCliProvider::build_arguments(const Options& options,
                                                            bool hide_thinking) {
    // `run`, never `serve`. The subcommand is a literal here rather than
    // anything configurable, so no config value can turn this backend into
    // something that starts a server.
    std::vector<std::string> arguments{"run", options.model};

    // Mandatory. Without it the CLI writes cursor-control bytes into stdout to
    // re-wrap words -- even when stdout is a pipe -- and a consumer parses
    // terminal escapes as answer text. Measured on 0.33.2.
    arguments.emplace_back("--nowordwrap");

    if (hide_thinking) {
        arguments.emplace_back("--hidethinking");
    }
    return arguments;
}

std::string OllamaCliProvider::flatten_prompt(const std::vector<harness::ChatMessage>& messages) {
    // No turn protocol exists, so the whole conversation is re-sent as one
    // prompt every turn. Labelled by speaker so the model can at least tell
    // the turns apart.
    std::string out;
    for (const harness::ChatMessage& message : messages) {
        const std::string text = message.content.plain_text();
        if (text.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += "\n\n";
        }
        out += speaker(message.role);
        out += ": ";
        out += text;
    }
    return out;
}

std::string_view OllamaCliProvider::backend_name() const noexcept {
    return options_.backend_name;
}

harness::StatusEvent OllamaCliProvider::model_status() const {
    harness::StatusEvent event;
    event.type = harness::StatusEvent::Type::ModelReady;
    event.phase = harness::StatusEvent::Phase::Done;
    event.name = options_.model;
    return event;
}

harness::ChatResponse OllamaCliProvider::chat(const harness::ChatRequest& request,
                                              const harness::CancellationToken& cancellation) {
    harness::StreamOptions options;
    options.cancellation = cancellation;
    return stream_chat(request, options);
}

harness::ChatResponse OllamaCliProvider::stream_chat(const harness::ChatRequest& request,
                                                     const harness::StreamOptions& options) {
    if (request.messages.empty()) {
        throw harness::ProviderError(options_.backend_name, "a request needs at least one message");
    }

    // THE PRE-FLIGHT. Refusing here is the whole reason this check exists: if
    // we spawned `ollama run` with no server up, the CLI would try to start one
    // and Apogee would have caused a listening socket. Telling the user to
    // start Ollama is the correct answer; starting it for them is not.
    if (probe_ && !probe_(options_.host)) {
        throw harness::ProviderError(
            options_.backend_name,
            "no Ollama server is reachable at " + options_.host +
                ". Start Ollama yourself (open the app, or run 'ollama serve' in another "
                "terminal) and try again -- Apogee will not start it for you, because a "
                "background server it launched would be a listening socket it owns");
    }

    platform::ChildCommand command;
    command.program = options_.binary;
    command.arguments = build_arguments(options_, false);
    command.arguments.push_back(flatten_prompt(request.messages));

    std::string error;
    std::unique_ptr<platform::ChildProcess> child = spawner_(command, error);
    if (child == nullptr) {
        throw harness::ProviderError(options_.backend_name,
                                     error.empty() ? "could not start the Ollama CLI" : error);
    }

    // The prompt rides in argv, so nothing is written to stdin. Closing it lets
    // the CLI proceed rather than wait on input it will never get.
    child->close_stdin();

    OllamaOutputDemux demux;
    const auto deadline = std::chrono::steady_clock::now() + options_.turn_timeout;

    const auto on_answer = [&options](std::string_view chunk) {
        if (options.on_token && !chunk.empty()) {
            options.on_token(chunk);
        }
    };
    const auto on_thinking = [&options](std::string_view chunk) {
        if (options.on_thinking && !chunk.empty()) {
            options.on_thinking(chunk);
        }
    };

    std::string chunk;
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw harness::ProviderError(options_.backend_name,
                                         "the Ollama CLI did not finish in time");
        }
        // Between chunks, not merely at entry.
        options.cancellation.throw_if_cancelled();

        const platform::ReadStatus status =
            child->read_stdout(chunk, std::chrono::milliseconds{200});
        if (status == platform::ReadStatus::Data) {
            demux.feed(chunk, on_answer, on_thinking);
            continue;
        }
        if (status == platform::ReadStatus::Timeout) {
            continue;
        }
        demux.flush(on_answer, on_thinking);
        break;
    }

    // stderr is terminal chrome in the success case, so it is only worth
    // surfacing when there is nothing else to explain a failure.
    std::string diagnostics;
    std::string piece;
    while (child->read_stderr(piece, std::chrono::milliseconds{0}) == platform::ReadStatus::Data) {
        diagnostics += piece;
        if (diagnostics.size() > 8192) {
            diagnostics.erase(0, diagnostics.size() - 8192);
        }
    }

    const std::optional<int> status = child->wait_for_exit(std::chrono::seconds{5});
    if (status.has_value() && *status != 0 && demux.answer().empty()) {
        throw harness::ProviderError(
            options_.backend_name, "the Ollama CLI exited with status " + std::to_string(*status) +
                                       (diagnostics.empty() ? "" : ": " + diagnostics));
    }

    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(demux.answer());
    response.model = options_.model;
    response.finish_reason = harness::FinishReason::Stop;
    // The CLI reports no token accounting on this path, and inventing an
    // estimate here would make it indistinguishable from a measured one. The
    // loop's own estimator handles the gap.
    return response;
}

std::vector<harness::ModelInfo> OllamaCliProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    harness::ModelInfo info;
    info.id = options_.model;
    info.name = options_.model;
    info.provider = "ollama-cli";
    info.backend = options_.backend_name;
    return {info};
}

}  // namespace apogee::backends
