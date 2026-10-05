#include "tasks/runner.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "logger/session.h"
#include "support/env_guard.h"
#include "tasks/ledger.h"

/// The outer loop against a scripted turn: the whole cycle -- plan, an
/// incomplete round, the corrective round, done -- with the ledger's
/// transition sequence; the budget and the breaker each stopping honestly;
/// a question, a provider failure, a cancel from another process, Ctrl-C
/// and a halt; a resume that re-runs an interrupted round with exactly the
/// message it was first sent, and one that takes a finished turn's answer
/// from the session rather than asking twice.
namespace {

namespace t = apogee::tasks;
using apogee::harness::ChatMessage;

/// One scripted turn.
struct Step {
    std::string answer;
    /// Wait for the cancellation token, then end cancelled.
    bool wait_for_cancel = false;
    std::string question;
    std::string error;
    std::vector<t::ToolUse> tools;
    std::vector<t::Denial> denied;
    std::vector<t::Permit> allowed;
    std::vector<t::Answered> answered;
    /// Run first: what another process -- or Ctrl-C -- does meanwhile.
    std::function<void()> before;
};

/// The turn the command hands the runner, played from a script: a
/// completed turn is appended to the session and saved, as `run_chat_turn`
/// does; anything else leaves the session as it was.
struct Scripted {
    std::vector<Step> steps;
    std::size_t next = 0;
    apogee::logger::Session session;
    std::vector<t::TurnRequest> seen;
    /// Where the ledger is, and what it said on disk as each turn began.
    std::filesystem::path root;
    std::vector<std::string> on_disk;

