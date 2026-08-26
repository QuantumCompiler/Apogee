#include "harness/types.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "harness/errors.h"

using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::ChatResponse;
using apogee::harness::ContentPart;
using apogee::harness::FinishReason;
using apogee::harness::InvalidRequestError;
using apogee::harness::MessageContent;
using apogee::harness::Role;
using apogee::harness::Tool;
using apogee::harness::ToolCall;
using apogee::harness::ToolResult;
using nlohmann::json;

TEST_CASE("plain-text content serializes as a bare JSON string", "[harness][ir]") {
    // The common case must stay a string, not become a one-element array:
    // every OpenAI-compatible client that only understands strings depends on
    // it, and it keeps payloads small.
    const ChatMessage message = ChatMessage::user("hello");
    const json encoded = message;

    REQUIRE(encoded.at("content").is_string());
    CHECK(encoded.at("content") == "hello");
    CHECK(encoded.at("role") == "user");
}

TEST_CASE("multi-part content serializes as a content-part array", "[harness][ir]") {
    ChatMessage message = ChatMessage::user(MessageContent::from_parts({
        ContentPart::from_text("what is in this image?"),
        ContentPart::from_image_url("data:image/png;base64,AAAA", "high"),
    }));

    const json encoded = message;
    REQUIRE(encoded.at("content").is_array());
    REQUIRE(encoded.at("content").size() == 2);
    CHECK(encoded.at("content")[0].at("type") == "text");
    CHECK(encoded.at("content")[1].at("type") == "image_url");
    CHECK(encoded.at("content")[1].at("image_url").at("url") == "data:image/png;base64,AAAA");
    CHECK(encoded.at("content")[1].at("image_url").at("detail") == "high");
}

TEST_CASE("both content shapes round-trip through JSON unchanged", "[harness][ir]") {
    const std::vector<ChatMessage> originals{
        ChatMessage::system("be brief"),
        ChatMessage::user("hello"),
        ChatMessage::user(MessageContent::from_parts(
            {ContentPart::from_text("look"), ContentPart::from_image_url("https://x/y.png")})),
    };

    for (const ChatMessage& original : originals) {
        const json encoded = original;
        const auto decoded = encoded.get<ChatMessage>();
        CHECK(decoded == original);
    }
}

TEST_CASE("a plain string is accepted where parts are expected, and vice versa", "[harness][ir]") {
    // Backward compatibility in both directions: a session file written before
    // images existed still loads, and a client sending parts works unchanged.
    const auto from_string = json(R"({"role":"user","content":"hi"})"_json).get<ChatMessage>();
    CHECK(from_string.content.plain_text() == "hi");
    CHECK_FALSE(from_string.content.is_rich());

    const auto from_parts =
        json(R"({"role":"user","content":[{"type":"text","text":"hi"}]})"_json).get<ChatMessage>();
    CHECK(from_parts.content.plain_text() == "hi");
    CHECK_FALSE(from_parts.content.is_rich());
    CHECK(from_parts.content.parts().size() == 1);
}

TEST_CASE("plain_text concatenates text parts and drops the rest", "[harness][ir]") {
    // The safe fallback for a provider with no image support: it gets the words
    // rather than an error or a placeholder it would have to strip.
    const MessageContent content = MessageContent::from_parts({
        ContentPart::from_text("before "),
        ContentPart::from_image_url("https://x/y.png"),
        ContentPart::from_text("after"),
    });
    CHECK(content.plain_text() == "before after");
    CHECK(content.is_rich());
}

TEST_CASE("an unknown content-part type degrades to text rather than failing", "[harness][ir]") {
    // A provider adding a part kind must not make old session files unreadable.
    const auto message =
        json(R"({"role":"user","content":[{"type":"video","text":"caption"}]})"_json)
            .get<ChatMessage>();
    CHECK(message.content.plain_text() == "caption");
}

TEST_CASE("an unknown role is a typed error", "[harness][ir]") {
    CHECK_THROWS_AS(json(R"({"role":"wizard","content":"x"})"_json).get<ChatMessage>(),
                    InvalidRequestError);
}

TEST_CASE("content that is neither string nor array is a typed error", "[harness][ir]") {
    CHECK_THROWS_AS(json(R"({"role":"user","content":42})"_json).get<ChatMessage>(),
                    InvalidRequestError);
}

TEST_CASE("tool calls round-trip, including object-shaped arguments", "[harness][ir]") {
    ChatMessage message = ChatMessage::assistant("");
    message.tool_calls = {ToolCall{"call_1", "search", R"({"query":"apogee"})"}};

    const json encoded = message;
    CHECK(encoded.at("tool_calls")[0].at("function").at("name") == "search");
    CHECK(encoded.get<ChatMessage>() == message);

    // Some providers send arguments as an object rather than a JSON string.
    const auto object_args =
        json(R"({"id":"c","function":{"name":"n","arguments":{"a":1}}})"_json).get<ToolCall>();
    CHECK(object_args.arguments == R"({"a":1})");
}

TEST_CASE("a tool result becomes a linked tool message", "[harness][ir]") {
    // The tool_call_id linkage is the part callers forget, and a tool result
    // that does not name its call is silently dropped by several providers.
    const ToolResult result{"call_7", "search", "3 results", false};
    const ChatMessage message = ChatMessage::from_tool_result(result);

    CHECK(message.role == Role::Tool);
    CHECK(message.tool_call_id == "call_7");
    CHECK(message.name == "search");
    CHECK(message.content.plain_text() == "3 results");
    CHECK(json(message).at("tool_call_id") == "call_7");
}

