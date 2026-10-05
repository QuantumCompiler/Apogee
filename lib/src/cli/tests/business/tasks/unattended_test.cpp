#include "tasks/unattended.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "agentloop/loop.h"
#include "backends/mock.h"
#include "contracts/config.h"

/// A task's turns with nobody present, through the real loop and gate:
/// deny-by-default byte-for-byte -- an `ask`-level call denies with exactly
/// the result an unwatched gate gives, the turn going on -- every refusal
/// and every call that ran recorded, the checker's decisions unchanged; and
/// a question that ends the turn naming itself, the half-turn rolled back.
namespace {

using apogee::agent::Permission;
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

namespace t = apogee::tasks;

struct Loop {
    std::shared_ptr<MockProvider> provider;
    std::unique_ptr<Harness> harness;
};

Loop make_loop(std::vector<MockTurn> turns) {
    MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = std::move(turns);
    Loop loop;
    loop.provider = std::make_shared<MockProvider>(std::move(options));
    loop.harness = std::make_unique<Harness>(Config{});
    loop.harness->register_provider("mock", loop.provider);
    loop.harness->use_default_router();
    return loop;
}

MockTurn text(std::string answer) {
    return MockTurn{std::move(answer), {}, apogee::harness::FinishReason::Stop, {}};
}

MockTurn calls(std::vector<ToolCall> tool_calls) {
    return MockTurn{"", std::move(tool_calls), apogee::harness::FinishReason::ToolCalls, {}};
}

/// `write_file` that writes nowhere, and `read_file`, ungated.
ToolRegistry registry(int& writes) {
    ToolRegistry out;
    Tool write;
    write.name = "write_file";
    write.writes = true;
    write.describe_target = [](std::string_view) { return std::string{"out.txt"}; };
    write.run = [&writes](std::string_view) {
        ++writes;
        return ToolOutcome{"written", false};
    };
    out.add(write);
    Tool read;
    read.name = "read_file";
    read.run = [](std::string_view arguments) {
        return ToolOutcome{"read " + std::string{arguments}, false};
    };
    out.add(read);
    out.set_environment([](const ToolRegistry&) { return std::string{"the note"}; });
    return out;
}

/// The tool result the turn's history holds for `name`'s call.
std::string result_of(const std::vector<ChatMessage>& history, std::string_view name) {
    for (const ChatMessage& message : history) {
        if (message.role == Role::Tool && message.name == name) {
            return message.content.plain_text();
        }
    }
    return {};
}

std::vector<ChatMessage> run_turn(const ToolRegistry& tools,
                                  const apogee::agent::PermissionChecker& permission) {
    Loop loop = make_loop({calls({{.id = "1", .name = "write_file", .arguments = "{}"},
                                  {.id = "2", .name = "read_file", .arguments = R"({"p":1})"}}),
                           text("carried on")});
    std::vector<ChatMessage> history{ChatMessage::user("go")};
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &tools;
    options.permission = permission;  // and no confirm function: nobody is present
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(*loop.harness, history, options);
    CHECK(result.answer == "carried on");
    return history;
}

}  // namespace

TEST_CASE("an ask-level call in a task denies exactly as an unwatched gate does, and is recorded",
          "[tasks][unattended][gate]") {
    int writes = 0;
    const ToolRegistry tools = registry(writes);
    const auto ask = [](const apogee::agent::GateRequest&) { return Permission::Ask; };

    // The gate as any pipe has it.
    const std::vector<ChatMessage> plain = run_turn(tools, ask);
    CHECK(writes == 0);

    // The same turn, watched.
    const t::TurnRecorder recorder;
    const ToolRegistry observed = recorder.observe(tools);
    const std::vector<ChatMessage> watched = run_turn(observed, recorder.gate(ask));
    CHECK(writes == 0);
    // Denial-as-tool-result, byte for byte, and the turn went on.
    CHECK_FALSE(result_of(watched, "write_file").empty());
    CHECK(result_of(watched, "write_file") == result_of(plain, "write_file"));
    CHECK(result_of(watched, "write_file").starts_with("Error: the user denied permission"));
    CHECK(result_of(watched, "read_file") == result_of(plain, "read_file"));

    const std::vector<t::Denial> denials = recorder.take_denials();
    REQUIRE(denials.size() == 1);
    CHECK(denials[0].tool == "write_file");
    CHECK(denials[0].target == "out.txt");
    // Only the call that ran is activity.
    const std::vector<t::ToolUse> ran = recorder.take_tools();
    REQUIRE(ran.size() == 1);
    CHECK(ran[0].tool == "read_file");
    CHECK(ran[0].fingerprint == t::fingerprint("read_file", R"({"p":1})"));
    // Taken once.
    CHECK(recorder.take_denials().empty());
    CHECK(recorder.take_tools().empty());
}

