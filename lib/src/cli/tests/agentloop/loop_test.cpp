#include "agentloop/loop.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "backends/mock.h"
#include "harness/config.h"
#include "harness/errors.h"

using apogee::agent::Permission;
using apogee::agent::Tool;
using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::agentloop::Answers;
using apogee::agentloop::Options;
using apogee::agentloop::QuestionRequest;
using apogee::agentloop::Reporter;
using apogee::agentloop::RunResult;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using apogee::harness::Config;
using apogee::harness::Harness;
using apogee::harness::Role;
using apogee::harness::ToolCall;

namespace {

/// The scripted-provider harness the whole conformance suite runs on. Every
/// later provider must pass these same cases.
struct Fixture {
    std::shared_ptr<MockProvider> provider;
    std::unique_ptr<Harness> harness;
    std::vector<ChatMessage> history;
};

Fixture make_fixture(std::vector<MockTurn> turns) {
    MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = std::move(turns);

    Fixture fixture;
    fixture.provider = std::make_shared<MockProvider>(std::move(options));
    fixture.harness = std::make_unique<Harness>(Config{});
    fixture.harness->register_provider("mock", fixture.provider);
    fixture.harness->use_default_router();
    fixture.history = {ChatMessage::user("do the thing")};
    return fixture;
}

MockTurn text_turn(std::string text) {
    return MockTurn{std::move(text), {}, apogee::harness::FinishReason::Stop, {}};
}

MockTurn tool_turn(std::vector<ToolCall> calls) {
    return MockTurn{"", std::move(calls), apogee::harness::FinishReason::ToolCalls, {}};
}

Options options_with(const ToolRegistry& registry) {
    Options options;
    options.model = "mock";
    options.tools = &registry;
    return options;
}

/// Records every Reporter call, in order — the surface contract, observable.
class RecordingReporter final : public Reporter {
public:
    std::vector<std::string> events;
    std::string answer;
    std::string thinking;

    void on_thinking() override {
        events.emplace_back("thinking");
    }

    void on_thinking_token(std::string_view chunk) override {
        events.emplace_back("thinking_token");
        thinking += chunk;
    }

    void on_tool_status(std::string_view detail) override {
        events.emplace_back("tool_status:" + std::string{detail});
    }

    void on_clear_status() override {
        events.emplace_back("clear");
    }

    void on_answer_start() override {
        events.emplace_back("answer_start");
    }

    void on_answer_token(std::string_view chunk) override {
        events.emplace_back("answer_token");
        answer += chunk;
    }

    void on_answer_end() override {
        events.emplace_back("answer_end");
    }

    [[nodiscard]] bool saw(std::string_view name) const {
        for (const std::string& event : events) {
            if (event == name) {
                return true;
            }
        }
        return false;
    }
};

Tool echo_tool(std::string name = "echo") {
    Tool tool;
    tool.name = std::move(name);
    tool.description = "Echoes its input";
    tool.run = [](std::string_view arguments) {
        return ToolOutcome{"echoed: " + std::string{arguments}, false};
    };
    return tool;
}

}  // namespace

// ---------------------------------------------------------------------------
// The core cycle
// ---------------------------------------------------------------------------

TEST_CASE("a scripted provider drives a multi-iteration loop to an answer",
          "[agentloop][conformance]") {
    // The headline criterion: model → tool → model → answer, fully offline.
    ToolRegistry registry;
    registry.add(echo_tool());

    Fixture f = make_fixture({
        tool_turn({ToolCall{"c1", "echo", R"({"v":1})"}}),
        tool_turn({ToolCall{"c2", "echo", R"({"v":2})"}}),
        text_turn("all done"),
    });

    RecordingReporter reporter;
    const RunResult result =
        apogee::agentloop::run(*f.harness, f.history, options_with(registry), reporter);

    CHECK(result.answer == "all done");
    CHECK(result.iterations == 3);
    CHECK_FALSE(result.hit_iteration_limit);

    // History carries the full exchange: user, assistant+call, tool result,
    // assistant+call, tool result, assistant answer.
    REQUIRE(f.history.size() == 6);
    CHECK(f.history[1].role == Role::Assistant);
    CHECK(f.history[1].tool_calls.size() == 1);
    CHECK(f.history[2].role == Role::Tool);
    CHECK(f.history[2].tool_call_id == "c1");
    CHECK(f.history[2].content.plain_text() == R"(echoed: {"v":1})");
    CHECK(f.history[5].content.plain_text() == "all done");
}

