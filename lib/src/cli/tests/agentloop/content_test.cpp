#include "agentloop/content.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "harness/config.h"

using apogee::agentloop::compact_history;
using apogee::agentloop::count_turns;
using apogee::agentloop::estimate_prompt_tokens;
using apogee::agentloop::estimate_tokens;
using apogee::agentloop::splice_transient;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using apogee::harness::Config;
using apogee::harness::Harness;
using apogee::harness::Role;

TEST_CASE("token counts always declare themselves estimates", "[agentloop][content]") {
    // An estimate and a provider's exact count are not interchangeable, and
    // conflating them is how a context warning fires at the wrong point.
    const auto count = estimate_tokens("abcdefgh");
    CHECK(count.tokens == 2);  // 8 characters / 4
    CHECK(count.estimated);

    CHECK(estimate_tokens("").tokens == 0);
    CHECK(estimate_tokens("a").tokens == 1);  // rounds up, never to zero
    CHECK(estimate_tokens("abcd").tokens == 1);
    CHECK(estimate_tokens("abcde").tokens == 2);
}

TEST_CASE("prompt estimation covers every message", "[agentloop][content]") {
    const std::vector<ChatMessage> messages{ChatMessage::system("abc"), ChatMessage::user("defgh")};
    const auto count = estimate_prompt_tokens(messages);
    CHECK(count.estimated);
    CHECK(count.tokens > 0);
    CHECK(count.tokens >= estimate_tokens("abcdefgh").tokens);
}

TEST_CASE("turns count user+assistant pairs, ignoring system messages", "[agentloop][content]") {
    const std::vector<ChatMessage> history{
        ChatMessage::system("prompt"), ChatMessage::user("one"),    ChatMessage::assistant("a"),
        ChatMessage::user("two"),      ChatMessage::assistant("b"),
    };
    CHECK(count_turns(history) == 2);
    CHECK(count_turns({}) == 0);
    CHECK(count_turns({ChatMessage::system("only")}) == 0);
}

TEST_CASE("splice inserts at the requested position", "[agentloop][content]") {
    const std::vector<ChatMessage> messages{ChatMessage::user("a"), ChatMessage::user("b")};
    const std::vector<ChatMessage> prefix{ChatMessage::system("X")};

    CHECK(splice_transient(messages, prefix, 0)[0].content.plain_text() == "X");
    CHECK(splice_transient(messages, prefix, 1)[1].content.plain_text() == "X");
    CHECK(splice_transient(messages, prefix, 2)[2].content.plain_text() == "X");
    // Past the end is clamped, not undefined.
    CHECK(splice_transient(messages, prefix, 99).size() == 3);
    CHECK(splice_transient(messages, prefix, 99)[2].content.plain_text() == "X");
    // An empty prefix is the identity.
    CHECK(splice_transient(messages, {}, 1).size() == 2);
}

TEST_CASE("compaction summarises into an authoritative system message",
          "[agentloop][content][compaction]") {
    // System, not assistant: the model must treat the summary as the record of
    // what happened, not as something it once said.
    MockProvider::Options provider_options;
    provider_options.backend_name = "mock";
    provider_options.turns = {MockTurn{"They discussed cats.", {}, {}, {}}};

    Harness harness{Config{}};
    harness.register_provider("mock", std::make_shared<MockProvider>(std::move(provider_options)));
    harness.use_default_router();

    const std::vector<ChatMessage> history{
        ChatMessage::system("be brief"),         ChatMessage::user("tell me about cats"),
        ChatMessage::assistant("cats are fine"), ChatMessage::user("more"),
        ChatMessage::assistant("still fine"),
    };

    const auto compacted = compact_history(harness, history, "mock");

    CHECK(compacted.size() < history.size());
    CHECK(compacted[0].role == Role::System);
    CHECK(compacted[0].content.plain_text() == "be brief");
    CHECK(compacted[1].role == Role::System);
    CHECK(compacted[1].content.plain_text().find("They discussed cats.") != std::string::npos);
    CHECK(compacted[1].content.plain_text().find("authoritative") != std::string::npos);
    // The most recent assistant message survives, so the model has one prior
    // turn to reference rather than a confusing "no prior context".
    CHECK(compacted.back().role == Role::Assistant);
    CHECK(compacted.back().content.plain_text() == "still fine");
}

TEST_CASE("a failed compaction returns the history unchanged", "[agentloop][content][compaction]") {
    // Degrading to a longer prompt is acceptable; losing the conversation is
    // not.
    Harness harness{Config{}};
    harness.use_default_router();  // nothing registered -- the summarise call fails

    const std::vector<ChatMessage> history{ChatMessage::user("one"), ChatMessage::assistant("two")};
    const auto compacted = compact_history(harness, history, "ghost");

    REQUIRE(compacted.size() == history.size());
    CHECK(compacted[0].content.plain_text() == "one");
    CHECK(compacted[1].content.plain_text() == "two");
}

TEST_CASE("compacting a system-only history is a no-op", "[agentloop][content][compaction]") {
    Harness harness{Config{}};
    harness.use_default_router();
    const std::vector<ChatMessage> history{ChatMessage::system("only a prompt")};
    CHECK(compact_history(harness, history, "m").size() == 1);
}
