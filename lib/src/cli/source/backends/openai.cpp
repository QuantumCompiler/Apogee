#include "backends/openai.h"

#include <map>
#include <utility>

#include "backends/sse_parser.h"
#include "harness/errors.h"

namespace apogee::backends {
namespace {

/// Accumulates a Responses stream.
///
/// Unlike Anthropic's indexed content blocks, Responses names what changed in
/// each event's `type`. The state that must be tracked is the mapping from an
/// output-item index to the tool call it is building: `output_item.added`
/// announces the call's name and id, then its arguments arrive as deltas
/// carrying only the index.
class StreamAccumulator {
public:
    StreamAccumulator(const harness::StreamOptions& options, std::string model)
        : options_{options}, model_{std::move(model)} {}

    bool handle(const SseEvent& event);

    [[nodiscard]] harness::ChatResponse take_response();

    [[nodiscard]] const std::string& error() const noexcept {
        return error_;
    }

private:
    struct PendingCall {
        std::string id;
        std::string name;
        std::string arguments;
    };

    const harness::StreamOptions& options_;
    std::string model_;
    std::map<std::int64_t, PendingCall> calls_;
    std::string text_;
    harness::FinishReason finish_reason_ = harness::FinishReason::Stop;
    harness::Usage usage_;
    std::string error_;
};

bool StreamAccumulator::handle(const SseEvent& event) {
    const nlohmann::json payload = nlohmann::json::parse(event.data, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        return true;  // one malformed frame must not abort a good answer
    }
    const std::string type = payload.value("type", event.name);

    if (type == "error" || type == "response.failed") {
        error_ = openai::error_message(0, event.data);
        return false;
    }

    if (type == "response.output_text.delta") {
        const std::string chunk = payload.value("delta", std::string{});
        text_ += chunk;
        if (options_.on_token && !chunk.empty()) {
            options_.on_token(chunk);
        }
        return true;
    }

    if (type == "response.reasoning_summary_text.delta") {
        // Its own channel, never into text_. Reasoning must not reach the
        // returned content or persisted history.
        if (options_.on_thinking) {
            const std::string chunk = payload.value("delta", std::string{});
            if (!chunk.empty()) {
                options_.on_thinking(chunk);
            }
        }
        return true;
    }

    if (type == "response.output_item.added") {
        const auto index = payload.value("output_index", std::int64_t{0});
        if (const auto item = payload.find("item"); item != payload.end()) {
            if (item->value("type", std::string{}) == "function_call") {
                PendingCall call;
                // call_id, not id -- `id` identifies the output item, and using
                // it makes every tool result fail to match its call.
                call.id = item->value("call_id", std::string{});
                call.name = item->value("name", std::string{});
                call.arguments = item->value("arguments", std::string{});
                calls_[index] = std::move(call);
            }
        }
        return true;
    }

    if (type == "response.function_call_arguments.delta") {
        const auto index = payload.value("output_index", std::int64_t{0});
        calls_[index].arguments += payload.value("delta", std::string{});
        return true;
    }

    if (type == "response.completed" || type == "response.incomplete") {
        if (const auto response = payload.find("response"); response != payload.end()) {
            if (const std::string model = response->value("model", std::string{}); !model.empty()) {
                model_ = model;
            }
            // Usage appears HERE and nowhere else in the stream.
            if (const auto usage = response->find("usage"); usage != response->end()) {
                usage_.prompt_tokens = usage->value("input_tokens", std::int64_t{0});
                usage_.completion_tokens = usage->value("output_tokens", std::int64_t{0});
            }
            std::string incomplete;
            if (const auto details = response->find("incomplete_details");
                details != response->end() && details->is_object()) {
                incomplete = details->value("reason", std::string{});
            }
            finish_reason_ = openai::finish_reason_from_status(
                response->value("status", std::string{}), incomplete);
        }
        return true;
    }

    // Unknown event types are ignored: OpenAI ships new ones, and a crash on
    // one turns an API addition into an outage.
    return true;
}

harness::ChatResponse StreamAccumulator::take_response() {
    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(text_);
    for (auto& [index, call] : calls_) {
        harness::ToolCall tool_call;
        tool_call.id = call.id;
        tool_call.name = call.name;
        tool_call.arguments = call.arguments.empty() ? "{}" : call.arguments;
        response.message.tool_calls.push_back(std::move(tool_call));
    }
    response.finish_reason =
        response.message.tool_calls.empty() ? finish_reason_ : harness::FinishReason::ToolCalls;
    response.usage = usage_;
    response.model = model_;
    return response;
}

}  // namespace

OpenAIProvider::OpenAIProvider(Options options, std::unique_ptr<HttpClient> client)
    : options_{std::move(options)}, client_{std::move(client)} {}

std::unique_ptr<OpenAIProvider> OpenAIProvider::from_config(const std::string& backend_name,
                                                            const harness::BackendConfig& config,
                                                            bool web_search) {
    Options options;
    options.backend_name = backend_name;
    options.api_key = config.api_key;
    options.web_search = web_search;
    if (!config.model.empty()) {
        options.model = config.model;
    }
    if (config.max_tokens.has_value() && *config.max_tokens > 0) {
        options.max_tokens = *config.max_tokens;
    }
    if (options.api_key.empty()) {
        throw harness::ProviderError(
            backend_name,
            "no API key configured. Set api_key on this backend -- a \"${OPENAI_API_KEY}\" "
            "reference is expanded when the config is read, so the key itself never has to be "
            "written to the file");
    }
    return std::make_unique<OpenAIProvider>(
        std::move(options), std::make_unique<HttpClient>(std::make_unique<CurlTransport>()));
}

std::string_view OpenAIProvider::backend_name() const noexcept {
    return options_.backend_name;
}

openai::RequestOptions OpenAIProvider::request_options(const harness::ChatRequest& request,
                                                       bool stream) const {
    openai::RequestOptions options;
    options.model = options_.model;
    options.max_output_tokens = request.max_tokens.value_or(options_.max_tokens);
    options.reasoning_effort = openai::effort_for_budget(options_.thinking_budget_tokens);
    options.web_search = options_.web_search;
    options.stream = stream;
    return options;
}

HttpRequest OpenAIProvider::build_http_request(const nlohmann::json& body) const {
    HttpRequest request;
    request.method = "POST";
    request.url = options_.base_url + "/v1/responses";
    request.body = body.dump();
    request.headers = {{"content-type", "application/json"},
                       {"authorization", "Bearer " + options_.api_key}};
    request.timeout = std::chrono::seconds{0};
    return request;
}

void OpenAIProvider::fail(long status, std::string_view body) const {
    // Built from the response only -- the request, and therefore the key, is
    // never echoed into an error message.
    throw harness::ProviderError(options_.backend_name, openai::error_message(status, body));
}

harness::ChatResponse OpenAIProvider::chat(const harness::ChatRequest& request,
                                           const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();

    const nlohmann::json body = openai::build_request(request, request_options(request, false));
    const HttpResponse response = client_->send(build_http_request(body), {}, cancellation);
    if (!response.ok()) {
        fail(response.status, response.body);
    }

    const nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded()) {
        throw harness::ProviderError(options_.backend_name,
                                     "the API returned a response that was not valid JSON");
    }
    return openai::parse_response(parsed);
}

harness::ChatResponse OpenAIProvider::stream_chat(const harness::ChatRequest& request,
                                                  const harness::StreamOptions& options) {
    options.cancellation.throw_if_cancelled();

    const nlohmann::json body = openai::build_request(request, request_options(request, true));

    StreamAccumulator accumulator{options, options_.model};
    SseParser parser{[&accumulator](const SseEvent& event) { return accumulator.handle(event); }};

    const BodySink sink = [&](std::string_view chunk) {
        options.cancellation.throw_if_cancelled();
        return parser.feed(chunk);
    };

    HttpRequest http_request = build_http_request(body);
    http_request.headers.push_back({"accept", "text/event-stream"});

    const HttpResponse response = client_->send(http_request, sink, options.cancellation);
    if (!response.ok()) {
        fail(response.status, response.body);
    }
    parser.finish();

    if (!accumulator.error().empty()) {
        throw harness::ProviderError(options_.backend_name, accumulator.error());
    }
    return accumulator.take_response();
}

std::vector<harness::ModelInfo> OpenAIProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    return {harness::ModelInfo{options_.model, options_.model, "openai", options_.backend_name}};
}

}  // namespace apogee::backends