TEST_CASE("several tool calls in one turn run in order", "[agentloop][conformance]") {
    // Result order must match call order, or a model that numbered its calls
    // reads the answers against the wrong questions.
    ToolRegistry registry;
    registry.add(echo_tool("a"));
    registry.add(echo_tool("b"));

    Fixture f = make_fixture({
        tool_turn({ToolCall{"c1", "a", "{}"}, ToolCall{"c2", "b", "{}"}}),
        text_turn("done"),
    });

    (void)apogee::agentloop::run(*f.harness, f.history, options_with(registry));

    REQUIRE(f.history.size() == 5);
    CHECK(f.history[1].tool_calls.size() == 2);
    CHECK(f.history[2].tool_call_id == "c1");
    CHECK(f.history[3].tool_call_id == "c2");
}

TEST_CASE("a tool error is a result the model can read, not an exception",
          "[agentloop][conformance]") {
    // Aborting the turn would throw away the conversation over a bad argument.
    // The model gets told, and gets to react.
    ToolRegistry registry;
    Tool failing;
    failing.name = "boom";
    failing.description = "always fails";
    failing.run = [](std::string_view) { return ToolOutcome{"Error: disk on fire", true}; };
    registry.add(std::move(failing));

    Fixture f = make_fixture({tool_turn({ToolCall{"c1", "boom", "{}"}}), text_turn("recovered")});

    const RunResult result = apogee::agentloop::run(*f.harness, f.history, options_with(registry));

    CHECK(result.answer == "recovered");
    CHECK(f.history[2].content.plain_text() == "Error: disk on fire");
}

TEST_CASE("a tool that throws does not take the turn down", "[agentloop][conformance]") {
    ToolRegistry registry;
    Tool thrower;
    thrower.name = "throws";
    thrower.description = "throws";
    thrower.run = [](std::string_view) -> ToolOutcome { throw std::runtime_error("unexpected"); };
    registry.add(std::move(thrower));

    Fixture f = make_fixture({tool_turn({ToolCall{"c1", "throws", "{}"}}), text_turn("ok")});

    const RunResult result = apogee::agentloop::run(*f.harness, f.history, options_with(registry));
    CHECK(result.answer == "ok");
    CHECK(f.history[2].content.plain_text().find("unexpected") != std::string::npos);
}

TEST_CASE("an unknown tool falls through as an error naming what exists",
          "[agentloop][conformance]") {
    // A model hallucinating a tool name must recover, not crash the run.
    ToolRegistry registry;
    registry.add(echo_tool("real_tool"));

    Fixture f = make_fixture({tool_turn({ToolCall{"c1", "imaginary", "{}"}}), text_turn("ok")});

    (void)apogee::agentloop::run(*f.harness, f.history, options_with(registry));

    const std::string result = f.history[2].content.plain_text();
    CHECK(result.find("no tool named 'imaginary'") != std::string::npos);
    CHECK(result.find("real_tool") != std::string::npos);
}

TEST_CASE("a tool call with no registry at all still answers", "[agentloop][conformance]") {
    Fixture f = make_fixture({tool_turn({ToolCall{"c1", "anything", "{}"}}), text_turn("ok")});

    Options options;
    options.model = "mock";
    const RunResult result = apogee::agentloop::run(*f.harness, f.history, options);

    CHECK(result.answer == "ok");
    CHECK(f.history[2].content.plain_text().find("No tools are available") != std::string::npos);
}

TEST_CASE("the iteration limit forces an answer instead of looping forever",
          "[agentloop][conformance]") {
    // A model can call the same tool indefinitely. Without a bound the only
    // symptom is a request that never returns while spending money.
    ToolRegistry registry;
    registry.add(echo_tool());

    // Every turn asks for a tool -- it would never stop on its own.
    Fixture f = make_fixture({tool_turn({ToolCall{"c", "echo", "{}"}})});

    Options options = options_with(registry);
    options.max_iterations = 3;
    const RunResult result = apogee::agentloop::run(*f.harness, f.history, options);

    CHECK(result.hit_iteration_limit);
    CHECK(result.iterations == 4);  // three tool passes, then the forced answer
    // The final call withdrew the tools, which is what forces a text answer.
    const auto& final_request = f.provider->requests().back();
    CHECK(final_request.tools.empty());
}

