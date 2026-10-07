#include "tasks/view.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "tasks/ledger.h"
#include "tasks/task.h"

/// The one view of a task (27j): every fact `task status` shows, decided
/// once from the ledger -- the status and whether a process runs the task,
/// each check as the newest completed round left it, the rounds used, every
/// turn with what it let through, refused and answered -- as the golden
/// document `task status --output-format json` prints and the admin plane
/// serves; and a declared answer in none of it, by construction.
namespace {

namespace t = apogee::tasks;

constexpr std::string_view kDeclared = "DECLARED-ANSWER-9c41";

/// A task with tools, a grant, a declared answer, a plan, an incomplete
/// round and a finished one -- each kind of record in it.
t::Task rich_task() {
    t::Task task;
    task.id = "task-20261004-120000";
    task.goal = "Write the report";
    task.checks = {{t::CheckKind::Require, "42"}, {t::CheckKind::RequireFile, "/work/report.txt"}};
    task.rounds_budget = 4;
    task.working_directory = "/work";
    task.session_id = "20261004-120000-abcd";
    task.tools = true;
    task.policy.grants = {"write_file"};
    task.policy.on_question = t::OnQuestion::Answer;
    task.policy.answer = std::string{kDeclared};
    task.status = std::string{t::kDone};
    task.reason = "every check passed and the model reported the task done, in 2 of 4 rounds";
    task.plan = "1. Write it.\n2. Say 42.";
    task.created_at = "2026-10-04T12:00:00Z";
    task.updated_at = "2026-10-04T12:00:09Z";
    task.rounds.push_back(t::Round{.index = 0,
                                   .kind = std::string{t::kPlanRound},
                                   .started_at = "2026-10-04T12:00:01Z",
                                   .ended_at = "2026-10-04T12:00:02Z",
                                   .outcome = std::string{t::kCompleted},
                                   .tokens = 7});
    t::Round first{.index = 1,
                   .kind = std::string{t::kExecuteRound},
                   .started_at = "2026-10-04T12:00:03Z",
                   .ended_at = "2026-10-04T12:00:05Z",
                   .session_turns_before = 1,
                   .outcome = std::string{t::kCompleted},
                   .self_report = "not_done",
                   .checks = {{.passed = false, .detail = "not in the answer"},
                              {.passed = true, .detail = "12 bytes"}},
                   .tools = {{.tool = "write_file", .fingerprint = "abc"}},
                   .denied = {{.tool = "run_command", .target = "make", .by = "nobody"}},
                   .allowed = {{.tool = "write_file", .target = "report.txt", .by = "grant"}},
                   .answered = {{.question = "Which colour?",
                                 .answer = std::string{kDeclared},
                                 .by = "declared"}},
                   .tokens = 20,
                   .progress = true};
    task.rounds.push_back(first);
    task.rounds.push_back(t::Round{.index = 2,
                                   .kind = std::string{t::kCorrectRound},
                                   .started_at = "2026-10-04T12:00:06Z",
                                   .ended_at = "2026-10-04T12:00:08Z",
                                   .session_turns_before = 2,
                                   .outcome = std::string{t::kCompleted},
                                   .adopted = true,
                                   .self_report = "done",
                                   .checks = {{.passed = true, .detail = "found in the answer"},
                                              {.passed = true, .detail = "12 bytes"}},
                                   .tokens = 15,
                                   .tokens_estimated = true});
    return task;
}

}  // namespace

TEST_CASE("the view of a task is the golden document every surface serves",
          "[tasks][view][golden]") {
    const t::TaskView view = t::make_task_view(rich_task(), std::nullopt);
    const nlohmann::json golden = nlohmann::json::parse(R"({
      "id": "task-20261004-120000", "status": "done", "process": null, "interrupted": false,
      "goal": "Write the report", "conversation": "20261004-120000-abcd", "folder": "/work",
      "tools": true,
      "policy": {"agent": "", "grants": ["write_file"], "on_question": "answer"},
      "rounds_used": 2, "rounds_budget": 4,
      "created_at": "2026-10-04T12:00:00Z", "updated_at": "2026-10-04T12:00:09Z",
      "reason": "every check passed and the model reported the task done, in 2 of 4 rounds",
      "checks": [
        {"kind": "require", "value": "42", "description": "the answer contains \"42\"",
         "ran": true, "passed": true, "detail": "found in the answer"},
        {"kind": "require_file", "value": "/work/report.txt",
         "description": "the file /work/report.txt exists and is not empty",
         "ran": true, "passed": true, "detail": "12 bytes"}],
      "self_report": "done",
      "plan": "1. Write it.\n2. Say 42.",
      "turns": [
        {"round": 0, "kind": "plan", "outcome": "completed", "adopted": false,
         "checks_passed": 0, "self_report": "", "tools": 0, "allowed": [], "denied": [],
         "answered": [], "tokens": 7, "tokens_estimated": false,
         "started_at": "2026-10-04T12:00:01Z", "ended_at": "2026-10-04T12:00:02Z"},
        {"round": 1, "kind": "execute", "outcome": "completed", "adopted": false,
         "checks_passed": 1, "self_report": "not_done", "tools": 1,
         "allowed": [{"tool": "write_file", "target": "report.txt", "by": "grant"}],
         "denied": [{"tool": "run_command", "target": "make", "by": "nobody"}],
         "answered": [{"question": "Which colour?", "by": "declared"}],
         "tokens": 20, "tokens_estimated": false,
         "started_at": "2026-10-04T12:00:03Z", "ended_at": "2026-10-04T12:00:05Z"},
        {"round": 2, "kind": "correct", "outcome": "completed", "adopted": true,
         "checks_passed": 2, "self_report": "done", "tools": 0, "allowed": [], "denied": [],
         "answered": [], "tokens": 15, "tokens_estimated": true,
         "started_at": "2026-10-04T12:00:06Z", "ended_at": "2026-10-04T12:00:08Z"}]
    })");
    CHECK(t::to_json(view) == golden);
}

