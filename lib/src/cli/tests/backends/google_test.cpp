#include "backends/google.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>

#include "backends/google_wire.h"
#include "harness/errors.h"
#include "support/fake_transport.h"

using apogee::backends::GoogleProvider;
using apogee::backends::HttpClient;
using apogee::backends::RetryPolicy;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::ContentPart;
using apogee::harness::FinishReason;
using apogee::harness::MessageContent;
using apogee::harness::ProviderError;
using apogee::harness::StreamOptions;
using apogee::harness::Tool;
using apogee::harness::ToolCall;
using apogee::harness::ToolResult;
using apogee::testing::FakeTransport;
using nlohmann::json;

namespace {

/// A recorded Gemini stream. Note that a thinking part and the answer arrive in
/// the SAME parts array, distinguished only by `thought`.
constexpr std::string_view kTextStream =
    R"(data: {"candidates":[{"content":{"parts":[{"text":"deliberating","thought":true}],"role":"model"}}],"modelVersion":"gemini-2.5-pro"})"
    "\n\n"
    R"(data: {"candidates":[{"content":{"parts":[{"text":"Hello"}],"role":"model"}}]})"
    "\n\n"
    R"(data: {"candidates":[{"content":{"parts":[{"text":", world"}],"role":"model"}}]})"
    "\n\n"
    R"(data: {"candidates":[{"content":{"parts":[]},"finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":25,"candidatesTokenCount":7}})"
    "\n\n";

constexpr std::string_view kToolStream =
    R"(data: {"candidates":[{"content":{"parts":[{"functionCall":{"name":"lookup","args":{"key":"answer"}}}],"role":"model"}}],"modelVersion":"gemini-2.5-pro"})"
    "\n\n"
    R"(data: {"candidates":[{"content":{"parts":[]},"finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":40,"candidatesTokenCount":15}})"
    "\n\n";

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

FakeTransport::Reply sse(std::string_view body, std::size_t chunk = 0) {
    return FakeTransport::Reply{.status = 200, .body = std::string{body}, .chunk_size = chunk};
}

ChatRequest chat_request() {
    ChatRequest request;
    request.messages = {ChatMessage::system("Be brief."), ChatMessage::user("Hello")};
    return request;
}

}  // namespace

TEST_CASE("a streamed Gemini chat yields tokens and usage", "[backends][google]") {
    Fixture f = make_provider({sse(kTextStream)});

    std::string streamed;
    std::string thinking;
    StreamOptions options;
    options.on_token = [&streamed](std::string_view c) { streamed += c; };
    options.on_thinking = [&thinking](std::string_view c) { thinking += c; };

    const auto response = f.provider->stream_chat(chat_request(), options);

    CHECK(streamed == "Hello, world");
    CHECK(response.usage.prompt_tokens == 25);
    CHECK(response.usage.completion_tokens == 7);
    CHECK(thinking == "deliberating");
}

TEST_CASE("a thought part never reaches the answer", "[backends][google][thinking]") {
    // THE Gemini trap: reasoning arrives in the same parts array as the answer,
    // flagged only by `thought`. A translator that does not check it emits the
    // model's private reasoning as the response.
    Fixture f = make_provider({sse(kTextStream)});

    std::string streamed;
    StreamOptions options;
    options.on_token = [&streamed](std::string_view c) { streamed += c; };

    const auto response = f.provider->stream_chat(chat_request(), options);

    CHECK(streamed.find("deliberating") == std::string::npos);
    CHECK(response.message.content.plain_text() == "Hello, world");
}

TEST_CASE("the Gemini stream parses identically at every chunk size", "[backends][google][split]") {
    for (std::size_t chunk : {1U, 3U, 17U, 128U, 4096U}) {
        INFO("chunk size " << chunk);
        Fixture f = make_provider({sse(kTextStream, chunk)});
        std::string streamed;
        StreamOptions options;
        options.on_token = [&streamed](std::string_view c) { streamed += c; };

        const auto response = f.provider->stream_chat(chat_request(), options);
        CHECK(streamed == "Hello, world");
        CHECK(response.usage.prompt_tokens == 25);
    }
}