    t::TurnResult operator()(const t::TurnRequest& request) {
        seen.push_back(request);
        std::string error;
        if (const std::optional<t::Task> written =
                t::load_task(root, "task-20261004-120000", error);
            written.has_value() && !written->transitions.empty()) {
            on_disk.push_back(written->transitions.back().event + " " +
                              std::to_string(written->transitions.back().round));
        }
        REQUIRE(next < steps.size());
        const Step step = steps[next++];
        if (step.before) {
            step.before();
        }
        t::TurnResult result;
        result.tools = step.tools;
        result.denied = step.denied;
        result.allowed = step.allowed;
        result.answered = step.answered;
        if (step.wait_for_cancel) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
            while (!request.cancellation.stop_requested() &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
            result.cancelled = request.cancellation.stop_requested();
            return result;
        }
        if (!step.question.empty()) {
            result.question = step.question;
            return result;
        }
        if (!step.error.empty()) {
            result.error = step.error;
            return result;
        }
        session.messages.push_back(ChatMessage::user(request.message));
        session.messages.push_back(ChatMessage::assistant(step.answer));
        ++session.turns;
        apogee::logger::save(session);
        result.completed = true;
        result.answer = step.answer;
        result.tokens = 10;
        return result;
    }
};

struct Fixture {
    apogee::testing::TempDir home{"task-runner-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path root = home.path() / "tasks";
    Scripted scripted;
    std::vector<std::string> said;

    Fixture() {
        scripted.root = root;
        scripted.session.chat_id = "20261004-120000-abcd";
        scripted.session.task = "task-20261004-120000";
        apogee::logger::save(scripted.session);
    }

    [[nodiscard]] t::Task task(std::vector<t::Check> checks, int budget = 8) const {
        t::Task made;
        made.id = "task-20261004-120000";
        made.goal = "Find the answer";
        made.checks = std::move(checks);
        made.rounds_budget = budget;
        made.session_id = scripted.session.chat_id;
        made.working_directory = home.path().string();
        return made;
    }

    t::RunOutcome run(t::Task task, bool resume = false,
                      apogee::harness::CancellationToken interrupt = {}) {
        return t::run_task(t::RunRequest{
            .root = root,
            .task = std::move(task),
            .resume = resume,
            .turn = [this](const t::TurnRequest& request) { return scripted(request); },
            .say = [this](std::string_view line) { said.emplace_back(line); },
            .interrupt = std::move(interrupt),
            .poll = std::chrono::milliseconds{5}});
    }

    [[nodiscard]] t::Task on_disk() const {
        std::string error;
        std::optional<t::Task> loaded = t::load_task(root, "task-20261004-120000", error);
        REQUIRE(loaded.has_value());
        return *loaded;
    }
};

std::vector<std::string> events(const t::Task& task) {
    std::vector<std::string> out;
    for (const t::Transition& transition : task.transitions) {
        out.push_back(
            transition.event +
            (transition.round > 0 ? " " + std::to_string(transition.round) : std::string{}) + ":" +
            transition.status);
    }
    return out;
}

const std::vector<t::Check> kAnswer42{{t::CheckKind::Require, "42"}};

}  // namespace

TEST_CASE("a task plans, corrects an incomplete round and finishes, every transition written",
          "[tasks][runner]") {
    Fixture fixture;
    fixture.scripted.steps = {{.answer = "1. Think.\n2. Answer."},
                              {.answer = "Not yet.\nTASK STATUS: NOT DONE"},
                              {.answer = "It is 42.\nTASK STATUS: DONE"}};
    const t::Task created = fixture.task(kAnswer42);
    const t::RunOutcome outcome = fixture.run(created);
    REQUIRE(outcome.error.empty());
    const t::Task& task = outcome.task;
    CHECK(task.status == t::kDone);
    CHECK(task.plan == "1. Think.\n2. Answer.");
    CHECK(t::rounds_used(task) == 2);
    CHECK(events(task) == std::vector<std::string>{
                              "started:planning", "plan_started:planning", "plan_recorded:running",
                              "round_started 1:running", "round_ended 1:running",
                              "round_started 2:running", "round_ended 2:done", "finished:done"});
    // The ledger on disk is the record the run ended with -- and was written
    // at every transition: each turn began with its own start already there.
    CHECK(t::task_to_json(fixture.on_disk()) == t::task_to_json(task));
    CHECK(fixture.scripted.on_disk ==
          std::vector<std::string>{"plan_started 0", "round_started 1", "round_started 2"});

    // What each turn was sent: the plan, the plan carried out, then the
    // correction naming what failed.
    REQUIRE(fixture.scripted.seen.size() == 3);
    CHECK(fixture.scripted.seen[0].message == t::plan_message(created));
    CHECK(fixture.scripted.seen[0].round == 0);
    CHECK(fixture.scripted.seen[0].kind == t::kPlanRound);
    CHECK(fixture.scripted.seen[1].kind == t::kExecuteRound);
    CHECK(fixture.scripted.seen[1].message.find("Your plan:\n1. Think.") != std::string::npos);
    CHECK(fixture.scripted.seen[2].kind == t::kCorrectRound);
    CHECK(fixture.scripted.seen[2].message.find(
              "- FAILED: the answer contains \"42\" -- not in the answer") != std::string::npos);
    CHECK(fixture.scripted.seen[2].message.find("you reported it not done") != std::string::npos);
    CHECK(fixture.scripted.seen[2].budget == 8);

    // Each round knows the session's turns before it, and its tokens.
    CHECK(task.rounds[1].session_turns_before == 1);
    CHECK(task.rounds[2].session_turns_before == 2);
    CHECK(task.rounds[2].tokens == 10);
    CHECK(std::ranges::find(fixture.said, "[task] round 2: 1 of 1 check passed, reported done") !=
          fixture.said.end());
}

TEST_CASE("a spent budget ends the task exhausted, naming the checks that never passed",
          "[tasks][runner][budget]") {
    Fixture fixture;
    // New tool activity every round, so only the budget can stop it.
    fixture.scripted.steps = {
        {.answer = "plan"},
        {.answer = "no", .tools = {{.tool = "read_file", .fingerprint = "a"}}},
        {.answer = "no", .tools = {{.tool = "read_file", .fingerprint = "b"}}},
        {.answer = "no", .tools = {{.tool = "read_file", .fingerprint = "c"}}}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42, 3));
    CHECK(outcome.task.status == t::kExhausted);
    CHECK(outcome.task.reason ==
          "the round budget (3) is spent and the task is not done -- not passed: the answer "
          "contains \"42\" (not in the answer); not reported done by the model");
    CHECK(fixture.scripted.seen.size() == 4);
    CHECK(t::rounds_used(outcome.task) == 3);
    // Spent is final: it does not run again.
    CHECK_FALSE(t::resume_refusal(outcome.task).empty());
    const t::RunOutcome again = fixture.run(outcome.task, true);
    CHECK(again.error.find("spent its round budget (3)") != std::string::npos);
    CHECK(fixture.scripted.seen.size() == 4);
}