// ---------------------------------------------------------------------------
// ask_user
// ---------------------------------------------------------------------------

TEST_CASE("a null AskFn means ask_user is never advertised", "[agentloop][ask]") {
    // Not advertised-and-refused: absent. A model told it may ask questions on
    // a surface with nobody attached will ask one, then hang or invent an
    // answer.
    ToolRegistry registry;
    registry.add(echo_tool());

    Options options = options_with(registry);
    REQUIRE_FALSE(options.ask);

    const auto tools = apogee::agentloop::advertised_tools(options);
    for (const auto& tool : tools) {
        CHECK(tool.name != apogee::agentloop::kQuestionToolName);
    }

    Fixture f = make_fixture({text_turn("done")});
    (void)apogee::agentloop::run(*f.harness, f.history, options);

    for (const auto& tool : f.provider->requests().front().tools) {
        CHECK(tool.name != apogee::agentloop::kQuestionToolName);
    }
}

TEST_CASE("a non-null AskFn advertises ask_user", "[agentloop][ask]") {
    Options options;
    options.model = "mock";
    options.ask = [](const QuestionRequest&) { return Answers{}; };

    const auto tools = apogee::agentloop::advertised_tools(options);
    REQUIRE(tools.size() == 1);
    CHECK(tools[0].name == apogee::agentloop::kQuestionToolName);
}

TEST_CASE("a call to an unadvertised ask_user falls through as unknown-tool", "[agentloop][ask]") {
    // Never a crash, never a silent drop.
    ToolRegistry registry;
    registry.add(echo_tool());

    Fixture f = make_fixture(
        {tool_turn({ToolCall{"c1", std::string{apogee::agentloop::kQuestionToolName}, "{}"}}),
         text_turn("ok")});

    const RunResult result = apogee::agentloop::run(*f.harness, f.history, options_with(registry));

    CHECK(result.answer == "ok");
    CHECK(f.history[2].content.plain_text().find("no tool named") != std::string::npos);
}

TEST_CASE("an answered ask_user round-trips into the tool result", "[agentloop][ask]") {
    const std::string arguments = R"({"questions":[{"question":"Which database?",
        "options":[{"label":"Postgres"},{"label":"SQLite"}]}]})";

    QuestionRequest seen;
    Options options;
    options.model = "mock";
    options.ask = [&seen](const QuestionRequest& request) {
        seen = request;
        return Answers{{"Postgres"}};
    };

    Fixture f = make_fixture(
        {tool_turn({ToolCall{"c1", std::string{apogee::agentloop::kQuestionToolName}, arguments}}),
         text_turn("using Postgres")});

    const RunResult result = apogee::agentloop::run(*f.harness, f.history, options);

    REQUIRE(seen.questions.size() == 1);
    CHECK(seen.questions[0].question == "Which database?");
    // The result pairs question with answer -- a bare "Postgres" would leave the
    // model guessing which of several questions it answers.
    const std::string encoded = f.history[2].content.plain_text();
    CHECK(encoded.find("Which database?") != std::string::npos);
    CHECK(encoded.find("Postgres") != std::string::npos);
    CHECK(result.answer == "using Postgres");
}

TEST_CASE("a malformed ask_user call is a fixable error, not an aborted turn", "[agentloop][ask]") {
    Options options;
    options.model = "mock";
    options.ask = [](const QuestionRequest&) { return Answers{}; };

    Fixture f =
        make_fixture({tool_turn({ToolCall{"c1", std::string{apogee::agentloop::kQuestionToolName},
                                          R"({"questions":[]})"}}),
                      text_turn("recovered")});

    const RunResult result = apogee::agentloop::run(*f.harness, f.history, options);

    CHECK(result.answer == "recovered");
    const std::string encoded = f.history[2].content.plain_text();
    CHECK(encoded.find("Error: invalid ask_user call") != std::string::npos);
    CHECK(encoded.find("call ask_user again") != std::string::npos);
}

