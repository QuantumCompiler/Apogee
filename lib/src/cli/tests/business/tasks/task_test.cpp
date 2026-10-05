#include "tasks/task.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/env_guard.h"

/// The task record and its transitions, table-tested: the self-report read
/// through Markdown and never guessed; each check against an answer and the
/// filesystem; a completed round moving the task to done, exhausted, stalled
/// or on -- progress meaning a check newly passing or a new tool call, the
/// budget winning over the breaker, done over both; the messages composed
/// from the record alone; the ledger's JSON round trip.
namespace {

using apogee::tasks::Check;
using apogee::tasks::CheckKind;
using apogee::tasks::Round;
using apogee::tasks::SelfReport;
using apogee::tasks::Task;
using apogee::tasks::ToolUse;

namespace t = apogee::tasks;

Task task_with(std::vector<Check> checks, int budget = 8) {
    Task task;
    task.id = "task-20261004-120000";
    task.goal = "Find the answer";
    task.checks = std::move(checks);
    task.rounds_budget = budget;
    task.plan = "1. Think.\n2. Answer.";
    task.status = std::string{t::kRunning};
    task.rounds.push_back(Round{
        .index = 0, .kind = std::string{t::kPlanRound}, .outcome = std::string{t::kCompleted}});
    return task;
}

/// Opens round `index` with `tools` run in it, and concludes it on `answer`.
void play(Task& task, int index, const std::string& answer, std::vector<ToolUse> tools = {}) {
    task.rounds.push_back(
        Round{.index = index,
              .kind = std::string{index == 1 ? t::kExecuteRound : t::kCorrectRound},
              .tools = std::move(tools)});
    t::conclude_round(task, answer);
}

ToolUse use(const std::string& tool, const std::string& arguments) {
    return ToolUse{.tool = tool, .fingerprint = t::fingerprint(tool, arguments)};
}

}  // namespace

TEST_CASE("the self-report is the last TASK STATUS line, read through Markdown, never guessed",
          "[tasks][self_report]") {
    CHECK(t::parse_self_report("Done it.\nTASK STATUS: DONE") == SelfReport::Done);
    CHECK(t::parse_self_report("TASK STATUS: NOT DONE") == SelfReport::NotDone);
    CHECK(t::parse_self_report("**TASK STATUS: DONE**") == SelfReport::Done);
    CHECK(t::parse_self_report("`TASK STATUS: NOT DONE`.") == SelfReport::NotDone);
    CHECK(t::parse_self_report("> task status:   done") == SelfReport::Done);
    CHECK(t::parse_self_report("- Task Status: not  done") == SelfReport::NotDone);
    CHECK(t::parse_self_report("TASK STATUS: NOT DONE\nmore\nTASK STATUS: DONE") ==
          SelfReport::Done);
    CHECK(t::parse_self_report("TASK STATUS: DONE\nTASK STATUS: NOT DONE") == SelfReport::NotDone);
    // Anything else is unreported -- which is never done.
    CHECK(t::parse_self_report("All finished.") == SelfReport::Unreported);
    CHECK(t::parse_self_report("") == SelfReport::Unreported);
    CHECK(t::parse_self_report("TASK STATUS: DONE soon") == SelfReport::Unreported);
    CHECK(t::parse_self_report("I will end with TASK STATUS: DONE") == SelfReport::Unreported);
    CHECK(t::parse_self_report("TASK STATUS: probably") == SelfReport::Unreported);
    for (const SelfReport report :
         {SelfReport::Unreported, SelfReport::Done, SelfReport::NotDone}) {
        CHECK(t::self_report_from_string(t::to_string(report)) == report);
    }
}

TEST_CASE("a require check reads the round's answer exactly", "[tasks][check]") {
    const Check check{.kind = CheckKind::Require, .value = "42"};
    CHECK(t::describe(check) == "the answer contains \"42\"");
    CHECK(t::evaluate(check, "It is 42.").passed);
    CHECK_FALSE(t::evaluate(check, "It is forty-two.").passed);
    CHECK(t::evaluate(check, "It is forty-two.").detail == "not in the answer");
    // Exact: case counts, and an empty text never passes.
    CHECK_FALSE(t::evaluate(Check{.kind = CheckKind::Require, .value = "Paris"}, "paris").passed);
    CHECK_FALSE(t::evaluate(Check{.kind = CheckKind::Require, .value = ""}, "anything").passed);
}