TEST_CASE("a functionCall becomes an IR tool call", "[backends][google]") {
    Fixture f = make_provider({sse(kToolStream)});
    const auto response = f.provider->stream_chat(chat_request(), {});

    REQUIRE(response.message.tool_calls.size() == 1);
    CHECK(response.message.tool_calls[0].name == "lookup");
    CHECK(json::parse(response.message.tool_calls[0].arguments).at("key") == "answer");
    // Gemini emits no call id, so one is synthesised from the name -- and the
    // result goes back matched by name, which is what Gemini expects.
    CHECK(response.message.tool_calls[0].id == "lookup");
    CHECK(response.finish_reason == FinishReason::ToolCalls);
}

TEST_CASE("the assistant role is 'model' on the wire", "[backends][google][wire]") {
    // Sending "assistant" is rejected outright.
    using apogee::backends::google::content_entry;
    const auto entry = content_entry(ChatMessage::assistant("hi"));
    REQUIRE(entry.has_value());
    CHECK(entry->at("role") == "model");
}

TEST_CASE("a tool result is a functionResponse inside a user turn", "[backends][google][wire]") {
    // There is no tool role at all.
    using apogee::backends::google::content_entry;
    const auto entry =
        content_entry(ChatMessage::from_tool_result(ToolResult{"id", "search", "found", false}));

    REQUIRE(entry.has_value());
    CHECK(entry->at("role") == "user");
    const json part = entry->at("parts")[0];
    CHECK(part.at("functionResponse").at("name") == "search");
    CHECK(part.at("functionResponse").at("response").at("result") == "found");
}

TEST_CASE("the system prompt becomes systemInstruction", "[backends][google][wire]") {
    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(chat_request(), {});

    const json body = json::parse(f.transport->requests()[0].body);
    CHECK(body.at("systemInstruction").at("parts")[0].at("text") == "Be brief.");
    for (const auto& entry : body.at("contents")) {
        CHECK(entry.at("role") != "system");
    }
}

TEST_CASE("an image becomes inlineData with its media type", "[backends][google][wire]") {
    using apogee::backends::google::content_entry;
    const auto entry = content_entry(ChatMessage::user(
        MessageContent::from_parts({ContentPart::from_text("look"),
                                    ContentPart::from_image_url("data:image/png;base64,AAAA")})));

    REQUIRE(entry.has_value());
    const json parts = entry->at("parts");
    CHECK(parts[0].at("text") == "look");
    CHECK(parts[1].at("inlineData").at("mimeType") == "image/png");
    CHECK(parts[1].at("inlineData").at("data") == "AAAA");
}

TEST_CASE("tools are functionDeclarations", "[backends][google][wire]") {
    ChatRequest request = chat_request();
    request.tools = {Tool{"search", "search the web", R"({"type":"object"})"}};

    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(request, {});

    const json tools = json::parse(f.transport->requests()[0].body).at("tools");
    CHECK(tools[0].at("functionDeclarations")[0].at("name") == "search");
}

TEST_CASE("a thinking budget passes through as a token count", "[backends][google][wire]") {
    // Unlike OpenAI's effort band, Gemini takes a real budget.
    Fixture off = make_provider({sse(kTextStream)});
    (void)off.provider->stream_chat(chat_request(), {});
    CHECK_FALSE(json::parse(off.transport->requests()[0].body)
                    .at("generationConfig")
                    .contains("thinkingConfig"));

    GoogleProvider::Options options;
    options.thinking_budget_tokens = 2048;
    Fixture on = make_provider({sse(kTextStream)}, options);
    (void)on.provider->stream_chat(chat_request(), {});

    const json thinking =
        json::parse(on.transport->requests()[0].body).at("generationConfig").at("thinkingConfig");
    CHECK(thinking.at("thinkingBudget") == 2048);
    // Without includeThoughts the budget applies but nothing is emitted.
    CHECK(thinking.at("includeThoughts") == true);
}

