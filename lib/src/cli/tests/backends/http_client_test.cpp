#include "backends/http_client.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "harness/errors.h"
#include "support/fake_transport.h"

using apogee::backends::HttpClient;
using apogee::backends::HttpError;
using apogee::backends::HttpRequest;
using apogee::backends::HttpResponse;
using apogee::backends::RetryPolicy;
using apogee::harness::CancellationToken;
using apogee::testing::FakeTransport;
using apogee::testing::SleepRecorder;

namespace {

HttpRequest request_for(std::string url = "https://example.test/v1/messages") {
    HttpRequest request;
    request.url = std::move(url);
    request.body = R"({"hello":"world"})";
    return request;
}

}  // namespace

TEST_CASE("a 2xx response is returned without retrying", "[backends][http]") {
    auto transport = FakeTransport::ok(R"({"ok":true})");
    auto* raw = transport.get();
    HttpClient client{std::move(transport)};

    const HttpResponse response = client.send(request_for(), {}, {});

    CHECK(response.status == 200);
    CHECK(response.ok());
    CHECK(response.body == R"({"ok":true})");
    CHECK(raw->attempts() == 1);
}

TEST_CASE("a 429 is retried and the eventual success is returned", "[backends][http][retry]") {
    auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
        {429, "rate limited", 0, false, "", std::nullopt, std::nullopt},
        {429, "rate limited", 0, false, "", std::nullopt, std::nullopt},
        {200, R"({"ok":true})", 0, false, "", std::nullopt, std::nullopt},
    });
    auto* raw = transport.get();

    HttpClient client{std::move(transport)};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    const HttpResponse response = client.send(request_for(), {}, {});

    CHECK(response.status == 200);
    CHECK(raw->attempts() == 3);
    // Exponential: 500ms then 1s.
    REQUIRE(recorder.delays.size() == 2);
    CHECK(recorder.delays[0] == std::chrono::milliseconds{500});
    CHECK(recorder.delays[1] == std::chrono::milliseconds{1000});
}

TEST_CASE("5xx is retried and 4xx is not", "[backends][http][retry]") {
    // A 400 will fail identically next time; retrying it just wastes the
    // user's time and the server's.
    struct Case {
        long status;
        std::size_t expected_attempts;
    };

    const std::vector<Case> cases{{500, 4}, {502, 4}, {503, 4}, {529, 4},
                                  {400, 1}, {401, 1}, {404, 1}, {422, 1}};

    for (const Case& c : cases) {
        INFO("status " << c.status);
        auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
            {c.status, "body", 0, false, "", std::nullopt, std::nullopt}});
        auto* raw = transport.get();

        HttpClient client{std::move(transport)};
        SleepRecorder recorder;
        client.set_sleeper(recorder.sleeper());

        const HttpResponse response = client.send(request_for(), {}, {});
        CHECK(response.status == c.status);
        CHECK(raw->attempts() == c.expected_attempts);
    }
}

TEST_CASE("backoff is capped by max_delay", "[backends][http][retry]") {
    RetryPolicy policy;
    policy.max_attempts = 6;
    policy.base_delay = std::chrono::milliseconds{1000};
    policy.max_delay = std::chrono::milliseconds{2000};

    auto transport = std::make_unique<FakeTransport>(
        std::vector<FakeTransport::Reply>{{503, "down", 0, false, "", std::nullopt, std::nullopt}});
    HttpClient client{std::move(transport), policy};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    (void)client.send(request_for(), {}, {});

    REQUIRE(recorder.delays.size() == 5);
    CHECK(recorder.delays[0] == std::chrono::milliseconds{1000});
    CHECK(recorder.delays[1] == std::chrono::milliseconds{2000});
    for (const auto delay : recorder.delays) {
        CHECK(delay <= policy.max_delay);
    }
}

TEST_CASE("a retry-after header overrides the backoff curve", "[backends][http][retry]") {
    // A server that says when to come back knows better than the curve does:
    // returning early spends an attempt on a request that will be refused.
    auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
        {429, "slow down", 0, false, "", std::chrono::milliseconds{3000}, std::nullopt},
        {200, "ok", 0, false, "", std::nullopt, std::nullopt},
    });
    HttpClient client{std::move(transport)};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    (void)client.send(request_for(), {}, {});

    REQUIRE(recorder.delays.size() == 1);
    CHECK(recorder.delays[0] == std::chrono::milliseconds{3000});
}

