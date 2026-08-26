#include "backends/anthropic.h"

#include <algorithm>
#include <utility>

#include "backends/sse_parser.h"
#include "harness/errors.h"

namespace apogee::backends {
namespace {

/// Accumulates a streamed message as SSE events arrive.
///
/// Anthropic streams a message as indexed content blocks, each opened by
/// `content_block_start`, filled by `content_block_delta`, and closed by
/// `content_block_stop`. Text, thinking, and tool-call arguments all arrive as
/// deltas on their own index, interleaved — so the accumulator has to track
/// them per index rather than assuming one block at a time.
class StreamAccumulator {
public:
    StreamAccumulator(const harness::StreamOptions& options, std::string model)
        : options_{options}, model_{std::move(model)} {}

    /// Handles one event. Returns false to stop the stream.
    bool handle(const SseEvent& event);

    [[nodiscard]] harness::ChatResponse take_response();

    /// The raw assistant content blocks, rebuilt for the replay cache.
    [[nodiscard]] nlohmann::json raw_blocks() const;

    [[nodiscard]] const std::string& error() const noexcept {
        return error_;
    }

    [[nodiscard]] bool saw_message_stop() const noexcept {
        return saw_message_stop_;
    }

private:
    struct Block {
        std::string type;
        std::string text;       ///< text, or accumulated thinking
        std::string signature;  ///< thinking signature, replayed verbatim
        std::string tool_id;
        std::string tool_name;
        std::string partial_json;
    };

