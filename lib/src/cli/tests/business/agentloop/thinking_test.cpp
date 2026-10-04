#include "agentloop/thinking.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "harness/harness.h"

/// `auto` thinking decided per question (26i), and the turn's thinking
/// reaching every request the loop sends.
namespace {

using apogee::agentloop::decide_thinking;
using apogee::agentloop::question_needs_thinking;
using apogee::agentloop::resolve_turn_thinking;
using apogee::agentloop::thinking_judge_request;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::Thinking;
using apogee::harness::ThinkingMode;

/// A harness whose `judge` answers `reply` and whose `chat` backend records
/// every request it is sent.
struct Harnessed {
    apogee::harness::Harness harness{apogee::harness::Config{}};
    std::vector<ChatRequest> judged;
    std::vector<ChatRequest> chatted;

    explicit Harnessed(std::string reply) {
        apogee::backends::MockProvider::Options judge;
        judge.backend_name = "judge";
        judge.turns = {apogee::backends::MockTurn{.text = std::move(reply)}};
        judge.on_request = [this](const ChatRequest& request) { judged.push_back(request); };
        harness.register_provider("judge", std::make_shared<apogee::backends::MockProvider>(judge));
        apogee::backends::MockProvider::Options chat;
        chat.backend_name = "chat";
        chat.turns = {apogee::backends::MockTurn{.text = "answered"}};
        chat.on_request = [this](const ChatRequest& request) { chatted.push_back(request); };
        harness.register_provider("chat", std::make_shared<apogee::backends::MockProvider>(chat));
        harness.use_default_router();
    }
};

/// A provider that reports its reasoning hit the budget mid-answer, as the
/// local backend does when its budget sampler forces the close.
class BudgetedProvider final : public apogee::harness::LLMProvider {
public:
    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "budgeted";
    }

    [[nodiscard]] apogee::harness::ChatResponse chat(
        const ChatRequest& request, const apogee::harness::CancellationToken& /*cancel*/) override {
        return stream_chat(request, {});
    }

    [[nodiscard]] apogee::harness::ChatResponse stream_chat(
        const ChatRequest& /*request*/, const apogee::harness::StreamOptions& options) override {
        if (options.on_thinking) {
            options.on_thinking("working");
        }
        if (options.on_status) {
            apogee::harness::StatusEvent event;
            event.type = apogee::harness::StatusEvent::Type::ThinkingBudget;
            event.phase = apogee::harness::StatusEvent::Phase::Done;
            event.detail = "the thinking budget of 64 tokens was reached";
            options.on_status(event);
        }
        apogee::harness::ChatResponse response;
        response.message = ChatMessage::assistant("done");
        return response;
    }

    [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
        const apogee::harness::CancellationToken& /*cancel*/) override {
        return {};
    }
};

struct BudgetRecorder final : apogee::agentloop::Reporter {
    int reached = 0;

    void on_thinking_budget_reached() override {
        ++reached;
    }
};

}  // namespace

TEST_CASE("the rule thinks for long questions and for code, maths, why and how",
          "[agentloop][thinking]") {
    // The recorded rule (26i), table by table. The six-task battery's
    // questions: the two that need working think, the four lookups do not.
    CHECK_FALSE(question_needs_thinking("What does notes.txt say?"));
    CHECK_FALSE(question_needs_thinking(
        "Write a file named summary.txt containing the words 'all systems nominal'."));
    CHECK(
        question_needs_thinking("How many lines does data.csv have? Use the shell to count them."));
    CHECK_FALSE(
        question_needs_thinking("Look in the docs folder and tell me what the budget file says."));
    CHECK_FALSE(question_needs_thinking(
        "links.txt has our status page's address. Fetch that page and tell me its title."));
    CHECK(question_needs_thinking("What is 17 * 23?"));

    // Small talk and lookups answer straight away.
    CHECK_FALSE(question_needs_thinking("Hello there!"));
    CHECK_FALSE(question_needs_thinking("What is the capital of France?"));
    CHECK_FALSE(question_needs_thinking("Thanks, that's all."));
    // "why" and "how" as words, not inside one.
    CHECK(question_needs_thinking("Why is the sky blue?"));
    CHECK(question_needs_thinking("how do I rebase onto main"));
    CHECK_FALSE(question_needs_thinking("Show me the showroom photos."));
    // Code: a fence, an inline span, a language's punctuation.
    CHECK(question_needs_thinking("What does `git reset --hard` do?"));
    CHECK(question_needs_thinking("fix this: if (x) { y(); }"));
    // Maths: an operator between numbers, or a word asking for working.
    CHECK(question_needs_thinking("2^10?"));
    CHECK(question_needs_thinking("Solve for x."));
    CHECK_FALSE(question_needs_thinking("Is version 2.0 out?"));
    // And anything long enough, whatever it holds.
    CHECK(question_needs_thinking(std::string(apogee::agentloop::kThinkingQuestionBytes + 1, 'a')));
    CHECK_FALSE(
        question_needs_thinking(std::string(apogee::agentloop::kThinkingQuestionBytes, 'a')));
}