TEST_CASE("retry-after is still capped by max_delay", "[backends][http][retry]") {
    // Otherwise a server sending `retry-after: 3600` hangs the CLI for an hour.
    RetryPolicy policy;
    policy.max_delay = std::chrono::milliseconds{5000};
    auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
        {429, "", 0, false, "", std::chrono::milliseconds{3600000}, std::nullopt},
        {200, "ok", 0, false, "", std::nullopt, std::nullopt},
    });
    HttpClient client{std::move(transport), policy};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    (void)client.send(request_for(), {}, {});
    REQUIRE(recorder.delays.size() == 1);
    CHECK(recorder.delays[0] == std::chrono::milliseconds{5000});
}

TEST_CASE("parse_retry_after handles the delta-seconds form only", "[backends][http]") {
    using apogee::backends::parse_retry_after;
    CHECK(parse_retry_after("3") == std::chrono::milliseconds{3000});
    CHECK(parse_retry_after("  10") == std::chrono::milliseconds{10000});
    CHECK(parse_retry_after("0") == std::chrono::milliseconds{0});
    // The HTTP-date form falls through to the backoff curve.
    CHECK_FALSE(parse_retry_after("Wed, 21 Oct 2026 07:28:00 GMT").has_value());
    CHECK_FALSE(parse_retry_after("").has_value());
    CHECK_FALSE(parse_retry_after("-5").has_value());
}

TEST_CASE("a transport failure is retried, then rethrown", "[backends][http][retry]") {
    auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
        {0, "", 0, true, "connection reset", std::nullopt, std::nullopt}});
    auto* raw = transport.get();

    HttpClient client{std::move(transport)};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    CHECK_THROWS_AS((void)client.send(request_for(), {}, {}), HttpError);
    CHECK(raw->attempts() == 4);
}

TEST_CASE("a transport failure that then succeeds is recovered", "[backends][http][retry]") {
    auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
        {0, "", 0, true, "dns failure", std::nullopt, std::nullopt},
        {200, "recovered", 0, false, "", std::nullopt},
    });
    HttpClient client{std::move(transport)};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    CHECK(client.send(request_for(), {}, {}).body == "recovered");
}

TEST_CASE("max_attempts of 1 disables retrying", "[backends][http][retry]") {
    auto transport = std::make_unique<FakeTransport>(
        std::vector<FakeTransport::Reply>{{503, "down", 0, false, "", std::nullopt, std::nullopt}});
    auto* raw = transport.get();

    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient client{std::move(transport), policy};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    (void)client.send(request_for(), {}, {});
    CHECK(raw->attempts() == 1);
    CHECK(recorder.delays.empty());
}

TEST_CASE("a streamed body reaches the sink in chunks", "[backends][http]") {
    auto transport = FakeTransport::ok("abcdefgh", 3);
    HttpClient client{std::move(transport)};

    std::vector<std::string> chunks;
    const auto response = client.send(request_for(),
                                      [&chunks](std::string_view chunk) {
                                          chunks.emplace_back(chunk);
                                          return true;
                                      },
                                      {});

    REQUIRE(chunks == std::vector<std::string>{"abc", "def", "gh"});
    // The body is not also accumulated -- the sink consumed it.
    CHECK(response.body.empty());
}

TEST_CASE("a stream is NOT retried once bytes have reached the caller", "[backends][http][retry]") {
    // The subtle one: a connection that drops HALFWAY THROUGH a successful
    // stream. Retrying would replay the first half into a sink that has
    // already rendered it -- the user sees the opening of the answer twice,
    // with no way to tell which is real. Better to surface the failure.
    auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
        {200, "the quick brown fox", 4, false, "", std::nullopt, std::size_t{8}},
        {200, "should never be reached", 0, false, "", std::nullopt, std::nullopt},
    });
    auto* raw = transport.get();

    HttpClient client{std::move(transport)};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    std::string streamed;
    CHECK_THROWS_AS((void)client.send(request_for(),
                                      [&streamed](std::string_view chunk) {
                                          streamed += chunk;
                                          return true;
                                      },
                                      {}),
                    HttpError);

    CHECK(raw->attempts() == 1);
    CHECK(recorder.delays.empty());
    // Exactly the bytes delivered before the drop -- the point is that they
    // reached the caller, so replaying them is not an option.
    CHECK(streamed == "the quic");
}