    const harness::StreamOptions& options_;
    std::string model_;
    std::map<std::int64_t, Block> blocks_;
    std::string text_;
    std::vector<harness::ToolCall> tool_calls_;
    harness::FinishReason finish_reason_ = harness::FinishReason::Stop;
    harness::Usage usage_;
    std::string error_;
    bool saw_message_stop_ = false;
};

bool StreamAccumulator::handle(const SseEvent& event) {
    const nlohmann::json payload = nlohmann::json::parse(event.data, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        // A malformed frame is skipped rather than fatal: one bad event must
        // not abort an answer that is otherwise arriving fine.
        return true;
    }

    // Prefer the payload's own `type` over the SSE `event:` name. They agree in
    // practice, but the payload is what the API documents as authoritative.
    const std::string type = payload.value("type", event.name);

    if (type == "error") {
        error_ = anthropic::error_message(0, event.data);
        return false;
    }
    if (type == "ping") {
        return true;
    }

    if (type == "message_start") {
        if (const auto message = payload.find("message"); message != payload.end()) {
            if (const std::string model = message->value("model", std::string{}); !model.empty()) {
                model_ = model;
            }
            if (const auto usage = message->find("usage"); usage != message->end()) {
                usage_.prompt_tokens = usage->value("input_tokens", std::int64_t{0});
            }
        }
        return true;
    }

    if (type == "content_block_start") {
        const auto index = payload.value("index", std::int64_t{0});
        Block block;
        if (const auto start = payload.find("content_block"); start != payload.end()) {
            block.type = start->value("type", std::string{});
            block.tool_id = start->value("id", std::string{});
            block.tool_name = start->value("name", std::string{});
            if (block.type == "text") {
                block.text = start->value("text", std::string{});
            }
        }
        blocks_[index] = std::move(block);
        return true;
    }

    if (type == "content_block_delta") {
        const auto index = payload.value("index", std::int64_t{0});
        Block& block = blocks_[index];
        const auto delta = payload.find("delta");
        if (delta == payload.end()) {
            return true;
        }
        const std::string delta_type = delta->value("type", std::string{});

        if (delta_type == "text_delta") {
            const std::string chunk = delta->value("text", std::string{});
            block.text += chunk;
            text_ += chunk;
            if (options_.on_token && !chunk.empty()) {
                options_.on_token(chunk);
            }
        } else if (delta_type == "thinking_delta") {
            const std::string chunk = delta->value("thinking", std::string{});
            block.text += chunk;
            // Thinking goes ONLY to its own sink -- never into text_, which
            // becomes the returned content and the persisted history.
            if (options_.on_thinking && !chunk.empty()) {
                options_.on_thinking(chunk);
            }
        } else if (delta_type == "signature_delta") {
            block.signature += delta->value("signature", std::string{});
        } else if (delta_type == "input_json_delta") {
            block.partial_json += delta->value("partial_json", std::string{});
        }
        return true;
    }

    if (type == "content_block_stop") {
        const auto index = payload.value("index", std::int64_t{0});
        const auto it = blocks_.find(index);
        if (it != blocks_.end() && it->second.type == "tool_use") {
            harness::ToolCall call;
            call.id = it->second.tool_id;
            call.name = it->second.tool_name;
            call.arguments = it->second.partial_json.empty() ? "{}" : it->second.partial_json;
            tool_calls_.push_back(std::move(call));
        }
        return true;
    }

    if (type == "message_delta") {
        if (const auto delta = payload.find("delta"); delta != payload.end()) {
            if (const std::string stop = delta->value("stop_reason", std::string{});
                !stop.empty()) {
                finish_reason_ = anthropic::finish_reason_from_stop_reason(stop);
            }
        }
        if (const auto usage = payload.find("usage"); usage != payload.end()) {
            usage_.completion_tokens = usage->value("output_tokens", usage_.completion_tokens);
        }
        return true;
    }

    if (type == "message_stop") {
        saw_message_stop_ = true;
        return true;
    }

    // Unknown event types are ignored. A vendor adding one must not turn a
    // working stream into an outage.
    return true;
}

harness::ChatResponse StreamAccumulator::take_response() {
    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(text_);
    response.message.tool_calls = tool_calls_;
    response.finish_reason = finish_reason_;
    response.usage = usage_;
    response.model = model_;
    return response;
}

nlohmann::json StreamAccumulator::raw_blocks() const {
    nlohmann::json blocks = nlohmann::json::array();
    for (const auto& [index, block] : blocks_) {
        if (block.type == "thinking") {
            blocks.push_back(
                {{"type", "thinking"}, {"thinking", block.text}, {"signature", block.signature}});
        } else if (block.type == "redacted_thinking") {
            blocks.push_back({{"type", "redacted_thinking"}, {"data", block.signature}});
        } else if (block.type == "text") {
            blocks.push_back({{"type", "text"}, {"text", block.text}});
        } else if (block.type == "tool_use") {
            nlohmann::json input = nlohmann::json::parse(block.partial_json, nullptr, false);
            if (input.is_discarded()) {
                input = nlohmann::json::object();
            }
            blocks.push_back({{"type", "tool_use"},
                              {"id", block.tool_id},
                              {"name", block.tool_name},
                              {"input", std::move(input)}});
        }
    }
    return blocks;
}

}  // namespace

// ---------------------------------------------------------------------------
// AnthropicProvider
// ---------------------------------------------------------------------------

AnthropicProvider::AnthropicProvider(Options options, std::unique_ptr<HttpClient> client)
    : options_{std::move(options)}, client_{std::move(client)} {}

std::unique_ptr<AnthropicProvider> AnthropicProvider::create(Options options) {
    auto client = std::make_unique<HttpClient>(std::make_unique<CurlTransport>());
    return std::make_unique<AnthropicProvider>(std::move(options), std::move(client));
}

std::unique_ptr<AnthropicProvider> AnthropicProvider::from_config(
    const std::string& backend_name, const harness::BackendConfig& config) {
    Options options;
    options.backend_name = backend_name;
    options.api_key = config.api_key;  // already ${ENV}-expanded by the loader
    if (!config.model.empty()) {
        options.model = config.model;
    }
    if (config.max_tokens.has_value() && *config.max_tokens > 0) {
        options.max_tokens = *config.max_tokens;
    }

    if (options.api_key.empty()) {
        // Actionable, and it names the key rather than printing one.
        throw harness::ProviderError(
            backend_name,
            "no API key configured. Set api_key on this backend -- a "
            "\"${ANTHROPIC_API_KEY}\" reference is expanded when the config is read, so "
            "the key itself never has to be written to the file");
    }
    return std::make_unique<AnthropicProvider>(
        std::move(options), std::make_unique<HttpClient>(std::make_unique<CurlTransport>()));
}

std::string_view AnthropicProvider::backend_name() const noexcept {
    return options_.backend_name;
}

void AnthropicProvider::reset_conversation() {
    thinking_cache_.clear();
}

anthropic::RequestOptions AnthropicProvider::request_options(const harness::ChatRequest& request,
                                                             bool stream) const {
    anthropic::RequestOptions options;
    options.model = options_.model;
    options.max_tokens = request.max_tokens.value_or(options_.max_tokens);
    options.thinking_budget_tokens = options_.thinking_budget_tokens;
    options.web_search = options_.web_search;
    options.web_search_max_uses = options_.web_search_max_uses;
    options.stream = stream;
    return options;
}

HttpRequest AnthropicProvider::build_http_request(const nlohmann::json& body,
                                                  std::string_view path) const {
    HttpRequest request;
    request.method = "POST";
    request.url = options_.base_url + std::string{path};
    request.body = body.dump();
    request.headers = {
        {"content-type", "application/json"},
        {"x-api-key", options_.api_key},
        {"anthropic-version", options_.api_version},
    };
    // No overall timeout: a long generation is not a hung connection, and a
    // timeout that cannot tell them apart truncates real answers.
    request.timeout = std::chrono::seconds{0};
    return request;
}

void AnthropicProvider::fail(long status, std::string_view body) const {
    // Built from the response only. The request -- and therefore the API key --
    // is never echoed into an error message.
    throw harness::ProviderError(options_.backend_name, anthropic::error_message(status, body));
}

harness::ChatResponse AnthropicProvider::chat(const harness::ChatRequest& request,
                                              const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();

    const nlohmann::json body =
        anthropic::build_request(request, request_options(request, false), thinking_cache_);
    const HttpResponse response =
        client_->send(build_http_request(body, "/v1/messages"), {}, cancellation);

    if (!response.ok()) {
        fail(response.status, response.body);
    }

    const nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded()) {
        throw harness::ProviderError(options_.backend_name,
                                     "the API returned a response that was not valid JSON");
    }
    if (const auto content = parsed.find("content"); content != parsed.end()) {
        thinking_cache_.remember(*content);
    }
    return anthropic::parse_response(parsed);
}

