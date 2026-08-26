#include "backends/anthropic.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

#include "backends/anthropic_wire.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "support/fake_transport.h"

using apogee::backends::AnthropicProvider;
using apogee::backends::HttpClient;
using apogee::backends::RetryPolicy;
using apogee::harness::CancellationToken;
using apogee::harness::CancelledError;
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

/// A recorded Anthropic SSE stream: text, tool call, and usage.
constexpr std::string_view kTextStream =
    "event: message_start\n"
    R"(data: {"type":"message_start","message":{"model":"claude-sonnet-5","usage":{"input_tokens":25}}})"
    "\n\n"
    "event: content_block_start\n"
    R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})"
    "\n\n"
    "event: content_block_delta\n"
    R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}})"
    "\n\n"
    "event: content_block_delta\n"
    R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":", world"}})"
    "\n\n"
    "event: content_block_stop\n"
    R"(data: {"type":"content_block_stop","index":0})"
    "\n\n"
    "event: message_delta\n"
    R"(data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":7}})"
    "\n\n"
    "event: message_stop\n"
    R"(data: {"type":"message_stop"})"
    "\n\n";

/// A stream that thinks, then calls a tool -- the extended-thinking replay case.
constexpr std::string_view kThinkingToolStream =
    "event: message_start\n"
    R"(data: {"type":"message_start","message":{"model":"claude-sonnet-5","usage":{"input_tokens":40}}})"
    "\n\n"
    "event: content_block_start\n"
    R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"thinking"}})"
    "\n\n"
    "event: content_block_delta\n"
    R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"Let me search"}})"
    "\n\n"
    "event: content_block_delta\n"
    R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"signature_delta","signature":"SIGabc"}})"
    "\n\n"
    "event: content_block_stop\n"
    R"(data: {"type":"content_block_stop","index":0})"
    "\n\n"
    "event: content_block_start\n"
    R"(data: {"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_1","name":"search"}})"
    "\n\n"
    "event: content_block_delta\n"
    R"(data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"query\":"}})"
    "\n\n"
    "event: content_block_delta\n"
    R"(data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"\"apogee\"}"}})"
    "\n\n"
    "event: content_block_stop\n"
    R"(data: {"type":"content_block_stop","index":1})"
    "\n\n"
    "event: message_delta\n"
    R"(data: {"type":"message_delta","delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":30}})"
    "\n\n"
    "event: message_stop\n"
    R"(data: {"type":"message_stop"})"
    "\n\n";

struct Fixture {
    FakeTransport* transport = nullptr;
    std::unique_ptr<AnthropicProvider> provider;
};

Fixture make_provider(std::vector<FakeTransport::Reply> replies,
                      AnthropicProvider::Options options = {}) {
    if (options.api_key.empty()) {
        options.api_key = "sk-ant-test-key";
    }
    options.base_url = "https://api.test";

    auto transport = std::make_unique<FakeTransport>(std::move(replies));
    Fixture fixture;
    fixture.transport = transport.get();

    RetryPolicy policy;
    policy.max_attempts = 3;
    auto client = std::make_unique<HttpClient>(std::move(transport), policy);
    client->set_sleeper([](std::chrono::milliseconds) {});

    fixture.provider = std::make_unique<AnthropicProvider>(std::move(options), std::move(client));
    return fixture;
}

FakeTransport::Reply sse(std::string_view body, std::size_t chunk = 0) {
    return FakeTransport::Reply{200, std::string{body}, chunk,       false,
                                "",  std::nullopt,      std::nullopt};
}

ChatRequest chat_request() {
    ChatRequest request;
    request.messages = {ChatMessage::system("Be brief."), ChatMessage::user("Hello")};
    return request;
}

}  // namespace

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

TEST_CASE("a streamed chat yields tokens and a complete response", "[backends][anthropic]") {
    Fixture f = make_provider({sse(kTextStream)});

    std::string streamed;
    StreamOptions options;
    options.on_token = [&streamed](std::string_view chunk) { streamed += chunk; };

    const auto response = f.provider->stream_chat(chat_request(), options);

    CHECK(streamed == "Hello, world");
    CHECK(response.message.content.plain_text() == "Hello, world");
    CHECK(response.finish_reason == FinishReason::Stop);
    CHECK(response.model == "claude-sonnet-5");
    CHECK(response.usage.prompt_tokens == 25);
    CHECK(response.usage.completion_tokens == 7);
}

