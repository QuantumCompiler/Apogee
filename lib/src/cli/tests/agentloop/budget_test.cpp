#include "agentloop/budget.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "agentloop/content.h"
#include "backends/mock.h"
#include "harness/config.h"
#include "harness/harness.h"

using apogee::agentloop::assemble_request;
using apogee::agentloop::Assembly;
using apogee::agentloop::BudgetSource;
using apogee::agentloop::ContextBudget;
using apogee::agentloop::current_turn_start;
using apogee::agentloop::fitting_prefix;
using apogee::agentloop::kStubbedAbove;
using apogee::agentloop::stub_tool_results;
using apogee::agentloop::TokenCount;
using apogee::agentloop::TurnBudget;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::ToolCall;

namespace {

/// A counter that reads one token per byte of content, exactly: every
/// decision below is then arithmetic the test can do by hand.
TurnBudget scripted(std::int64_t window) {
    TurnBudget budget;
    budget.budget.window = window;
    budget.budget.reserve = 0;
    budget.count = [](const ChatRequest& request) {
        std::int64_t tokens = 0;
        for (const ChatMessage& message : request.messages) {
            tokens += static_cast<std::int64_t>(message.content.plain_text().size());
        }
        return TokenCount{.tokens = tokens, .estimated = false};
    };
    return budget;
}

ChatMessage calling(std::vector<ToolCall> calls) {
    ChatMessage message = ChatMessage::assistant("");
    message.tool_calls = std::move(calls);
    return message;
}

ChatMessage result(const std::string& id, const std::string& name, std::string content) {
    apogee::harness::ToolResult out;
    out.tool_call_id = id;
    out.name = name;
    out.content = std::move(content);
    return ChatMessage::from_tool_result(out);
}

std::int64_t size_of(const std::vector<ChatMessage>& messages) {
    std::int64_t bytes = 0;
    for (const ChatMessage& message : messages) {
        bytes += static_cast<std::int64_t>(message.content.plain_text().size());
    }
    return bytes;
}

/// A system prompt, two finished exchanges -- the first read a file -- and
/// the turn in progress, which has read two more.
std::vector<ChatMessage> conversation() {
    return {
        ChatMessage::system(std::string(100, 'S')),
        ChatMessage::user(std::string(100, '1')),
        calling({ToolCall{.id = "c1", .name = "read_file", .arguments = R"({"path":"a.txt"})"}}),
        result("c1", "read_file", std::string(2000, 'a')),
        ChatMessage::assistant(std::string(100, 'x')),
        ChatMessage::user(std::string(100, '2')),
        ChatMessage::assistant(std::string(100, 'y')),
        ChatMessage::user(std::string(100, 'q')),
        calling({ToolCall{.id = "c2", .name = "read_file", .arguments = R"({"path":"b.txt"})"},
                 ToolCall{.id = "c3", .name = "read_file", .arguments = R"({"path":"c.txt"})"}}),
        result("c2", "read_file", std::string(3000, 'b')),
        result("c3", "read_file", std::string(3000, 'c')),
    };
}

bool has(const std::vector<ChatMessage>& messages, char fill, std::size_t count) {
    return std::ranges::any_of(messages, [&](const ChatMessage& message) {
        return message.content.plain_text() == std::string(count, fill);
    });
}

/// A provider that counts every prompt as 777 tokens -- a local model's own
/// tokenizer, as far as the budget can tell.
class Counting final : public apogee::harness::LLMProvider, public apogee::harness::TokenCounting {
public:
    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "counting";
    }

    [[nodiscard]] apogee::harness::ChatResponse chat(
        const ChatRequest&, const apogee::harness::CancellationToken&) override {
        return {};
    }

    [[nodiscard]] apogee::harness::ChatResponse stream_chat(
        const ChatRequest&, const apogee::harness::StreamOptions&) override {
        return {};
    }

    [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
        const apogee::harness::CancellationToken&) override {
        return {};
    }

    [[nodiscard]] std::int64_t count_prompt_tokens(const ChatRequest&) override {
        return 777;
    }
};

}  // namespace