harness::ChatResponse AnthropicProvider::stream_chat(const harness::ChatRequest& request,
                                                     const harness::StreamOptions& options) {
    options.cancellation.throw_if_cancelled();

    const nlohmann::json body =
        anthropic::build_request(request, request_options(request, true), thinking_cache_);

    StreamAccumulator accumulator{options, options_.model};
    SseParser parser{[&accumulator](const SseEvent& event) { return accumulator.handle(event); }};

    // The sink sees only a successful body: HttpTransport accumulates an error
    // response into `response.body` instead of streaming it, which is what
    // keeps a 429 retryable even on a streaming request.
    const BodySink sink = [&](std::string_view chunk) {
        options.cancellation.throw_if_cancelled();
        return parser.feed(chunk);
    };

    HttpRequest http_request = build_http_request(body, "/v1/messages");
    http_request.headers.push_back({"accept", "text/event-stream"});

    const HttpResponse response = client_->send(http_request, sink, options.cancellation);

    if (!response.ok()) {
        fail(response.status, response.body);
    }

    parser.finish();

    if (!accumulator.error().empty()) {
        throw harness::ProviderError(options_.backend_name, accumulator.error());
    }

    thinking_cache_.remember(accumulator.raw_blocks());
    return accumulator.take_response();
}

std::vector<harness::ModelInfo> AnthropicProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    // The configured model, not a live catalogue call. A backend entry pins one
    // model; listing the vendor's full catalogue here would report models this
    // entry cannot actually serve.
    return {harness::ModelInfo{options_.model, options_.model, "anthropic", options_.backend_name}};
}

std::int64_t AnthropicProvider::count_tokens(const harness::ChatRequest& request,
                                             const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();

    nlohmann::json body =
        anthropic::build_request(request, request_options(request, false), thinking_cache_);
    // count_tokens rejects these; it counts input only.
    body.erase("max_tokens");
    body.erase("stream");

    const HttpResponse response =
        client_->send(build_http_request(body, "/v1/messages/count_tokens"), {}, cancellation);
    if (!response.ok()) {
        fail(response.status, response.body);
    }

    const nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw harness::ProviderError(options_.backend_name,
                                     "count_tokens returned a response that was not valid JSON");
    }
    return parsed.value("input_tokens", std::int64_t{0});
}

}  // namespace apogee::backends