TEST_CASE("two rounds without progress trip the breaker, naming what did not pass",
          "[tasks][runner][breaker]") {
    Fixture fixture;
    fixture.scripted.steps = {{.answer = "plan"},
                              {.answer = "no", .tools = {{.tool = "t", .fingerprint = "a"}}},
                              {.answer = "no", .tools = {{.tool = "t", .fingerprint = "a"}}},
                              {.answer = "no"}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42, 8));
    CHECK(outcome.task.status == t::kStalled);
    CHECK(outcome.task.reason.starts_with("no progress in 2 rounds"));
    CHECK(outcome.task.reason.find("not passed: the answer contains \"42\"") != std::string::npos);
    CHECK(t::rounds_used(outcome.task) == 3);

    // Resuming clears the count, as `cycle resume` clears the cycle's: one
    // more round without progress does not stall it at once.
    fixture.scripted.steps.push_back({.answer = "no"});
    fixture.scripted.steps.push_back({.answer = "42\nTASK STATUS: DONE"});
    const t::RunOutcome resumed = fixture.run(outcome.task, true);
    CHECK(resumed.task.status == t::kDone);
    CHECK(t::rounds_used(resumed.task) == 5);
    CHECK(events(resumed.task).back() == "finished:done");
    CHECK(std::ranges::count(events(resumed.task), std::string{"resumed:running"}) == 1);
}

TEST_CASE("a question nobody can answer fails the task; resume asks the round again verbatim",
          "[tasks][runner][question]") {
    Fixture fixture;
    fixture.scripted.steps = {{.answer = "plan"}, {.question = "Which city?"}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42));
    CHECK(outcome.task.status == t::kFailed);
    CHECK(outcome.task.reason ==
          "round 1: the model asked a question and no one is present to answer it: Which city?");
    CHECK(outcome.task.rounds.back().outcome == t::kInterrupted);
    CHECK(events(outcome.task).back() == "finished:failed");
    CHECK(fixture.scripted.session.turns == 1);

    fixture.scripted.steps.push_back({.answer = "42\nTASK STATUS: DONE"});
    const t::RunOutcome resumed = fixture.run(outcome.task, true);
    CHECK(resumed.task.status == t::kDone);
    REQUIRE(fixture.scripted.seen.size() == 3);
    CHECK(fixture.scripted.seen[2].message == fixture.scripted.seen[1].message);
    CHECK(fixture.scripted.seen[2].round == 1);
    // The round was run again under its own number, not a new one.
    CHECK(resumed.task.rounds.size() == 2);
    CHECK(t::rounds_used(resumed.task) == 1);
}

TEST_CASE("a provider failure fails the task with its error", "[tasks][runner]") {
    Fixture fixture;
    fixture.scripted.steps = {{.error = "connection refused"}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42));
    CHECK(outcome.task.status == t::kFailed);
    CHECK(outcome.task.reason == "the plan turn failed: connection refused");
    CHECK(outcome.task.plan.empty());
    // A plan cut short records no plan.
    CHECK(events(outcome.task) ==
          std::vector<std::string>{"started:planning", "plan_started:planning", "finished:failed"});
}

TEST_CASE("a cancel another process leaves ends the turn in flight through the token",
          "[tasks][runner][cancel]") {
    Fixture fixture;
    fixture.scripted.steps = {
        {.answer = "plan"},
        {.wait_for_cancel = true, .before = [&fixture] {
             REQUIRE(t::write_request(fixture.root, "task-20261004-120000", t::Request::Cancel)
                         .empty());
         }}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42));
    CHECK(outcome.task.status == t::kCancelled);
    CHECK(outcome.task.reason == "cancelled by 'apogee task cancel' during round 1");
    CHECK(events(outcome.task).back() == "finished:cancelled");
    // The request is consumed.
    CHECK(t::read_request(fixture.root, "task-20261004-120000") == t::Request::None);
    // Cancelled may be resumed.
    CHECK(t::resume_refusal(outcome.task).empty());
}

