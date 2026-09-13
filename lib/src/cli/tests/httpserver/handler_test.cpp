#include "httpserver/handler.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agent/tool.h"
#include "backends/mock.h"
#include "commands/embed.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "httpserver/mux.h"
#include "logger/session.h"
#include "support/env_guard.h"

/// The conformance suite: every route, driven through the listener-free mux.
///
/// No socket, no thread pool, no timing. A test hands the mux an
/// `HttpRequest`, reads the `HttpResponse`, and for a streamed one runs the
/// body inline and collects the frames -- which is how a streaming turn can be
/// asserted frame by frame. The requests are shaped the way a stock OpenAI
/// client library sends them, and the assertions are on the fields such a
/// client reads back.
namespace {

using apogee::agent::Tool;
using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using apogee::harness::Config;
using apogee::harness::Harness;
using apogee::harness::Role;
using apogee::harness::ToolCall;
using apogee::httpserver::Handler;
using apogee::httpserver::HandlerOptions;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;
using apogee::httpserver::Mux;

constexpr std::string_view kConfig = R"(
models:
  default: mock
backends:
  mock:
    type: mock
    model: mock-1
  second:
    type: mock
    model: mock-2
  tiny:
    type: mock
    model: mock-tiny
    context_size: 60
  claude-sub:
    type: claude-cli
  local:
    type: llamacpp
    model_path: /nonexistent.gguf
)";

MockTurn text_turn(std::string text, apogee::harness::Usage usage = {}) {
    return MockTurn{std::move(text), {}, apogee::harness::FinishReason::Stop, usage};
}

MockTurn tool_turn(std::vector<ToolCall> calls) {
    return MockTurn{"", std::move(calls), apogee::harness::FinishReason::ToolCalls, {}};
}

Tool echo_tool() {
    Tool tool;
    tool.name = "echo";
    tool.description = "echoes its arguments";
    tool.run = [](std::string_view arguments) {
        return ToolOutcome{std::string{arguments}, false};
    };
    return tool;
}

HandlerOptions served_default() {
    HandlerOptions options;
    options.served = {"mock", "second", "tiny"};
    options.default_backend = "mock";
    options.unavailable["local"] = "llama.cpp is not built in";
    return options;
}

/// A provider that streams exactly the pieces it is given -- empty ones
/// included -- and can fail part way. The mock cannot emit an empty piece or
/// throw, and both are what the server's streaming paths must survive.
class ScriptedStreamProvider final : public apogee::harness::LLMProvider {
public:
    std::vector<std::string> pieces;
    /// When non-negative, throws a ProviderError after emitting this many.
    int fail_after = -1;

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "mock";
    }

    [[nodiscard]] apogee::harness::ChatResponse chat(
        const apogee::harness::ChatRequest& request,
        const apogee::harness::CancellationToken& cancellation) override {
        apogee::harness::StreamOptions options;
        options.cancellation = cancellation;
        return stream_chat(request, options);
    }

    [[nodiscard]] apogee::harness::ChatResponse stream_chat(
        const apogee::harness::ChatRequest& /*request*/,
        const apogee::harness::StreamOptions& options) override {
        std::string full;
        int emitted = 0;
        for (const std::string& piece : pieces) {
            if (fail_after >= 0 && emitted == fail_after) {
                throw apogee::harness::ProviderError("mock", "the upstream fell over");
            }
            if (options.on_token) {
                options.on_token(piece);
            }
            full += piece;
            ++emitted;
        }
        apogee::harness::ChatResponse response;
        response.message = ChatMessage::assistant(full);
        response.model = "mock-1";
        return response;
    }

    [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
        const apogee::harness::CancellationToken& /*cancellation*/) override {
        return {};
    }
};