TEST_CASE("each source's share is a fraction of the window after the reserve",
          "[agentloop][budget]") {
    ContextBudget budget;
    budget.window = 32768;
    budget.reserve = 2048;
    CHECK(budget.known());
    CHECK(budget.available() == 30720);
    CHECK(budget.share(BudgetSource::Attachments) == 9216);
    CHECK(budget.share(BudgetSource::Retrieval) == 6144);
    CHECK(budget.share(BudgetSource::ToolResults) == 7680);

    // A reserve past half the window holds back half: the question needs room.
    budget.reserve = 30000;
    CHECK(budget.available() == 16384);

    // Unknown is not roomy, and not empty either: it is unknown, and says so.
    const ContextBudget unknown;
    CHECK_FALSE(unknown.known());
    CHECK(unknown.available() == 0);
    CHECK(unknown.share(BudgetSource::Retrieval) == 0);
}

TEST_CASE("the budget holds back the request's max_tokens, else the backend's, else 4096",
          "[agentloop][budget]") {
    const apogee::harness::Config config = apogee::harness::parse_config(R"(
backends:
  sized:
    type: mock
    context_size: 8000
    max_tokens: 1000
  plain:
    type: mock
    context_size: 8000
  unknown:
    type: mock
)",
                                                                         "<test>");
    apogee::harness::Harness harness{config};
    for (const char* name : {"sized", "plain", "unknown"}) {
        harness.register_provider(name, std::make_shared<apogee::backends::MockProvider>(
                                            apogee::backends::MockProvider::Options{}));
    }
    harness.use_default_router();

    CHECK(apogee::agentloop::budget_for(harness, "sized", std::nullopt).reserve == 1000);
    CHECK(apogee::agentloop::budget_for(harness, "sized", 300).reserve == 300);
    CHECK(apogee::agentloop::budget_for(harness, "plain", std::nullopt).reserve == 4096);
    CHECK(apogee::agentloop::budget_for(harness, "plain", std::nullopt).window == 8000);
    CHECK_FALSE(apogee::agentloop::budget_for(harness, "unknown", std::nullopt).known());
}

TEST_CASE("a whole request's estimate counts its tool calls and definitions",
          "[agentloop][budget]") {
    ChatRequest request;
    request.messages.push_back(ChatMessage::user(std::string(400, 'u')));
    const std::int64_t bare = apogee::agentloop::estimate_request_tokens(request).tokens;
    request.tools.push_back(apogee::harness::Tool{
        .name = "read_file", .description = std::string(400, 'd'), .parameters_schema = "{}"});
    request.messages.push_back(
        calling({ToolCall{.id = "c", .name = "grep", .arguments = std::string(400, 'g')}}));
    const TokenCount full = apogee::agentloop::estimate_request_tokens(request);
    CHECK(full.estimated);
    CHECK(full.tokens >= bare + 200);
}

TEST_CASE("the turn in progress starts at the last user message", "[agentloop][budget]") {
    CHECK(current_turn_start(conversation()) == 7);
    CHECK(current_turn_start({ChatMessage::system("s")}) == 1);
    CHECK(current_turn_start({}) == 0);
}

TEST_CASE("a finished turn's tool result is sent as a one-line stub, never saved as one",
          "[agentloop][budget]") {
    const std::vector<ChatMessage> history = conversation();
    const auto stubbed = stub_tool_results(history, current_turn_start(history));

    CHECK(stubbed.stubs == 1);
    CHECK(stubbed.bytes == 2000);
    const std::string stub = stubbed.messages[3].content.plain_text();
    CHECK(stub == R"([read_file({"path":"a.txt"}) returned 2 KB; not kept after its turn -- )"
                  "call it again if you need it.]");
    // The link to its call is kept, so the request stays well formed.
    CHECK(stubbed.messages[3].tool_call_id == "c1");
    // This turn's results are whole: the model has not answered from them.
    CHECK(stubbed.messages[9].content.plain_text() == std::string(3000, 'b'));
    // Pure: the transcript is untouched.
    CHECK(history[3].content.plain_text() == std::string(2000, 'a'));
}

