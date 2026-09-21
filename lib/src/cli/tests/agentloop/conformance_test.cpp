#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "backends/anthropic.h"
#include "backends/anthropic_wire.h"
#include "backends/google.h"
#include "backends/google_wire.h"
#include "backends/http_client.h"
#include "backends/mock.h"
#include "backends/openai.h"
#include "backends/openai_wire.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "support/fake_transport.h"

/// **The cross-provider conformance table.**
///
/// One set of behaviours, asserted identically against every real provider. It
/// is the regression net the openai-google-backends item asks for by name, and
/// it is what makes the `LLMProvider` seam a claim rather than a hope: if the
/// IR or a translator changes and one vendor drifts, exactly one provider goes
/// red here and the rest stay green.
///
/// Each provider supplies the SAME two turns as recorded fixtures — a tool call
/// followed by a final answer — in its own wire dialect. Everything below the
/// fixture is shared: the same loop, the same tool registry, the same
/// assertions. Adding a provider means adding a row, not a test file.
namespace {

using apogee::agent::Tool;
using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::backends::HttpClient;
using apogee::harness::ChatMessage;
using apogee::harness::Harness;
using apogee::harness::LLMProvider;
using apogee::harness::Role;
using apogee::testing::FakeTransport;

/// Builds a provider already primed with two scripted turns.
using ProviderFactory = std::function<std::shared_ptr<LLMProvider>()>;

struct ProviderCase {
    std::string name;
    ProviderFactory make;
};

std::unique_ptr<HttpClient> client_for(std::vector<FakeTransport::Reply> replies) {
    return std::make_unique<HttpClient>(std::make_unique<FakeTransport>(std::move(replies)));
}

// --- Anthropic ---------------------------------------------------------------

constexpr std::string_view kAnthropicToolTurn =
    R"(event: message_start
data: {"type":"message_start","message":{"model":"claude-sonnet-5","usage":{"input_tokens":10}}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_1","name":"lookup"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"key\":\"answer\"}"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":5}}

event: message_stop
data: {"type":"message_stop"}

)";

constexpr std::string_view kAnthropicAnswerTurn =
    R"(event: message_start
data: {"type":"message_start","message":{"model":"claude-sonnet-5","usage":{"input_tokens":20}}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"The answer is 42"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":8}}

event: message_stop
data: {"type":"message_stop"}

)";

// --- OpenAI ------------------------------------------------------------------

constexpr std::string_view kOpenAIToolTurn =
    R"(data: {"type":"response.output_item.added","output_index":0,"item":{"type":"function_call","id":"fc_1","call_id":"toolu_1","name":"lookup","arguments":""}}

data: {"type":"response.function_call_arguments.delta","output_index":0,"delta":"{\"key\":\"answer\"}"}

data: {"type":"response.completed","response":{"model":"gpt-5","status":"completed","usage":{"input_tokens":10,"output_tokens":5}}}

)";

constexpr std::string_view kOpenAIAnswerTurn =
    R"(data: {"type":"response.output_text.delta","delta":"The answer is 42"}

data: {"type":"response.completed","response":{"model":"gpt-5","status":"completed","usage":{"input_tokens":20,"output_tokens":8}}}

)";

// --- Google ------------------------------------------------------------------

constexpr std::string_view kGoogleToolTurn =
    R"(data: {"candidates":[{"content":{"parts":[{"functionCall":{"name":"lookup","args":{"key":"answer"}}}],"role":"model"}}],"modelVersion":"gemini-2.5-pro"}

data: {"candidates":[{"content":{"parts":[]},"finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":10,"candidatesTokenCount":5}}

)";

constexpr std::string_view kGoogleAnswerTurn =
    R"(data: {"candidates":[{"content":{"parts":[{"text":"The answer is 42"}],"role":"model"}}],"modelVersion":"gemini-2.5-pro"}

data: {"candidates":[{"content":{"parts":[]},"finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":20,"candidatesTokenCount":8}}

)";

