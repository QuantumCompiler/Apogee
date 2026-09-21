#include "backends/google_embed.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "backends/google.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "support/embedding_fixtures.h"
#include "support/fake_transport.h"

/// Gemini embeddings: the translator, then the provider over a scripted
/// transport.
namespace {

using apogee::backends::GoogleProvider;
using apogee::backends::HttpClient;
using apogee::backends::RetryPolicy;
using apogee::harness::ProviderError;
using apogee::testing::FakeTransport;
using nlohmann::json;
namespace embed = apogee::backends::google_embed;
namespace fixtures = apogee::testing::embeddings;

struct Fixture {
    FakeTransport* transport = nullptr;
    std::unique_ptr<GoogleProvider> provider;
};

Fixture make_provider(std::vector<FakeTransport::Reply> replies,
                      GoogleProvider::Options options = {}) {
    if (options.api_key.empty()) {
        options.api_key = "AIza-test-key";
    }
    options.base_url = "https://gen.test";
    auto transport = std::make_unique<FakeTransport>(std::move(replies));
    Fixture fixture;
    fixture.transport = transport.get();
    RetryPolicy policy;
    policy.max_attempts = 3;
    auto client = std::make_unique<HttpClient>(std::move(transport), policy);
    client->set_sleeper([](std::chrono::milliseconds) {});
    fixture.provider = std::make_unique<GoogleProvider>(std::move(options), std::move(client));
    return fixture;
}

std::string body_for(std::size_t count) {
    json rows = json::array();
    for (std::size_t index = 0; index < count; ++index) {
        rows.push_back({{"values", {static_cast<float>(index), 1.0F, 0.0F}}});
    }
    return json{{"embeddings", rows}}.dump();
}

}  // namespace

// --- the translator --------------------------------------------------------------

TEST_CASE("a Gemini request repeats the prefixed model in every row",
          "[backends][google][embed][wire]") {
    // The shape difference that fails a whole batch when missed: the model is
    // in the URL AND in each row, and in the row it wants the `models/` prefix.
    const json body = embed::build_request("gemini-embedding-001", {"a", "b"});
    const json& requests = body.at("requests");
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].at("model") == "models/gemini-embedding-001");
    CHECK(requests[0].at("content").at("parts")[0].at("text") == "a");
    CHECK(requests[1].at("content").at("parts")[0].at("text") == "b");
}

TEST_CASE("Gemini rows are taken in order", "[backends][google][embed][wire]") {
    const auto vectors = embed::parse_response(json::parse(fixtures::kGoogleTwoRows), 2);
    REQUIRE(vectors.size() == 2);
    CHECK(vectors[0] == std::vector<float>{1.0F, 0.0F, 0.0F});
    CHECK(vectors[1] == std::vector<float>{0.0F, 1.0F, 0.0F});
}

TEST_CASE("a Gemini response that does not match the request is refused",
          "[backends][google][embed][wire]") {
    CHECK_THROWS_WITH(embed::parse_response(json::parse(fixtures::kGoogleTwoRows), 1),
                      Catch::Matchers::ContainsSubstring("2 vector(s) for 1 input(s)"));
    CHECK_THROWS_WITH(embed::parse_response(json::parse(R"({})"), 1),
                      Catch::Matchers::ContainsSubstring("no 'embeddings' array"));
}

TEST_CASE("Gemini dimensions are known for the documented model", "[backends][google][embed]") {
    CHECK(embed::known_dimensions("gemini-embedding-001") == fixtures::kGoogleDefaultWidth);
    CHECK(embed::known_dimensions("unknown") == 0);
    CHECK(embed::default_model() == "gemini-embedding-001");
}

// --- the provider ----------------------------------------------------------------

TEST_CASE("the provider embeds over batchEmbedContents with the key in a header",
          "[backends][google][embed]") {
    Fixture f = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(2)}});
    const auto vectors = f.provider->embed({"alpha", "beta"}, {});
    REQUIRE(vectors.size() == 2);

    REQUIRE(f.transport->requests().size() == 1);
    const auto& request = f.transport->requests()[0];
    // The EMBEDDING model in the path, not the chat model this entry answers with.
    CHECK(request.url == "https://gen.test/v1beta/models/gemini-embedding-001:batchEmbedContents");
    bool keyed = false;
    for (const auto& header : request.headers) {
        if (header.name == "x-goog-api-key") {
            keyed = header.value == "AIza-test-key";
        }
    }
    CHECK(keyed);
    // Never in the URL: a URL is logged by every proxy and lands in history.
    CHECK(request.url.find("AIza") == std::string::npos);
}

TEST_CASE("more than a hundred inputs become more requests, split at the boundary",
          "[backends][google][embed][batch]") {
    constexpr std::size_t kMax = embed::kMaxInputsPerRequest;
    Fixture f = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(kMax)},
                               FakeTransport::Reply{.status = 200, .body = body_for(1)}});
    std::vector<std::string> inputs(kMax + 1, "x");
    const auto vectors = f.provider->embed(inputs, {});
    CHECK(vectors.size() == kMax + 1);
    REQUIRE(f.transport->requests().size() == 2);
    CHECK(json::parse(f.transport->requests()[0].body).at("requests").size() == kMax);
    CHECK(json::parse(f.transport->requests()[1].body).at("requests").size() == 1);
}

TEST_CASE("a Gemini rate limit is retried", "[backends][google][embed][retry]") {
    Fixture f = make_provider({FakeTransport::Reply{.status = 429, .body = "{}"},
                               FakeTransport::Reply{.status = 200, .body = body_for(1)}});
    CHECK(f.provider->embed({"alpha"}, {}).size() == 1);
    CHECK(f.transport->attempts() == 2);
}

TEST_CASE("a Gemini embeddings error never echoes the key", "[backends][google][embed][error]") {
    Fixture f = make_provider({FakeTransport::Reply{
        .status = 400,
        .body = R"({"error":{"message":"API key not valid","status":"INVALID_ARGUMENT"}})"}});
    try {
        (void)f.provider->embed({"alpha"}, {});
        FAIL("expected a ProviderError");
    } catch (const ProviderError& e) {
        const std::string what = e.what();
        CHECK(what.find("API key not valid") != std::string::npos);
        CHECK(what.find("AIza") == std::string::npos);
    }
}

TEST_CASE("the Gemini embedding model is the entry's, else the vendor default",
          "[backends][google][embed]") {
    GoogleProvider::Options options;
    options.embedding_model = "text-embedding-005";
    Fixture f = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(1)}},
                              std::move(options));
    CHECK(f.provider->embedding_dimensions() == 0);  // unknown here until seen
    (void)f.provider->embed({"alpha"}, {});
    CHECK(f.transport->requests()[0].url.find("/models/text-embedding-005:") != std::string::npos);
    CHECK(f.provider->embedding_dimensions() == 3);

    Fixture d = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(1)}});
    CHECK(d.provider->embedding_dimensions() == fixtures::kGoogleDefaultWidth);
}

TEST_CASE("a Google entry answers can_embed through the harness",
          "[backends][google][embed][capability]") {
    Fixture f = make_provider({FakeTransport::Reply{.status = 200, .body = body_for(1)}});
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("gemini", std::shared_ptr<GoogleProvider>(std::move(f.provider)));
    harness.use_default_router();
    CHECK(harness.can_embed("gemini"));
}
