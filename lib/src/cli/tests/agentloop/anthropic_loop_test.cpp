#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "backends/anthropic.h"
#include "backends/http_client.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "support/fake_transport.h"

using apogee::agent::Tool;
using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::agentloop::Options;
using apogee::agentloop::RunResult;
using apogee::backends::AnthropicProvider;
using apogee::backends::HttpClient;
using apogee::harness::ChatMessage;
using apogee::harness::Config;
using apogee::harness::Harness;
using apogee::harness::Role;
using apogee::testing::FakeTransport;
using nlohmann::json;

namespace {

/// Turn 1: the model asks for a tool.
constexpr std::string_view kToolTurn =
    "event: message_start\n"
    R"(data: {"type":"message_start","message":{"model":"claude-sonnet-5","usage":{"input_tokens":20}}})"
    "\n\n"
    "event: content_block_start\n"
    R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_1","name":"lookup"}})"
    "\n\n"
    "event: content_block_delta\n"
    R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"key\":\"answer\"}"}})"
    "\n\n"
    "event: content_block_stop\n"
    R"(data: {"type":"content_block_stop","index":0})"
    "\n\n"
    "event: message_delta\n"
    R"(data: {"type":"message_delta","delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":15}})"
    "\n\n"
    "event: message_stop\n"
    R"(data: {"type":"message_stop"})"
    "\n\n";

/// Turn 2: having seen the tool result, the model answers.
constexpr std::string_view kAnswerTurn =
    "event: message_start\n"
    R"(data: {"type":"message_start","message":{"model":"claude-sonnet-5","usage":{"input_tokens":60}}})"
    "\n\n"
    "event: content_block_start\n"
    R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})"
    "\n\n"
    "event: content_block_delta\n"
    R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"The answer is 42"}})"
    "\n\n"
    "event: content_block_stop\n"
    R"(data: {"type":"content_block_stop","index":0})"
    "\n\n"
    "event: message_delta\n"
    R"(data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":9}})"
    "\n\n"
    "event: message_stop\n"
    R"(data: {"type":"message_stop"})"
    "\n\n";

FakeTransport::Reply sse(std::string_view body) {
    return FakeTransport::Reply{.status = 200, .body = std::string{body}};
}

}  // namespace

TEST_CASE("Anthropic tool_use round-trips through the loop end to end",
          "[agentloop][anthropic][conformance]") {
    // The pieces are tested separately -- the backend's tool_use parsing, and
    // the loop against a scripted provider. This is the join: a REAL provider,
    // fed recorded SSE, driven by the REAL loop. A dialect mistake anywhere
    // between the wire and the tool registry shows up here and nowhere else.
    auto transport = std::make_unique<FakeTransport>(
        std::vector<FakeTransport::Reply>{sse(kToolTurn), sse(kAnswerTurn)});
    auto* raw = transport.get();

    AnthropicProvider::Options provider_options;
    provider_options.backend_name = "claude";
    provider_options.api_key = "sk-ant-test";
    provider_options.base_url = "https://api.test";

    Harness harness{Config{}};
    harness.register_provider("claude", std::make_shared<AnthropicProvider>(
                                            std::move(provider_options),
                                            std::make_unique<HttpClient>(std::move(transport))));
    harness.use_default_router();

    std::string tool_saw;
    ToolRegistry registry;
    Tool lookup;
    lookup.name = "lookup";
    lookup.description = "Looks something up";
    lookup.run = [&tool_saw](std::string_view arguments) {
        tool_saw = std::string{arguments};
        return ToolOutcome{"42", false};
    };
    registry.add(std::move(lookup));

    std::vector<ChatMessage> history{ChatMessage::user("what is the answer?")};
    Options options;
    options.model = "claude";
    options.tools = &registry;

    const RunResult result = apogee::agentloop::run(harness, history, options);

    // The tool ran with the arguments the model streamed as JSON fragments.
    CHECK(json::parse(tool_saw).at("key") == "answer");
    CHECK(result.answer == "The answer is 42");
    CHECK(result.iterations == 2);
    // Usage from both turns, exact (the fixtures report it).
    CHECK(result.tokens.tokens == 20 + 15 + 60 + 9);
    CHECK_FALSE(result.tokens.estimated);

    // History is the full exchange, linked correctly.
    REQUIRE(history.size() == 4);
    CHECK(history[1].role == Role::Assistant);
    REQUIRE(history[1].tool_calls.size() == 1);
    CHECK(history[1].tool_calls[0].id == "toolu_1");
    CHECK(history[2].role == Role::Tool);
    CHECK(history[2].tool_call_id == "toolu_1");
    CHECK(history[2].content.plain_text() == "42");
    CHECK(history[3].content.plain_text() == "The answer is 42");

    // The second request carried the tool definition AND the tool result in the
    // shape Anthropic requires: a tool_result block inside a USER turn, since
    // Anthropic has no tool role.
    REQUIRE(raw->requests().size() == 2);
    const json second = json::parse(raw->requests()[1].body);
    CHECK(second.at("tools")[0].at("name") == "lookup");

    bool found_tool_result = false;
    for (const auto& message : second.at("messages")) {
        if (message.at("role") != "user" || !message.at("content").is_array()) {
            continue;
        }
        for (const auto& block : message.at("content")) {
            if (block.value("type", std::string{}) == "tool_result") {
                found_tool_result = true;
                CHECK(block.at("tool_use_id") == "toolu_1");
                CHECK(block.at("content") == "42");
            }
        }
    }
    CHECK(found_tool_result);
}

TEST_CASE("ask_user reaches Anthropic as a normal tool definition", "[agentloop][anthropic][ask]") {
    // Apogee owns the loop on every provider, so ask_user works uniformly --
    // it is just a tool in the request, with no vendor special case anywhere.
    auto transport =
        std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{sse(kAnswerTurn)});
    auto* raw = transport.get();

    AnthropicProvider::Options provider_options;
    provider_options.backend_name = "claude";
    provider_options.api_key = "sk-ant-test";
    provider_options.base_url = "https://api.test";

    Harness harness{Config{}};
    harness.register_provider("claude", std::make_shared<AnthropicProvider>(
                                            std::move(provider_options),
                                            std::make_unique<HttpClient>(std::move(transport))));
    harness.use_default_router();

    std::vector<ChatMessage> history{ChatMessage::user("hi")};
    Options options;
    options.model = "claude";
    options.ask = [](const apogee::agentloop::QuestionRequest&) {
        return apogee::agentloop::Answers{};
    };

    (void)apogee::agentloop::run(harness, history, options);

    const json body = json::parse(raw->requests()[0].body);
    REQUIRE(body.contains("tools"));
    bool advertised = false;
    for (const auto& tool : body.at("tools")) {
        if (tool.at("name") == "ask_user") {
            advertised = true;
            // Anthropic names it input_schema, not parameters.
            CHECK(tool.contains("input_schema"));
        }
    }
    CHECK(advertised);
}