TEST_CASE("a require-file check reads the filesystem as it is now", "[tasks][check]") {
    const apogee::testing::TempDir dir{"task-check-" + std::to_string(std::random_device{}())};
    const std::filesystem::path file = dir.path() / "out.txt";
    const Check check{.kind = CheckKind::RequireFile, .value = file.string()};
    CHECK(t::describe(check) == "the file " + file.string() + " exists and is not empty");

    CHECK(t::evaluate(check, "").detail == "no such file");
    std::ofstream{file}.close();
    CHECK_FALSE(t::evaluate(check, "").passed);
    CHECK(t::evaluate(check, "").detail == "empty");
    std::ofstream{file} << "hello";
    CHECK(t::evaluate(check, "").passed);
    CHECK(t::evaluate(check, "").detail == "5 bytes");
    const Check folder{.kind = CheckKind::RequireFile, .value = dir.path().string()};
    CHECK_FALSE(t::evaluate(folder, "").passed);
    CHECK(t::evaluate(folder, "").detail == "not a regular file");
}

TEST_CASE("a round that passes every check and reports done finishes the task",
          "[tasks][transitions]") {
    Task task = task_with({{CheckKind::Require, "42"}});
    play(task, 1, "It is 42.\nTASK STATUS: DONE");
    CHECK(task.status == t::kDone);
    CHECK(task.rounds.back().outcome == t::kCompleted);
    CHECK(task.rounds.back().self_report == "done");
    CHECK(task.rounds.back().progress);
    CHECK(task.reason ==
          "every check passed and the model reported the task done, in 1 of 8 "
          "rounds");
    CHECK(t::finished(task));
    CHECK_FALSE(t::live(task));
}

TEST_CASE("passing checks without the self-report is not done, and neither is the reverse",
          "[tasks][transitions]") {
    Task passing = task_with({{CheckKind::Require, "42"}});
    play(passing, 1, "It is 42.");
    CHECK(passing.status == t::kRunning);

    Task claimed = task_with({{CheckKind::Require, "42"}});
    play(claimed, 1, "TASK STATUS: DONE");
    CHECK(claimed.status == t::kRunning);
    CHECK_FALSE(claimed.rounds.back().progress);

    // With no checks, the self-report alone decides.
    Task bare = task_with({});
    play(bare, 1, "TASK STATUS: DONE");
    CHECK(bare.status == t::kDone);
}

TEST_CASE("progress is a check newly passing or a new tool call, never a repeat",
          "[tasks][transitions][breaker]") {
    Task task = task_with({{CheckKind::Require, "alpha"}, {CheckKind::Require, "beta"}});
    play(task, 1, "alpha", {use("read_file", R"({"path":"a"})")});
    CHECK(task.rounds.back().progress);
    CHECK(task.no_progress == 0);

    // alpha again, the same call again: nothing new.
    play(task, 2, "alpha", {use("read_file", R"({"path":"a"})")});
    CHECK_FALSE(task.rounds.back().progress);
    CHECK(task.no_progress == 1);
    CHECK(task.status == t::kRunning);

    // A different call is new activity, and resets the count.
    play(task, 3, "nothing", {use("read_file", R"({"path":"b"})")});
    CHECK(task.rounds.back().progress);
    CHECK(task.no_progress == 0);

    // beta passes for the first time: progress, though alpha has gone.
    play(task, 4, "beta");
    CHECK(task.rounds.back().progress);
    CHECK(task.no_progress == 0);
}

TEST_CASE("two rounds without progress stall the task, naming what did not pass",
          "[tasks][transitions][breaker]") {
    Task task = task_with({{CheckKind::Require, "42"}});
    play(task, 1, "no idea");
    CHECK(task.status == t::kRunning);
    CHECK(task.no_progress == 1);
    play(task, 2, "still no idea\nTASK STATUS: NOT DONE");
    CHECK(task.status == t::kStalled);
    CHECK(task.reason ==
          "no progress in 2 rounds -- no check newly passed and no new tool call ran -- not "
          "passed: the answer contains \"42\" (not in the answer); not reported done by the "
          "model");
    CHECK_FALSE(t::finished(task));
    CHECK_FALSE(t::live(task));
}

TEST_CASE("the budget is spent at its last round, and wins over the breaker",
          "[tasks][transitions][budget]") {
    Task task = task_with({{CheckKind::Require, "42"}}, 2);
    play(task, 1, "no idea");
    CHECK(task.status == t::kRunning);
    play(task, 2, "no idea");
    // Two rounds without progress AND the budget spent: exhausted, which is
    // final -- stalled would invite a resume with nothing left to run.
    CHECK(task.status == t::kExhausted);
    CHECK(task.reason ==
          "the round budget (2) is spent and the task is not done -- not passed: the answer "
          "contains \"42\" (not in the answer); not reported done by the model");
    CHECK(t::finished(task));
    CHECK(t::rounds_used(task) == 2);
    CHECK(t::unpassed_checks(task) == std::vector<std::string>{"the answer contains \"42\""});
}