TEST_CASE("google_search is added when web search is enabled", "[backends][google][wire]") {
    GoogleProvider::Options options;
    options.web_search = true;
    Fixture f = make_provider({sse(kTextStream)}, options);
    (void)f.provider->stream_chat(chat_request(), {});

    const json tools = json::parse(f.transport->requests()[0].body).at("tools");
    bool found = false;
    for (const auto& tool : tools) {
        if (tool.contains("google_search")) {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("the model rides the URL path and the key rides a header", "[backends][google]") {
    // A key in the query string is logged by every proxy and lands in shell
    // history.
    GoogleProvider::Options options;
    options.model = "gemini-2.5-flash";
    Fixture f = make_provider({sse(kTextStream)}, options);
    (void)f.provider->stream_chat(chat_request(), {});

    const auto& request = f.transport->requests()[0];
    CHECK(request.url ==
          "https://gen.test/v1beta/models/gemini-2.5-flash:streamGenerateContent?alt=sse");
    CHECK(request.url.find("AIza") == std::string::npos);

    bool key_header = false;
    for (const auto& header : request.headers) {
        if (header.name == "x-goog-api-key") {
            key_header = true;
        }
    }
    CHECK(key_header);
}

TEST_CASE("the non-streaming endpoint has no alt=sse", "[backends][google]") {
    Fixture f = make_provider({FakeTransport::Reply{
        .status = 200,
        .body =
            R"({"candidates":[{"content":{"parts":[{"text":"hi"}]},"finishReason":"STOP"}]})"}});
    (void)f.provider->chat(chat_request(), {});

    const std::string url = f.transport->requests()[0].url;
    CHECK(url.find(":generateContent") != std::string::npos);
    CHECK(url.find("alt=sse") == std::string::npos);
}

TEST_CASE("Gemini finish reasons map onto the IR", "[backends][google][wire]") {
    using apogee::backends::google::finish_reason_from_string;
    CHECK(finish_reason_from_string("STOP") == FinishReason::Stop);
    CHECK(finish_reason_from_string("MAX_TOKENS") == FinishReason::Length);
    CHECK(finish_reason_from_string("SAFETY") == FinishReason::ContentFilter);
    CHECK(finish_reason_from_string("RECITATION") == FinishReason::ContentFilter);
    CHECK(finish_reason_from_string("SOMETHING_NEW") == FinishReason::Other);
}

TEST_CASE("an API error surfaces its message", "[backends][google][error]") {
    Fixture f = make_provider({FakeTransport::Reply{
        .status = 400,
        .body = R"({"error":{"status":"INVALID_ARGUMENT","message":"bad request"}})"}});
    try {
        (void)f.provider->chat(chat_request(), {});
        FAIL("expected ProviderError");
    } catch (const ProviderError& e) {
        CHECK(std::string{e.what()}.find("bad request") != std::string::npos);
    }
}

TEST_CASE("no error path can emit the Gemini API key", "[backends][google][secrets]") {
    constexpr std::string_view kKey = "AIza-SUPERSECRET-google";
    GoogleProvider::Options options;
    options.api_key = kKey;

    const std::vector<FakeTransport::Reply> failures{
        {.status = 403, .body = R"({"error":{"message":"forbidden"}})"},
        {.status = 500, .body = "internal"},
        {.status = 200, .body = "not json"},
        {.status = 0, .transport_error = true, .error_message = "reset"},
    };
    for (const auto& failure : failures) {
        Fixture f = make_provider({failure}, options);
        std::string message;
        try {
            (void)f.provider->chat(chat_request(), {});
        } catch (const std::exception& e) {
            message = e.what();
        }
        CHECK(message.find("SUPERSECRET") == std::string::npos);
    }

    Fixture ok = make_provider({sse(kTextStream)}, options);
    (void)ok.provider->stream_chat(chat_request(), {});
    CHECK(ok.transport->requests()[0].body.find("SUPERSECRET") == std::string::npos);
    CHECK(ok.transport->requests()[0].url.find("SUPERSECRET") == std::string::npos);
}

TEST_CASE("a missing Gemini key is refused with an actionable message",
          "[backends][google][error]") {
    apogee::harness::BackendConfig config;
    config.type = apogee::harness::BackendType::Google;
    try {
        (void)GoogleProvider::from_config("gem", config);
        FAIL("expected ProviderError");
    } catch (const ProviderError& e) {
        CHECK(std::string{e.what()}.find("GEMINI_API_KEY") != std::string::npos);
    }
}

TEST_CASE("a 429 is retried", "[backends][google][error]") {
    Fixture f =
        make_provider({FakeTransport::Reply{.status = 429, .body = "slow"}, sse(kTextStream)});
    const auto response = f.provider->stream_chat(chat_request(), {});
    CHECK(response.message.content.plain_text() == "Hello, world");
    CHECK(f.transport->attempts() == 2);
}