TEST_CASE("a small result and an ask_user answer are kept whole", "[agentloop][budget]") {
    const std::vector<ChatMessage> history{
        ChatMessage::user("first"),
        calling({ToolCall{.id = "s", .name = "run_command", .arguments = "{}"},
                 ToolCall{.id = "q", .name = "ask_user", .arguments = "{}"}}),
        result("s", "run_command", std::string(kStubbedAbove, 's')),
        result("q", "ask_user", std::string(2000, 'q')),
        ChatMessage::assistant("done"),
        ChatMessage::user("second"),
    };
    const auto stubbed = stub_tool_results(history, current_turn_start(history));
    CHECK(stubbed.stubs == 0);
    CHECK(stubbed.messages[2].content.plain_text() == std::string(kStubbedAbove, 's'));
    CHECK(stubbed.messages[3].content.plain_text() == std::string(2000, 'q'));
}

TEST_CASE("a stub clips long arguments on a character boundary", "[agentloop][budget]") {
    const std::string path = std::string(119, 'p') + "é" + std::string(100, 'p');
    const std::vector<ChatMessage> history{
        ChatMessage::user("read it"),
        calling({ToolCall{.id = "c", .name = "read_file", .arguments = path}}),
        result("c", "read_file", std::string(1024, 'r')),
        ChatMessage::assistant("read"),
        ChatMessage::user("next"),
    };
    const std::string stub = stub_tool_results(history, 4).messages[2].content.plain_text();
    CHECK(stub.starts_with("[read_file(" + std::string(119, 'p') + "...) returned 1 KB;"));
}

TEST_CASE("with room, the request is the history with its stubs and nothing trimmed",
          "[agentloop][budget]") {
    const std::vector<ChatMessage> history = conversation();
    const std::vector<ChatMessage> pinned{ChatMessage::system(std::string(50, 'E'))};
    const std::vector<ChatMessage> injected{ChatMessage::system(std::string(1000, 'R'))};
    const Assembly assembly =
        assemble_request(scripted(100000), ChatRequest{}, history, pinned, injected, 0, 7);

    CHECK(assembly.trims.empty());
    REQUIRE(assembly.messages.size() == history.size() + 2);
    CHECK(assembly.transient_start == 0);
    CHECK(assembly.transient_length == 2);
    CHECK(assembly.messages[0].content.plain_text() == std::string(50, 'E'));
    CHECK(assembly.messages[1].content.plain_text() == std::string(1000, 'R'));
    CHECK(assembly.stubs == 1);
    CHECK_FALSE(has(assembly.messages, 'a', 2000));
    CHECK(has(assembly.messages, 'b', 3000));
    CHECK(has(assembly.messages, 'c', 3000));
}