TEST_CASE("an aborted ask_user rolls the half-turn out of history", "[agentloop][ask][rollback]") {
    // An assistant message whose tool calls were never answered is rejected
    // outright by several providers -- so an interrupted prompt must leave
    // nothing dangling.
    const std::string arguments = R"({"questions":[{"question":"Go on?",
        "options":[{"label":"Yes"},{"label":"No"}]}]})";

    Options options;
    options.model = "mock";
    options.ask = [](const QuestionRequest&) -> Answers {
        throw apogee::harness::CancelledError();
    };

    Fixture f = make_fixture(
        {tool_turn({ToolCall{"c1", std::string{apogee::agentloop::kQuestionToolName}, arguments}}),
         text_turn("never reached")});

    const std::size_t before = f.history.size();
    CHECK_THROWS_AS(apogee::agentloop::run(*f.harness, f.history, options),
                    apogee::harness::CancelledError);

    // Exactly as it was: no assistant message, no partial tool results.
    CHECK(f.history.size() == before);
    CHECK(f.history.back().role == Role::User);
}

// ---------------------------------------------------------------------------
// The permission gate
// ---------------------------------------------------------------------------

TEST_CASE("a write tool is gated; a read tool is not", "[agentloop][permission]") {
    // Prompting for every read trains the user to approve without looking,
    // which makes the prompt worthless where it matters.
    ToolRegistry registry;
    registry.add(echo_tool("read_thing"));

    Tool writer;
    writer.name = "write_thing";
    writer.description = "writes";
    writer.writes = true;
    writer.run = [](std::string_view) { return ToolOutcome{"wrote it", false}; };
    registry.add(std::move(writer));

    SECTION("allow lets it through") {
        Fixture f =
            make_fixture({tool_turn({ToolCall{"c1", "write_thing", "{}"}}), text_turn("done")});
        Options options = options_with(registry);
        options.permission = [](std::string_view, std::string_view) { return Permission::Allow; };

        (void)apogee::agentloop::run(*f.harness, f.history, options);
        CHECK(f.history[2].content.plain_text() == "wrote it");
    }

    SECTION("deny is honoured and tells the model not to retry") {
        Fixture f = make_fixture(
            {tool_turn({ToolCall{"c1", "write_thing", "{}"}}), text_turn("understood")});
        Options options = options_with(registry);
        options.permission = [](std::string_view, std::string_view) { return Permission::Deny; };

        (void)apogee::agentloop::run(*f.harness, f.history, options);
        const std::string result = f.history[2].content.plain_text();
        CHECK(result.find("denied permission") != std::string::npos);
        CHECK(result.find("Do not retry") != std::string::npos);
    }

    SECTION("ask consults the confirm function") {
        Fixture f =
            make_fixture({tool_turn({ToolCall{"c1", "write_thing", "{}"}}), text_turn("done")});
        bool asked = false;
        Options options = options_with(registry);
        options.permission = [](std::string_view, std::string_view) { return Permission::Ask; };
        options.confirm = [&asked](std::string_view, std::string_view) {
            asked = true;
            return true;
        };

        (void)apogee::agentloop::run(*f.harness, f.history, options);
        CHECK(asked);
        CHECK(f.history[2].content.plain_text() == "wrote it");
    }

    SECTION("a read tool never consults the gate") {
        Fixture f =
            make_fixture({tool_turn({ToolCall{"c1", "read_thing", "{}"}}), text_turn("done")});
        bool consulted = false;
        Options options = options_with(registry);
        options.permission = [&consulted](std::string_view, std::string_view) {
            consulted = true;
            return Permission::Deny;
        };

        (void)apogee::agentloop::run(*f.harness, f.history, options);
        CHECK_FALSE(consulted);
        CHECK(f.history[2].content.plain_text().find("echoed") != std::string::npos);
    }
}

