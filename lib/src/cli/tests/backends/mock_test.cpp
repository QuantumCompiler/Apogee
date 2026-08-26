#include "backends/mock.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "harness/errors.h"

using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::CancellationToken;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::FinishReason;
using apogee::harness::StreamOptions;
using apogee::harness::ToolCall;

namespace {

ChatRequest simple_request() {
    ChatRequest request;
    request.model = "mock";
    request.messages = {ChatMessage::user("hello")};
    return request;
}

}  // namespace

// These are the contract tests the item calls for: MockProvider implements the
// REAL LLMProvider, so they pin the interface itself. When a later item changes
// the interface, these break -- which is the point. A bespoke fake per test
// would drift from the interface it stands in for, silently.

TEST_CASE("chat answers from the script in order", "[backends][mock]") {
    MockProvider::Options options;
    options.turns = {MockTurn{"first", {}, {}, {}}, MockTurn{"second", {}, {}, {}}};
    MockProvider provider{std::move(options)};

    CHECK(provider.chat(simple_request(), {}).message.content.plain_text() == "first");
    CHECK(provider.chat(simple_request(), {}).message.content.plain_text() == "second");
    // Past the end it replays the last turn rather than failing: a test running
    // one extra turn is nearly always testing something else, and "script
    // exhausted" sends the reader hunting in the wrong place.
    CHECK(provider.chat(simple_request(), {}).message.content.plain_text() == "second");
    CHECK(provider.turn_count() == 3);
}

TEST_CASE("an empty script still answers", "[backends][mock]") {
    MockProvider provider{MockProvider::Options{}};
    CHECK_FALSE(provider.chat(simple_request(), {}).message.content.plain_text().empty());
}

TEST_CASE("every request is recorded for inspection", "[backends][mock]") {
    // The assertion point for downstream items: what did the harness actually
    // hand the provider?
    std::vector<std::string> seen;
    MockProvider::Options options;
    options.on_request = [&seen](const ChatRequest& request) {
        seen.push_back(request.messages.back().content.plain_text());
    };
    MockProvider provider{std::move(options)};

    ChatRequest first = simple_request();
    first.messages = {ChatMessage::user("one")};
    (void)provider.chat(first, {});

    ChatRequest second = simple_request();
    second.messages = {ChatMessage::user("two")};
    (void)provider.chat(second, {});

    REQUIRE(seen == std::vector<std::string>{"one", "two"});
    REQUIRE(provider.requests().size() == 2);
    CHECK(provider.requests()[0].messages.back().content.plain_text() == "one");
}

TEST_CASE("the transient region survives the trip to the provider", "[backends][mock]") {
    // Transient state is process-local, not un-passable: a provider with a
    // prompt cache has to see it to honour it.
    ChatRequest request = simple_request();
    request.messages = {ChatMessage::user("q"), ChatMessage::system("rag")};
    request.transient.start = 1;
    request.transient.length = 1;
    request.transient.side_request = true;

    MockProvider provider{MockProvider::Options{}};
    (void)provider.chat(request, {});

    REQUIRE(provider.requests().size() == 1);
    const ChatRequest& seen = provider.requests()[0];
    CHECK(seen.transient.length == 1);
    CHECK(seen.transient.side_request);
    CHECK(seen.is_transient(1));
}

TEST_CASE("streaming emits the text in chunks and returns the whole response", "[backends][mock]") {
    MockProvider::Options options;
    options.turns = {MockTurn{"abcdefgh", {}, {}, {}}};
    options.chunk_size = 3;
    MockProvider provider{std::move(options)};

    std::vector<std::string> chunks;
    StreamOptions stream;
    stream.on_token = [&chunks](std::string_view chunk) { chunks.emplace_back(chunk); };

    const auto response = provider.stream_chat(simple_request(), stream);

    // Small chunks on purpose: a sink that mishandles a token split across
    // chunks fails here rather than in production.
    REQUIRE(chunks == std::vector<std::string>{"abc", "def", "gh"});
    CHECK(response.message.content.plain_text() == "abcdefgh");
}