TEST_CASE("the stream parses identically at every chunk size", "[backends][anthropic][split]") {
    // The acceptance criterion: events split across read boundaries. This runs
    // the whole provider, not just the parser, so a bug anywhere between the
    // transport and the accumulator shows up here.
    for (std::size_t chunk : {1U, 2U, 3U, 7U, 13U, 64U, 512U}) {
        INFO("chunk size " << chunk);
        Fixture f = make_provider({sse(kTextStream, chunk)});

        std::string streamed;
        StreamOptions options;
        options.on_token = [&streamed](std::string_view c) { streamed += c; };

        const auto response = f.provider->stream_chat(chat_request(), options);
        CHECK(streamed == "Hello, world");
        CHECK(response.usage.prompt_tokens == 25);
        CHECK(response.usage.completion_tokens == 7);
    }
}

TEST_CASE("tool calls arrive as structured IR tool calls", "[backends][anthropic]") {
    Fixture f = make_provider({sse(kThinkingToolStream)});

    const auto response = f.provider->stream_chat(chat_request(), {});

    REQUIRE(response.message.tool_calls.size() == 1);
    CHECK(response.message.tool_calls[0].id == "toolu_1");
    CHECK(response.message.tool_calls[0].name == "search");
    // Arguments arrive as input_json_delta fragments and must reassemble.
    CHECK(json::parse(response.message.tool_calls[0].arguments).at("query") == "apogee");
    CHECK(response.finish_reason == FinishReason::ToolCalls);
}

TEST_CASE("tool-call argument fragments reassemble at every chunk size",
          "[backends][anthropic][split]") {
    for (std::size_t chunk : {1U, 5U, 17U, 128U}) {
        INFO("chunk size " << chunk);
        Fixture f = make_provider({sse(kThinkingToolStream, chunk)});
        const auto response = f.provider->stream_chat(chat_request(), {});
        REQUIRE(response.message.tool_calls.size() == 1);
        CHECK(json::parse(response.message.tool_calls[0].arguments).at("query") == "apogee");
    }
}

// ---------------------------------------------------------------------------
// Thinking -- display only, always
// ---------------------------------------------------------------------------

TEST_CASE("thinking reaches its own sink and never the answer text",
          "[backends][anthropic][thinking]") {
    // The contract: thinking is display and loop metadata only. If it reaches
    // returned text it lands in persisted history, gets re-sent on every later
    // turn, and shows up in an API response where no client expects it.
    Fixture f = make_provider({sse(kThinkingToolStream)});

    std::string thinking;
    std::string text;
    StreamOptions options;
    options.on_thinking = [&thinking](std::string_view c) { thinking += c; };
    options.on_token = [&text](std::string_view c) { text += c; };

    const auto response = f.provider->stream_chat(chat_request(), options);

    CHECK(thinking == "Let me search");
    // Not in the token stream...
    CHECK(text.empty());
    // ...and not in the returned message.
    CHECK(response.message.content.plain_text().empty());
    CHECK(response.message.content.plain_text().find("Let me search") == std::string::npos);
}

TEST_CASE("a request with no thinking sink still streams correctly",
          "[backends][anthropic][thinking]") {
    // An absent sink must not drop tokens or crash -- most callers have none.
    Fixture f = make_provider({sse(kThinkingToolStream)});
    const auto response = f.provider->stream_chat(chat_request(), {});
    CHECK(response.message.tool_calls.size() == 1);
}

TEST_CASE("thinking blocks are replayed on the following request",
          "[backends][anthropic][thinking]") {
    // An Anthropic requirement Ommi never had to meet: with extended thinking
    // on, an assistant turn that called a tool must have its thinking block --
    // signature included -- sent back in the next request, or the API rejects
    // the turn. Thinking must not enter the IR, so the provider caches the raw
    // blocks and splices them back itself.
    Fixture f = make_provider({sse(kThinkingToolStream), sse(kTextStream)});

    // Turn 1: the model thinks, then calls a tool.
    const auto first = f.provider->stream_chat(chat_request(), {});
    REQUIRE(first.message.tool_calls.size() == 1);

    // Turn 2: we answer the tool call.
    ChatRequest second = chat_request();
    second.messages.push_back(first.message);
    second.messages.push_back(
        ChatMessage::from_tool_result(ToolResult{"toolu_1", "search", "3 results", false}));
    (void)f.provider->stream_chat(second, {});

    REQUIRE(f.transport->requests().size() == 2);
    const json body = json::parse(f.transport->requests()[1].body);

    // The assistant turn carries its thinking block first, verbatim.
    const json& messages = body.at("messages");
    const json* assistant = nullptr;
    for (const auto& message : messages) {
        if (message.at("role") == "assistant") {
            assistant = &message;
        }
    }
    REQUIRE(assistant != nullptr);
    const json& blocks = assistant->at("content");
    REQUIRE(blocks.is_array());
    REQUIRE(!blocks.empty());
    CHECK(blocks[0].at("type") == "thinking");
    CHECK(blocks[0].at("thinking") == "Let me search");
    CHECK(blocks[0].at("signature") == "SIGabc");
}

