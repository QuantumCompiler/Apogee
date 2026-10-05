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
/// And with the authority handed to it (27i): every call let through
/// recorded with whose authority -- the config's, the grant's, the person's
/// -- every refusal with its reason, a declared answer consumed and
/// recorded, and the person at the terminal asked and recorded either way.
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
                                  const apogee::agent::PermissionChecker& permission,
                                  const apogee::agent::ConfirmFn& confirm = {}) {
    Loop loop = make_loop({calls({{.id = "1", .name = "write_file", .arguments = "{}"},
                                  {.id = "2", .name = "read_file", .arguments = R"({"p":1})"}}),
                           text("carried on")});
    std::vector<ChatMessage> history{ChatMessage::user("go")};
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &tools;
    options.permission = permission;
    options.confirm = confirm;  // null: nobody is present
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
    const t::WatchedGate gate = recorder.watch(ask, ask, {}, nullptr);
    CHECK_FALSE(gate.confirm);  // nobody present stays nobody present
    const std::vector<ChatMessage> watched = run_turn(observed, gate.permission, gate.confirm);
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
    CHECK(denials[0].by == t::kByNobody);
    CHECK(recorder.take_allowed().empty());
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
        const auto checker = [decision](const apogee::agent::GateRequest&) { return decision; };
        CHECK(recorder.watch(checker, checker, {}, nullptr).permission(request) == decision);
    }
    // Allow is no refusal -- the config's own, since the standing checker
    // allows it too; Deny is the config's refusal, and Ask nobody's.
    const std::vector<t::Permit> allowed = recorder.take_allowed();
    REQUIRE(allowed.size() == 1);
    CHECK(allowed[0].by == t::kByConfig);
    CHECK(allowed[0].target == "out.txt");
    const std::vector<t::Denial> denials = recorder.take_denials();
    REQUIRE(denials.size() == 2);
    CHECK(denials[0].by == t::kByConfig);
    CHECK(denials[1].by == t::kByNobody);
    // No checker at all asks -- which nobody answers.
    CHECK(recorder.watch({}, {}, {}, nullptr).permission(request) == Permission::Ask);
}

TEST_CASE("a config-allowed tool runs in a task and is recorded as activity",
          "[tasks][unattended][gate]") {
    int writes = 0;
    const ToolRegistry tools = registry(writes);
    const t::TurnRecorder recorder;
    const auto allow = [](const apogee::agent::GateRequest&) { return Permission::Allow; };
    const t::WatchedGate gate = recorder.watch(allow, allow, {"write_file"}, nullptr);
    const std::vector<ChatMessage> history = run_turn(recorder.observe(tools), gate.permission);
    CHECK(writes == 1);
    CHECK(result_of(history, "write_file") == "written");
    CHECK(recorder.take_tools().size() == 2);
    CHECK(recorder.take_denials().empty());
    // The config allowed it: a grant of the same tool is not what let it run.
    const std::vector<t::Permit> allowed = recorder.take_allowed();
    REQUIRE(allowed.size() == 1);
    CHECK(allowed[0].by == t::kByConfig);
}

TEST_CASE("a granted tool runs unprompted and is recorded as the grant's, with its target",
          "[tasks][unattended][gate][grant]") {
    int writes = 0;
    const ToolRegistry tools = registry(writes);
    const t::TurnRecorder recorder;
    // The task's checker allows what the config alone asks about.
    const auto granted = [](const apogee::agent::GateRequest&) { return Permission::Allow; };
    const auto standing = [](const apogee::agent::GateRequest&) { return Permission::Ask; };
    const t::WatchedGate gate = recorder.watch(granted, standing, {"write_file"}, nullptr);
    const std::vector<ChatMessage> history = run_turn(recorder.observe(tools), gate.permission);
    CHECK(writes == 1);
    CHECK(result_of(history, "write_file") == "written");
    const std::vector<t::Permit> allowed = recorder.take_allowed();
    REQUIRE(allowed.size() == 1);
    CHECK(allowed[0].tool == "write_file");
    CHECK(allowed[0].target == "out.txt");
    CHECK(allowed[0].by == t::kByGrant);
    CHECK(recorder.take_denials().empty());

    // An allow the grant does not name is the person's -- an earlier
    // `session` answer -- and an outbound call is never the grant's.
    const t::WatchedGate unnamed = recorder.watch(granted, standing, {"edit_file"}, nullptr);
    (void)unnamed.permission(apogee::agent::GateRequest{"write_file", "out.txt", "", false});
    (void)gate.permission(apogee::agent::GateRequest{"write_file", "example.org", "", true});
    const std::vector<t::Permit> others = recorder.take_allowed();
    REQUIRE(others.size() == 2);
    CHECK(others[0].by == t::kByPerson);
    CHECK(others[1].by == t::kByPerson);
}