TEST_CASE("the recorder returns the checker's decisions unchanged", "[tasks][unattended][gate]") {
    const t::TurnRecorder recorder;
    const apogee::agent::GateRequest request{"write_file", "out.txt", "", false};
    for (const Permission decision : {Permission::Allow, Permission::Deny, Permission::Ask}) {
        const auto checker =
            recorder.gate([decision](const apogee::agent::GateRequest&) { return decision; });
        CHECK(checker(request) == decision);
    }
    // Allow is no refusal; Deny and Ask are.
    CHECK(recorder.take_denials().size() == 2);
    // No checker at all asks -- which nobody answers.
    CHECK(recorder.gate({})(request) == Permission::Ask);
}

TEST_CASE("a config-allowed tool runs in a task and is recorded as activity",
          "[tasks][unattended][gate]") {
    int writes = 0;
    const ToolRegistry tools = registry(writes);
    const t::TurnRecorder recorder;
    const std::vector<ChatMessage> history = run_turn(
        recorder.observe(tools),
        recorder.gate([](const apogee::agent::GateRequest&) { return Permission::Allow; }));
    CHECK(writes == 1);
    CHECK(result_of(history, "write_file") == "written");
    CHECK(recorder.take_tools().size() == 2);
    CHECK(recorder.take_denials().empty());
}

TEST_CASE("a watched registry keeps every tool's definition and the environment note",
          "[tasks][unattended]") {
    int writes = 0;
    const ToolRegistry tools = registry(writes);
    const ToolRegistry observed = t::TurnRecorder{}.observe(tools);
    CHECK(observed.names() == tools.names());
    CHECK(observed.definition_hashes() == tools.definition_hashes());
    CHECK(observed.environment() == "the note");
    CHECK(observed.find("write_file")->writes);
}

TEST_CASE("a question in a task ends the turn naming it, the half-turn rolled back",
          "[tasks][unattended][question]") {
    Loop loop = make_loop(
        {calls({{.id = "q",
                 .name = "ask_user",
                 .arguments = R"({"questions":[{"header":"City","question":"Which city?",)"
                              R"("options":[{"label":"Paris"},{"label":"Rome"}]}]})"}}),
         text("never reached")});
    std::vector<ChatMessage> history{ChatMessage::user("go")};
    apogee::agentloop::Options options;
    options.model = "mock";
    const ToolRegistry none;
    options.tools = &none;
    options.ask = t::fail_on_question();
    // Advertised: a task fails on a question rather than never being able
    // to ask one, so 27i's declared answer has something to answer.
    CHECK(std::ranges::any_of(
        apogee::agentloop::advertised_tools(options),
        [](const apogee::harness::Tool& tool) { return tool.name == "ask_user"; }));
    try {
        (void)apogee::agentloop::run(*loop.harness, history, options);
        FAIL("the question did not end the turn");
    } catch (const t::UnansweredQuestion& question) {
        CHECK(question.question() == "Which city?");
        CHECK(std::string{question.what()} ==
              "the model asked a question and no one is present to answer it: Which city?");
    }
    // No assistant message with an unanswered call is left behind.
    REQUIRE(history.size() == 1);
    CHECK(history[0].role == Role::User);
}

TEST_CASE("several questions are named together", "[tasks][unattended][question]") {
    apogee::agentloop::QuestionRequest request;
    request.questions.push_back({.header = "A", .question = "First?"});
    request.questions.push_back({.header = "B", .question = ""});
    CHECK(t::question_text(request) == "First? / B");
}