struct Fixture {
    // First, so every session file lands in a throwaway home.
    // The label carries a random suffix: ctest may run two of these test
    // processes at once, and the support TempDir's counter is per-process.
    apogee::testing::TempDir home{"serve-handler-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};

    std::shared_ptr<MockProvider> provider;
    std::shared_ptr<MockProvider> second;
    std::shared_ptr<MockProvider> tiny;
    std::unique_ptr<Harness> harness;
    ToolRegistry registry;
    std::unique_ptr<Handler> handler;
    std::unique_ptr<Mux> mux;

    explicit Fixture(std::vector<MockTurn> turns = {text_turn("mock response")},
                     HandlerOptions options = served_default(), bool with_tools = false,
                     std::shared_ptr<apogee::harness::LLMProvider> custom = nullptr) {
        MockProvider::Options primary;
        primary.backend_name = "mock";
        primary.turns = std::move(turns);
        provider = std::make_shared<MockProvider>(std::move(primary));

        MockProvider::Options other;
        other.backend_name = "second";
        other.model = "mock-2";
        other.turns = {text_turn("second response")};
        second = std::make_shared<MockProvider>(std::move(other));

        MockProvider::Options small;
        small.backend_name = "tiny";
        small.model = "mock-tiny";
        small.turns = {text_turn("mock response")};
        tiny = std::make_shared<MockProvider>(std::move(small));

        MockProvider::Options subscription;
        subscription.backend_name = "claude-sub";

        harness = std::make_unique<Harness>(apogee::harness::parse_config(kConfig, "<test>"));
        harness->register_provider("mock", custom != nullptr ? custom : provider);
        harness->register_provider("second", second);
        harness->register_provider("tiny", tiny);
        harness->register_provider("claude-sub",
                                   std::make_shared<MockProvider>(std::move(subscription)));
        harness->use_default_router();

        if (with_tools) {
            registry.add(echo_tool());
        }
        handler = std::make_unique<Handler>(*harness, std::move(options),
                                            with_tools ? &registry : nullptr);
        mux = std::make_unique<Mux>(*handler);
    }

    [[nodiscard]] HttpResponse send(const HttpRequest& request) const {
        return mux->dispatch(request);
    }
};

HttpRequest post(std::string path, const nlohmann::json& body,
                 std::map<std::string, std::string> query = {}) {
    HttpRequest request;
    request.method = "POST";
    request.path = std::move(path);
    request.body = body.dump();
    request.query = std::move(query);
    return request;
}

HttpRequest get(std::string path, std::map<std::string, std::string> query = {}) {
    HttpRequest request;
    request.method = "GET";
    request.path = std::move(path);
    request.query = std::move(query);
    return request;
}

HttpRequest del(std::string path) {
    HttpRequest request;
    request.method = "DELETE";
    request.path = std::move(path);
    return request;
}

nlohmann::json chat_body(std::string text) {
    return nlohmann::json{{"messages", nlohmann::json::array({nlohmann::json{
                                           {"role", "user"}, {"content", std::move(text)}}})}};
}

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

/// Runs a streamed body inline and returns every byte it wrote.
std::string collect(const HttpResponse& response) {
    REQUIRE(response.streamed());
    std::string out;
    response.stream([&out](std::string_view piece) {
        out += piece;
        return true;
    });
    return out;
}

struct Frames {
    std::vector<nlohmann::json> data;
    bool done = false;
    bool done_last = false;
};

/// Splits an SSE body into its `data:` payloads, insisting on the framing.
Frames frames(const std::string& sse) {
    Frames out;
    std::size_t start = 0;
    while (start < sse.size()) {
        const std::size_t end = sse.find("\n\n", start);
        REQUIRE(end != std::string::npos);  // every frame ends with a blank line
        const std::string block = sse.substr(start, end - start);
        start = end + 2;
        REQUIRE(block.rfind("data: ", 0) == 0);
        const std::string payload = block.substr(6);
        if (payload == "[DONE]") {
            out.done = true;
            out.done_last = start >= sse.size();
            continue;
        }
        REQUIRE_FALSE(out.done);  // nothing follows [DONE]
        const nlohmann::json json = nlohmann::json::parse(payload, nullptr, false);
        REQUIRE_FALSE(json.is_discarded());
        out.data.push_back(json);
    }
    return out;
}

std::string content_of(const Frames& parsed_frames) {
    std::string text;
    for (const nlohmann::json& frame : parsed_frames.data) {
        if (!frame.contains("choices")) {
            continue;
        }
        const nlohmann::json& delta = frame["choices"][0]["delta"];
        if (delta.contains("content") && delta["content"].is_string()) {
            text += delta["content"].get<std::string>();
        }
    }
    return text;
}

std::vector<nlohmann::json> metas(const Frames& parsed_frames) {
    std::vector<nlohmann::json> out;
    for (const nlohmann::json& frame : parsed_frames.data) {
        if (frame.contains("meta")) {
            out.push_back(frame);
        }
    }
    return out;
}

bool has_meta(const std::vector<nlohmann::json>& meta_frames, std::string_view type,
              std::string_view phase = {}) {
    for (const nlohmann::json& frame : meta_frames) {
        if (frame["meta"]["type"] == type && (phase.empty() || frame["meta"]["phase"] == phase)) {
            return true;
        }
    }
    return false;
}

constexpr std::string_view kSecret = "the zarquon protocol requires seventeen widgets";

}  // namespace

// ---------------------------------------------------------------------------
// The OpenAI shape
// ---------------------------------------------------------------------------

