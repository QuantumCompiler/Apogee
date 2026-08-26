#include "backends/mock.h"

#include <functional>
#include <utility>

#include "harness/errors.h"

namespace apogee::backends {
namespace {

const MockTurn& default_turn() {
    static const MockTurn turn{"mock response", {}, harness::FinishReason::Stop, {}};
    return turn;
}

}  // namespace

// ---------------------------------------------------------------------------
// MockProvider
// ---------------------------------------------------------------------------

MockProvider::MockProvider(Options options) : options_{std::move(options)} {
    if (options_.chunk_size == 0) {
        options_.chunk_size = 1;
    }
}

std::string_view MockProvider::backend_name() const noexcept {
    return options_.backend_name;
}

void MockProvider::record(const harness::ChatRequest& request) {
    requests_.push_back(request);
    if (options_.on_request) {
        options_.on_request(request);
    }
}

const MockTurn& MockProvider::next_turn() {
    if (options_.turns.empty()) {
        ++turn_index_;
        return default_turn();
    }
    // Clamp rather than wrap or throw: a test running one turn past its script
    // is nearly always testing something else, and "script exhausted" would
    // send the reader hunting in the wrong place.
    const std::size_t index = std::min(turn_index_, options_.turns.size() - 1);
    ++turn_index_;
    return options_.turns[index];
}

harness::ChatResponse MockProvider::build_response(const MockTurn& turn) const {
    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(turn.text);
    response.message.tool_calls = turn.tool_calls;
    response.finish_reason = turn.finish_reason;
    response.usage = turn.usage;
    response.model = options_.model;
    return response;
}

harness::ChatResponse MockProvider::chat(const harness::ChatRequest& request,
                                         const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    record(request);
    return build_response(next_turn());
}

harness::ChatResponse MockProvider::stream_chat(const harness::ChatRequest& request,
                                                const harness::StreamOptions& options) {
    options.cancellation.throw_if_cancelled();
    record(request);
    const MockTurn& turn = next_turn();

    if (options.on_status) {
        harness::StatusEvent event;
        event.type = harness::StatusEvent::Type::Thinking;
        event.phase = harness::StatusEvent::Phase::Start;
        event.name = options_.model;
        options.on_status(event);
    }

    // Chunked deliberately, and checked for cancellation between chunks: this
    // is the contract every real provider must honour, so the mock has to hold
    // itself to it or tests of cancellation prove nothing.
    const std::string& text = turn.text;
    for (std::size_t offset = 0; offset < text.size(); offset += options_.chunk_size) {
        options.cancellation.throw_if_cancelled();
        if (options.on_token) {
            options.on_token(std::string_view{text}.substr(
                offset, std::min(options_.chunk_size, text.size() - offset)));
        }
    }
    options.cancellation.throw_if_cancelled();

    if (options.on_status) {
        harness::StatusEvent event;
        event.type = harness::StatusEvent::Type::Thinking;
        event.phase = harness::StatusEvent::Phase::Done;
        event.name = options_.model;
        options.on_status(event);
    }

    return build_response(turn);
}

std::vector<harness::ModelInfo> MockProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    return {harness::ModelInfo{options_.model, options_.model, "mock", options_.backend_name}};
}

harness::ModelBehavior MockProvider::model_behavior() const {
    return options_.behavior;
}

// ---------------------------------------------------------------------------
// MockEmbeddingProvider
// ---------------------------------------------------------------------------

MockEmbeddingProvider::MockEmbeddingProvider(std::string backend_name, std::size_t dimensions)
    : backend_name_{std::move(backend_name)}, dimensions_{dimensions == 0 ? 1 : dimensions} {}

std::string_view MockEmbeddingProvider::backend_name() const noexcept {
    return backend_name_;
}

harness::ChatResponse MockEmbeddingProvider::chat(const harness::ChatRequest& request,
                                                  const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    (void)request;
    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant("mock embedding backend");
    response.model = backend_name_;
    return response;
}

harness::ChatResponse MockEmbeddingProvider::stream_chat(const harness::ChatRequest& request,
                                                         const harness::StreamOptions& options) {
    options.cancellation.throw_if_cancelled();
    harness::ChatResponse response = chat(request, options.cancellation);
    if (options.on_token) {
        options.on_token(response.message.content.plain_text());
    }
    return response;
}

std::vector<harness::ModelInfo> MockEmbeddingProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    return {harness::ModelInfo{backend_name_, backend_name_, "mock", backend_name_}};
}

std::vector<std::vector<float>> MockEmbeddingProvider::embed(
    const std::vector<std::string>& inputs, const harness::CancellationToken& cancellation) {
    std::vector<std::vector<float>> vectors;
    vectors.reserve(inputs.size());
    for (const std::string& input : inputs) {
        cancellation.throw_if_cancelled();
        // Deterministic from the text: identical inputs embed identically and
        // different inputs almost certainly do not, which is all a test needs
        // to assert without pinning magic numbers.
        const std::size_t seed = std::hash<std::string>{}(input);
        std::vector<float> vector(dimensions_);
        for (std::size_t i = 0; i < dimensions_; ++i) {
            const auto component = static_cast<float>((seed >> (i % 32U)) & 0xFFU);
            vector[i] = component / 255.0F;
        }
        vectors.push_back(std::move(vector));
    }
    return vectors;
}

std::size_t MockEmbeddingProvider::embedding_dimensions() const noexcept {
    return dimensions_;
}

}  // namespace apogee::backends
