#include "agentloop/query_rewrite.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "harness/config.h"
#include "harness/harness.h"

/// A follow-up searched as a standalone question (26b).
namespace {

using apogee::agentloop::has_earlier_turn;
using apogee::agentloop::QueryRewrite;
using apogee::agentloop::rewrite_query;
using apogee::agentloop::rewrite_request;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;

/// A harness whose `helper` answers `reply`, or fails when `fails`.
struct Helper {
    apogee::harness::Harness harness{apogee::harness::Config{}};
    std::shared_ptr<MockProvider> provider;

    explicit Helper(std::string reply) {
        MockProvider::Options options;
        options.backend_name = "helper";
        options.turns = {MockTurn{.text = std::move(reply)}};
        provider = std::make_shared<MockProvider>(std::move(options));
        harness.register_provider("helper", provider);
        harness.use_default_router();
    }
};

const std::vector<ChatMessage> kFollowUp{
    ChatMessage::system("be brief"),
    ChatMessage::user("Tell me about the Hale telescope and the Hooker telescope."),
    ChatMessage::assistant("Both are on California mountains; the Hale is on Palomar."),
    ChatMessage::user("How big is the second one?"),
};

}  // namespace

TEST_CASE("only a conversation with an earlier turn is rewritten", "[agentloop][rewrite]") {
    CHECK_FALSE(has_earlier_turn({ChatMessage::user("a first question")}));
    CHECK_FALSE(has_earlier_turn({ChatMessage::system("s"), ChatMessage::user("q")}));
    CHECK(has_earlier_turn(kFollowUp));

    // A first question already stands alone: nothing is asked.
    Helper helper{"never asked"};
    const QueryRewrite first = rewrite_query(
        helper.harness, "helper", {ChatMessage::user("what is BM25?")}, "what is BM25?", {});
    CHECK(first.query == "what is BM25?");
    CHECK_FALSE(first.rewritten);
    CHECK(helper.provider->requests().empty());
}

TEST_CASE("the rewrite request shows the conversation, the question once, as a side request",
          "[agentloop][rewrite]") {
    const apogee::harness::ChatRequest request =
        rewrite_request("helper", kFollowUp, "How big is the second one?");
    CHECK(request.model == "helper");
    CHECK(request.transient.side_request);
    CHECK(request.transient.skip_reasoning);
    CHECK(request.max_tokens == 64);
    REQUIRE(request.messages.size() == 1);
    const std::string prompt = request.messages.front().content.plain_text();
    CHECK(prompt.find("User: Tell me about the Hale telescope") != std::string::npos);
    CHECK(prompt.find("Assistant: Both are on California mountains") != std::string::npos);
    CHECK(prompt.find("Last question: How big is the second one?") != std::string::npos);
    // The question is not also shown as a conversation line.
    CHECK(prompt.find("User: How big is the second one?") == std::string::npos);
    // System prompts say nothing a query needs.
    CHECK(prompt.find("be brief") == std::string::npos);

    // Long messages are clipped, on a character boundary.
    std::vector<ChatMessage> long_history{ChatMessage::user(std::string(1000, 'x') + "é"),
                                          ChatMessage::assistant("ok"), ChatMessage::user("and?")};
    const std::string clipped =
        rewrite_request("helper", long_history, "and?").messages.front().content.plain_text();
    CHECK(clipped.find(std::string(400, 'x') + " ...") != std::string::npos);
    CHECK(clipped.find(std::string(401, 'x')) == std::string::npos);
}

TEST_CASE("the rewrite is shown the latest turns, not the whole conversation",
          "[agentloop][rewrite]") {
    std::vector<ChatMessage> history;
    for (int turn = 1; turn <= 10; ++turn) {
        history.push_back(ChatMessage::user("question " + std::to_string(turn) + "."));
        history.push_back(ChatMessage::assistant("answer " + std::to_string(turn) + "."));
    }
    history.push_back(ChatMessage::user("and the last?"));
    const std::string prompt =
        rewrite_request("helper", history, "and the last?").messages.front().content.plain_text();
    // The six messages before the question: turns 8 to 10.
    CHECK(prompt.find("User: question 8.") != std::string::npos);
    CHECK(prompt.find("Assistant: answer 10.") != std::string::npos);
    CHECK(prompt.find("question 7.") == std::string::npos);
    CHECK(prompt.find("answer 7.") == std::string::npos);
}

TEST_CASE("a rewrite is taken from its first line, without its wrapping", "[agentloop][rewrite]") {
    Helper helper{"\n  Query: \"size of the Hooker telescope\"\nIt has a 100-inch mirror."};
    const QueryRewrite rewrite =
        rewrite_query(helper.harness, "helper", kFollowUp, "How big is the second one?", {});
    CHECK(rewrite.rewritten);
    CHECK(rewrite.query == "size of the Hooker telescope");
    REQUIRE(helper.provider->requests().size() == 1);
    CHECK(helper.provider->requests().front().transient.side_request);
}

TEST_CASE("a rewrite that fails, says nothing, or answers instead keeps the question",
          "[agentloop][rewrite]") {
    SECTION("nothing") {
        Helper helper{"   \n  "};
        const QueryRewrite kept =
            rewrite_query(helper.harness, "helper", kFollowUp, "How big is the second one?", {});
        CHECK_FALSE(kept.rewritten);
        CHECK(kept.query == "How big is the second one?");
        CHECK(kept.note.find("not a query") != std::string::npos);
    }
    SECTION("an answer, not a query") {
        Helper helper{std::string(500, 'w')};
        const QueryRewrite kept =
            rewrite_query(helper.harness, "helper", kFollowUp, "How big is the second one?", {});
        CHECK_FALSE(kept.rewritten);
        CHECK(kept.query == "How big is the second one?");
    }
    SECTION("an error") {
        Helper helper{"unused"};
        const QueryRewrite kept =
            rewrite_query(helper.harness, "ghost", kFollowUp, "How big is the second one?", {});
        CHECK_FALSE(kept.rewritten);
        CHECK(kept.query == "How big is the second one?");
        CHECK(kept.note.find("could not be rewritten") != std::string::npos);
    }
}