TEST_CASE("with someone present the prompt decides, recorded either way",
          "[tasks][unattended][gate][attended]") {
    int writes = 0;
    const ToolRegistry tools = registry(writes);
    const auto ask = [](const apogee::agent::GateRequest&) { return Permission::Ask; };
    for (const bool yes : {true, false}) {
        const t::TurnRecorder recorder;
        int asked = 0;
        const t::WatchedGate gate =
            recorder.watch(ask, ask, {}, [&asked, yes](const apogee::agent::GateRequest&) {
                ++asked;
                return yes;
            });
        REQUIRE(gate.confirm);
        const std::vector<ChatMessage> history =
            run_turn(recorder.observe(tools), gate.permission, gate.confirm);
        CHECK(asked == 1);
        if (yes) {
            CHECK(result_of(history, "write_file") == "written");
            const std::vector<t::Permit> allowed = recorder.take_allowed();
            REQUIRE(allowed.size() == 1);
            CHECK(allowed[0].by == t::kByPerson);
            CHECK(recorder.take_denials().empty());
        } else {
            CHECK(result_of(history, "write_file").starts_with("Error: the user denied"));
            const std::vector<t::Denial> denials = recorder.take_denials();
            REQUIRE(denials.size() == 1);
            CHECK(denials[0].by == t::kByPerson);
            CHECK(recorder.take_allowed().empty());
        }
    }
    CHECK(writes == 1);
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

namespace {

std::vector<ChatMessage> ask_turn(const apogee::agentloop::AskFn& ask, std::string& answer) {
    Loop loop = make_loop(
        {calls({{.id = "q",
                 .name = "ask_user",
                 .arguments = R"({"questions":[{"header":"Colour","question":"Which colour?",)"
                              R"("options":[{"label":"Red"},{"label":"Green"}]}]})"}}),
         text("going with it")});
    std::vector<ChatMessage> history{ChatMessage::user("go")};
    apogee::agentloop::Options options;
    options.model = "mock";
    const ToolRegistry none;
    options.tools = &none;
    options.ask = ask;
    answer = apogee::agentloop::run(*loop.harness, history, options).answer;
    return history;
}

}  // namespace

TEST_CASE("a declared answer is given to every question, and each is recorded",
          "[tasks][unattended][question][declared]") {
    const t::TurnRecorder recorder;
    std::string answer;
    const std::vector<ChatMessage> history = ask_turn(recorder.declared_answer("blue"), answer);
    // The question was answered, not failed: the turn went on.
    CHECK(answer == "going with it");
    CHECK(result_of(history, "ask_user").find("Which colour?\n  blue") != std::string::npos);
    const std::vector<t::Answered> answered = recorder.take_answered();
    REQUIRE(answered.size() == 1);
    CHECK(answered[0].question == "Which colour?");
    CHECK(answered[0].answer == "blue");
    CHECK(answered[0].by == t::kByDeclared);
    CHECK(recorder.take_answered().empty());

    // Several at once: the one answer, each.
    apogee::agentloop::QuestionRequest request;
    request.questions.push_back({.header = "A", .question = "First?"});
    request.questions.push_back({.header = "B", .question = ""});
    const apogee::agentloop::Answers answers = recorder.declared_answer("blue")(request);
    CHECK(answers.values == std::vector<std::string>{"blue", "blue"});
    const std::vector<t::Answered> both = recorder.take_answered();
    REQUIRE(both.size() == 2);
    CHECK(both[1].question == "B");
}

TEST_CASE("the person at the terminal answers a question, recorded as theirs",
          "[tasks][unattended][question][attended]") {
    const t::TurnRecorder recorder;
    CHECK_FALSE(recorder.person({}));  // nobody there: no ask_user to wrap
    std::string answer;
    const std::vector<ChatMessage> history =
        ask_turn(recorder.person([](const apogee::agentloop::QuestionRequest&) {
            return apogee::agentloop::Answers{{"Green"}};
        }),
                 answer);
    CHECK(answer == "going with it");
    const std::vector<t::Answered> answered = recorder.take_answered();
    REQUIRE(answered.size() == 1);
    CHECK(answered[0].answer == "Green");
    CHECK(answered[0].by == t::kByPerson);
}