TEST_CASE("streaming reports start and done status events", "[backends][mock]") {
    MockProvider::Options options;
    options.turns = {MockTurn{"hi", {}, {}, {}}};
    MockProvider provider{std::move(options)};

    std::vector<apogee::harness::StatusEvent::Phase> phases;
    StreamOptions stream;
    stream.on_status = [&phases](const apogee::harness::StatusEvent& event) {
        phases.push_back(event.phase);
    };

    (void)provider.stream_chat(simple_request(), stream);
    REQUIRE(phases.size() == 2);
    CHECK(phases[0] == apogee::harness::StatusEvent::Phase::Start);
    CHECK(phases[1] == apogee::harness::StatusEvent::Phase::Done);
}

TEST_CASE("streaming with no sinks is valid", "[backends][mock]") {
    // An empty sink is common -- a caller that only wants the final response.
    MockProvider provider{MockProvider::Options{}};
    CHECK_NOTHROW((void)provider.stream_chat(simple_request(), StreamOptions{}));
}

TEST_CASE("a scripted turn can carry tool calls and a finish reason", "[backends][mock]") {
    MockProvider::Options options;
    options.turns = {
        MockTurn{"", {ToolCall{"call_1", "search", R"({"q":"x"})"}}, FinishReason::ToolCalls, {}}};
    MockProvider provider{std::move(options)};

    const auto response = provider.chat(simple_request(), {});
    REQUIRE(response.message.tool_calls.size() == 1);
    CHECK(response.message.tool_calls[0].name == "search");
    CHECK(response.finish_reason == FinishReason::ToolCalls);
}

TEST_CASE("usage is reported when the script sets it", "[backends][mock]") {
    MockProvider::Options options;
    apogee::harness::Usage usage;
    usage.prompt_tokens = 12;
    usage.completion_tokens = 3;
    options.turns = {MockTurn{"hi", {}, FinishReason::Stop, usage}};
    MockProvider provider{std::move(options)};

    const auto response = provider.chat(simple_request(), {});
    CHECK(response.usage.total_tokens() == 15);
    CHECK(response.usage.reported());
}

TEST_CASE("complete defaults to chat", "[backends][mock]") {
    // The base-class default. Most providers have no cheaper single-turn
    // endpoint, and duplicating the call in each would only let the two paths
    // drift.
    MockProvider::Options options;
    options.turns = {MockTurn{"answered", {}, {}, {}}};
    MockProvider provider{std::move(options)};

    CHECK(provider.complete(simple_request(), {}).message.content.plain_text() == "answered");
}

TEST_CASE("list_models reports the configured model", "[backends][mock]") {
    MockProvider::Options options;
    options.backend_name = "my-mock";
    options.model = "mock-9";
    MockProvider provider{std::move(options)};

    const auto models = provider.list_models({});
    REQUIRE(models.size() == 1);
    CHECK(models[0].id == "mock-9");
    CHECK(models[0].provider == "mock");
    CHECK(models[0].backend == "my-mock");
}

TEST_CASE("every entry point honours a cancelled token", "[backends][mock]") {
    // The mock holds itself to the same contract every real provider must, or
    // tests of cancellation prove nothing.
    MockProvider provider{MockProvider::Options{}};
    const CancellationToken token = CancellationToken::create();
    token.cancel();

    CHECK_THROWS_AS((void)provider.chat(simple_request(), token), apogee::harness::CancelledError);
    CHECK_THROWS_AS((void)provider.list_models(token), apogee::harness::CancelledError);

    StreamOptions stream;
    stream.cancellation = token;
    CHECK_THROWS_AS((void)provider.stream_chat(simple_request(), stream),
                    apogee::harness::CancelledError);
}

TEST_CASE("a chunk size of zero is corrected rather than looping forever", "[backends][mock]") {
    MockProvider::Options options;
    options.turns = {MockTurn{"abc", {}, {}, {}}};
    options.chunk_size = 0;
    MockProvider provider{std::move(options)};

    std::string streamed;
    StreamOptions stream;
    stream.on_token = [&streamed](std::string_view chunk) { streamed += chunk; };
    (void)provider.stream_chat(simple_request(), stream);
    CHECK(streamed == "abc");
}