TEST_CASE("reset_conversation drops the replay cache", "[backends][anthropic][thinking]") {
    Fixture f = make_provider({sse(kThinkingToolStream), sse(kTextStream)});
    const auto first = f.provider->stream_chat(chat_request(), {});
    f.provider->reset_conversation();

    ChatRequest second = chat_request();
    second.messages.push_back(first.message);
    second.messages.push_back(
        ChatMessage::from_tool_result(ToolResult{"toolu_1", "search", "r", false}));
    (void)f.provider->stream_chat(second, {});

    const json body = json::parse(f.transport->requests()[1].body);
    for (const auto& message : body.at("messages")) {
        if (message.at("role") != "assistant") {
            continue;
        }
        for (const auto& block : message.at("content")) {
            CHECK(block.at("type") != "thinking");
        }
    }
}

// ---------------------------------------------------------------------------
// Request shape
// ---------------------------------------------------------------------------

TEST_CASE("the system prompt is lifted out of the message list", "[backends][anthropic][wire]") {
    // Anthropic takes it as a top-level field; sending it as a message is
    // simply wrong.
    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(chat_request(), {});

    const json body = json::parse(f.transport->requests()[0].body);
    CHECK(body.at("system") == "Be brief.");
    for (const auto& message : body.at("messages")) {
        CHECK(message.at("role") != "system");
    }
    CHECK(body.at("messages").size() == 1);
}

TEST_CASE("multiple system messages concatenate rather than overwrite",
          "[backends][anthropic][wire]") {
    ChatRequest request;
    request.messages = {ChatMessage::system("First."), ChatMessage::system("Second."),
                        ChatMessage::user("Hi")};

    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(request, {});

    const json body = json::parse(f.transport->requests()[0].body);
    CHECK(body.at("system") == "First.\n\nSecond.");
}

TEST_CASE("tool results become tool_result blocks in a user turn", "[backends][anthropic][wire]") {
    // The IR has a Tool role, mirroring OpenAI. Anthropic has no such role and
    // rejects it outright.
    ChatRequest request;
    request.messages = {
        ChatMessage::user("search please"),
        ChatMessage::from_tool_result(ToolResult{"toolu_9", "search", "found", false})};

    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(request, {});

    const json body = json::parse(f.transport->requests()[0].body);
    const json& last = body.at("messages").back();
    CHECK(last.at("role") == "user");
    CHECK(last.at("content")[0].at("type") == "tool_result");
    CHECK(last.at("content")[0].at("tool_use_id") == "toolu_9");
    CHECK(last.at("content")[0].at("content") == "found");
}

TEST_CASE("consecutive tool results merge into one user turn", "[backends][anthropic][wire]") {
    ChatRequest request;
    request.messages = {ChatMessage::user("go"),
                        ChatMessage::from_tool_result(ToolResult{"t1", "a", "r1", false}),
                        ChatMessage::from_tool_result(ToolResult{"t2", "b", "r2", false})};

    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(request, {});

    const json body = json::parse(f.transport->requests()[0].body);
    CHECK(body.at("messages").size() == 2);
    CHECK(body.at("messages").back().at("content").size() == 2);
}

TEST_CASE("images map to Anthropic source blocks", "[backends][anthropic][wire]") {
    using apogee::backends::anthropic::content_blocks;

    const auto base64 = content_blocks(
        MessageContent::from_parts({ContentPart::from_text("look"),
                                    ContentPart::from_image_url("data:image/png;base64,AAAA")}));
    REQUIRE(base64.is_array());
    CHECK(base64[0].at("type") == "text");
    CHECK(base64[1].at("type") == "image");
    CHECK(base64[1].at("source").at("type") == "base64");
    CHECK(base64[1].at("source").at("media_type") == "image/png");
    CHECK(base64[1].at("source").at("data") == "AAAA");

    const auto remote = content_blocks(
        MessageContent::from_parts({ContentPart::from_image_url("https://x/y.png")}));
    CHECK(remote[0].at("source").at("type") == "url");
    CHECK(remote[0].at("source").at("url") == "https://x/y.png");

    // Plain text stays a bare string.
    CHECK(content_blocks(MessageContent{"just text"}).is_string());
}