TEST_CASE("a declared answer is in the ledger and in no view of it", "[tasks][view][leak]") {
    const t::Task task = rich_task();
    // The ledger holds it -- else the check below proves nothing.
    REQUIRE(t::task_to_json(task).dump().find(kDeclared) != std::string::npos);
    const nlohmann::json view = t::to_json(t::make_task_view(task, std::nullopt));
    CHECK(view.dump().find(kDeclared) == std::string::npos);
    // It says one existed: the policy answers, and the question was answered
    // by the declared answer -- never what it said.
    CHECK(view["policy"]["on_question"] == "answer");
    CHECK(view["turns"][1]["answered"][0] ==
          nlohmann::json{{"question", "Which colour?"}, {"by", "declared"}});
    for (const t::Round& round : task.rounds) {
        CHECK(t::to_json(t::make_turn_view(round)).dump().find(kDeclared) == std::string::npos);
    }
    // Nor does the view name a path into the private layout.
    CHECK(view.dump().find("task.json") == std::string::npos);
    CHECK(view.dump().find("sessions/") == std::string::npos);
}

TEST_CASE("whether a process runs a task is the lock's to say, and only while it runs",
          "[tasks][view]") {
    t::Task task = rich_task();
    task.status = std::string{t::kRunning};
    const t::LockHolder ours{.pid = 4153, .task_id = task.id, .running = true};
    const t::LockHolder other{.pid = 99, .task_id = "task-20261004-130000", .running = true};
    const t::LockHolder dead{.pid = 4153, .task_id = task.id, .running = false};

    t::TaskView view = t::make_task_view(task, ours);
    CHECK(view.process == 4153);
    CHECK_FALSE(view.interrupted);
    CHECK(t::to_json(view)["process"] == 4153);
    for (const std::optional<t::LockHolder>& holder :
         {std::optional<t::LockHolder>{}, std::optional{other}, std::optional{dead}}) {
        view = t::make_task_view(task, holder);
        CHECK_FALSE(view.process.has_value());
        CHECK(view.interrupted);
    }
    task.status = std::string{t::kPlanning};
    CHECK(t::make_task_view(task, std::nullopt).interrupted);
    // A task that stopped is neither, whatever holds the lock.
    for (const std::string_view status :
         {t::kDone, t::kHalted, t::kCancelled, t::kStalled, t::kFailed, t::kExhausted}) {
        task.status = std::string{status};
        view = t::make_task_view(task, ours);
        CHECK_FALSE(view.process.has_value());
        CHECK_FALSE(view.interrupted);
    }
}

TEST_CASE("before a round completes, no check has run and nothing is reported", "[tasks][view]") {
    t::Task task = rich_task();
    task.status = std::string{t::kRunning};
    task.rounds.resize(1);
    task.rounds.push_back(t::Round{.index = 1, .kind = std::string{t::kExecuteRound}});
    const t::TaskView view = t::make_task_view(task, std::nullopt);
    REQUIRE(view.checks.size() == 2);
    for (const t::CheckView& check : view.checks) {
        CHECK_FALSE(check.ran);
        CHECK_FALSE(check.passed);
        CHECK(check.detail.empty());
    }
    CHECK(view.self_report == "unreported");
    CHECK(view.rounds_used == 0);
    REQUIRE(view.turns.size() == 2);
    CHECK(view.turns[1].outcome == "in_flight");
    // The newest COMPLETED round decides: an in-flight one after a finished
    // one leaves the finished one's states.
    task = rich_task();
    task.rounds.push_back(t::Round{.index = 3, .kind = std::string{t::kCorrectRound}});
    const t::TaskView later = t::make_task_view(task, std::nullopt);
    CHECK(later.checks[0].passed);
    CHECK(later.self_report == "done");
    CHECK(later.rounds_used == 2);
}

TEST_CASE("a task without tools has no policy to show", "[tasks][view]") {
    t::Task task = rich_task();
    task.tools = false;
    const nlohmann::json view = t::to_json(t::make_task_view(task, std::nullopt));
    CHECK(view["policy"].is_null());
    CHECK(view["tools"] == false);
}

TEST_CASE("the listing is the newest fifty unless all, and says how many there are",
          "[tasks][view][list]") {
    std::vector<t::Task> tasks;
    for (int index = 0; index < 60; ++index) {
        t::Task task = rich_task();
        task.id = "task-" + std::to_string(1000 + index);
        task.goal = "goal " + std::to_string(index);
        tasks.push_back(task);
    }
    const t::TaskListView newest = t::make_task_list(tasks, false);
    CHECK(newest.shown.size() == t::kListLimit);
    CHECK(newest.total == 60);
    CHECK(newest.shown.front().id == "task-1000");
    const t::TaskListView all = t::make_task_list(tasks, true);
    CHECK(all.shown.size() == 60);
    const nlohmann::json document = t::to_json(newest);
    CHECK(document["object"] == "list");
    CHECK(document["total"] == 60);
    CHECK(document["data"].size() == 50);
    CHECK(document["data"][0] == nlohmann::json::parse(R"({"id": "task-1000", "status": "done",
        "rounds_used": 2, "rounds_budget": 4, "goal": "goal 0"})"));
    // Empty is a list, never null.
    const nlohmann::json none = t::to_json(t::make_task_list({}, false));
    CHECK(none["data"].is_array());
    CHECK(none["data"].empty());
    CHECK(none["total"] == 0);
}