TEST_CASE("a tool's JSON-Schema parameters survive serialization", "[harness][ir]") {
    const Tool tool{"search", "search the web", R"({"type":"object","required":["q"]})"};
    const json encoded = tool;

    CHECK(encoded.at("type") == "function");
    CHECK(encoded.at("function").at("parameters").at("required")[0] == "q");
    CHECK(encoded.get<Tool>().name == "search");

    // A malformed schema degrades to an empty object rather than throwing: the
    // tool is still callable and the provider gives a better message than we
    // could.
    const Tool broken{"t", "d", "not json"};
    CHECK(json(broken).at("function").at("parameters").is_object());
}

TEST_CASE("every role and finish reason round-trips through its name", "[harness][ir]") {
    for (const Role role : {Role::System, Role::User, Role::Assistant, Role::Tool}) {
        const auto parsed = apogee::harness::role_from_string(apogee::harness::to_string(role));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == role);
    }
    for (const FinishReason reason :
         {FinishReason::Stop, FinishReason::Length, FinishReason::ToolCalls,
          FinishReason::ContentFilter, FinishReason::Cancelled, FinishReason::Other}) {
        const auto parsed =
            apogee::harness::finish_reason_from_string(apogee::harness::to_string(reason));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == reason);
    }
}

// ---------------------------------------------------------------------------
// The transient region -- the contract this item exists to make unbreakable
// ---------------------------------------------------------------------------

TEST_CASE("transient fields never appear in a serialized request", "[harness][ir][transient]") {
    // If a RAG blob reaches persisted history it is re-sent on every later
    // turn, growing the prompt without bound and confusing the model with
    // context it was told was for one question only. Silent, and expensive.
    ChatRequest request;
    request.model = "mock";
    request.messages = {ChatMessage::user("real question"),
                        ChatMessage::system("INJECTED RAG CONTEXT")};
    request.transient.start = 1;
    request.transient.length = 1;
    request.transient.side_request = true;
    request.transient.response_schema = R"({"type":"object"})";

    const std::string encoded = json(request).dump();

    CHECK(encoded.find("side_request") == std::string::npos);
    CHECK(encoded.find("response_schema") == std::string::npos);
    CHECK(encoded.find("transient") == std::string::npos);
    CHECK(encoded.find("\"start\"") == std::string::npos);
}

TEST_CASE("durable_messages excludes exactly the transient region", "[harness][ir][transient]") {
    ChatRequest request;
    request.messages = {ChatMessage::user("one"), ChatMessage::system("rag-a"),
                        ChatMessage::system("rag-b"), ChatMessage::user("two")};
    request.transient.start = 1;
    request.transient.length = 2;

    CHECK_FALSE(request.is_transient(0));
    CHECK(request.is_transient(1));
    CHECK(request.is_transient(2));
    CHECK_FALSE(request.is_transient(3));

    const std::vector<ChatMessage> durable = request.durable_messages();
    REQUIRE(durable.size() == 2);
    CHECK(durable[0].content.plain_text() == "one");
    CHECK(durable[1].content.plain_text() == "two");
}

TEST_CASE("a zero-length transient region marks nothing", "[harness][ir][transient]") {
    // The zero value must mean "no transient region", not "message 0 is
    // transient" -- every request that never sets one relies on this.
    ChatRequest request;
    request.messages = {ChatMessage::user("one"), ChatMessage::user("two")};

    CHECK_FALSE(request.is_transient(0));
    CHECK_FALSE(request.is_transient(1));
    CHECK(request.durable_messages().size() == 2);
}

TEST_CASE("a deserialized request always starts with an empty transient region",
          "[harness][ir][transient]") {
    // Transient state is process-local. Reading a request from JSON must never
    // resurrect one, whatever the JSON claims.
    const auto request = json(R"({
        "model": "m",
        "messages": [{"role":"user","content":"x"}],
        "transient": {"start": 0, "length": 1, "side_request": true}
    })"_json)
                             .get<ChatRequest>();

    CHECK(request.transient.length == 0);
    CHECK_FALSE(request.transient.side_request);
    CHECK(request.transient.response_schema.empty());
    CHECK(request.durable_messages().size() == 1);
}

TEST_CASE("a request round-trips its wire fields", "[harness][ir]") {
    ChatRequest request;
    request.model = "claude";
    request.messages = {ChatMessage::user("hi")};
    request.temperature = 0.5;
    request.max_tokens = 256;
    request.tools = {Tool{"search", "d", R"({"type":"object"})"}};

    const auto decoded = json(request).get<ChatRequest>();
    CHECK(decoded.model == "claude");
    CHECK(decoded.temperature == 0.5);
    CHECK(decoded.max_tokens == 256);
    REQUIRE(decoded.tools.size() == 1);
    CHECK(decoded.tools[0].name == "search");
    CHECK(decoded.messages == request.messages);
}

TEST_CASE("unreported usage is omitted rather than serialized as zeros", "[harness][ir]") {
    // Zero tokens and "the provider did not say" are different facts, and a
    // surface showing "0 tokens" for the second is lying.
    ChatResponse response;
    response.message = ChatMessage::assistant("hi");
    CHECK_FALSE(json(response).contains("usage"));

    response.usage.prompt_tokens = 10;
    response.usage.completion_tokens = 5;
    const json encoded = response;
    REQUIRE(encoded.contains("usage"));
    CHECK(encoded.at("usage").at("total_tokens") == 15);
    CHECK(encoded.get<ChatResponse>().usage.total_tokens() == 15);
}