TEST_CASE("tools are sent with input_schema, not parameters", "[backends][anthropic][wire]") {
    ChatRequest request = chat_request();
    request.tools = {Tool{"search", "search the web", R"({"type":"object"})"}};

    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(request, {});

    const json body = json::parse(f.transport->requests()[0].body);
    REQUIRE(body.at("tools").size() == 1);
    CHECK(body.at("tools")[0].at("name") == "search");
    CHECK(body.at("tools")[0].contains("input_schema"));
    CHECK_FALSE(body.at("tools")[0].contains("parameters"));
}

TEST_CASE("the server-side web_search tool is added when configured",
          "[backends][anthropic][wire]") {
    // What replaces Ommi's dependence on the claude CLI for web search.
    AnthropicProvider::Options options;
    options.web_search = true;
    options.web_search_max_uses = 3;

    Fixture f = make_provider({sse(kTextStream)}, options);
    (void)f.provider->stream_chat(chat_request(), {});

    const json body = json::parse(f.transport->requests()[0].body);
    REQUIRE(body.at("tools").size() == 1);
    CHECK(body.at("tools")[0].at("name") == "web_search");
    CHECK(body.at("tools")[0].at("type") == "web_search_20250305");
    CHECK(body.at("tools")[0].at("max_uses") == 3);
}

TEST_CASE("web_search is absent unless configured", "[backends][anthropic][wire]") {
    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(chat_request(), {});
    CHECK_FALSE(json::parse(f.transport->requests()[0].body).contains("tools"));
}

TEST_CASE("extended thinking is requested only when a budget is set",
          "[backends][anthropic][wire]") {
    Fixture off = make_provider({sse(kTextStream)});
    (void)off.provider->stream_chat(chat_request(), {});
    CHECK_FALSE(json::parse(off.transport->requests()[0].body).contains("thinking"));

    AnthropicProvider::Options options;
    options.thinking_budget_tokens = 2048;
    Fixture on = make_provider({sse(kThinkingToolStream)}, options);
    (void)on.provider->stream_chat(chat_request(), {});

    const json body = json::parse(on.transport->requests()[0].body);
    CHECK(body.at("thinking").at("type") == "enabled");
    CHECK(body.at("thinking").at("budget_tokens") == 2048);
}

TEST_CASE("required headers are sent", "[backends][anthropic]") {
    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(chat_request(), {});

    bool has_key = false;
    bool has_version = false;
    for (const auto& header : f.transport->requests()[0].headers) {
        if (header.name == "x-api-key") {
            has_key = true;
        }
        if (header.name == "anthropic-version") {
            has_version = true;
        }
    }
    CHECK(has_key);
    CHECK(has_version);
    CHECK(f.transport->requests()[0].url == "https://api.test/v1/messages");
}

// ---------------------------------------------------------------------------
// Errors, retries, cancellation
// ---------------------------------------------------------------------------

TEST_CASE("an API error surfaces its message, not raw JSON", "[backends][anthropic][error]") {
    Fixture f = make_provider({FakeTransport::Reply{
        400,
        R"({"type":"error","error":{"type":"invalid_request_error","message":"max_tokens is required"}})",
        0, false, "", std::nullopt, std::nullopt}});

    try {
        (void)f.provider->chat(chat_request(), {});
        FAIL("expected ProviderError");
    } catch (const ProviderError& e) {
        const std::string message = e.what();
        CHECK(message.find("max_tokens is required") != std::string::npos);
        CHECK(message.find("invalid_request_error") != std::string::npos);
    }
}

TEST_CASE("a non-JSON error body falls back to the status", "[backends][anthropic][error]") {
    // A gateway 502 is HTML. Dumping markup at the user helps nobody.
    Fixture f = make_provider(
        {FakeTransport::Reply{.status = 502, .body = "<html><body>Bad Gateway</body></html>"}});

    try {
        (void)f.provider->chat(chat_request(), {});
        FAIL("expected ProviderError");
    } catch (const ProviderError& e) {
        const std::string message = e.what();
        CHECK(message.find("502") != std::string::npos);
        CHECK(message.find("<html>") == std::string::npos);
    }
}