TEST_CASE("a judge is asked one word, with its own thinking off", "[agentloop][thinking]") {
    const ChatRequest request = thinking_judge_request("judge", "Why is the sky blue?");
    CHECK(request.model == "judge");
    CHECK(request.transient.side_request);
    CHECK(request.thinking.off());
    CHECK(request.temperature == 0.0);
    REQUIRE(request.max_tokens.has_value());
    CHECK(*request.max_tokens <= 8);
    REQUIRE(request.messages.size() == 2);
    CHECK(request.messages.back().content.plain_text() == "Why is the sky blue?");
}

TEST_CASE("auto follows the judge's yes or no, and the rule when it says anything else",
          "[agentloop][thinking]") {
    // The judge outranks the rule both ways: "no" for a question the rule
    // would think about, "yes" for one it would not.
    Harnessed no{"No."};
    const auto said_no = decide_thinking(no.harness, "judge", "Why is the sky blue?", {});
    CHECK_FALSE(said_no.think);
    CHECK(said_no.by == "the utility model");
    REQUIRE(no.judged.size() == 1);

    Harnessed yes{"yes"};
    CHECK(decide_thinking(yes.harness, "judge", "Hello there!", {}).think);

    // An answer that is neither falls back to the rule.
    Harnessed rambling{"Perhaps, it depends."};
    const auto ruled = decide_thinking(rambling.harness, "judge", "What is 17 * 23?", {});
    CHECK(ruled.think);
    CHECK(ruled.by != "the utility model");

    // No judge named: the rule, and no model is asked.
    Harnessed unasked{"yes"};
    CHECK_FALSE(decide_thinking(unasked.harness, "", "Hello there!", {}).think);
    CHECK(unasked.judged.empty());
}

TEST_CASE("a judge that cannot be reached leaves the rule to decide", "[agentloop][thinking]") {
    Harnessed harnessed{"yes"};
    // A backend nobody registered: the call fails, the turn does not.
    const auto decided = decide_thinking(harnessed.harness, "nowhere", "Hello there!", {});
    CHECK_FALSE(decided.think);
}

TEST_CASE("on and off pass through untouched; auto is decided and keeps its budget",
          "[agentloop][thinking]") {
    Harnessed harnessed{"no"};
    const Thinking on = resolve_turn_thinking(
        harnessed.harness, Thinking{.mode = ThinkingMode::On, .budget = 512}, "judge", "x", {});
    CHECK(on.mode == ThinkingMode::On);
    CHECK(on.budget == 512);
    CHECK(harnessed.judged.empty());

    const Thinking off = resolve_turn_thinking(
        harnessed.harness, Thinking{.mode = ThinkingMode::Off}, "judge", "x", {});
    CHECK(off.off());

    const Thinking decided = resolve_turn_thinking(
        harnessed.harness, Thinking{.mode = ThinkingMode::Auto, .budget = 256}, "judge",
        "Why is the sky blue?", {});
    CHECK(decided.off());
    CHECK(decided.budget == 256);
}

TEST_CASE("every request of a turn carries its thinking, auto decided once",
          "[agentloop][thinking][loop]") {
    Harnessed harnessed{"no"};
    std::vector<ChatMessage> history{ChatMessage::user("Why is the sky blue?")};
    apogee::agentloop::Options options;
    options.model = "chat";
    options.thinking = Thinking{.mode = ThinkingMode::Auto, .budget = 128};
    options.thinking_judge = "judge";
    options.stream_answer = false;
    (void)apogee::agentloop::run(harnessed.harness, history, options);
    REQUIRE_FALSE(harnessed.chatted.empty());
    for (const ChatRequest& request : harnessed.chatted) {
        CHECK(request.thinking.off());
        CHECK(request.thinking.budget == 128);
    }
    CHECK(harnessed.judged.size() == 1);

    // Off asked is off sent, with no judge consulted.
    Harnessed plain{"yes"};
    std::vector<ChatMessage> again{ChatMessage::user("Why?")};
    options.thinking = Thinking{.mode = ThinkingMode::Off};
    (void)apogee::agentloop::run(plain.harness, again, options);
    REQUIRE_FALSE(plain.chatted.empty());
    CHECK(plain.chatted.front().thinking.off());
    CHECK(plain.judged.empty());
}

TEST_CASE("a backend's spent budget reaches the reporter while the thinking shows",
          "[agentloop][thinking][loop]") {
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("budgeted", std::make_shared<BudgetedProvider>());
    harness.use_default_router();
    std::vector<ChatMessage> history{ChatMessage::user("think hard")};
    apogee::agentloop::Options options;
    options.model = "budgeted";
    BudgetRecorder recorder;
    (void)apogee::agentloop::run(harness, history, options, recorder);
    CHECK(recorder.reached == 1);
    // Thinking never reaches history.
    for (const ChatMessage& message : history) {
        CHECK(message.content.plain_text().find("working") == std::string::npos);
    }
}
