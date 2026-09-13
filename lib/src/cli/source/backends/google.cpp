#include "backends/google.h"

#include <utility>

#include "backends/embedding_batch.h"
#include "backends/google_embed.h"
#include "backends/sse_parser.h"
#include "harness/errors.h"

namespace apogee::backends {
namespace {

/// Accumulates a Gemini stream.
///
/// Gemini does not send typed deltas: every SSE frame is a whole
/// `GenerateContentResponse` carrying the *increment* in its parts. So each
/// chunk is parsed with the same translator the non-streaming path uses, and
/// the pieces are appended -- which is why the wire layer exposes
/// `parse_response` rather than a delta-specific entry point.
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
    const harness::StreamOptions& options_;
    std::string model_;
    std::string text_;
    std::vector<harness::ToolCall> calls_;
    harness::FinishReason finish_reason_ = harness::FinishReason::Stop;
    harness::Usage usage_;
    std::string error_;
};

bool StreamAccumulator::handle(const SseEvent& event) {
    const nlohmann::json payload = nlohmann::json::parse(event.data, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        return true;
    }
    if (payload.contains("error")) {
        error_ = google::error_message(0, event.data);
        return false;
    }

    std::string thinking;
    const harness::ChatResponse chunk = google::parse_response(payload, &thinking);

    if (!thinking.empty() && options_.on_thinking) {
        // Its own channel. A `thought: true` part must never reach the answer.
        options_.on_thinking(thinking);
    }

    const std::string chunk_text = chunk.message.content.plain_text();
    if (!chunk_text.empty()) {
        text_ += chunk_text;
        if (options_.on_token) {
            options_.on_token(chunk_text);
        }
    }

    for (const harness::ToolCall& call : chunk.message.tool_calls) {
        calls_.push_back(call);
    }
    if (chunk.usage.reported()) {
        // Later frames restate the running totals rather than adding to them.
        usage_ = chunk.usage;
    }
    if (!chunk.model.empty()) {
        model_ = chunk.model;
    }
    if (chunk.finish_reason != harness::FinishReason::Stop) {
        finish_reason_ = chunk.finish_reason;
    }
    return true;
}

harness::ChatResponse StreamAccumulator::take_response() {
    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(text_);
    response.message.tool_calls = calls_;
    response.finish_reason = calls_.empty() ? finish_reason_ : harness::FinishReason::ToolCalls;
    response.usage = usage_;
    response.model = model_;
    return response;
}

}  // namespace

GoogleProvider::GoogleProvider(Options options, std::unique_ptr<HttpClient> client)
    : options_{std::move(options)}, client_{std::move(client)} {}

std::unique_ptr<GoogleProvider> GoogleProvider::from_config(const std::string& backend_name,
                                                            const harness::BackendConfig& config,
                                                            bool web_search) {
    Options options;
    options.backend_name = backend_name;
    options.api_key = config.api_key;
    options.web_search = web_search;
    if (!config.model.empty()) {
        options.model = config.model;
    }
    options.embedding_model = config.embedding_model;
    if (config.max_tokens.has_value() && *config.max_tokens > 0) {
        options.max_tokens = *config.max_tokens;
    }
    if (options.api_key.empty()) {
        throw harness::ProviderError(
            backend_name,
            "no API key configured. Set api_key on this backend -- a \"${GEMINI_API_KEY}\" "
            "reference is expanded when the config is read, so the key itself never has to be "
            "written to the file");
    }
    return std::make_unique<GoogleProvider>(
        std::move(options), std::make_unique<HttpClient>(std::make_unique<CurlTransport>()));
}

std::string_view GoogleProvider::backend_name() const noexcept {
    return options_.backend_name;
}

google::RequestOptions GoogleProvider::request_options(const harness::ChatRequest& request) const {
    google::RequestOptions options;
    options.model = options_.model;
    options.max_output_tokens = request.max_tokens.value_or(options_.max_tokens);
    options.thinking_budget_tokens = options_.thinking_budget_tokens;
    options.web_search = options_.web_search;
    return options;
}