std::vector<ProviderCase> providers() {
    std::vector<ProviderCase> cases;

    cases.push_back(
        {"mock", [] {
             apogee::backends::MockProvider::Options options;
             options.backend_name = "mock";
             options.turns = {
                 apogee::backends::MockTurn{
                     "",
                     {apogee::harness::ToolCall{"toolu_1", "lookup", R"({"key":"answer"})"}},
                     apogee::harness::FinishReason::ToolCalls,
                     {}},
                 apogee::backends::MockTurn{
                     "The answer is 42", {}, apogee::harness::FinishReason::Stop, {}},
             };
             // The mock reports no usage, so the loop must fall back to estimating.
             return std::make_shared<apogee::backends::MockProvider>(std::move(options));
         }});

    cases.push_back(
        {"anthropic", [] {
             apogee::backends::AnthropicProvider::Options options;
             options.backend_name = "anthropic";
             options.api_key = "sk-ant-test";
             options.base_url = "https://api.test";
             return std::make_shared<apogee::backends::AnthropicProvider>(
                 std::move(options),
                 client_for({{.status = 200, .body = std::string{kAnthropicToolTurn}},
                             {.status = 200, .body = std::string{kAnthropicAnswerTurn}}}));
         }});

    cases.push_back({"openai", [] {
                         apogee::backends::OpenAIProvider::Options options;
                         options.backend_name = "openai";
                         options.api_key = "sk-test";
                         options.base_url = "https://api.test";
                         return std::make_shared<apogee::backends::OpenAIProvider>(
                             std::move(options),
                             client_for({{.status = 200, .body = std::string{kOpenAIToolTurn}},
                                         {.status = 200, .body = std::string{kOpenAIAnswerTurn}}}));
                     }});

    cases.push_back({"google", [] {
                         apogee::backends::GoogleProvider::Options options;
                         options.backend_name = "google";
                         options.api_key = "AIza-test";
                         options.base_url = "https://gen.test";
                         return std::make_shared<apogee::backends::GoogleProvider>(
                             std::move(options),
                             client_for({{.status = 200, .body = std::string{kGoogleToolTurn}},
                                         {.status = 200, .body = std::string{kGoogleAnswerTurn}}}));
                     }});

    return cases;
}

}  // namespace

TEST_CASE("every provider drives the loop identically", "[conformance]") {
    // The item's central claim, asserted rather than assumed: a provider is an
    // ordinary LLMProvider, and the loop has no special case for any of them.
    for (const ProviderCase& provider : providers()) {
        INFO("provider: " << provider.name);

        ToolRegistry registry;
        Tool lookup;
        lookup.name = "lookup";
        lookup.description = "Looks something up";
        std::string tool_saw;
        lookup.run = [&tool_saw](std::string_view arguments) {
            tool_saw = std::string{arguments};
            return ToolOutcome{"42", false};
        };
        registry.add(std::move(lookup));

        Harness harness{apogee::harness::Config{}};
        harness.register_provider(provider.name, provider.make());
        harness.use_default_router();

        std::vector<ChatMessage> history{ChatMessage::user("what is the answer?")};
        apogee::agentloop::Options options;
        options.model = provider.name;
        options.tools = &registry;

        std::string streamed;

        struct Collector final : apogee::agentloop::Reporter {
            std::string* out;

            explicit Collector(std::string* target) : out{target} {}

            void on_answer_token(std::string_view chunk) override {
                *out += chunk;
            }
        } reporter{&streamed};

        const apogee::agentloop::RunResult result =
            apogee::agentloop::run(harness, history, options, reporter);

        // 1. The tool ran, with the arguments the model asked for.
        CHECK(nlohmann::json::parse(tool_saw).at("key") == "answer");

        // 2. Two model calls: tool turn, then answer turn.
        CHECK(result.iterations == 2);
        CHECK_FALSE(result.hit_iteration_limit);

        // 3. The final answer is identical across every dialect.
        CHECK(result.answer == "The answer is 42");
        CHECK(streamed == "The answer is 42");

        // 4. History has the same SHAPE everywhere: user, assistant+call,
        //    tool result, assistant answer -- linked by the same id.
        REQUIRE(history.size() == 4);
        CHECK(history[0].role == Role::User);
        CHECK(history[1].role == Role::Assistant);
        REQUIRE(history[1].tool_calls.size() == 1);
        CHECK(history[1].tool_calls[0].name == "lookup");
        CHECK(history[2].role == Role::Tool);
        CHECK(history[2].tool_call_id == history[1].tool_calls[0].id);
        CHECK(history[2].content.plain_text() == "42");
        CHECK(history[3].role == Role::Assistant);
        CHECK(history[3].content.plain_text() == "The answer is 42");

        // 5. No thinking anywhere in persisted history, on any provider.
        for (const ChatMessage& message : history) {
            CHECK(message.content.plain_text().find("deliberat") == std::string::npos);
        }
    }
}

TEST_CASE("every real provider reports exact usage", "[conformance]") {
    // The mock is excluded deliberately: it reports none, which is what makes
    // the loop's estimate fallback testable. Every provider that DOES report
    // must be believed rather than estimated over.
    for (const ProviderCase& provider : providers()) {
        if (provider.name == "mock") {
            continue;
        }
        INFO("provider: " << provider.name);

        ToolRegistry registry;
        Tool lookup;
        lookup.name = "lookup";
        lookup.description = "d";
        lookup.run = [](std::string_view) { return ToolOutcome{"42", false}; };
        registry.add(std::move(lookup));

        Harness harness{apogee::harness::Config{}};
        harness.register_provider(provider.name, provider.make());
        harness.use_default_router();

        std::vector<ChatMessage> history{ChatMessage::user("q")};
        apogee::agentloop::Options options;
        options.model = provider.name;
        options.tools = &registry;

        const auto result = apogee::agentloop::run(harness, history, options);

        // 10 + 5 from the tool turn, 20 + 8 from the answer turn.
        CHECK(result.tokens.tokens == 43);
        CHECK_FALSE(result.tokens.estimated);
    }
}

