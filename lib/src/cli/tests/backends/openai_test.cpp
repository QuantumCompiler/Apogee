#include "backends/openai.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>

#include "backends/openai_wire.h"
#include "harness/errors.h"
#include "support/fake_transport.h"

using apogee::backends::HttpClient;
using apogee::backends::OpenAIProvider;
using apogee::backends::RetryPolicy;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::ContentPart;
using apogee::harness::FinishReason;
using apogee::harness::MessageContent;
using apogee::harness::ProviderError;
using apogee::harness::StreamOptions;
using apogee::harness::Tool;
using apogee::harness::ToolResult;
using apogee::testing::FakeTransport;
using nlohmann::json;

namespace {

/// A recorded Responses stream: reasoning summary, then answer text.
constexpr std::string_view kTextStream =
    "data: {\"type\":\"response.created\",\"response\":{\"model\":\"gpt-5\"}}\n\n"
    "data: {\"type\":\"response.reasoning_summary_text.delta\",\"delta\":\"weighing it up\"}\n\n"
    "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hello\"}\n\n"
    "data: {\"type\":\"response.output_text.delta\",\"delta\":\", world\"}\n\n"
    R"(data: {"type":"response.completed","response":{"model":"gpt-5","status":"completed","usage":{"input_tokens":25,"output_tokens":7}}})"
    "\n\n";

/// A recorded stream that calls a tool: the item is announced, then its
/// arguments arrive as deltas.
constexpr std::string_view kToolStream =
    R"(data: {"type":"response.output_item.added","output_index":0,"item":{"type":"function_call","id":"fc_1","call_id":"call_abc","name":"lookup","arguments":""}})"
    "\n\n"
    R"(data: {"type":"response.function_call_arguments.delta","output_index":0,"delta":"{\"key\":"})"
    "\n\n"
    R"(data: {"type":"response.function_call_arguments.delta","output_index":0,"delta":"\"answer\"}"})"
    "\n\n"
    R"(data: {"type":"response.completed","response":{"model":"gpt-5","status":"completed","usage":{"input_tokens":40,"output_tokens":15}}})"
    "\n\n";

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

FakeTransport::Reply sse(std::string_view body, std::size_t chunk = 0) {
    return FakeTransport::Reply{.status = 200, .body = std::string{body}, .chunk_size = chunk};
}

ChatRequest chat_request() {
    ChatRequest request;
    request.messages = {ChatMessage::system("Be brief."), ChatMessage::user("Hello")};
    return request;
}

}  // namespace

TEST_CASE("a streamed OpenAI chat yields tokens and usage", "[backends][openai]") {
    Fixture f = make_provider({sse(kTextStream)});

    std::string streamed;
    std::string thinking;
    StreamOptions options;
    options.on_token = [&streamed](std::string_view c) { streamed += c; };
    options.on_thinking = [&thinking](std::string_view c) { thinking += c; };

    const auto response = f.provider->stream_chat(chat_request(), options);

    CHECK(streamed == "Hello, world");
    CHECK(response.message.content.plain_text() == "Hello, world");
    CHECK(response.usage.prompt_tokens == 25);
    CHECK(response.usage.completion_tokens == 7);
    CHECK(response.finish_reason == FinishReason::Stop);

    // Reasoning goes to its own channel and never into the answer.
    CHECK(thinking == "weighing it up");
    CHECK(response.message.content.plain_text().find("weighing") == std::string::npos);
}

