#include "backends/openai_embed.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "backends/openai.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "support/embedding_fixtures.h"
#include "support/fake_transport.h"

/// OpenAI embeddings: the wire translator on its own, then the provider over a
/// scripted transport. No network and no key anywhere here.
namespace {

using apogee::backends::HttpClient;
using apogee::backends::OpenAIProvider;
using apogee::backends::RetryPolicy;
using apogee::harness::ProviderError;
using apogee::testing::FakeTransport;
using nlohmann::json;
namespace embed = apogee::backends::openai_embed;
namespace fixtures = apogee::testing::embeddings;

struct Fixture {
    FakeTransport* transport = nullptr;
    std::unique_ptr<OpenAIProvider> provider;
};

Fixture make_provider(std::vector<FakeTransport::Reply> replies,
                      OpenAIProvider::Options options = {}) {
    if (options.api_key.empty()) {
        options.api_key = "sk-test-key";
    }
    options.base_url = "https://api.test";
    auto transport = std::make_unique<FakeTransport>(std::move(replies));
    Fixture fixture;
    fixture.transport = transport.get();
    RetryPolicy policy;
    policy.max_attempts = 3;
    auto client = std::make_unique<HttpClient>(std::move(transport), policy);
    client->set_sleeper([](std::chrono::milliseconds) {});
    fixture.provider = std::make_unique<OpenAIProvider>(std::move(options), std::move(client));
    return fixture;
}

/// A documented-shape body with one 4-wide vector per input, in order.
std::string body_for(std::size_t count) {
    json data = json::array();
    for (std::size_t index = 0; index < count; ++index) {
        data.push_back({{"object", "embedding"},
                        {"index", index},
                        {"embedding", {static_cast<float>(index), 0.0F, 0.0F, 1.0F}}});
    }
    return json{{"object", "list"}, {"data", data}, {"model", "text-embedding-3-small"}}.dump();
}

}  // namespace

// --- the translator --------------------------------------------------------------

TEST_CASE("the OpenAI request carries the model, every input, and float encoding",
          "[backends][openai][embed][wire]") {
    const json body = embed::build_request("text-embedding-3-small", {"a", "b"});
    CHECK(body.at("model") == "text-embedding-3-small");
    CHECK(body.at("input") == json::array({"a", "b"}));
    // Stated, not assumed: the alternative is base64, and a float parser fed
    // base64 reads garbage rather than failing.
    CHECK(body.at("encoding_format") == "float");
}

TEST_CASE("OpenAI rows are placed by index, never by position", "[backends][openai][embed][wire]") {
    // The fixture's rows arrive 1 then 0. A parser that trusts position hands
    // input 0 the vector for input 1 -- silently, and every search afterwards
    // is wrong in a way nobody can see.
    const auto vectors = embed::parse_response(json::parse(fixtures::kOpenAiTwoRowsOutOfOrder), 2);
    REQUIRE(vectors.size() == 2);
    CHECK(vectors[0] == std::vector<float>{1.0F, 0.0F, 0.0F, 0.0F});
    CHECK(vectors[1] == std::vector<float>{0.5F, 0.5F, 0.5F, 0.5F});
}

TEST_CASE("an OpenAI response that does not match the request is refused",
          "[backends][openai][embed][wire]") {
    CHECK_THROWS_WITH(embed::parse_response(json::parse(fixtures::kOpenAiTwoRowsOutOfOrder), 3),
                      Catch::Matchers::ContainsSubstring("2 vector(s) for 3 input(s)"));
    CHECK_THROWS_WITH(embed::parse_response(json::parse(R"({"object":"list"})"), 1),
                      Catch::Matchers::ContainsSubstring("no 'data' array"));
    // A duplicate index would leave another row unfilled.
    CHECK_THROWS(embed::parse_response(
        json::parse(R"({"data":[{"index":0,"embedding":[1]},{"index":0,"embedding":[2]}]})"), 2));
}

TEST_CASE("OpenAI dimensions are known for the documented models", "[backends][openai][embed]") {
    CHECK(embed::known_dimensions("text-embedding-3-small") == fixtures::kOpenAiDefaultWidth);
    CHECK(embed::known_dimensions("text-embedding-3-large") == 3072);
    CHECK(embed::known_dimensions("some-future-model") == 0);
    CHECK(embed::default_model() == "text-embedding-3-small");
}

// --- the provider ----------------------------------------------------------------

