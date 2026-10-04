#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "backends/anthropic.h"
#include "backends/google.h"
#include "backends/llamacpp.h"
#include "backends/openai.h"
#include "harness/harness.h"
#include "support/embedding_fixtures.h"
#include "support/fake_llama.h"
#include "support/fake_transport.h"

/// Each provider, asked through the Harness the only way callers may ask it:
/// routed, and probed for a capability. The cases came from the providers'
/// own suites (A4): a test of the Harness driving a backend spans Business and
/// Data, so it lives in the higher of the two (ADR 0004), and the backends'
/// suites stay in their layer.
namespace {

using apogee::backends::AnthropicProvider;
using apogee::backends::GoogleProvider;
using apogee::backends::HttpClient;
using apogee::backends::LlamaCppProvider;
using apogee::backends::OpenAIProvider;
using apogee::backends::RetryPolicy;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::testing::FakeLlamaRuntime;
using apogee::testing::FakeTransport;
using nlohmann::json;
namespace fixtures = apogee::testing::embeddings;

/// A provider over a scripted transport, as each provider's own suite builds it.
template <typename Provider>
struct Fixture {
    FakeTransport* transport = nullptr;
    std::unique_ptr<Provider> provider;
};

template <typename Provider>
Fixture<Provider> make_provider(std::vector<FakeTransport::Reply> replies, std::string api_key,
                                std::string base_url) {
    typename Provider::Options options;
    options.api_key = std::move(api_key);
    options.base_url = std::move(base_url);
    auto transport = std::make_unique<FakeTransport>(std::move(replies));
    Fixture<Provider> fixture;
    fixture.transport = transport.get();
    RetryPolicy policy;
    policy.max_attempts = 3;
    auto client = std::make_unique<HttpClient>(std::move(transport), policy);
    client->set_sleeper([](std::chrono::milliseconds) {});
    fixture.provider = std::make_unique<Provider>(std::move(options), std::move(client));
    return fixture;
}

/// A documented-shape OpenAI embeddings body: one 4-wide vector per input.
std::string openai_body_for(std::size_t count) {
    json data = json::array();
    for (std::size_t index = 0; index < count; ++index) {
        data.push_back({{"object", "embedding"},
                        {"index", index},
                        {"embedding", {static_cast<float>(index), 0.0F, 0.0F, 1.0F}}});
    }
    return json{{"object", "list"}, {"data", data}, {"model", "text-embedding-3-small"}}.dump();
}

/// A documented-shape Gemini embeddings body: one 3-wide vector per input.
std::string google_body_for(std::size_t count) {
    json rows = json::array();
    for (std::size_t index = 0; index < count; ++index) {
        rows.push_back({{"values", {static_cast<float>(index), 1.0F, 0.0F}}});
    }
    return json{{"embeddings", rows}}.dump();
}

ChatRequest chat_request() {
    ChatRequest request;
    request.messages = {ChatMessage::system("Be brief."), ChatMessage::user("Hello")};
    return request;
}

}  // namespace

TEST_CASE("the provider satisfies the LLMProvider interface via the harness",
          "[backends][anthropic]") {
    // It must be routable and usable through the Harness like any other
    // provider -- that is the whole point of the interface.
    auto f = make_provider<AnthropicProvider>(
        {FakeTransport::Reply{
            .status = 200,
            .body =
                R"({"model":"m","stop_reason":"end_turn","content":[{"type":"text","text":"hi"}]})"}},
        "sk-ant-test-key", "https://api.test");
    apogee::harness::Harness registry{apogee::harness::Config{}};
    registry.register_provider("claude", std::move(f.provider));
    registry.use_default_router();

    ChatRequest routed = chat_request();
    routed.model = "claude";
    const auto response = registry.chat(routed);
    CHECK_FALSE(response.message.content.plain_text().empty());
    CHECK_FALSE(registry.can_embed("claude"));
}

TEST_CASE("a Google entry answers can_embed through the harness",
          "[backends][google][embed][capability]") {
    auto f = make_provider<GoogleProvider>(
        {FakeTransport::Reply{.status = 200, .body = google_body_for(1)}}, "AIza-test-key",
        "https://gen.test");
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("gemini", std::shared_ptr<GoogleProvider>(std::move(f.provider)));
    harness.use_default_router();
    CHECK(harness.can_embed("gemini"));
}

TEST_CASE("an OpenAI entry answers can_embed through the harness",
          "[backends][openai][embed][capability]") {
    // The per-provider capability, asked the only way callers may ask it.
    auto f = make_provider<OpenAIProvider>(
        {FakeTransport::Reply{.status = 200, .body = openai_body_for(1)}}, "sk-test-key",
        "https://api.test");
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("gpt", std::shared_ptr<OpenAIProvider>(std::move(f.provider)));
    harness.use_default_router();
    CHECK(harness.can_embed("gpt"));
    REQUIRE(harness.embedder_for("gpt") != nullptr);
    CHECK(harness.embedder_for("gpt")->embedding_dimensions() == fixtures::kOpenAiDefaultWidth);
}

TEST_CASE("a local entry answers can_embed through the harness",
          "[backends][llamacpp][embed][capability]") {
    auto owned = std::make_unique<FakeLlamaRuntime>();
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model = "embedder";
    options.model_path = "/models/embedder.gguf";
    auto provider = std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned));
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("local", std::shared_ptr<LlamaCppProvider>(std::move(provider)));
    harness.use_default_router();
    CHECK(harness.can_embed("local"));
}