TEST_CASE("a non-streamed chat completion has the shape a stock client reads",
          "[httpserver][chat]") {
    const Fixture fixture;
    const HttpResponse response = fixture.send(post("/v1/chat/completions", chat_body("hello")));

    REQUIRE(response.status == 200);
    CHECK(response.content_type == "application/json");
    const nlohmann::json body = parsed(response);
    CHECK(body["object"] == "chat.completion");
    CHECK(body["id"].get<std::string>().rfind("chatcmpl-", 0) == 0);
    CHECK(body["created"].get<std::int64_t>() > 0);
    CHECK(body["model"] == "mock");
    REQUIRE(body["choices"].size() == 1);
    CHECK(body["choices"][0]["index"] == 0);
    CHECK(body["choices"][0]["message"]["role"] == "assistant");
    CHECK(body["choices"][0]["message"]["content"] == "mock response");
    CHECK(body["choices"][0]["finish_reason"] == "stop");
    // The mock reported no usage, so there is none: absent is not zero.
    CHECK_FALSE(body.contains("usage"));
    // Stateless: no session was asked for, so none exists.
    CHECK_FALSE(body.contains("session_id"));
    CHECK(response.headers.find("X-Apogee-Session-Id") == response.headers.end());
    CHECK(fixture.handler->sessions().size() == 0);
}

TEST_CASE("usage is reported exactly when the provider reported it", "[httpserver][chat]") {
    apogee::harness::Usage usage;
    usage.prompt_tokens = 12;
    usage.completion_tokens = 3;
    const Fixture fixture{{text_turn("mock response", usage)}};

    const nlohmann::json body =
        parsed(fixture.send(post("/v1/chat/completions", chat_body("hello"))));
    REQUIRE(body.contains("usage"));
    CHECK(body["usage"]["prompt_tokens"] == 12);
    CHECK(body["usage"]["completion_tokens"] == 3);
    CHECK(body["usage"]["total_tokens"] == 15);
}

TEST_CASE("a truncated answer says length, in both shapes", "[httpserver][chat]") {
    // Found live: a reasoning model that spends its whole budget thinking
    // returns an empty answer, and a server that calls that `stop` tells the
    // client nothing went wrong. `length` is the word a stock client acts on.
    const Fixture fixture{{MockTurn{"cut sh", {}, apogee::harness::FinishReason::Length, {}}}};

    const nlohmann::json body = parsed(fixture.send(post("/v1/chat/completions", chat_body("go"))));
    CHECK(body["choices"][0]["finish_reason"] == "length");

    nlohmann::json streamed = chat_body("go");
    streamed["stream"] = true;
    const Frames parsed_frames =
        frames(collect(fixture.send(post("/v1/chat/completions", streamed))));
    CHECK(parsed_frames.data.back()["choices"][0]["finish_reason"] == "length");
    CHECK(parsed(fixture.send(
              post("/v1/completions",
                   nlohmann::json{{"prompt", "go"}})))["choices"][0]["finish_reason"] == "length");
}

TEST_CASE("a streamed chat completion frames chunks and ends with [DONE]", "[httpserver][sse]") {
    const Fixture fixture;
    nlohmann::json body = chat_body("hello");
    body["stream"] = true;
    const HttpResponse response = fixture.send(post("/v1/chat/completions", body));

    REQUIRE(response.status == 200);
    CHECK(response.content_type == "text/event-stream");
    const Frames parsed_frames = frames(collect(response));

    CHECK(parsed_frames.done);
    CHECK(parsed_frames.done_last);
    REQUIRE(parsed_frames.data.size() >= 3);
    // The first chunk names the role; the last carries the finish reason with
    // an empty delta; the ones between carry the answer, split as the
    // provider streamed it.
    CHECK(parsed_frames.data.front()["choices"][0]["delta"]["role"] == "assistant");
    CHECK(parsed_frames.data.front()["choices"][0]["finish_reason"].is_null());
    CHECK(parsed_frames.data.back()["choices"][0]["finish_reason"] == "stop");
    CHECK(parsed_frames.data.back()["choices"][0]["delta"].empty());
    for (const nlohmann::json& frame : parsed_frames.data) {
        CHECK(frame["object"] == "chat.completion.chunk");
        CHECK(frame["id"] == parsed_frames.data.front()["id"]);
    }
    CHECK(content_of(parsed_frames) == "mock response");
    // Nothing asked for events, so nothing carries a meta field.
    CHECK(metas(parsed_frames).empty());
}

TEST_CASE("stream false never produces SSE", "[httpserver][sse]") {
    const Fixture fixture;
    nlohmann::json body = chat_body("hello");
    body["stream"] = false;
    const HttpResponse response = fixture.send(post("/v1/chat/completions", body));
    CHECK_FALSE(response.streamed());
    CHECK(parsed(response)["object"] == "chat.completion");
}