TEST_CASE("an error mid-stream surfaces as a provider error", "[backends][anthropic][error]") {
    constexpr std::string_view kErrorStream =
        "event: message_start\n"
        R"(data: {"type":"message_start","message":{"model":"m"}})"
        "\n\n"
        "event: error\n"
        R"(data: {"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})"
        "\n\n";

    Fixture f = make_provider({sse(kErrorStream)});
    try {
        (void)f.provider->stream_chat(chat_request(), {});
        FAIL("expected ProviderError");
    } catch (const ProviderError& e) {
        CHECK(std::string{e.what()}.find("Overloaded") != std::string::npos);
    }
}

TEST_CASE("a 429 is retried before the request is given up", "[backends][anthropic][error]") {
    Fixture f = make_provider({
        FakeTransport::Reply{.status = 429, .body = R"({"error":{"message":"rate limited"}})"},
        sse(kTextStream),
    });

    const auto response = f.provider->stream_chat(chat_request(), {});
    CHECK(response.message.content.plain_text() == "Hello, world");
    CHECK(f.transport->attempts() == 2);
}

TEST_CASE("a missing API key is refused with an actionable message",
          "[backends][anthropic][error]") {
    apogee::harness::BackendConfig config;
    config.type = apogee::harness::BackendType::Anthropic;
    config.model = "claude-sonnet-5";

    try {
        (void)AnthropicProvider::from_config("claude", config);
        FAIL("expected ProviderError");
    } catch (const ProviderError& e) {
        const std::string message = e.what();
        CHECK(message.find("api_key") != std::string::npos);
        CHECK(message.find("ANTHROPIC_API_KEY") != std::string::npos);
    }
}

TEST_CASE("cancellation mid-stream stops and throws", "[backends][anthropic][cancel]") {
    Fixture f = make_provider({sse(kTextStream, 8)});

    const CancellationToken token = CancellationToken::create();
    std::string streamed;
    StreamOptions options;
    options.cancellation = token;
    options.on_token = [&streamed, &token](std::string_view chunk) {
        streamed += chunk;
        token.cancel();
    };

    CHECK_THROWS_AS((void)f.provider->stream_chat(chat_request(), options), CancelledError);
}

// ---------------------------------------------------------------------------
// The guardrail: no API key may reach a message or a log
// ---------------------------------------------------------------------------

TEST_CASE("no error path can emit the API key", "[backends][anthropic][secrets]") {
    // Constructed so the key is distinctive and would be trivially greppable if
    // any path echoed the request into its message.
    constexpr std::string_view kKey = "sk-ant-SUPERSECRET-should-never-appear";

    AnthropicProvider::Options options;
    options.api_key = kKey;

    const std::vector<FakeTransport::Reply> failures{
        {.status = 400, .body = R"({"error":{"type":"invalid_request_error","message":"bad"}})"},
        {401, R"({"error":{"type":"authentication_error","message":"invalid x-api-key"}})", 0,
         false, "", std::nullopt, std::nullopt},
        {500, "internal", 0, false, "", std::nullopt, std::nullopt},
        {200, "not json at all", 0, false, "", std::nullopt, std::nullopt},
        {0, "", 0, true, "connection reset by peer", std::nullopt, std::nullopt},
    };

    for (const FakeTransport::Reply& failure : failures) {
        INFO("status " << failure.status);
        Fixture f = make_provider({failure}, options);
        std::string message;
        try {
            (void)f.provider->chat(chat_request(), {});
        } catch (const std::exception& e) {
            message = e.what();
        }
        CHECK(message.find(kKey) == std::string::npos);
        CHECK(message.find("SUPERSECRET") == std::string::npos);
    }
}