TEST_CASE("a transport failure BEFORE any byte is delivered is still retried",
          "[backends][http][retry]") {
    // The mirror case: nothing reached the caller, so replaying is safe.
    auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
        {200, "body", 4, false, "", std::nullopt, std::size_t{0}},
        {200, "recovered", 0, false, "", std::nullopt, std::nullopt},
    });
    auto* raw = transport.get();

    HttpClient client{std::move(transport)};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    std::string streamed;
    (void)client.send(request_for(),
                      [&streamed](std::string_view chunk) {
                          streamed += chunk;
                          return true;
                      },
                      {});

    CHECK(raw->attempts() == 2);
    CHECK(streamed == "recovered");
}

TEST_CASE("an error body is never streamed to the sink", "[backends][http][retry]") {
    // The contract that makes retry and streaming coexist. An error body
    // delivered to the sink would look like "already partly answered" and a
    // perfectly retryable 429 would stop being retried.
    auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
        {429, R"({"error":"slow down"})", 4, false, "", std::nullopt, std::nullopt},
        {200, "ok", 0, false, "", std::nullopt, std::nullopt},
    });
    auto* raw = transport.get();

    HttpClient client{std::move(transport)};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    std::string streamed;
    const HttpResponse response = client.send(request_for(),
                                              [&streamed](std::string_view chunk) {
                                                  streamed += chunk;
                                                  return true;
                                              },
                                              {});

    CHECK(response.status == 200);
    CHECK(raw->attempts() == 2);
    // The error body went to response.body on its attempt, never to the sink.
    CHECK(streamed == "ok");
}

TEST_CASE("an empty-bodied error response is still retried", "[backends][http][retry]") {
    // Nothing reached the caller, so replaying is safe.
    auto transport = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
        {503, "", 4, false, "", std::nullopt, std::nullopt},
        {200, "ok", 0, false, "", std::nullopt, std::nullopt},
    });
    auto* raw = transport.get();

    HttpClient client{std::move(transport)};
    SleepRecorder recorder;
    client.set_sleeper(recorder.sleeper());

    std::string streamed;
    const HttpResponse response = client.send(request_for(),
                                              [&streamed](std::string_view chunk) {
                                                  streamed += chunk;
                                                  return true;
                                              },
                                              {});

    CHECK(response.status == 200);
    CHECK(raw->attempts() == 2);
}

TEST_CASE("a cancelled token aborts before any request is made", "[backends][http][cancel]") {
    auto transport = FakeTransport::ok("body");
    auto* raw = transport.get();
    HttpClient client{std::move(transport)};

    const CancellationToken token = CancellationToken::create();
    token.cancel();

    CHECK_THROWS_AS((void)client.send(request_for(), {}, token), apogee::harness::CancelledError);
    CHECK(raw->attempts() == 0);
}

TEST_CASE("a sink returning false stops the transfer", "[backends][http]") {
    auto transport = FakeTransport::ok("abcdefghij", 2);
    HttpClient client{std::move(transport)};

    std::string streamed;
    (void)client.send(request_for(),
                      [&streamed](std::string_view chunk) {
                          streamed += chunk;
                          return streamed.size() < 4;
                      },
                      {});

    CHECK(streamed.size() == 4);
}

TEST_CASE("the request reaches the transport unchanged", "[backends][http]") {
    auto transport = FakeTransport::ok("{}");
    auto* raw = transport.get();
    HttpClient client{std::move(transport)};

    HttpRequest request = request_for("https://example.test/v1/thing");
    request.headers = {{"x-api-key", "secret"}, {"content-type", "application/json"}};
    (void)client.send(request, {}, {});

    REQUIRE(raw->requests().size() == 1);
    CHECK(raw->requests()[0].url == "https://example.test/v1/thing");
    CHECK(raw->requests()[0].body == R"({"hello":"world"})");
    CHECK(raw->requests()[0].headers.size() == 2);
}