TEST_CASE("Ctrl-C ends the turn in flight and the task is cancelled, said as interrupted",
          "[tasks][runner][cancel]") {
    Fixture fixture;
    const apogee::harness::CancellationToken interrupt =
        apogee::harness::CancellationToken::create();
    fixture.scripted.steps = {
        {.wait_for_cancel = true, .before = [interrupt] { interrupt.cancel(); }}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42), false, interrupt);
    CHECK(outcome.task.status == t::kCancelled);
    CHECK(outcome.task.reason == "interrupted during the plan turn");
}

TEST_CASE("a halt stops the task when its round ends; resume continues with the next",
          "[tasks][runner][halt]") {
    Fixture fixture;
    fixture.scripted.steps = {
        {.answer = "plan"},
        {.answer = "not yet", .before = [&fixture] {
             REQUIRE(
                 t::write_request(fixture.root, "task-20261004-120000", t::Request::Halt).empty());
         }}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42));
    CHECK(outcome.task.status == t::kHalted);
    CHECK(outcome.task.reason == "halted by 'apogee task halt' after round 1");
    CHECK(outcome.task.rounds.back().outcome == t::kCompleted);
    CHECK(t::live(outcome.task));
    CHECK(t::read_request(fixture.root, "task-20261004-120000") == t::Request::None);

    fixture.scripted.steps.push_back({.answer = "42\nTASK STATUS: DONE"});
    const t::RunOutcome resumed = fixture.run(outcome.task, true);
    CHECK(resumed.task.status == t::kDone);
    CHECK(fixture.scripted.seen.back().round == 2);
    CHECK(fixture.scripted.seen.back().kind == t::kCorrectRound);
}

TEST_CASE(
    "a round whose turn finished before a restart is taken from the session, not asked "
    "again",
    "[tasks][runner][resume]") {
    Fixture fixture;
    t::Task task = fixture.task(kAnswer42);
    task.plan = "1. Answer.";
    task.status = std::string{t::kRunning};
    task.rounds.push_back(t::Round{
        .index = 0, .kind = std::string{t::kPlanRound}, .outcome = std::string{t::kCompleted}});
    task.rounds.push_back(t::Round{.index = 1,
                                   .kind = std::string{t::kExecuteRound},
                                   .started_at = "2026-10-04T12:00:00Z",
                                   .session_turns_before = 1});
    // The session the killed process saved: the plan's turn, then round 1's.
    fixture.scripted.session.messages = {ChatMessage::user(t::plan_message(task)),
                                         ChatMessage::assistant("1. Answer."),
                                         ChatMessage::user(t::round_message(task, 1)),
                                         ChatMessage::assistant("42\nTASK STATUS: DONE")};
    fixture.scripted.session.turns = 2;
    apogee::logger::save(fixture.scripted.session);

    const t::RunOutcome outcome = fixture.run(task, true);
    CHECK(outcome.task.status == t::kDone);
    CHECK(fixture.scripted.seen.empty());
    CHECK(outcome.task.rounds.back().adopted);
    CHECK(outcome.task.rounds.back().outcome == t::kCompleted);
}