TEST_CASE("non-interactive ask resolves to deny", "[agentloop][permission]") {
    // A pipe, a cron job, or `serve` has nobody to ask. Allowing a destructive
    // operation because no one was around to object is the wrong direction to
    // fail.
    ToolRegistry registry;
    Tool writer;
    writer.name = "write_thing";
    writer.description = "writes";
    writer.writes = true;
    writer.run = [](std::string_view) { return ToolOutcome{"wrote it", false}; };
    registry.add(std::move(writer));

    Fixture f = make_fixture({tool_turn({ToolCall{"c1", "write_thing", "{}"}}), text_turn("ok")});

    Options options = options_with(registry);
    options.permission = [](std::string_view, std::string_view) { return Permission::Ask; };
    // No confirm function -- nobody to ask.
    REQUIRE_FALSE(options.confirm);

    (void)apogee::agentloop::run(*f.harness, f.history, options);
    CHECK(f.history[2].content.plain_text().find("denied permission") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Transient content and the Reporter contract
// ---------------------------------------------------------------------------

TEST_CASE("transient content reaches the request but never history", "[agentloop][transient]") {
    // If injected context landed in history it would be re-sent on every later
    // turn, growing the prompt without bound and feeding the model material it
    // was told applied to one question.
    Fixture f = make_fixture({text_turn("answered")});

    Options options;
    options.model = "mock";
    options.transient_prefix = {ChatMessage::system("INJECTED RAG CONTEXT")};
    options.transient_at = 0;

    (void)apogee::agentloop::run(*f.harness, f.history, options);

    // It reached the provider...
    const auto& request = f.provider->requests().front();
    REQUIRE(request.messages.size() == 2);
    CHECK(request.messages[0].content.plain_text() == "INJECTED RAG CONTEXT");
    // ...with the markers a prompt-caching provider reads...
    CHECK(request.transient.start == 0);
    CHECK(request.transient.length == 1);
    CHECK(request.is_transient(0));
    // ...and it is absent from persisted history.
    for (const ChatMessage& message : f.history) {
        CHECK(message.content.plain_text().find("INJECTED") == std::string::npos);
    }
}

TEST_CASE("the Reporter sees a coherent event sequence", "[agentloop][reporter]") {
    ToolRegistry registry;
    registry.add(echo_tool());

    Fixture f = make_fixture({tool_turn({ToolCall{"c1", "echo", "{}"}}), text_turn("final")});

    RecordingReporter reporter;
    (void)apogee::agentloop::run(*f.harness, f.history, options_with(registry), reporter);

    CHECK(reporter.saw("thinking"));
    CHECK(reporter.saw("tool_status:[tool] echo"));
    CHECK(reporter.saw("answer_start"));
    CHECK(reporter.saw("answer_end"));
    CHECK(reporter.answer == "final");

    // answer_start precedes its tokens and answer_end closes them.
    const auto start = std::find(reporter.events.begin(), reporter.events.end(), "answer_start");
    const auto end = std::find(reporter.events.begin(), reporter.events.end(), "answer_end");
    REQUIRE(start != reporter.events.end());
    REQUIRE(end != reporter.events.end());
    CHECK(start < end);
}

TEST_CASE("stream_answer false still reports status but emits no answer tokens",
          "[agentloop][reporter]") {
    // A caller that only wants the returned text -- a background clerk -- gets
    // progress without the answer being pushed at it twice.
    Fixture f = make_fixture({text_turn("quiet answer")});

    RecordingReporter reporter;
    Options options;
    options.model = "mock";
    options.stream_answer = false;

    const RunResult result = apogee::agentloop::run(*f.harness, f.history, options, reporter);

    CHECK(result.answer == "quiet answer");
    CHECK(reporter.saw("thinking"));
    CHECK_FALSE(reporter.saw("answer_token"));
    CHECK(reporter.answer.empty());
}

TEST_CASE("a run with no reporter still works", "[agentloop][reporter]") {
    Fixture f = make_fixture({text_turn("fine")});
    Options options;
    options.model = "mock";
    CHECK(apogee::agentloop::run(*f.harness, f.history, options).answer == "fine");
}

TEST_CASE("a cancelled token aborts the loop", "[agentloop]") {
    Fixture f = make_fixture({text_turn("never")});
    Options options;
    options.model = "mock";
    options.cancellation = apogee::harness::CancellationToken::create();
    options.cancellation.cancel();

    CHECK_THROWS_AS(apogee::agentloop::run(*f.harness, f.history, options),
                    apogee::harness::CancelledError);
}

TEST_CASE("a provider error clears the status before propagating", "[agentloop]") {
    Harness harness{Config{}};
    harness.use_default_router();  // no providers registered

    std::vector<ChatMessage> history{ChatMessage::user("x")};
    RecordingReporter reporter;
    Options options;
    options.model = "ghost";

    CHECK_THROWS_AS(apogee::agentloop::run(harness, history, options, reporter),
                    apogee::harness::NoAvailableBackendError);
    CHECK(reporter.saw("clear"));
}
