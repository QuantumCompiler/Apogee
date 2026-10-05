#include "tasks/task.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "cli/task_cmd.h"
#include "contracts/layout.h"
#include "logger/session.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "tasks/ledger.h"

/// `apogee task` on the real command tree against a scripted mock: a run to
/// done with no input, its status and its place in the list; refusals before
/// anything is made; an `ask`-level tool denied and recorded, nothing
/// prompting; the lock refusing a second run by the first's name; halt and
/// cancel of a task no process runs; a resume refused outside the task's
/// folder; and `chats delete` refusing a live task's conversation.
namespace {

namespace fs = std::filesystem;
namespace t = apogee::tasks;
using apogee::testing::CliHome;

/// A home whose one backend answers from `turns`, recall off so no summary
/// spends a scripted turn.
struct Home {
    explicit Home(const std::string& turns)
        : home{
              "backends:\n  m:\n    type: mock\nmodels:\n  default: m\nmemory:\n  recall: "
              "false\n"} {
        script = home.home() / "script.json";
        std::ofstream{script} << R"({"turns": [)" << turns << "]}";
        std::ofstream{home.config_path()}
            << "backends:\n  m:\n    type: mock\n    model_path: " << script.string()
            << "\nmodels:\n  default: m\nmemory:\n  recall: false\n";
    }

    int run(const std::vector<std::string>& args, std::string* out) const {
        return home.run(args, out);
    }

    [[nodiscard]] fs::path tasks() const {
        return home.home() / "tasks";
    }

    /// The one task this home holds.
    [[nodiscard]] t::Task task() const {
        const std::vector<t::Task> all = t::list_tasks(tasks());
        REQUIRE(all.size() == 1);
        return all.front();
    }

    CliHome home;
    fs::path script;
};

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

TEST_CASE("task run drives a goal to done with no input, and status and list say so",
          "[commands][task]") {
    const Home home{R"({"text": "1. Answer."},
                       {"text": "Not yet.\nTASK STATUS: NOT DONE"},
                       {"text": "It is 42.\nTASK STATUS: DONE"})"};
    std::string out;
    REQUIRE(home.run({"task", "run", "Find the answer", "--require", "42"}, &out) == 0);
    CHECK(has(out, "[task] planning"));
    CHECK(has(out, "[task] round 2 of 8 -- correcting: 1 check failing, not reported done"));
    const t::Task task = home.task();
    CHECK(task.status == t::kDone);
    CHECK(has(out, "task " + task.id + " done -- every check passed"));
    CHECK(task.plan == "1. Answer.");
    CHECK(t::rounds_used(task) == 2);

    // The conversation is an ordinary chat, naming its task.
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.home.home().string()};
    const apogee::logger::Session session = apogee::logger::load(task.session_id, {}).session;
    CHECK(session.task == task.id);
    CHECK(session.turns == 3);
    CHECK(session.title == "task: Find the answer");

    REQUIRE(home.run({"task", "status", task.id}, &out) == 0);
    CHECK(has(out, "task " + task.id + "  done"));
    CHECK(has(out, "conversation:  " + task.session_id));
    CHECK(has(out, "rounds:        2 of 8 used"));
    CHECK(has(out, "[x] the answer contains \"42\" -- found in the answer"));
    CHECK(has(out, "[x] the model reports the task done"));
    CHECK(has(out, "plan:\n  1. Answer.\n"));
    CHECK(has(out, "  1  execute  completed  0/1 checks, reported not done"));

    REQUIRE(home.run({"task", "list"}, &out) == 0);
    CHECK(has(out, task.id + "  done  2/8 rounds  Find the answer"));
}

TEST_CASE("task run refuses a bad goal, check or budget before anything is made",
          "[commands][task]") {
    const Home home{R"({"text": "x"})"};
    std::string out;
    CHECK(home.run({"task", "run", "", "--require", "42"}, &out) == 1);
    CHECK(has(out, "a task needs a goal"));
    CHECK(home.run({"task", "run", "goal", "--require", ""}, &out) == 1);
    CHECK(has(out, "--require needs the text"));
    CHECK(home.run({"task", "run", "goal", "--rounds", "0"}, &out) != 0);
    CHECK(home.run({"task", "run", "goal", "--rounds", "33"}, &out) != 0);
    CHECK(home.run({"task", "run", "goal", "--suite", "nope"}, &out) == 1);
    CHECK(has(out, "--suite: "));
    CHECK(t::list_tasks(home.tasks()).empty());
}