TEST_CASE("a round killed mid-turn is run again with the message it was first sent",
          "[tasks][runner][resume]") {
    Fixture fixture;
    t::Task task = fixture.task(kAnswer42);
    task.plan = "1. Answer.";
    task.status = std::string{t::kRunning};
    task.rounds.push_back(t::Round{
        .index = 0, .kind = std::string{t::kPlanRound}, .outcome = std::string{t::kCompleted}});
    task.rounds.push_back(t::Round{.index = 1,
                                   .kind = std::string{t::kExecuteRound},
                                   .started_at = "2026-10-04T12:00:00Z",
                                   .session_turns_before = 1});
    fixture.scripted.session.messages = {ChatMessage::user(t::plan_message(task)),
                                         ChatMessage::assistant("1. Answer.")};
    fixture.scripted.session.turns = 1;
    apogee::logger::save(fixture.scripted.session);
    fixture.scripted.steps = {{.answer = "42\nTASK STATUS: DONE"}};

    const t::RunOutcome outcome = fixture.run(task, true);
    CHECK(outcome.task.status == t::kDone);
    REQUIRE(fixture.scripted.seen.size() == 1);
    CHECK(fixture.scripted.seen[0].message == t::round_message(task, 1));
    CHECK_FALSE(outcome.task.rounds.back().adopted);
    // One user message per turn: nothing said twice.
    const std::vector<ChatMessage>& messages = fixture.scripted.session.messages;
    CHECK(std::ranges::count_if(messages, [](const ChatMessage& message) {
              return message.role == apogee::harness::Role::User;
          }) == 2);
}

TEST_CASE("a denial and the tools a round ran are recorded on it", "[tasks][runner]") {
    Fixture fixture;
    fixture.scripted.steps = {{.answer = "plan"},
                              {.answer = "42\nTASK STATUS: DONE",
                               .tools = {{.tool = "read_file", .fingerprint = "f"}},
                               .denied = {{.tool = "write_file", .target = "out.txt"}}}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42));
    CHECK(outcome.task.status == t::kDone);
    const t::Round& round = outcome.task.rounds.back();
    REQUIRE(round.denied.size() == 1);
    CHECK(round.denied[0].tool == "write_file");
    CHECK(round.denied[0].target == "out.txt");
    CHECK(round.tools.size() == 1);
    CHECK(std::ranges::find(fixture.said,
                            "[task] denied: write_file on out.txt -- nobody is "
                            "present to allow it") != fixture.said.end());
    CHECK(t::task_to_json(fixture.on_disk()) == t::task_to_json(outcome.task));
}

TEST_CASE("what a round's authority let through, refused and answered is recorded and said",
          "[tasks][runner][policy]") {
    Fixture fixture;
    fixture.scripted.steps = {
        {.answer = "plan"},
        {.answer = "blue\nTASK STATUS: DONE",
         .denied = {{.tool = "run_command", .target = "make", .by = std::string{t::kByConfig}},
                    {.tool = "delete_file", .target = "a.txt", .by = std::string{t::kByPerson}}},
         .allowed = {{.tool = "write_file", .target = "out.txt", .by = std::string{t::kByGrant}},
                     {.tool = "edit_file", .target = "b.txt", .by = std::string{t::kByConfig}}},
         .answered = {
             {.question = "Which colour?", .answer = "blue", .by = std::string{t::kByDeclared}}}}};
    const t::RunOutcome outcome = fixture.run(fixture.task({}));
    CHECK(outcome.task.status == t::kDone);
    const t::Round& round = outcome.task.rounds.back();
    REQUIRE(round.allowed.size() == 2);
    CHECK(round.allowed[0].by == t::kByGrant);
    REQUIRE(round.answered.size() == 1);
    CHECK(round.answered[0].answer == "blue");
    const auto said = [&fixture](const std::string& line) {
        return std::ranges::find(fixture.said, line) != fixture.said.end();
    };
    CHECK(said("[task] write_file on out.txt -- allowed by this task's grant"));
    // The config's standing allows are the config's to say.
    CHECK(std::ranges::none_of(fixture.said, [](const std::string& line) {
        return line.find("edit_file") != std::string::npos;
    }));
    CHECK(said("[task] denied: run_command on make -- the config denies it"));
    CHECK(said("[task] denied: delete_file on a.txt -- refused at the prompt"));
    // The question is said; the declared answer is the ledger's.
    CHECK(said("[task] a question answered with the declared answer: Which colour?"));
    CHECK(std::ranges::none_of(fixture.said, [](const std::string& line) {
        return line.find("blue") != std::string::npos;
    }));
    CHECK(t::task_to_json(fixture.on_disk()) == t::task_to_json(outcome.task));
}

