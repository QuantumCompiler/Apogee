#include "agentloop/reasoning_summary.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "harness/harness.h"

/// The reasoning as one line while it streams (43): the line's shape, the
/// cadence and its bounds, and the routing -- which reasoning is summarised,
/// on which member, and what is said when none can be.
namespace {

using apogee::agentloop::kSummaryEvery;
using apogee::agentloop::kSummaryFirst;
using apogee::agentloop::ReasoningSummarizer;
using apogee::agentloop::summary_line;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using Clock = std::chrono::steady_clock;

/// A clock a test moves by hand.
struct Hands {
    Clock::time_point now{};

    [[nodiscard]] ReasoningSummarizer::Clock clock() {
        return [this] { return now; };
    }
};

/// A chat model whose one turn reasons `reasoning` and answers, a helper that
/// answers each summary request in turn, all on one harness.
struct World {
    apogee::harness::Harness harness{apogee::harness::Config{}};
    std::shared_ptr<MockProvider> chat;
    std::shared_ptr<MockProvider> helper;

    World(std::string reasoning, std::vector<std::string> lines, bool helper_metered = false,
          bool summarized = false, bool helper_fails = false) {
        MockProvider::Options chat_options;
        chat_options.backend_name = "chat";
        chat_options.turns = {MockTurn{.text = "the answer", .reasoning = std::move(reasoning)}};
        chat_options.behavior.reasoning_summarized = summarized;
        chat = std::make_shared<MockProvider>(std::move(chat_options));
        MockProvider::Options helper_options;
        helper_options.backend_name = "helper";
        helper_options.metered = helper_metered;
        if (helper_fails) {
            helper_options.on_request = [](const apogee::harness::ChatRequest&) {
                throw apogee::harness::ProviderError("helper", "the helper is down");
            };
        }
        for (std::string& line : lines) {
            helper_options.turns.push_back(MockTurn{.text = std::move(line)});
        }
        helper = std::make_shared<MockProvider>(std::move(helper_options));
        harness.register_provider("chat", chat);
        harness.register_provider("helper", helper);
        harness.use_default_router();
    }
};

/// What a run says about its reasoning.
struct Heard {
    std::string reasoning;
    std::vector<std::string> summaries;
    std::vector<std::string> raw;
};

/// The reporter that hears it.
struct Listener : apogee::agentloop::Reporter {
    Heard heard;

    void on_thinking_token(std::string_view chunk) override {
        heard.reasoning += chunk;
    }

    void on_thinking_summary(std::string_view line) override {
        heard.summaries.emplace_back(line);
    }

    void on_thinking_raw(std::string_view why) override {
        heard.raw.emplace_back(why);
    }
};

/// A clock that moves a second each time it is read: a reasoning stream of a
/// few dozen chunks then spans a minute.
[[nodiscard]] ReasoningSummarizer::Clock ticking() {
    auto now = std::make_shared<Clock::time_point>();
    return [now] {
        *now += std::chrono::seconds{1};
        return *now;
    };
}

/// One run of `world` asking for summaries by `summary_model`.
Heard run(World& world, bool summarize, std::string summary_model) {
    std::vector<ChatMessage> history{ChatMessage::user("why?")};
    apogee::agentloop::Options options;
    options.model = "chat";
    options.summarize_reasoning = summarize;
    options.reasoning_summary_model = std::move(summary_model);
    options.summary_clock = ticking();
    Listener listener;
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(world.harness, history, options, listener);
    CHECK(result.answer == "the answer");
    return listener.heard;
}

const std::string kReasoning(400, 'r');

}  // namespace

TEST_CASE("the summary request shows the reasoning's tail and thinks nothing of its own",
          "[agentloop][reasoning_summary]") {
    const apogee::harness::ChatRequest request =
        apogee::agentloop::reasoning_summary_request("helper", "weighing the two paths");
    CHECK(request.model == "helper");
    CHECK(request.transient.side_request);
    CHECK(request.thinking.mode == apogee::harness::ThinkingMode::Off);
    CHECK(request.max_tokens == apogee::agentloop::kSummaryTokens);
    REQUIRE(request.messages.size() == 1);
    const std::string prompt = request.messages.front().content.plain_text();
    CHECK(prompt.find("present-tense phrase") != std::string::npos);
    CHECK(prompt.ends_with("Reasoning:\nweighing the two paths"));
}