TEST_CASE("an ask-level tool in a task is denied and recorded, and nothing prompts",
          "[commands][task][gate]") {
    const Home home{R"({"text": "1. Write it."},
                       {"text": "", "tool_calls": [{"name": "write_file",
                         "arguments": {"path": "task-test-out.txt", "content": "hi"}}]},
                       {"text": "Refused: {{last_tool_result}}\nTASK STATUS: DONE"})"};
    std::string out;
    // One round: the write is denied, the file never appears, and the budget
    // ends the task naming the check -- never a claimed success.
    CHECK(home.run({"task", "run", "Write a file", "--tools", "--rounds", "1", "--require-file",
                    "task-test-out.txt"},
                   &out) == 1);
    CHECK_FALSE(fs::exists("task-test-out.txt"));
    CHECK(has(out, "[task] denied: write_file on task-test-out.txt"));
    const t::Task task = home.task();
    CHECK(task.status == t::kExhausted);
    CHECK(has(task.reason, "not passed: the file "));
    CHECK(has(task.reason, "task-test-out.txt exists and is not empty (no such file)"));
    const t::Round& round = task.rounds.back();
    REQUIRE(round.denied.size() == 1);
    CHECK(round.denied[0].tool == "write_file");
    // The denial was the tool's result, and the turn went on to answer.
    CHECK(round.outcome == t::kCompleted);
    CHECK(round.self_report == "done");
    CHECK(has(out, "Refused: Error: the user denied permission to run 'write_file'"));

    REQUIRE(home.run({"task", "status", task.id}, &out) == 0);
    CHECK(has(out, "denied:\n  round 1: write_file on task-test-out.txt\n"));
}

TEST_CASE("a second task run while one is live is refused by the lock, naming it",
          "[commands][task][lock]") {
    const Home home{R"({"text": "x"})"};
    std::string error;
    const std::optional<t::TaskLock> held =
        t::TaskLock::acquire(home.tasks(), "task-20261004-120000", error);
    REQUIRE(held.has_value());
    std::string out;
    CHECK(home.run({"task", "run", "goal"}, &out) == 1);
    CHECK(has(out, "task task-20261004-120000 is running (process "));
    CHECK(has(out, "one task runs at a time"));
    CHECK(t::list_tasks(home.tasks()).empty());
}

TEST_CASE(
    "halt and cancel of a task no process runs are written to its ledger, and chats "
    "delete refuses a live task's conversation",
    "[commands][task][chats]") {
    const Home home{R"({"text": "1. Plan."}, {"text": "not yet"})"};
    std::string out;
    REQUIRE(home.run({"task", "run", "goal", "--require", "42", "--rounds", "1"}, &out) == 1);
    t::Task task = home.task();
    REQUIRE(task.status == t::kExhausted);

    // Make it a halted task, as `task halt` leaves one, to try the verbs on.
    task.status = std::string{t::kHalted};
    REQUIRE(t::save_task(home.tasks(), task).empty());

    CHECK(home.run({"chats", "delete", task.session_id}, &out) == 1);
    CHECK(has(out, "is the conversation of task " + task.id + ", which is halted"));
    CHECK(has(out, "'apogee task cancel " + task.id + "' stops it first"));

    REQUIRE(home.run({"task", "halt", task.id}, &out) == 0);
    CHECK(has(out, "already halted"));
    REQUIRE(home.run({"task", "cancel", task.id}, &out) == 0);
    CHECK(has(out, "task " + task.id + " cancelled"));
    CHECK(home.task().status == t::kCancelled);
    CHECK(home.task().transitions.back().event == t::kFinishedEvent);
    CHECK(home.run({"task", "halt", task.id}, &out) == 1);
    CHECK(has(out, "is cancelled and not running"));

    REQUIRE(home.run({"chats", "delete", task.session_id}, &out) == 0);
    CHECK(has(out, "deleted " + task.session_id));

    // A finished task has nothing to stop.
    task.status = std::string{t::kDone};
    REQUIRE(t::save_task(home.tasks(), task).empty());
    CHECK(home.run({"task", "cancel", task.id}, &out) == 1);
    CHECK(has(out, "is done -- there is nothing to cancel"));
}

TEST_CASE("a task resumes only in the folder it was started in, and never once finished",
          "[commands][task][resume]") {
    const Home home{R"({"text": "x"})"};
    t::Task task;
    task.id = "task-20261004-120000";
    task.goal = "goal";
    task.status = std::string{t::kHalted};
    task.working_directory = (home.home.home() / "elsewhere").string();
    task.session_id = "20261004-120000-abcd";
    REQUIRE(t::save_task(home.tasks(), task).empty());
    std::string out;
    CHECK(home.run({"task", "resume", task.id}, &out) == 1);
    CHECK(has(out, "resumes only there -- cd there first"));

    task.status = std::string{t::kExhausted};
    REQUIRE(t::save_task(home.tasks(), task).empty());
    CHECK(home.run({"task", "resume", task.id}, &out) == 1);
    CHECK(has(out, "spent its round budget"));

    CHECK(home.run({"task", "status", "task-nope"}, &out) == 1);
    CHECK(has(out, "no task 'task-nope'"));
}

TEST_CASE("task verbs with no task say how to start one", "[commands][task]") {
    const Home home{R"({"text": "x"})"};
    std::string out;
    REQUIRE(home.run({"task", "list"}, &out) == 0);
    CHECK(has(out, "no tasks yet"));
    CHECK(home.run({"task", "status"}, &out) == 1);
    CHECK(has(out, "no tasks yet -- 'apogee task run \"<goal>\"' starts one"));
    CHECK(home.run({"task", "cancel"}, &out) == 1);
}