TEST_CASE("meta-frames appear only when asked for, always on an empty delta",
          "[httpserver][sse][events]") {
    const Fixture fixture;
    nlohmann::json body = chat_body("hello");
    body["stream"] = true;
    body["apogee_events"] = true;
    const Frames parsed_frames = frames(collect(fixture.send(post("/v1/chat/completions", body))));

    const std::vector<nlohmann::json> meta_frames = metas(parsed_frames);
    REQUIRE_FALSE(meta_frames.empty());
    CHECK(has_meta(meta_frames, "thinking", "start"));
    CHECK(has_meta(meta_frames, "token_count"));
    for (const nlohmann::json& frame : meta_frames) {
        // THE contract: a meta-frame contributes nothing to the answer a
        // spec-compliant client concatenates.
        CHECK(frame["choices"][0]["delta"]["content"] == "");
        CHECK(frame["object"] == "chat.completion.chunk");
        CHECK(frame["meta"].contains("type"));
        CHECK(frame["meta"].contains("phase"));
    }
    // And the answer is still whole.
    CHECK(content_of(parsed_frames) == "mock response");
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

TEST_CASE("tool calls run server-side and the client sees only the final answer",
          "[httpserver][tools]") {
    ToolCall call;
    call.id = "call_1";
    call.name = "echo";
    call.arguments = R"({"text":"hi"})";
    const Fixture fixture{{tool_turn({call}), text_turn("after the tool")}, served_default(), true};

    SECTION("non-streamed") {
        const HttpResponse response =
            fixture.send(post("/v1/chat/completions", chat_body("use the tool")));
        REQUIRE(response.status == 200);
        const nlohmann::json body = parsed(response);
        CHECK(body["choices"][0]["message"]["content"] == "after the tool");
        CHECK_FALSE(body["choices"][0]["message"].contains("tool_calls"));
        REQUIRE(body.contains("apogee_tool_calls"));
        CHECK(body["apogee_tool_calls"] == nlohmann::json::array({"echo"}));
        CHECK(response.headers.at("X-Apogee-Tools-Used") == "echo");
        // The loop ran twice: the tool turn, then the answer.
        CHECK(fixture.provider->turn_count() == 2);
    }

    SECTION("streamed, with events") {
        nlohmann::json body = chat_body("use the tool");
        body["stream"] = true;
        body["apogee_events"] = true;
        const Frames parsed_frames =
            frames(collect(fixture.send(post("/v1/chat/completions", body))));
        CHECK(content_of(parsed_frames) == "after the tool");
        const std::vector<nlohmann::json> meta_frames = metas(parsed_frames);
        CHECK(has_meta(meta_frames, "tool_call", "start"));
        CHECK(has_meta(meta_frames, "tool_call", "done"));
        for (const nlohmann::json& frame : meta_frames) {
            if (frame["meta"]["type"] == "tool_call") {
                CHECK(frame["meta"]["name"] == "echo");
            }
        }
        CHECK(parsed_frames.data.back()["apogee_tool_calls"] == nlohmann::json::array({"echo"}));
    }
}

TEST_CASE("tool_mode selects the tools a request may use", "[httpserver][tools]") {
    const Fixture fixture{{text_turn("mock response")}, served_default(), true};

    nlohmann::json all = chat_body("hi");
    all["tool_mode"] = "all";
    CHECK(fixture.send(post("/v1/chat/completions", all)).status == 200);
    REQUIRE_FALSE(fixture.provider->requests().empty());
    CHECK(fixture.provider->requests().back().tools.size() == 1);
    CHECK(fixture.provider->requests().back().tools.front().name == "echo");

    nlohmann::json none = chat_body("hi");
    none["tool_mode"] = "none";
    CHECK(fixture.send(post("/v1/chat/completions", none)).status == 200);
    CHECK(fixture.provider->requests().back().tools.empty());

    nlohmann::json bogus = chat_body("hi");
    bogus["tool_mode"] = "everything";
    const HttpResponse refused = fixture.send(post("/v1/chat/completions", bogus));
    CHECK(refused.status == 400);
    CHECK(parsed(refused)["error"]["message"].get<std::string>().find("tool_mode") !=
          std::string::npos);
}

TEST_CASE("client-side tools are refused rather than silently ignored", "[httpserver][tools]") {
    const Fixture fixture;
    nlohmann::json body = chat_body("hi");
    body["tools"] = nlohmann::json::array({nlohmann::json{
        {"type", "function"}, {"function", {{"name", "mine"}, {"parameters", {}}}}}});
    const HttpResponse response = fixture.send(post("/v1/chat/completions", body));
    CHECK(response.status == 400);
    CHECK(parsed(response)["error"]["message"].get<std::string>().find("client-side tools") !=
          std::string::npos);
    // An empty list is what some clients send by default, and means nothing.
    body["tools"] = nlohmann::json::array();
    CHECK(fixture.send(post("/v1/chat/completions", body)).status == 200);
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

TEST_CASE("every refusal takes the OpenAI error shape", "[httpserver][errors]") {
    const Fixture fixture;

    HttpRequest garbage;
    garbage.method = "POST";
    garbage.path = "/v1/chat/completions";
    garbage.body = "not json";
    const HttpResponse not_json = fixture.send(garbage);
    CHECK(not_json.status == 400);
    const nlohmann::json envelope = parsed(not_json);
    CHECK(envelope["error"]["type"] == "invalid_request_error");
    CHECK(envelope["error"]["message"].is_string());

    const HttpResponse empty = fixture.send(post("/v1/chat/completions", nlohmann::json::object()));
    CHECK(empty.status == 400);
    CHECK(parsed(empty)["error"]["message"].get<std::string>().find("messages") !=
          std::string::npos);

    nlohmann::json bad_role{
        {"messages", nlohmann::json::array({nlohmann::json{{"role", "robot"}, {"content", "x"}}})}};
    const HttpResponse role = fixture.send(post("/v1/chat/completions", bad_role));
    CHECK(role.status == 400);
    CHECK(parsed(role)["error"]["message"].get<std::string>().find("robot") != std::string::npos);
}

TEST_CASE("an unknown model is a 400 naming what is served", "[httpserver][routing]") {
    const Fixture fixture;
    nlohmann::json body = chat_body("hi");
    body["model"] = "sonnnet";
    const HttpResponse response = fixture.send(post("/v1/chat/completions", body));
    CHECK(response.status == 400);
    const std::string message = parsed(response)["error"]["message"];
    CHECK(message.find("sonnnet") != std::string::npos);
    CHECK(message.find("serving: mock") != std::string::npos);
}

TEST_CASE("a vendor-CLI backend is refused by type, even when served and built",
          "[httpserver][routing]") {
    HandlerOptions options = served_default();
    options.served.push_back("claude-sub");
    const Fixture fixture{{text_turn("mock response")}, options};
    nlohmann::json body = chat_body("hi");
    body["model"] = "claude-sub";
    const HttpResponse response = fixture.send(post("/v1/chat/completions", body));
    CHECK(response.status == 400);
    CHECK(parsed(response)["error"]["message"].get<std::string>().find("vendor CLI") !=
          std::string::npos);
}

TEST_CASE("a configured but unbuilt backend is a 503 with its own reason",
          "[httpserver][routing]") {
    const Fixture fixture;
    nlohmann::json body = chat_body("hi");
    body["model"] = "local";
    const HttpResponse response = fixture.send(post("/v1/chat/completions", body));
    CHECK(response.status == 503);
    const nlohmann::json envelope = parsed(response);
    CHECK(envelope["error"]["type"] == "backend_unavailable");
    CHECK(envelope["error"]["message"].get<std::string>().find("llama.cpp is not built in") !=
          std::string::npos);
}

TEST_CASE("a request without a model lands on the served default", "[httpserver][routing]") {
    HandlerOptions options = served_default();
    options.default_backend = "second";
    const Fixture fixture{{text_turn("mock response")}, options};
    const nlohmann::json body = parsed(fixture.send(post("/v1/chat/completions", chat_body("hi"))));
    CHECK(body["choices"][0]["message"]["content"] == "second response");
    CHECK(fixture.second->turn_count() == 1);
    CHECK(fixture.provider->turn_count() == 0);
    // Named by its model id rather than its key, the same entry answers.
    nlohmann::json by_id = chat_body("hi");
    by_id["model"] = "mock-2";
    CHECK(fixture.send(post("/v1/chat/completions", by_id)).status == 200);
    CHECK(fixture.second->turn_count() == 2);
}

TEST_CASE("a provider failure is a 502, or an error frame that still ends the stream",
          "[httpserver][errors]") {
    auto scripted = std::make_shared<ScriptedStreamProvider>();
    scripted->pieces = {"one", "two", "three"};
    scripted->fail_after = 1;
    const Fixture fixture{{}, served_default(), false, scripted};

    const HttpResponse blocking = fixture.send(post("/v1/chat/completions", chat_body("hi")));
    CHECK(blocking.status == 502);
    CHECK(parsed(blocking)["error"]["type"] == "backend_error");

    nlohmann::json body = chat_body("hi");
    body["stream"] = true;
    const Frames parsed_frames = frames(collect(fixture.send(post("/v1/chat/completions", body))));
    CHECK(parsed_frames.done_last);
    bool saw_error = false;
    for (const nlohmann::json& frame : parsed_frames.data) {
        if (frame.contains("error")) {
            saw_error = true;
            CHECK(frame["error"]["type"] == "backend_error");
        }
    }
    CHECK(saw_error);
    CHECK(content_of(parsed_frames) == "one");
}

TEST_CASE("an empty streamed chunk is skipped, never the end of the stream",
          "[httpserver][sse][filter]") {
    // The reasoning and markup filters legitimately reduce a chunk that was
    // entirely framing to nothing. A server that read that as end-of-stream
    // would truncate the answer the moment the model started thinking --
    // continue, not break.
    auto scripted = std::make_shared<ScriptedStreamProvider>();
    scripted->pieces = {"head", "", "", " and", "", " tail"};
    const Fixture fixture{{}, served_default(), false, scripted};

    nlohmann::json body = chat_body("hi");
    body["stream"] = true;
    const Frames parsed_frames = frames(collect(fixture.send(post("/v1/chat/completions", body))));
    CHECK(content_of(parsed_frames) == "head and tail");
    CHECK(parsed_frames.done_last);
    // No content chunk was written for an empty piece: only the role chunk
    // and the finish chunk carry no text.
    int text_chunks = 0;
    for (const nlohmann::json& frame : parsed_frames.data) {
        const nlohmann::json& delta = frame["choices"][0]["delta"];
        if (delta.contains("content") && delta["content"] != "") {
            ++text_chunks;
        }
    }
    CHECK(text_chunks == 3);
}

// ---------------------------------------------------------------------------
// The system shorthand
// ---------------------------------------------------------------------------

TEST_CASE("the system shorthand becomes the leading system message", "[httpserver][chat]") {
    const Fixture fixture;
    nlohmann::json body = chat_body("hi");
    body["system"] = "be terse";
    REQUIRE(fixture.send(post("/v1/chat/completions", body)).status == 200);
    const auto& sent = fixture.provider->requests().back().messages;
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].role == Role::System);
    CHECK(sent[0].content.plain_text() == "be terse");

    // With a system message already leading, the shorthand is merged in front
    // rather than becoming a second one.
    nlohmann::json merged{
        {"system", "be terse"},
        {"messages",
         nlohmann::json::array({nlohmann::json{{"role", "system"}, {"content", "and kind"}},
                                nlohmann::json{{"role", "user"}, {"content", "hi"}}})}};
    REQUIRE(fixture.send(post("/v1/chat/completions", merged)).status == 200);
    const auto& again = fixture.provider->requests().back().messages;
    REQUIRE(again.size() == 2);
    CHECK(again[0].content.plain_text() == "be terse\n\nand kind");
}