TEST_CASE("the OpenAI stream parses identically at every chunk size", "[backends][openai][split]") {
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

TEST_CASE("a tool call reassembles from item + argument deltas", "[backends][openai]") {
    Fixture f = make_provider({sse(kToolStream)});
    const auto response = f.provider->stream_chat(chat_request(), {});

    REQUIRE(response.message.tool_calls.size() == 1);
    // call_id, NOT the item id -- using `id` makes every tool result fail to
    // match its call.
    CHECK(response.message.tool_calls[0].id == "call_abc");
    CHECK(response.message.tool_calls[0].name == "lookup");
    CHECK(json::parse(response.message.tool_calls[0].arguments).at("key") == "answer");
    CHECK(response.finish_reason == FinishReason::ToolCalls);
}

TEST_CASE("the system prompt becomes instructions, not a message", "[backends][openai][wire]") {
    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(chat_request(), {});

    const json body = json::parse(f.transport->requests()[0].body);
    CHECK(body.at("instructions") == "Be brief.");
    for (const auto& item : body.at("input")) {
        CHECK(item.value("role", std::string{}) != "system");
    }
}

TEST_CASE("a tool call and its result become separate top-level items",
          "[backends][openai][wire]") {
    // The shape difference from Anthropic that breaks a naive port: neither is
    // a field on a message.
    ChatRequest request;
    ChatMessage assistant = ChatMessage::assistant("working");
    assistant.tool_calls = {apogee::harness::ToolCall{"call_1", "search", R"({"q":"x"})"}};
    request.messages = {
        ChatMessage::user("go"), assistant,
        ChatMessage::from_tool_result(ToolResult{"call_1", "search", "found", false})};

    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(request, {});

    const json input = json::parse(f.transport->requests()[0].body).at("input");
    bool saw_call = false;
    bool saw_output = false;
    for (const auto& item : input) {
        const std::string type = item.value("type", std::string{});
        if (type == "function_call") {
            saw_call = true;
            CHECK(item.at("call_id") == "call_1");
            CHECK(item.at("name") == "search");
        }
        if (type == "function_call_output") {
            saw_output = true;
            CHECK(item.at("call_id") == "call_1");
            CHECK(item.at("output") == "found");
        }
    }
    CHECK(saw_call);
    CHECK(saw_output);
}

TEST_CASE("tools are flat, not nested under a function key", "[backends][openai][wire]") {
    // Chat Completions nests {type:function, function:{...}}; Responses does not.
    ChatRequest request = chat_request();
    request.tools = {Tool{"search", "search the web", R"({"type":"object"})"}};

    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(request, {});

    const json tools = json::parse(f.transport->requests()[0].body).at("tools");
    REQUIRE(tools.size() == 1);
    CHECK(tools[0].at("type") == "function");
    CHECK(tools[0].at("name") == "search");
    CHECK_FALSE(tools[0].contains("function"));
}

TEST_CASE("input and output text parts are named differently", "[backends][openai][wire]") {
    // input_text vs output_text: getting it wrong produces a silent "no
    // content" rather than an error.
    using apogee::backends::openai::input_items;

    const json user = input_items(ChatMessage::user("hi"));
    CHECK(user[0].at("content")[0].at("type") == "input_text");

    const json assistant = input_items(ChatMessage::assistant("hi"));
    CHECK(assistant[0].at("content")[0].at("type") == "output_text");
}

TEST_CASE("an image becomes an input_image part", "[backends][openai][wire]") {
    using apogee::backends::openai::input_items;
    const json items = input_items(ChatMessage::user(
        MessageContent::from_parts({ContentPart::from_text("look"),
                                    ContentPart::from_image_url("data:image/png;base64,AAAA")})));

    const json content = items[0].at("content");
    CHECK(content[0].at("type") == "input_text");
    CHECK(content[1].at("type") == "input_image");
    // A data: URI passes through whole -- no base64/media-type split needed.
    CHECK(content[1].at("image_url") == "data:image/png;base64,AAAA");
}

TEST_CASE("a token budget maps onto an effort band", "[backends][openai][wire]") {
    // OpenAI takes a band, not a count. One knob per provider, mapped here.
    using apogee::backends::openai::effort_for_budget;
    CHECK(effort_for_budget(0).empty());
    CHECK(effort_for_budget(1024) == "low");
    CHECK(effort_for_budget(4096) == "medium");
    CHECK(effort_for_budget(32000) == "high");
}

TEST_CASE("reasoning is requested with a summary, or not at all", "[backends][openai][wire]") {
    Fixture off = make_provider({sse(kTextStream)});
    (void)off.provider->stream_chat(chat_request(), {});
    CHECK_FALSE(json::parse(off.transport->requests()[0].body).contains("reasoning"));

    OpenAIProvider::Options options;
    options.thinking_budget_tokens = 4096;
    Fixture on = make_provider({sse(kTextStream)}, options);
    (void)on.provider->stream_chat(chat_request(), {});

    const json reasoning = json::parse(on.transport->requests()[0].body).at("reasoning");
    CHECK(reasoning.at("effort") == "medium");
    // Without summary:auto the effort applies but nothing streams to display.
    CHECK(reasoning.at("summary") == "auto");
}

TEST_CASE("web_search is a server-side tool when enabled", "[backends][openai][wire]") {
    OpenAIProvider::Options options;
    options.web_search = true;
    Fixture f = make_provider({sse(kTextStream)}, options);
    (void)f.provider->stream_chat(chat_request(), {});

    const json tools = json::parse(f.transport->requests()[0].body).at("tools");
    bool found = false;
    for (const auto& tool : tools) {
        if (tool.value("type", std::string{}) == "web_search") {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("a non-streaming response parses content, tools, and reasoning", "[backends][openai]") {
    Fixture f = make_provider(
        {FakeTransport::Reply{.status = 200, .body = R"({"model":"gpt-5","status":"completed",
            "output":[
              {"type":"reasoning","summary":[{"type":"summary_text","text":"private"}]},
              {"type":"message","content":[{"type":"output_text","text":"public"}]},
              {"type":"function_call","call_id":"c1","name":"search","arguments":"{}"}],
            "usage":{"input_tokens":11,"output_tokens":22}})"}});

    const auto response = f.provider->chat(chat_request(), {});

    CHECK(response.message.content.plain_text() == "public");
    CHECK(response.message.content.plain_text().find("private") == std::string::npos);
    REQUIRE(response.message.tool_calls.size() == 1);
    CHECK(response.message.tool_calls[0].id == "c1");
    CHECK(response.usage.total_tokens() == 33);
}

TEST_CASE("max_output_tokens marks a length finish", "[backends][openai][wire]") {
    using apogee::backends::openai::finish_reason_from_status;
    CHECK(finish_reason_from_status("completed", "") == FinishReason::Stop);
    CHECK(finish_reason_from_status("incomplete", "max_output_tokens") == FinishReason::Length);
    CHECK(finish_reason_from_status("incomplete", "content_filter") == FinishReason::ContentFilter);
}

TEST_CASE("an API error surfaces its message", "[backends][openai][error]") {
    Fixture f = make_provider({FakeTransport::Reply{
        .status = 400,
        .body = R"({"error":{"type":"invalid_request_error","message":"bad model"}})"}});
    try {
        (void)f.provider->chat(chat_request(), {});
        FAIL("expected ProviderError");
    } catch (const ProviderError& e) {
        CHECK(std::string{e.what()}.find("bad model") != std::string::npos);
    }
}

TEST_CASE("no error path can emit the OpenAI API key", "[backends][openai][secrets]") {
    constexpr std::string_view kKey = "sk-SUPERSECRET-openai";
    OpenAIProvider::Options options;
    options.api_key = kKey;

    const std::vector<FakeTransport::Reply> failures{
        {.status = 401, .body = R"({"error":{"message":"bad key"}})"},
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

    // And it rides a header, never the body.
    Fixture ok = make_provider({sse(kTextStream)}, options);
    (void)ok.provider->stream_chat(chat_request(), {});
    CHECK(ok.transport->requests()[0].body.find("SUPERSECRET") == std::string::npos);
}

TEST_CASE("a missing OpenAI key is refused with an actionable message",
          "[backends][openai][error]") {
    apogee::harness::BackendConfig config;
    config.type = apogee::harness::BackendType::OpenAI;
    try {
        (void)OpenAIProvider::from_config("gpt", config);
        FAIL("expected ProviderError");
    } catch (const ProviderError& e) {
        CHECK(std::string{e.what()}.find("OPENAI_API_KEY") != std::string::npos);
    }
}

TEST_CASE("a 429 is retried", "[backends][openai][error]") {
    Fixture f =
        make_provider({FakeTransport::Reply{.status = 429, .body = "slow down"}, sse(kTextStream)});
    const auto response = f.provider->stream_chat(chat_request(), {});
    CHECK(response.message.content.plain_text() == "Hello, world");
    CHECK(f.transport->attempts() == 2);
}

TEST_CASE("the request goes to /v1/responses with a bearer token", "[backends][openai]") {
    Fixture f = make_provider({sse(kTextStream)});
    (void)f.provider->stream_chat(chat_request(), {});

    CHECK(f.transport->requests()[0].url == "https://api.test/v1/responses");
    bool bearer = false;
    for (const auto& header : f.transport->requests()[0].headers) {
        if (header.name == "authorization" && header.value.rfind("Bearer ", 0) == 0) {
            bearer = true;
        }
    }
    CHECK(bearer);
}