TEST_CASE("a provider reporting no usage falls back to an estimate", "[conformance]") {
    Harness harness{apogee::harness::Config{}};
    harness.register_provider("mock", providers().front().make());
    harness.use_default_router();

    ToolRegistry registry;
    Tool lookup;
    lookup.name = "lookup";
    lookup.description = "d";
    lookup.run = [](std::string_view) { return ToolOutcome{"42", false}; };
    registry.add(std::move(lookup));

    std::vector<ChatMessage> history{ChatMessage::user("q")};
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &registry;

    const auto result = apogee::agentloop::run(harness, history, options);
    // An unreported count must not silently read as an exact zero.
    CHECK(result.tokens.estimated);
}

TEST_CASE("ask_user is advertised uniformly across every provider", "[conformance]") {
    // Apogee owns the loop everywhere, so this needs no vendor special case --
    // the divergence from Ommi, asserted.
    for (const ProviderCase& provider : providers()) {
        INFO("provider: " << provider.name);

        apogee::agentloop::Options options;
        options.model = provider.name;
        options.ask = [](const apogee::agentloop::QuestionRequest&) {
            return apogee::agentloop::Answers{};
        };

        const auto tools = apogee::agentloop::advertised_tools(options);
        REQUIRE(tools.size() == 1);
        CHECK(tools[0].name == apogee::agentloop::kQuestionToolName);
    }
}

TEST_CASE("a mid-session /model switch carries the whole history", "[conformance]") {
    // `/model` sets session.backend and nothing else -- the claim being that
    // history is neutral IR, so a transcript begun on one vendor continues on
    // another. That only holds if EVERY translator renders a history it did not
    // produce, including the tool call/result pair, which is where the dialects
    // differ most: Anthropic uses tool_use/tool_result blocks, OpenAI a
    // top-level function_call plus function_call_output, Gemini a
    // functionCall part answered by a functionResponse in a *user* turn.
    //
    // So: build one transcript, hand it to all three, and require every turn to
    // survive. A translator that silently drops foreign turns loses the user's
    // conversation on switch -- the failure this test exists to catch.
    apogee::harness::ChatRequest request;
    request.messages = {
        ChatMessage::system("be brief"),
        ChatMessage::user("what is the answer?"),
        [] {
            ChatMessage assistant = ChatMessage::assistant("let me look");
            assistant.tool_calls = {
                apogee::harness::ToolCall{"toolu_1", "lookup", R"({"key":"answer"})"}};
            return assistant;
        }(),
        // A value that appears nowhere else: asserting on "42" would also
        // match the assistant's "The answer is 42" and pass on a translator
        // that drops the tool result entirely.
        ChatMessage::from_tool_result(
            apogee::harness::ToolResult{"toolu_1", "lookup", "ANSWER-FROM-TOOL", false}),
        ChatMessage::assistant("The answer is 42"),
        ChatMessage::user("are you sure?"),
    };

    const std::string dump_anthropic =
        apogee::backends::anthropic::build_request(request, {}, {}).dump();
    const std::string dump_openai = apogee::backends::openai::build_request(request, {}).dump();
    const std::string dump_google = apogee::backends::google::build_request(request, {}).dump();

    for (const auto& [name, dump] :
         {std::pair{"anthropic", dump_anthropic}, std::pair{"openai", dump_openai},
          std::pair{"google", dump_google}}) {
        INFO("continuing on: " << name);

        // Every user and assistant turn reaches the new provider...
        CHECK(dump.find("what is the answer?") != std::string::npos);
        CHECK(dump.find("let me look") != std::string::npos);
        CHECK(dump.find("The answer is 42") != std::string::npos);
        CHECK(dump.find("are you sure?") != std::string::npos);

        // ...and so does the tool exchange, in whatever shape the vendor
        // wants: the call, its arguments, and the result it produced.
        CHECK(dump.find("lookup") != std::string::npos);
        CHECK(dump.find("answer") != std::string::npos);
        CHECK(dump.find("ANSWER-FROM-TOOL") != std::string::npos);

        // The system prompt survives too, though all three park it elsewhere.
        CHECK(dump.find("be brief") != std::string::npos);
    }
}