TEST_CASE("a reply's line is its first with words, unmarked, unquoted, and cut short",
          "[agentloop][reasoning_summary]") {
    CHECK(summary_line("Comparing the two config paths.") == "Comparing the two config paths");
    CHECK(summary_line("\n\n  - checking the cache  \nand more") == "Checking the cache");
    CHECK(summary_line("\"weighing   options\"") == "Weighing options");
    CHECK(summary_line("“reading the logs”") == "Reading the logs");
    // The subject a small model puts first is dropped (found on real weights).
    CHECK(summary_line("It is parsing the problem.") == "Parsing the problem");
    CHECK(summary_line("it's checking the CMake files") == "Checking the CMake files");
    CHECK(summary_line("It is") == "It is");
    CHECK(summary_line(" \n\t ").empty());
    const std::string cut = summary_line(std::string(200, 'w'));
    CHECK(cut == "W" + std::string(apogee::agentloop::kSummaryLine - 1, 'w') + "…");
    // Never inside a character: eighty of them, each two bytes.
    std::string accents;
    for (int i = 0; i < 100; ++i) {
        accents += "é";
    }
    CHECK(summary_line(accents).ends_with("é…"));
}

TEST_CASE("the cadence: the first line once the reasoning has run, the next only later and newer",
          "[agentloop][reasoning_summary]") {
    World world{"", {"Opening the file", "Reading the config", "Reading the config"}};
    Hands hands;
    ReasoningSummarizer summarizer{world.harness, "helper", {}, hands.clock()};
    const std::string piece(100, 'x');

    // Not yet: too soon, then too little.
    CHECK_FALSE(summarizer.feed(piece).has_value());
    hands.now += kSummaryFirst;
    CHECK_FALSE(summarizer.feed("").has_value());
    CHECK(summarizer.feed(piece) == std::optional<std::string>{"Opening the file"});
    CHECK(world.helper->requests().size() == 1);
    // The line stands until the cadence is up, whatever arrives.
    hands.now += kSummaryEvery - std::chrono::milliseconds{1};
    CHECK_FALSE(summarizer.feed(piece).has_value());
    hands.now += std::chrono::milliseconds{1};
    CHECK(summarizer.feed(piece) == std::optional<std::string>{"Reading the config"});
    // Due, but too little new reasoning: nothing asked.
    hands.now += kSummaryEvery;
    CHECK_FALSE(summarizer.feed("tiny").has_value());
    CHECK(world.helper->requests().size() == 2);
    // A line no different from the last is not said again.
    CHECK_FALSE(summarizer.feed(piece + piece).has_value());
    CHECK(world.helper->requests().size() == 3);
    CHECK_FALSE(summarizer.failed());
    // The request carried the reasoning's tail, never more than the bound.
    const std::string shown = world.helper->requests().back().messages.front().content.plain_text();
    CHECK(shown.size() < apogee::agentloop::kSummaryInput + 400);
}

TEST_CASE("a summary that fails ends the summaries; a cancelled one ends them quietly",
          "[agentloop][reasoning_summary]") {
    World down{"", {}, false, false, true};
    Hands hands;
    ReasoningSummarizer failing{down.harness, "helper", {}, hands.clock()};
    CHECK_FALSE(failing.feed(std::string(200, 'x')).has_value());
    hands.now += kSummaryFirst;
    CHECK_FALSE(failing.feed(std::string(200, 'x')).has_value());
    CHECK(failing.failed());
    // Never asked again.
    hands.now += kSummaryEvery;
    CHECK_FALSE(failing.feed(std::string(200, 'x')).has_value());
    CHECK(down.helper->requests().size() == 1);

    World world{"", {"unused"}};
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    ReasoningSummarizer cancelled{world.harness, "helper", token, hands.clock()};
    CHECK_FALSE(cancelled.feed(std::string(200, 'x')).has_value());
    token.cancel();
    hands.now += kSummaryFirst;
    CHECK_FALSE(cancelled.feed(std::string(200, 'x')).has_value());
    CHECK_FALSE(cancelled.failed());
    hands.now += kSummaryEvery;
    CHECK_FALSE(cancelled.feed(std::string(200, 'x')).has_value());
}