TEST_CASE("a last round that succeeds is done, not exhausted", "[tasks][transitions][budget]") {
    Task task = task_with({{CheckKind::Require, "42"}}, 1);
    play(task, 1, "42\nTASK STATUS: DONE");
    CHECK(task.status == t::kDone);
}

TEST_CASE("only planning, running and halted tasks are live", "[tasks][transitions]") {
    Task task;
    for (const std::string_view status : {t::kPlanning, t::kRunning, t::kHalted, t::kCancelled,
                                          t::kDone, t::kExhausted, t::kStalled, t::kFailed}) {
        task.status = std::string{status};
        CHECK(t::live(task) ==
              (status == t::kPlanning || status == t::kRunning || status == t::kHalted));
        CHECK(t::finished(task) == (status == t::kDone || status == t::kExhausted));
    }
}

TEST_CASE("the plan message states the goal, the acceptance and the unattended rules",
          "[tasks][messages]") {
    Task task = task_with({{CheckKind::Require, "42"}});
    task.plan.clear();
    const std::string message = t::plan_message(task);
    CHECK(message.starts_with("Task: Find the answer\n\n"));
    CHECK(message.find("up to 8 rounds") != std::string::npos);
    CHECK(message.find("- the answer contains \"42\"\n- you report the task done\n") !=
          std::string::npos);
    CHECK(message.find("Do not carry it out yet.") != std::string::npos);
    CHECK(message.find("asking one ends the task") != std::string::npos);
    CHECK(message.find("refusal is its result") == std::string::npos);
    task.tools = true;
    CHECK(t::plan_message(task).find("refusal is its result") != std::string::npos);
}

TEST_CASE("the plan message says what the task was handed, and nothing when it was not",
          "[tasks][messages][policy]") {
    Task task = task_with({{CheckKind::Require, "42"}});
    task.plan.clear();
    task.tools = true;
    // 27h's message, byte for byte, for a task handed nothing.
    const std::string plain = t::plan_message(task);
    CHECK(plain.ends_with(
        "No one will answer questions while the task runs: asking one ends the task. A tool "
        "that changes something runs only where the configuration allows it; otherwise it is "
        "refused, and the refusal is its result."));

    task.policy.grants = {"edit_file", "write_file"};
    const std::string granted = t::plan_message(task);
    CHECK(granted.find("runs only where the configuration allows it or this task was granted "
                       "it (edit_file, write_file); otherwise it is refused") != std::string::npos);

    task.policy.on_question = t::OnQuestion::Answer;
    task.policy.answer = "blue";
    const std::string answered = t::plan_message(task);
    CHECK(answered.find("any question you ask gets the one answer declared for this task") !=
          std::string::npos);
    CHECK(answered.find("asking one ends the task") == std::string::npos);
    // The declared answer itself is the answer to a question, never the plan's.
    CHECK(answered.find("blue") == std::string::npos);
}

TEST_CASE("round messages carry the plan, and a correction names what failed",
          "[tasks][messages]") {
    Task task = task_with({{CheckKind::Require, "42"}, {CheckKind::Require, "Paris"}});
    const std::string first = t::round_message(task, 1);
    CHECK(first.starts_with("Round 1 of 8: carry out your plan now.\n\n"));
    CHECK(first.find("Your plan:\n1. Think.\n2. Answer.\n") != std::string::npos);
    CHECK(first.find("- the answer contains \"Paris\"\n") != std::string::npos);
    CHECK(first.ends_with("`TASK STATUS: NOT DONE` if work remains."));

    play(task, 1, "It is in Paris.");
    const std::string second = t::round_message(task, 2);
    CHECK(second.starts_with("Round 2 of 8. The task is not done -- after round 1:\n"));
    CHECK(second.find("- FAILED: the answer contains \"42\" -- not in the answer\n") !=
          std::string::npos);
    CHECK(second.find("- passed: the answer contains \"Paris\" -- found in the answer\n") !=
          std::string::npos);
    CHECK(second.find("- FAILED: you report the task done -- your answer did not end with") !=
          std::string::npos);
    CHECK(second.find("Your plan:\n1. Think.") != std::string::npos);
    // A function of the record alone: the same round composes the same text.
    CHECK(t::round_message(task, 2) == second);
}

TEST_CASE("a long plan is quoted up to its limit and kept whole in the record",
          "[tasks][messages]") {
    Task task = task_with({});
    task.plan = std::string(t::kPlanQuoteLimit + 100, 'x');
    const std::string message = t::round_message(task, 1);
    CHECK(message.find(std::string(t::kPlanQuoteLimit, 'x') + "\n[... the plan continues") !=
          std::string::npos);
    CHECK(task.plan.size() == t::kPlanQuoteLimit + 100);
}