TEST_CASE("an overflowing request is trimmed in reverse priority, and every trim is said",
          "[agentloop][budget]") {
    const std::vector<ChatMessage> history = conversation();
    const std::vector<ChatMessage> pinned{ChatMessage::system(std::string(50, 'E'))};
    const std::vector<ChatMessage> injected{ChatMessage::system(std::string(1000, 'R'))};
    const auto assemble = [&](std::int64_t window) {
        return assemble_request(scripted(window), ChatRequest{}, history, pinned, injected, 0, 7);
    };
    const std::int64_t whole = size_of(assemble(100000).messages);

    SECTION("the oldest exchange first; the system prompt stays") {
        const Assembly assembly = assemble(whole - 1);
        CHECK(assembly.trims == std::vector<std::string>{"1 earlier exchange not sent"});
        CHECK(has(assembly.messages, 'S', 100));
        CHECK_FALSE(has(assembly.messages, '1', 100));
        CHECK(has(assembly.messages, '2', 100));
        CHECK(assembly.tokens.tokens <= whole - 1);
        // The call and its result leave together: nothing answers a call
        // that is not there.
        for (const ChatMessage& message : assembly.messages) {
            CHECK(message.tool_call_id != "c1");
        }
    }

    SECTION("then every earlier exchange, then this turn's older results") {
        const Assembly assembly = assemble(whole - 2500);
        CHECK(assembly.trims ==
              std::vector<std::string>{"2 earlier exchanges not sent",
                                       "1 of this turn's tool results sent as a stub"});
        CHECK_FALSE(has(assembly.messages, '2', 100));
        CHECK_FALSE(has(assembly.messages, 'b', 3000));
        CHECK(has(assembly.messages, 'c', 3000));  // the newest, never
        CHECK(has(assembly.messages, 'R', 1000));  // retrieval outranks results
        CHECK(has(assembly.messages, 'q', 100));   // the question, always
    }

    SECTION("then the injected context; the newest result and the pinned note stay") {
        const Assembly assembly = assemble(3500);
        CHECK(assembly.trims ==
              std::vector<std::string>{"2 earlier exchanges not sent",
                                       "1 of this turn's tool results sent as a stub",
                                       "the retrieved context not sent"});
        CHECK_FALSE(has(assembly.messages, 'R', 1000));
        CHECK(has(assembly.messages, 'E', 50));
        CHECK(has(assembly.messages, 'c', 3000));
        CHECK(assembly.transient_length == 1);
    }

    SECTION("and a request that still cannot fit is sent, and said to be over") {
        const Assembly assembly = assemble(1000);
        REQUIRE(assembly.trims.size() == 4);
        CHECK(assembly.trims.back().starts_with("still "));
        CHECK(
            assembly.trims.back().ends_with(" tokens over the window after the reserve -- sent "
                                            "as it is"));
    }
}

TEST_CASE("an unknown window never reads as room to spare: nothing is sized by it",
          "[agentloop][budget]") {
    const std::vector<ChatMessage> history = conversation();
    TurnBudget unknown = scripted(0);
    const Assembly assembly = assemble_request(unknown, ChatRequest{}, history, {}, {}, 0, 7);
    // Only the stubs, which need no window.
    CHECK(assembly.trims.empty());
    CHECK(assembly.messages.size() == history.size());
    CHECK(assembly.stubs == 1);
    // A source that sizes itself keeps its own cap.
    CHECK(fitting_prefix(unknown, 0, 12, [](std::size_t) { return std::string(99999, 'x'); }) ==
          12);
}

TEST_CASE("the leading items that fit a share are found, and none when one is too big",
          "[agentloop][budget]") {
    const TurnBudget budget = scripted(100000);
    const auto render = [](std::size_t count) { return std::string(count * 100, 'k'); };
    CHECK(fitting_prefix(budget, 1000, 12, render) == 10);
    CHECK(fitting_prefix(budget, 99, 12, render) == 0);
    CHECK(fitting_prefix(budget, 5000, 12, render) == 12);
}

TEST_CASE("a provider that counts is asked; one that cannot is estimated, and says so",
          "[agentloop][budget]") {
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("counting", std::make_shared<Counting>());
    harness.register_provider("plain", std::make_shared<apogee::backends::MockProvider>(
                                           apogee::backends::MockProvider::Options{}));
    harness.use_default_router();
    ChatRequest request;
    request.messages.push_back(ChatMessage::user(std::string(400, 'u')));

    const TokenCount exact = apogee::agentloop::token_counter(harness, "counting")(request);
    CHECK(exact.tokens == 777);
    CHECK_FALSE(exact.estimated);
    const TokenCount guessed = apogee::agentloop::token_counter(harness, "plain")(request);
    CHECK(guessed.tokens == apogee::agentloop::estimate_request_tokens(request).tokens);
    CHECK(guessed.estimated);
}

TEST_CASE("an ask_user answer is kept whole even when its call is no longer there",
          "[agentloop][budget]") {
    // A compacted or clipped history can keep an answer without the call
    // that asked it; the answer is still the user's words.
    const std::vector<ChatMessage> history{
        ChatMessage::user("first"),
        result("gone", "ask_user", std::string(2000, 'a')),
        ChatMessage::assistant("noted"),
        ChatMessage::user("second"),
    };
    const auto stubbed = stub_tool_results(history, current_turn_start(history));
    CHECK(stubbed.stubs == 0);
    CHECK(stubbed.messages[1].content.plain_text() == std::string(2000, 'a'));
}