// ---------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------

TEST_CASE("a session is minted on request, continued by id, and resumable from disk",
          "[httpserver][session]") {
    const Fixture fixture;

    nlohmann::json first = chat_body("one");
    first["session_id"] = "new";
    const HttpResponse turn_one = fixture.send(post("/v1/chat/completions", first));
    REQUIRE(turn_one.status == 200);
    const std::string id = turn_one.headers.at("X-Apogee-Session-Id");
    REQUIRE_FALSE(id.empty());
    CHECK(parsed(turn_one)["session_id"] == id);
    REQUIRE(fixture.provider->requests().back().messages.size() == 1);

    // Turn two sends ONLY the new message; the provider sees the whole
    // conversation, because the server owns it.
    nlohmann::json second = chat_body("two");
    second["session_id"] = id;
    const HttpResponse turn_two = fixture.send(post("/v1/chat/completions", second));
    REQUIRE(turn_two.status == 200);
    CHECK(turn_two.headers.at("X-Apogee-Session-Id") == id);
    const auto& sent = fixture.provider->requests().back().messages;
    REQUIRE(sent.size() == 3);
    CHECK(sent[0].content.plain_text() == "one");
    CHECK(sent[1].role == Role::Assistant);
    CHECK(sent[1].content.plain_text() == "mock response");
    CHECK(sent[2].content.plain_text() == "two");

    // The session plane reports it...
    const nlohmann::json detail = parsed(fixture.send(get("/v1/sessions/" + id)));
    CHECK(detail["turn_count"] == 2);
    CHECK(detail["messages"].size() == 4);

    // ...and the SAME file `apogee chat --resume <id>` reads holds the same
    // conversation: a served session is a chat session, not a second format.
    const apogee::logger::LoadedSession loaded = apogee::logger::load(id, {});
    CHECK(loaded.session.turns == 2);
    REQUIRE(loaded.session.messages.size() == 4);
    CHECK(loaded.session.messages[3].content.plain_text() == "mock response");
    CHECK(loaded.session.backend == "mock");
}