HttpRequest GoogleProvider::build_http_request(const nlohmann::json& body, bool stream) const {
    HttpRequest request;
    request.method = "POST";
    // The model is in the PATH, not the body -- the shape difference that makes
    // a Gemini URL look unlike every other provider's.
    request.url = options_.base_url + "/" + options_.api_version + "/models/" + options_.model +
                  (stream ? ":streamGenerateContent?alt=sse" : ":generateContent");
    request.body = body.dump();
    // The key rides a header, not the query string: a URL is logged by every
    // proxy and appears in shell history.
    request.headers = {{"content-type", "application/json"}, {"x-goog-api-key", options_.api_key}};
    request.timeout = std::chrono::seconds{0};
    return request;
}

HttpRequest GoogleProvider::build_embed_request(const nlohmann::json& body) const {
    HttpRequest request;
    request.method = "POST";
    // The embedding model in the path -- not the chat model this entry answers
    // with -- and repeated inside every row by the translator.
    request.url = options_.base_url + "/" + options_.api_version + "/models/" +
                  std::string{embedding_model()} + ":batchEmbedContents";
    request.body = body.dump();
    request.headers = {{"content-type", "application/json"}, {"x-goog-api-key", options_.api_key}};
    request.timeout = std::chrono::seconds{120};
    return request;
}

std::string_view GoogleProvider::embedding_model() const noexcept {
    return options_.embedding_model.empty() ? google_embed::default_model()
                                            : std::string_view{options_.embedding_model};
}

std::size_t GoogleProvider::embedding_dimensions() const noexcept {
    const std::size_t known = google_embed::known_dimensions(embedding_model());
    return known != 0 ? known : observed_dimensions_;
}

std::vector<std::vector<float>> GoogleProvider::embed(
    const std::vector<std::string>& inputs, const harness::CancellationToken& cancellation) {
    std::vector<std::vector<float>> vectors;
    vectors.reserve(inputs.size());

    for (const auto& [begin, end] :
         batch_ranges(inputs.size(), google_embed::kMaxInputsPerRequest)) {
        cancellation.throw_if_cancelled();
        const std::vector<std::string> batch(inputs.begin() + static_cast<std::ptrdiff_t>(begin),
                                             inputs.begin() + static_cast<std::ptrdiff_t>(end));

        const nlohmann::json body = google_embed::build_request(embedding_model(), batch);
        const HttpResponse response = client_->send(build_embed_request(body), {}, cancellation);
        if (!response.ok()) {
            fail(response.status, response.body);
        }

        const nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
        if (parsed.is_discarded()) {
            throw harness::ProviderError(options_.backend_name,
                                         "the API returned a response that was not valid JSON");
        }
        try {
            std::vector<std::vector<float>> got =
                google_embed::parse_response(parsed, batch.size());
            for (std::vector<float>& vector : got) {
                vectors.push_back(std::move(vector));
            }
        } catch (const std::runtime_error& e) {
            throw harness::ProviderError(options_.backend_name, e.what());
        }
    }

    if (observed_dimensions_ == 0 && !vectors.empty()) {
        observed_dimensions_ = vectors.front().size();
    }
    return vectors;
}

void GoogleProvider::fail(long status, std::string_view body) const {
    throw harness::ProviderError(options_.backend_name, google::error_message(status, body));
}

harness::ChatResponse GoogleProvider::chat(const harness::ChatRequest& request,
                                           const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();

    const nlohmann::json body = google::build_request(request, request_options(request));
    const HttpResponse response = client_->send(build_http_request(body, false), {}, cancellation);
    if (!response.ok()) {
        fail(response.status, response.body);
    }

    const nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded()) {
        throw harness::ProviderError(options_.backend_name,
                                     "the API returned a response that was not valid JSON");
    }
    return google::parse_response(parsed);
}

harness::ChatResponse GoogleProvider::stream_chat(const harness::ChatRequest& request,
                                                  const harness::StreamOptions& options) {
    options.cancellation.throw_if_cancelled();

    const nlohmann::json body = google::build_request(request, request_options(request));

    StreamAccumulator accumulator{options, options_.model};
    SseParser parser{[&accumulator](const SseEvent& event) { return accumulator.handle(event); }};

    const BodySink sink = [&](std::string_view chunk) {
        options.cancellation.throw_if_cancelled();
        return parser.feed(chunk);
    };

    HttpRequest http_request = build_http_request(body, true);
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

std::vector<harness::ModelInfo> GoogleProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    return {harness::ModelInfo{options_.model, options_.model, "google", options_.backend_name}};
}

}  // namespace apogee::backends