TEST_CASE("an inlined attachment rides its message in what is sent, never in history",
          "[agentloop][budget][attachments]") {
    const std::vector<ChatMessage> history{ChatMessage::system("be brief"),
                                           ChatMessage::user("what is in it?")};
    const std::vector<apogee::agentloop::InlineAttachment> inlined{
        {.message = 1, .name = "notes.md", .text = "--- attached file: notes.md ---\nhi\n"}};
    const Assembly assembly =
        assemble_request(scripted(100000), ChatRequest{}, history, {}, {}, 0, 1, inlined);
    REQUIRE(assembly.messages.size() == 2);
    CHECK(assembly.messages[1].content.plain_text() ==
          "--- attached file: notes.md ---\nhi\n\nwhat is in it?");
    CHECK(history[1].content.plain_text() == "what is in it?");
    CHECK(assembly.inline_dropped.empty());

    // A message that is not the user's -- compacted away -- carries nothing.
    const Assembly gone = assemble_request(scripted(100000), ChatRequest{}, history, {}, {}, 0, 1,
                                           {{.message = 0, .name = "a.md", .text = "x"}});
    CHECK(gone.inline_dropped == std::vector<std::string>{"a.md"});
}

TEST_CASE("an inlined attachment is trimmed last, and with the exchange it rode",
          "[agentloop][budget][attachments]") {
    // An old exchange carries a big attachment; the turn in progress a small one.
    const std::vector<ChatMessage> history{ChatMessage::user("old question"),
                                           ChatMessage::assistant(std::string(100, 'o')),
                                           ChatMessage::user("new question")};
    const std::vector<apogee::agentloop::InlineAttachment> inlined{
        {.message = 0, .name = "old.md", .text = std::string(2000, 'A')},
        {.message = 2, .name = "new.md", .text = std::string(1000, 'B')}};
    const std::vector<ChatMessage> injected{ChatMessage::system(std::string(500, 'R'))};

    SECTION("the old exchange goes, and its attachment with it") {
        const Assembly assembly =
            assemble_request(scripted(2000), ChatRequest{}, history, {}, injected, 0, 2, inlined);
        CHECK(assembly.inline_dropped == std::vector<std::string>{"old.md"});
        CHECK(has(assembly.messages, 'R', 500));  // retrieval before the last resort
    }

    SECTION("past the injected context, the attachment itself") {
        const Assembly assembly =
            assemble_request(scripted(600), ChatRequest{}, history, {}, injected, 0, 2, inlined);
        CHECK(assembly.inline_dropped == std::vector<std::string>{"old.md", "new.md"});
        REQUIRE(assembly.trims.size() >= 3);
        CHECK(assembly.trims[1] == "the retrieved context not sent");
        CHECK(assembly.trims[2] == "1 inlined attachment not sent");
        CHECK(assembly.messages.back().content.plain_text() == "new question");
    }
}

TEST_CASE("an inlined attachment goes ahead of an image, as a text part",
          "[agentloop][budget][attachments]") {
    const std::vector<ChatMessage> history{
        ChatMessage::user(apogee::harness::MessageContent::from_parts(
            {apogee::harness::ContentPart::from_text("look"),
             apogee::harness::ContentPart::from_image_url("data:image/png;base64,AAAA")}))};
    const Assembly assembly = assemble_request(scripted(100000), ChatRequest{}, history, {}, {}, 0,
                                               0, {{.message = 0, .name = "a.md", .text = "A"}});
    const auto& parts = assembly.messages[0].content.parts();
    REQUIRE(parts.size() == 3);
    CHECK(parts[0].text == "A");
    CHECK(parts[1].text == "look");
    CHECK(parts[2].kind == apogee::harness::ContentPart::Kind::ImageUrl);
}