TEST_CASE("an unknown session id is a typed 404", "[httpserver][session]") {
    const Fixture fixture;
    nlohmann::json body = chat_body("hi");
    body["session_id"] = "does-not-exist";
    const HttpResponse response = fixture.send(post("/v1/chat/completions", body));
    CHECK(response.status == 404);
    const nlohmann::json envelope = parsed(response);
    CHECK(envelope["error"]["type"] == "session_not_found");
    CHECK(envelope["error"]["session_id"] == "does-not-exist");
    // Nothing was dispatched for it.
    CHECK(fixture.provider->turn_count() == 0);
}

TEST_CASE("the session routes list, show, and end a session", "[httpserver][session]") {
    const Fixture fixture;
    CHECK(parsed(fixture.send(get("/v1/sessions")))["data"].empty());

    nlohmann::json body = chat_body("hi");
    body["session_id"] = "new";
    const std::string id =
        fixture.send(post("/v1/chat/completions", body)).headers.at("X-Apogee-Session-Id");

    const nlohmann::json listed = parsed(fixture.send(get("/v1/sessions")));
    REQUIRE(listed["data"].size() == 1);
    CHECK(listed["data"][0]["session_id"] == id);
    CHECK(listed["data"][0]["model"] == "mock");
    CHECK(listed["data"][0]["turn_count"] == 1);
    CHECK(listed["data"][0]["last_active"].get<std::string>().find('T') != std::string::npos);

    const HttpResponse gone = fixture.send(del("/v1/sessions/" + id));
    CHECK(gone.status == 204);
    CHECK(gone.body.empty());
    CHECK(fixture.send(get("/v1/sessions/" + id)).status == 404);
    CHECK(fixture.send(del("/v1/sessions/" + id)).status == 404);
    CHECK(parsed(fixture.send(get("/v1/sessions")))["data"].empty());
    // Ending the live session does not delete the user's transcript.
    CHECK(std::filesystem::exists(apogee::logger::session_path(id)));
}