TEST_CASE("the API key does not appear in a serialized request body",
          "[backends][anthropic][secrets]") {
    // It belongs in a header and nowhere else. A key in the body would be
    // logged by any middlebox that logs request payloads.
    AnthropicProvider::Options options;
    options.api_key = "sk-ant-SUPERSECRET";

    Fixture f = make_provider({sse(kTextStream)}, options);
    (void)f.provider->stream_chat(chat_request(), {});

    CHECK(f.transport->requests()[0].body.find("SUPERSECRET") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Non-streaming and token counting
// ---------------------------------------------------------------------------

TEST_CASE("a non-streaming chat parses content, tools, and usage", "[backends][anthropic]") {
    Fixture f =
        make_provider({FakeTransport::Reply{200,
                                            R"({"model":"claude-sonnet-5","stop_reason":"tool_use",
            "content":[{"type":"text","text":"Working"},
                       {"type":"tool_use","id":"t1","name":"search","input":{"q":"x"}}],
            "usage":{"input_tokens":11,"output_tokens":22}})",
                                            0, false, "", std::nullopt, std::nullopt}});

    const auto response = f.provider->chat(chat_request(), {});

    CHECK(response.message.content.plain_text() == "Working");
    REQUIRE(response.message.tool_calls.size() == 1);
    CHECK(response.message.tool_calls[0].name == "search");
    CHECK(response.finish_reason == FinishReason::ToolCalls);
    CHECK(response.usage.prompt_tokens == 11);
    CHECK(response.usage.completion_tokens == 22);
}

TEST_CASE("a non-streaming thinking block stays out of the text",
          "[backends][anthropic][thinking]") {
    Fixture f = make_provider({FakeTransport::Reply{200,
                                                    R"({"model":"m","stop_reason":"end_turn",
            "content":[{"type":"thinking","thinking":"private"},{"type":"text","text":"public"}]})",
                                                    0, false, "", std::nullopt, std::nullopt}});

    const auto response = f.provider->chat(chat_request(), {});
    CHECK(response.message.content.plain_text() == "public");
    CHECK(response.message.content.plain_text().find("private") == std::string::npos);
}

TEST_CASE("count_tokens reports the exact input count", "[backends][anthropic]") {
    // Exact, not estimated: a context warning that fires at the wrong point is
    // worse than none.
    Fixture f = make_provider({FakeTransport::Reply{200, R"({"input_tokens":2095})", 0, false, "",
                                                    std::nullopt, std::nullopt}});

    CHECK(f.provider->count_tokens(chat_request(), {}) == 2095);
    CHECK(f.transport->requests()[0].url == "https://api.test/v1/messages/count_tokens");
    // The endpoint rejects these.
    const json body = json::parse(f.transport->requests()[0].body);
    CHECK_FALSE(body.contains("max_tokens"));
    CHECK_FALSE(body.contains("stream"));
}

TEST_CASE("list_models reports the configured model only", "[backends][anthropic]") {
    // A backend entry pins one model; listing the vendor catalogue would report
    // models this entry cannot serve.
    AnthropicProvider::Options options;
    options.backend_name = "claude";
    options.model = "claude-opus-5";

    Fixture f = make_provider({sse(kTextStream)}, options);
    const auto models = f.provider->list_models({});

    REQUIRE(models.size() == 1);
    CHECK(models[0].id == "claude-opus-5");
    CHECK(models[0].provider == "anthropic");
    CHECK(models[0].backend == "claude");
}

TEST_CASE("stop reasons map onto the IR", "[backends][anthropic][wire]") {
    using apogee::backends::anthropic::finish_reason_from_stop_reason;
    CHECK(finish_reason_from_stop_reason("end_turn") == FinishReason::Stop);
    CHECK(finish_reason_from_stop_reason("stop_sequence") == FinishReason::Stop);
    CHECK(finish_reason_from_stop_reason("max_tokens") == FinishReason::Length);
    CHECK(finish_reason_from_stop_reason("tool_use") == FinishReason::ToolCalls);
    CHECK(finish_reason_from_stop_reason("refusal") == FinishReason::ContentFilter);
    CHECK(finish_reason_from_stop_reason("something_new") == FinishReason::Other);
}

TEST_CASE("the provider satisfies the LLMProvider interface via the harness",
          "[backends][anthropic]") {
    // It must be routable and usable through the Harness like any other
    // provider -- that is the whole point of the interface.
    Fixture f = make_provider({FakeTransport::Reply{
        200, R"({"model":"m","stop_reason":"end_turn","content":[{"type":"text","text":"hi"}]})", 0,
        false, "", std::nullopt, std::nullopt}});
    apogee::harness::Harness registry{apogee::harness::Config{}};
    registry.register_provider("claude", std::move(f.provider));
    registry.use_default_router();

    ChatRequest routed = chat_request();
    routed.model = "claude";
    const auto response = registry.chat(routed);
    CHECK_FALSE(response.message.content.plain_text().empty());
    CHECK_FALSE(registry.can_embed("claude"));
}