TEST_CASE("the provider embeds a batch over the embeddings endpoint with the key in a header",
          "[backends][openai][embed]") {
    Fixture f = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(2)}});
    const auto vectors = f.provider->embed({"alpha", "beta"}, {});
    REQUIRE(vectors.size() == 2);
    CHECK(vectors[1][0] == 1.0F);

    REQUIRE(f.transport->requests().size() == 1);
    const auto& request = f.transport->requests()[0];
    CHECK(request.url == "https://api.test/v1/embeddings");
    CHECK(json::parse(request.body).at("model") == "text-embedding-3-small");
    bool bearer = false;
    for (const auto& header : request.headers) {
        if (header.name == "authorization") {
            bearer = header.value == "Bearer sk-test-key";
        }
    }
    CHECK(bearer);
    // The key is in the header and nowhere else -- not the URL, not the body.
    CHECK(request.url.find("sk-test") == std::string::npos);
    CHECK(request.body.find("sk-test") == std::string::npos);
}

TEST_CASE("more inputs than the maximum become more requests, split at the boundary",
          "[backends][openai][embed][batch]") {
    // The batch-splitting boundary test the item asks for: 2049 inputs is two
    // requests of 2048 and 1, in order, and the vectors come back as one list.
    constexpr std::size_t kMax = embed::kMaxInputsPerRequest;
    Fixture f = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(kMax)},
                               FakeTransport::Reply{.status = 200, .body = body_for(1)}});
    std::vector<std::string> inputs(kMax + 1, "x");
    const auto vectors = f.provider->embed(inputs, {});
    CHECK(vectors.size() == kMax + 1);

    REQUIRE(f.transport->requests().size() == 2);
    CHECK(json::parse(f.transport->requests()[0].body).at("input").size() == kMax);
    CHECK(json::parse(f.transport->requests()[1].body).at("input").size() == 1);
}

TEST_CASE("a rate limit is retried like the chat path", "[backends][openai][embed][retry]") {
    Fixture f = make_provider({FakeTransport::Reply{.status = 429, .body = R"({"error":{}})"},
                               FakeTransport::Reply{.status = 200, .body = body_for(1)}});
    const auto vectors = f.provider->embed({"alpha"}, {});
    REQUIRE(vectors.size() == 1);
    CHECK(f.transport->attempts() == 2);
}

TEST_CASE("an embeddings error names the problem and never the key",
          "[backends][openai][embed][error]") {
    Fixture f = make_provider({FakeTransport::Reply{
        .status = 401,
        .body =
            R"({"error":{"message":"Incorrect API key provided","type":"invalid_request_error"}})"}});
    try {
        (void)f.provider->embed({"alpha"}, {});
        FAIL("expected a ProviderError");
    } catch (const ProviderError& e) {
        const std::string what = e.what();
        CHECK(what.find("Incorrect API key") != std::string::npos);
        CHECK(what.find("sk-test") == std::string::npos);
    }
}

TEST_CASE("the embedding model is the entry's, else the vendor default",
          "[backends][openai][embed]") {
    OpenAIProvider::Options options;
    options.embedding_model = "text-embedding-3-large";
    Fixture f = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(1)}},
                              std::move(options));
    CHECK(f.provider->embedding_dimensions() == 3072);
    (void)f.provider->embed({"alpha"}, {});
    CHECK(json::parse(f.transport->requests()[0].body).at("model") == "text-embedding-3-large");

    Fixture d = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(1)}});
    CHECK(d.provider->embedding_model() == "text-embedding-3-small");
    CHECK(d.provider->embedding_dimensions() == fixtures::kOpenAiDefaultWidth);
}

TEST_CASE("an unknown model's width is 0 until the first vector arrives",
          "[backends][openai][embed]") {
    // The interface's "known after the first call", made true rather than
    // guessed at.
    OpenAIProvider::Options options;
    options.embedding_model = "text-embedding-9-hypothetical";
    Fixture f = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(1)}},
                              std::move(options));
    CHECK(f.provider->embedding_dimensions() == 0);
    (void)f.provider->embed({"alpha"}, {});
    CHECK(f.provider->embedding_dimensions() == 4);
}

TEST_CASE("an OpenAI entry answers can_embed through the harness",
          "[backends][openai][embed][capability]") {
    // The per-provider capability, asked the only way callers may ask it.
    Fixture f = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(1)}});
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("gpt", std::shared_ptr<OpenAIProvider>(std::move(f.provider)));
    harness.use_default_router();
    CHECK(harness.can_embed("gpt"));
    REQUIRE(harness.embedder_for("gpt") != nullptr);
    CHECK(harness.embedder_for("gpt")->embedding_dimensions() == fixtures::kOpenAiDefaultWidth);
}