TEST_CASE("a session near its window is compacted before the turn, a stateless request only warned",
          "[httpserver][session][context]") {
    const Fixture fixture;
    const std::string large(120, 'x');

    nlohmann::json first = chat_body(large);
    first["model"] = "tiny";
    first["session_id"] = "new";
    const HttpResponse turn_one = fixture.send(post("/v1/chat/completions", first));
    REQUIRE(turn_one.status == 200);
    const std::string id = turn_one.headers.at("X-Apogee-Session-Id");
    CHECK(fixture.tiny->turn_count() == 1);

    nlohmann::json second = chat_body(large);
    second["model"] = "tiny";
    second["session_id"] = id;
    second["stream"] = true;
    second["apogee_events"] = true;
    const Frames parsed_frames =
        frames(collect(fixture.send(post("/v1/chat/completions", second))));
    CHECK(content_of(parsed_frames) == "mock response");
    bool compacted = false;
    for (const nlohmann::json& frame : metas(parsed_frames)) {
        if (frame["meta"]["type"] == "context_warning") {
            compacted = frame["meta"]["name"] == "compact";
            CHECK(frame["meta"]["context_size"] == 60);
            CHECK(frame["meta"].contains("used_tokens"));
        }
    }
    CHECK(compacted);
    // The summary call plus the turn: two model calls on turn two.
    CHECK(fixture.tiny->turn_count() == 3);

    const nlohmann::json detail = parsed(fixture.send(get("/v1/sessions/" + id)));
    CHECK(detail["compactions"] == 1);
    REQUIRE_FALSE(detail["messages"].empty());
    // The compacted history opens with the summary as a SYSTEM message.
    CHECK(detail["messages"][0]["role"] == "system");
    CHECK(detail["messages"][0]["content"].get<std::string>().find("summary") != std::string::npos);

    // A stateless request owns its messages: warned, never compacted.
    nlohmann::json stateless = chat_body(large + large);
    stateless["model"] = "tiny";
    stateless["stream"] = true;
    stateless["apogee_events"] = true;
    const Frames warned = frames(collect(fixture.send(post("/v1/chat/completions", stateless))));
    CHECK(has_meta(metas(warned), "context_warning"));
    REQUIRE(fixture.tiny->requests().back().messages.size() == 1);
    CHECK(fixture.tiny->requests().back().messages[0].content.plain_text() == large + large);
}

// ---------------------------------------------------------------------------
// Retrieval
// ---------------------------------------------------------------------------

TEST_CASE("retrieval is injected into the request and never into the transcript",
          "[httpserver][rag]") {
    HandlerOptions options = served_default();
    options.rag_collection = "notes";
    options.rag_source = apogee::commands::RagSource::Flag;
    const Fixture fixture{{text_turn("mock response")}, options};

    // The collection lives where every surface looks for it.
    const std::filesystem::path db = apogee::commands::collection_path("notes");
    std::filesystem::create_directories(db.parent_path());
    {
        apogee::embedstore::Store store{db};
        store.replace_source("notes.md", {std::string{kSecret}, "an unrelated second chunk"});
    }

    nlohmann::json body = chat_body("what does the zarquon protocol require?");
    body["session_id"] = "new";
    body["stream"] = true;
    body["apogee_events"] = true;
    const HttpResponse response = fixture.send(post("/v1/chat/completions", body));
    const std::string id = response.headers.at("X-Apogee-Session-Id");
    const Frames parsed_frames = frames(collect(response));
    CHECK(content_of(parsed_frames) == "mock response");

    // The provider saw the chunk...
    bool provider_saw_secret = false;
    for (const ChatMessage& message : fixture.provider->requests().back().messages) {
        if (message.content.plain_text().find(kSecret) != std::string::npos) {
            provider_saw_secret = true;
        }
    }
    CHECK(provider_saw_secret);
    // ...the client was told, with the retriever named...
    bool announced = false;
    for (const nlohmann::json& frame : metas(parsed_frames)) {
        if (frame["meta"]["type"] == "rag_result") {
            announced = true;
            CHECK(frame["meta"]["collection"] == "notes");
            CHECK(frame["meta"]["retriever"] == "lexical");
            CHECK(frame["meta"]["chunks_found"].get<int>() >= 1);
            CHECK(frame["meta"]["reranked"] == false);
        }
    }
    CHECK(announced);
    CHECK(has_meta(metas(parsed_frames), "rag_search", "start"));
    CHECK(has_meta(metas(parsed_frames), "rag_search", "done"));
    // ...and the transcript never learned it, on either path to it.
    for (const nlohmann::json& message :
         parsed(fixture.send(get("/v1/sessions/" + id)))["messages"]) {
        CHECK(message["content"].get<std::string>().find(kSecret) == std::string::npos);
    }
    for (const ChatMessage& message : apogee::logger::load(id, {}).session.messages) {
        CHECK(message.content.plain_text().find(kSecret) == std::string::npos);
    }

    // The retriever query parameter goes through the one shared validator
    // and the one shared resolver: a typo is refused, and an explicit vector
    // search that cannot run is a hard error rather than a quiet lexical one.
    const HttpResponse typo =
        fixture.send(post("/v1/chat/completions", chat_body("hi"), {{"retriever", "vectr"}}));
    CHECK(typo.status == 400);
    CHECK(parsed(typo)["error"]["message"].get<std::string>().find("retriever") !=
          std::string::npos);
    const HttpResponse impossible =
        fixture.send(post("/v1/chat/completions", chat_body("hi"), {{"retriever", "vector"}}));
    CHECK(impossible.status == 400);
    CHECK(parsed(impossible)["error"]["message"].get<std::string>().find("lexical") !=
          std::string::npos);
}