TEST_CASE(
    "the loop summarises raw reasoning on an unmetered member, the stream still reaching "
    "the surface",
    "[agentloop][reasoning_summary]") {
    const std::vector<std::string> lines{"Tracing the bug", "Testing a fix", "Writing it up",
                                         "Checking the answer"};
    World world{kReasoning, lines};
    const Heard heard = run(world, true, "helper");
    CHECK(heard.reasoning == kReasoning);
    CHECK(heard.raw.empty());
    // A line per call, each said as it came, between the chat model's chunks.
    const std::size_t asked = world.helper->requests().size();
    REQUIRE(asked >= 2);
    REQUIRE(asked <= lines.size());
    CHECK(heard.summaries == std::vector<std::string>(lines.begin(), lines.begin() + asked));
    for (const apogee::harness::ChatRequest& request : world.helper->requests()) {
        CHECK(request.transient.side_request);
    }
}

TEST_CASE("what the loop says when the reasoning is shown raw, once, and never asks then",
          "[agentloop][reasoning_summary]") {
    SECTION("a vendor's own summary: shown as it is, nothing said") {
        World world{kReasoning, {"unused"}, false, true};
        const Heard heard = run(world, true, "helper");
        CHECK(heard.raw == std::vector<std::string>{""});
        CHECK(heard.summaries.empty());
        CHECK(world.helper->requests().empty());
    }
    SECTION("no utility model") {
        World world{kReasoning, {}};
        CHECK(run(world, true, "").raw ==
              std::vector<std::string>{"no utility model is set to summarise it"});
    }
    SECTION("the utility model is the chat model") {
        World world{kReasoning, {}};
        CHECK(run(world, true, "chat").raw ==
              std::vector<std::string>{
                  "the utility model is the chat model, which cannot summarise while it thinks"});
        CHECK(world.chat->requests().size() == 1);
    }
    SECTION("a metered utility model is never asked") {
        World world{kReasoning, {"unused"}, true};
        CHECK(run(world, true, "helper").raw ==
              std::vector<std::string>{
                  "the utility model, helper, is billed per call, and a summary is not worth a "
                  "charge"});
        CHECK(world.helper->requests().empty());
    }
    SECTION("a summary that fails: the reasoning shown raw from there") {
        World world{kReasoning, {}, false, false, true};
        const Heard heard = run(world, true, "helper");
        CHECK(heard.raw == std::vector<std::string>{"the utility model could not summarise it"});
    }
    SECTION("not asked for: nothing new said, nothing asked") {
        World world{kReasoning, {"unused"}};
        const Heard heard = run(world, false, "helper");
        CHECK(heard.raw.empty());
        CHECK(heard.summaries.empty());
        CHECK(heard.reasoning == kReasoning);
        CHECK(world.helper->requests().empty());
    }
    SECTION("no reasoning at all: nothing said") {
        World world{"", {}};
        CHECK(run(world, true, "").raw.empty());
    }
}

TEST_CASE("raw after all is said each model call, and why only the first time in the run",
          "[agentloop][reasoning_summary]") {
    // A turn that calls a tool reasons twice; each call opens its own block
    // on a surface, so each is told -- the reason once.
    apogee::harness::Harness harness{apogee::harness::Config{}};
    MockProvider::Options chat_options;
    chat_options.backend_name = "chat";
    chat_options.turns = {
        MockTurn{.text = "",
                 .tool_calls = {apogee::harness::ToolCall{.id = "c1", .name = "probe"}},
                 .finish_reason = apogee::harness::FinishReason::ToolCalls,
                 .reasoning = "first thoughts"},
        MockTurn{.text = "the answer", .reasoning = "second thoughts"}};
    harness.register_provider("chat", std::make_shared<MockProvider>(std::move(chat_options)));
    harness.use_default_router();
    apogee::agent::ToolRegistry tools;
    apogee::agent::Tool probe;
    probe.name = "probe";
    probe.description = "probe";
    probe.run = [](std::string_view) { return apogee::agent::ToolOutcome{"ok"}; };
    tools.add(probe);

    std::vector<ChatMessage> history{ChatMessage::user("go")};
    apogee::agentloop::Options options;
    options.model = "chat";
    options.tools = &tools;
    options.summarize_reasoning = true;
    Listener listener;
    (void)apogee::agentloop::run(harness, history, options, listener);
    CHECK(listener.heard.raw ==
          std::vector<std::string>{"no utility model is set to summarise it", ""});
    CHECK(listener.heard.reasoning == "first thoughtssecond thoughts");
}