TEST_CASE("a tool call's fingerprint is its name and arguments, hashed", "[tasks][fingerprint]") {
    CHECK(t::fingerprint("read_file", "{}") == t::fingerprint("read_file", "{}"));
    CHECK(t::fingerprint("read_file", "{}").size() == 16);
    CHECK(t::fingerprint("read_file", "{}") != t::fingerprint("read_file", "{ }"));
    CHECK(t::fingerprint("read_file", "{}") != t::fingerprint("write_file", "{}"));
    // The separator keeps a name and its arguments apart.
    CHECK(t::fingerprint("ab", "c") != t::fingerprint("a", "bc"));
}

TEST_CASE("the ledger's JSON round-trips every field", "[tasks][json]") {
    Task task = task_with({{CheckKind::Require, "42"}, {CheckKind::RequireFile, "/tmp/x"}}, 5);
    task.working_directory = "/work";
    task.session_id = "20261004-120000-abcd";
    task.tools = true;
    task.created_at = "2026-10-04T12:00:00Z";
    play(task, 1, "42", {use("write_file", "{}")});
    task.rounds.back().denied.push_back({.tool = "run_command", .target = "rm -rf /"});
    task.rounds.back().denied.push_back(
        {.tool = "delete_file", .target = "a.txt", .by = std::string{t::kByNobody}});
    task.rounds.back().allowed.push_back(
        {.tool = "write_file", .target = "out.txt", .by = std::string{t::kByGrant}});
    task.rounds.back().answered.push_back(
        {.question = "Which colour?", .answer = "blue", .by = std::string{t::kByDeclared}});
    task.policy.grants = {"write_file"};
    task.policy.on_question = t::OnQuestion::Answer;
    task.policy.answer = "blue";
    task.policy.agent = "helper";
    task.rounds.back().tokens = 120;
    task.rounds.back().tokens_estimated = true;
    task.rounds.back().adopted = true;
    t::record_transition(task, t::kRoundEndedEvent, "2026-10-04T12:01:00Z", 1, "1 of 2");

    const Task back = t::task_from_json(nlohmann::json::parse(t::task_to_json(task).dump()));
    CHECK(t::task_to_json(back) == t::task_to_json(task));
    CHECK(back.checks[1].kind == CheckKind::RequireFile);
    CHECK(back.rounds.back().denied.front().target == "rm -rf /");
    CHECK(back.rounds.back().denied.front().by.empty());
    CHECK(back.rounds.back().denied.back().by == t::kByNobody);
    CHECK(back.rounds.back().allowed.front().by == t::kByGrant);
    CHECK(back.rounds.back().allowed.front().target == "out.txt");
    CHECK(back.rounds.back().answered.front().answer == "blue");
    CHECK(back.policy.grants == std::vector<std::string>{"write_file"});
    CHECK(back.policy.answer == "blue");
    CHECK(back.policy.agent == "helper");
    CHECK(back.rounds.back().adopted);
    CHECK(back.transitions.back().round == 1);
    CHECK(back.updated_at == "2026-10-04T12:01:00Z");
}

TEST_CASE("a ledger written before the policy reads as handed nothing", "[tasks][json][policy]") {
    nlohmann::json json = t::task_to_json(task_with({}));
    json.erase("policy");
    for (nlohmann::json& round : json["rounds"]) {
        round.erase("allowed");
        round.erase("answered");
    }
    const Task older = t::task_from_json(json);
    CHECK(older.policy.grants.empty());
    CHECK(older.policy.on_question == t::OnQuestion::Fail);
    CHECK(older.rounds.front().allowed.empty());
    CHECK(older.rounds.front().answered.empty());
}

TEST_CASE("a ledger of the wrong shape is refused, and a budget outside its bounds is held",
          "[tasks][json]") {
    CHECK_THROWS_AS(t::task_from_json(nlohmann::json::array()), std::runtime_error);
    CHECK_THROWS_AS(t::task_from_json(nlohmann::json{{"goal", "g"}}), std::runtime_error);
    CHECK_THROWS_AS(t::task_from_json(nlohmann::json{
                        {"id", "task-x"}, {"checks", {{{"kind", "judge"}, {"value", "v"}}}}}),
                    std::runtime_error);
    CHECK(t::task_from_json(nlohmann::json{{"id", "task-x"}, {"rounds_budget", 0}}).rounds_budget ==
          1);
    CHECK(t::task_from_json(nlohmann::json{{"id", "task-x"}, {"rounds_budget", 1000}})
              .rounds_budget == t::kMaxRounds);
}