// ---------------------------------------------------------------------------
// /v1/completions, /v1/models, /v1/model/status, /health
// ---------------------------------------------------------------------------

TEST_CASE("the completions endpoint answers and streams as text_completion",
          "[httpserver][completions]") {
    const Fixture fixture;

    const HttpResponse blocking =
        fixture.send(post("/v1/completions", nlohmann::json{{"prompt", "hello"}}));
    REQUIRE(blocking.status == 200);
    const nlohmann::json body = parsed(blocking);
    CHECK(body["object"] == "text_completion");
    CHECK(body["id"].get<std::string>().rfind("cmpl-", 0) == 0);
    CHECK(body["choices"][0]["text"] == "mock response");
    CHECK(body["choices"][0]["finish_reason"] == "stop");
    CHECK(body["model"] == "mock");

    const Frames parsed_frames = frames(collect(fixture.send(
        post("/v1/completions", nlohmann::json{{"prompt", "hello"}, {"stream", true}}))));
    CHECK(parsed_frames.done_last);
    std::string text;
    for (const nlohmann::json& frame : parsed_frames.data) {
        CHECK(frame["object"] == "text_completion");
        text += frame["choices"][0]["text"].get<std::string>();
    }
    CHECK(text == "mock response");
    CHECK(parsed_frames.data.back()["choices"][0]["finish_reason"] == "stop");

    CHECK(fixture.send(post("/v1/completions", nlohmann::json::object())).status == 400);
    CHECK(fixture
              .send(post("/v1/completions",
                         nlohmann::json{{"prompt", nlohmann::json::array({"a", "b"})}}))
              .status == 400);
}

TEST_CASE("the model list names only served backends and marks the default",
          "[httpserver][models]") {
    HandlerOptions options = served_default();
    options.served = {"mock", "second"};
    const Fixture fixture{{text_turn("mock response")}, options};
    const nlohmann::json body = parsed(fixture.send(get("/v1/models")));
    CHECK(body["object"] == "list");
    REQUIRE(body["data"].size() == 2);
    int defaults = 0;
    for (const nlohmann::json& model : body["data"]) {
        CHECK(model["object"] == "model");
        CHECK(model["owned_by"] == "mock");
        CHECK((model["apogee_backend"] == "mock" || model["apogee_backend"] == "second"));
        if (model["default"] == true) {
            ++defaults;
            CHECK(model["apogee_backend"] == "mock");
        }
    }
    CHECK(defaults == 1);
}

TEST_CASE("health and model status answer without a model call", "[httpserver][status]") {
    const Fixture fixture;
    CHECK(parsed(fixture.send(get("/health")))["status"] == "ok");
    // A mock reports no load state, so the map is empty and a named query is
    // a 404 -- never an invented "ready".
    CHECK(parsed(fixture.send(get("/v1/model/status")))["backends"].empty());
    CHECK(fixture.send(get("/v1/model/status", {{"backend", "mock"}})).status == 404);
    CHECK(fixture.provider->turn_count() == 0);
}

TEST_CASE("the mux answers 404 and 405 in the error shape", "[httpserver][mux]") {
    const Fixture fixture;
    const HttpResponse missing = fixture.send(get("/nope"));
    CHECK(missing.status == 404);
    CHECK(parsed(missing)["error"]["type"] == "not_found_error");

    const HttpResponse wrong_method = fixture.send(get("/v1/chat/completions"));
    CHECK(wrong_method.status == 405);
    CHECK(wrong_method.headers.at("Allow") == "POST");
    CHECK(parsed(wrong_method)["error"]["type"] == "invalid_request_error");
}