TEST_CASE("a round run again keeps what its interrupted attempt let through and answered",
          "[tasks][runner][policy]") {
    Fixture fixture;
    fixture.scripted.steps = {
        {.answer = "plan"},
        // The first attempt writes, is answered, then fails on a question.
        {.question = "Which file?",
         .denied = {{.tool = "run_command", .target = "make", .by = std::string{t::kByNobody}}},
         .allowed = {{.tool = "write_file", .target = "a.txt", .by = std::string{t::kByGrant}}},
         .answered = {
             {.question = "Which colour?", .answer = "blue", .by = std::string{t::kByDeclared}}}}};
    const t::RunOutcome failed = fixture.run(fixture.task({}));
    REQUIRE(failed.task.status == t::kFailed);
    REQUIRE(failed.task.rounds.back().allowed.size() == 1);
    CHECK(t::task_to_json(fixture.on_disk()) == t::task_to_json(failed.task));

    fixture.scripted.steps.push_back(
        {.answer = "TASK STATUS: DONE",
         .allowed = {{.tool = "write_file", .target = "b.txt", .by = std::string{t::kByGrant}}}});
    const t::RunOutcome resumed = fixture.run(failed.task, /*resume=*/true);
    CHECK(resumed.task.status == t::kDone);
    const t::Round& round = resumed.task.rounds.back();
    // The write the rolled-back attempt made happened: it stays recorded.
    REQUIRE(round.allowed.size() == 2);
    CHECK(round.allowed[0].target == "a.txt");
    CHECK(round.allowed[1].target == "b.txt");
    REQUIRE(round.answered.size() == 1);
    // Denials and activity are the attempt's own, as before.
    CHECK(round.denied.empty());
}

TEST_CASE("a request a process that ended left behind does not stop the next run",
          "[tasks][runner][cancel]") {
    Fixture fixture;
    REQUIRE(t::save_task(fixture.root, fixture.task(kAnswer42)).empty());
    REQUIRE(t::write_request(fixture.root, "task-20261004-120000", t::Request::Cancel).empty());
    fixture.scripted.steps = {{.answer = "plan"}, {.answer = "42\nTASK STATUS: DONE"}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42));
    CHECK(outcome.task.status == t::kDone);
    CHECK(fixture.scripted.seen.size() == 2);
}

TEST_CASE("a finished task is not run again", "[tasks][runner]") {
    Fixture fixture;
    t::Task task = fixture.task(kAnswer42);
    task.status = std::string{t::kDone};
    const t::RunOutcome outcome = fixture.run(task, true);
    CHECK(outcome.error == "task task-20261004-120000 is done -- there is nothing to resume");
    CHECK(fixture.scripted.seen.empty());
}

TEST_CASE("a ledger that cannot be written stops the run and says so", "[tasks][runner]") {
    Fixture fixture;
    std::ofstream{fixture.root} << "a file where the tasks directory goes";
    fixture.scripted.steps = {{.answer = "plan"}};
    const t::RunOutcome outcome = fixture.run(fixture.task(kAnswer42));
    CHECK(outcome.error.starts_with("the task's ledger could not be written"));
    CHECK(fixture.scripted.seen.empty());
}

TEST_CASE("a turn's answer is found in the session only where that turn finished",
          "[tasks][runner][resume]") {
    const std::vector<ChatMessage> messages = {
        ChatMessage::user("ask"), ChatMessage::assistant("first"), ChatMessage::user("ask"),
        ChatMessage::assistant("second"), ChatMessage::user("other")};
    CHECK(t::answer_in_session(messages, "ask") == "second");
    CHECK_FALSE(t::answer_in_session(messages, "other").has_value());
    CHECK_FALSE(t::answer_in_session(messages, "never asked").has_value());

    ChatMessage calling = ChatMessage::assistant("");
    calling.tool_calls.push_back({.id = "1", .name = "read_file", .arguments = "{}"});
    const std::vector<ChatMessage> cut = {ChatMessage::user("ask"), calling};
    CHECK_FALSE(t::answer_in_session(cut, "ask").has_value());
    const std::vector<ChatMessage> empty_answer = {ChatMessage::user("ask"),
                                                   ChatMessage::assistant("")};
    CHECK(t::answer_in_session(empty_answer, "ask") == "");
}
