#include "tasks/task.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "cli/permissions.h"
#include "cli/task_cmd.h"
#include "contracts/layout.h"
#include "logger/session.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "tasks/ledger.h"
#include "tasks/policy.h"
#include "tasks/unattended.h"

/// `apogee task` on the real command tree against a scripted mock: a run to
/// done with no input, its status and its place in the list; refusals before
/// anything is made; an `ask`-level tool denied and recorded, nothing
/// prompting; the lock refusing a second run by the first's name; halt and
/// cancel of a task no process runs; a resume refused outside the task's
/// folder; and `chats delete` refusing a live task's conversation. And the
/// autonomy policy (27i): a granted write run unprompted and recorded, a
/// declared answer consumed and recorded, every grant past the config or the
/// agent refused naming the rule -- and the composition the command runs,
/// exhaustively, through the real gate.
namespace {

namespace fs = std::filesystem;
namespace t = apogee::tasks;
using apogee::testing::CliHome;

/// A home whose one backend answers from `turns`, recall off so no summary
/// spends a scripted turn. `extra` is more of the config, at its top level;
/// the filesystem tools work in `work()`.
struct Home {
    explicit Home(const std::string& turns, const std::string& extra = {})
        : home{
              "backends:\n  m:\n    type: mock\nmodels:\n  default: m\nmemory:\n  recall: "
              "false\n"} {
        script = home.home() / "script.json";
        std::ofstream{script} << R"({"turns": [)" << turns << "]}";
        fs::create_directories(work());
        std::ofstream{home.config_path()}
            << "backends:\n  m:\n    type: mock\n    model_path: " << script.string()
            << "\nmodels:\n  default: m\nmemory:\n  recall: false\ntools:\n  fs_root: "
            << work().string() << "\n"
            << extra;
    }

    [[nodiscard]] fs::path work() const {
        return home.home() / "work";
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
    CHECK(has(out,
              "denied:\n  round 1: write_file on task-test-out.txt -- nobody present to "
              "allow it\n"));
    CHECK(has(out, "grants:        none -- with nobody present, a tool that asks is denied\n"));
    CHECK(has(out, "on question:   fail -- a question nobody answers ends the task\n"));
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

// ---------------------------------------------------------------------------
// The autonomy policy (27i)
// ---------------------------------------------------------------------------

namespace {

/// A model that plans, writes `out.txt`, then reports what the write said.
constexpr const char* kWrites = R"({"text": "1. Write it."},
    {"text": "", "tool_calls": [{"name": "write_file",
      "arguments": {"path": "out.txt", "content": "the report"}}]},
    {"text": "Wrote it: {{last_tool_result}}\nTASK STATUS: DONE"})";

/// A model that plans, asks which colour, then answers with what it was told.
constexpr const char* kAsks = R"({"text": "1. Ask."},
    {"text": "", "tool_calls": [{"name": "ask_user", "arguments": {"questions": [
      {"header": "Colour", "question": "Which colour?",
       "options": [{"label": "Red"}, {"label": "Green"}]}]}}]},
    {"text": "Told: {{last_tool_result}}\nTASK STATUS: DONE"})";

}  // namespace

TEST_CASE("a granted tool writes unprompted, and status shows the grant and each use",
          "[commands][task][policy][grant]") {
    const Home home{kWrites};
    const fs::path written = home.work() / "out.txt";
    std::string out;
    REQUIRE(home.run({"task", "run", "Write the report", "--tools", "--allow", "write_file",
                      "--rounds", "1", "--require-file", written.string()},
                     &out) == 0);
    CHECK(fs::exists(written));
    CHECK(has(out, "[task] write_file on out.txt -- allowed by this task's grant"));
    CHECK_FALSE(has(out, "[y]es"));
    const t::Task task = home.task();
    CHECK(task.status == t::kDone);
    CHECK(task.policy.grants == std::vector<std::string>{"write_file"});
    const t::Round& round = task.rounds.back();
    REQUIRE(round.allowed.size() == 1);
    CHECK(round.allowed[0].tool == "write_file");
    CHECK(round.allowed[0].target == "out.txt");
    CHECK(round.allowed[0].by == t::kByGrant);
    CHECK(round.denied.empty());

    REQUIRE(home.run({"task", "status", task.id}, &out) == 0);
    CHECK(has(out, "grants:        write_file\n"));
    CHECK(has(out, "allowed:\n  round 1: write_file on out.txt -- this task's grant\n"));
}

TEST_CASE("a declared answer lets a question-asking task finish, recorded; without it, it fails",
          "[commands][task][policy][question]") {
    const Home home{kAsks};
    std::string out;
    REQUIRE(home.run({"task", "run", "Pick a colour", "--tools", "--on-question", "answer:blue",
                      "--require", "blue"},
                     &out) == 0);
    CHECK(has(out, "[task] a question answered with the declared answer: Which colour?"));
    const t::Task task = home.task();
    CHECK(task.status == t::kDone);
    CHECK(task.policy.on_question == t::OnQuestion::Answer);
    CHECK(task.policy.answer == "blue");
    const t::Round& round = task.rounds.back();
    REQUIRE(round.answered.size() == 1);
    CHECK(round.answered[0].question == "Which colour?");
    CHECK(round.answered[0].answer == "blue");
    CHECK(round.answered[0].by == t::kByDeclared);
    REQUIRE(home.run({"task", "status", task.id}, &out) == 0);
    CHECK(has(out, "on question:   answer \"blue\" -- every question gets this declared answer\n"));
    CHECK(has(out, "answered:\n  round 1: Which colour? -- \"blue\", the declared answer\n"));

    // The same task, nothing declared: it fails naming the question.
    const Home bare{kAsks};
    CHECK(bare.run({"task", "run", "Pick a colour", "--tools", "--require", "blue"}, &out) == 1);
    const t::Task failed = bare.task();
    CHECK(failed.status == t::kFailed);
    CHECK(failed.reason ==
          "round 1: the model asked a question and no one is present to answer it: Which "
          "colour?");
    CHECK(failed.rounds.back().answered.empty());
}

TEST_CASE("a grant wider than the config or the agent is refused at task run, naming the rule",
          "[commands][task][policy][ceiling]") {
    const Home home{kWrites, "permissions:\n  delete_file: deny\n"};
    std::string out;

    struct Refusal {
        std::vector<std::string> args;
        std::string says;
    };

    const std::vector<Refusal> refusals{
        {{"--tools", "--allow", "delete_file"},
         "--allow delete_file: the config says permissions.delete_file: deny -- a task's grant "
         "is never wider than the config"},
        {{"--tools", "--agent", "security-review", "--allow", "write_file"},
         "--allow write_file: the agent 'security-review' runs with tools: read-only, which "
         "leaves out write_file -- a task's grant is never wider than its agent's policy"},
        {{"--tools", "--allow", "read_file"}, "--allow read_file: read_file never asks"},
        {{"--tools", "--allow", "fetch_url"},
         "--allow fetch_url: fetch_url is asked about per website, never per tool"},
        {{"--tools", "--allow", "wrte_file"}, "--allow wrte_file: no tool named 'wrte_file'"},
        {{"--allow", "write_file"}, "--allow needs --tools"},
        {{"--on-question", "answer:blue"}, "--on-question answer: needs --tools"},
        {{"--agent", "security-review"}, "--agent needs --tools"},
        {{"--tools", "--on-question", "maybe"}, "--on-question: 'maybe' is neither fail nor"},
        {{"--tools", "--on-question", "answer:"}, "--on-question answer: needs the answer"},
        {{"--tools", "--agent", "nope"}, "--agent: no agent named 'nope' (available: "},
        {{"--tools", "--agent", "security-review", "--on-question", "answer:blue"},
         "the agent 'security-review' asks no questions"},
    };
    for (const Refusal& refusal : refusals) {
        std::vector<std::string> args{"task", "run", "Write the report"};
        args.insert(args.end(), refusal.args.begin(), refusal.args.end());
        INFO(refusal.says);
        CHECK(home.run(args, &out) == 1);
        CHECK(has(out, refusal.says));
        // Refused before anything is made.
        CHECK(t::list_tasks(home.tasks()).empty());
    }
    CHECK_FALSE(fs::exists(home.work() / "out.txt"));

    // Under the agent, with nothing granted: its policy holds -- the write is
    // not among its tools, so it is never offered, let alone run.
    REQUIRE(home.run({"task", "run", "Write the report", "--tools", "--agent", "security-review",
                      "--rounds", "1"},
                     &out) == 0);
    CHECK_FALSE(fs::exists(home.work() / "out.txt"));
    CHECK(has(out, "Error: no tool named 'write_file'"));
    const t::Task task = home.task();
    CHECK(task.policy.agent == "security-review");
    REQUIRE(home.run({"task", "status", task.id}, &out) == 0);
    CHECK(has(out, "agent:         security-review -- the task runs under its policy\n"));
}

TEST_CASE("an agent that asks no questions is never offered ask_user in a task",
          "[commands][task][policy][question]") {
    // The bundled reviewer's `questions: false` is its policy too: a model
    // reaching for ask_user under it finds no such tool, rather than a
    // question that fails the task.
    const Home home{kAsks};
    std::string out;
    REQUIRE(home.run({"task", "run", "Pick a colour", "--tools", "--agent", "security-review"},
                     &out) == 0);
    CHECK(has(out, "Told: Error: no tool named 'ask_user'"));
    CHECK(home.task().status == t::kDone);
    CHECK(home.task().rounds.back().answered.empty());
}

TEST_CASE("a task resumes under its grants only while the config still allows them",
          "[commands][task][policy][resume]") {
    const Home home{R"({"text": "1. Plan."}, {"text": "not yet"})"};
    std::string out;
    REQUIRE(home.run({"task", "run", "goal", "--tools", "--allow", "write_file", "--require", "42",
                      "--rounds", "1"},
                     &out) == 1);
    t::Task task = home.task();
    REQUIRE(task.status == t::kExhausted);
    task.status = std::string{t::kHalted};
    REQUIRE(t::save_task(home.tasks(), task).empty());
    // The config changed under it: the grant is now wider than the config.
    std::ofstream{home.home.config_path(), std::ios::app} << "permissions:\n  write_file: deny\n";
    CHECK(home.run({"task", "resume", task.id}, &out) == 1);
    CHECK(has(out, "task " + task.id +
                       " cannot resume: --allow write_file: the config says "
                       "permissions.write_file: deny"));
    CHECK(home.task().status == t::kHalted);

    // Nor under an agent the config no longer has.
    task.policy.grants.clear();
    task.policy.agent = "gone";
    REQUIRE(t::save_task(home.tasks(), task).empty());
    CHECK(home.run({"task", "resume", task.id}, &out) == 1);
    CHECK(has(out, "task " + task.id +
                       " cannot resume: it runs under the agent 'gone', which the config no "
                       "longer has"));
    CHECK(home.task().status == t::kHalted);
}

TEST_CASE("repeatable task flags are taken whole", "[commands][task][policy]") {
    const Home home{R"({"text": "1. Plan."}, {"text": "a and b\nTASK STATUS: DONE"})"};
    std::string out;
    REQUIRE(home.run({"task",
                      "run",
                      "goal",
                      "--require",
                      "a",
                      "--require",
                      "b",
                      "--require-file",
                      (home.work() / "x").string(),
                      "--require-file",
                      (home.work() / "y").string(),
                      "--tools",
                      "--allow",
                      "write_file",
                      "--allow",
                      "edit_file",
                      "--allow",
                      "write_file",
                      "--rounds",
                      "1"},
                     &out) == 1);
    const t::Task task = home.task();
    CHECK(task.checks.size() == 4);
    CHECK(task.policy.grants == std::vector<std::string>{"edit_file", "write_file"});
}

namespace {

/// The tools a composition-table row starts from: one that writes, one that
/// reads, one that reaches a website -- each counting the times it ran.
struct TableTools {
    int writes = 0;
    int reads = 0;
    int fetches = 0;
    apogee::agent::ToolRegistry registry;

    TableTools() {
        apogee::agent::Tool write;
        write.name = "write_file";
        write.writes = true;
        write.describe_target = [](std::string_view) { return std::string{"out.txt"}; };
        write.run = [this](std::string_view) {
            ++writes;
            return apogee::agent::ToolOutcome{"written", false};
        };
        registry.add(write);
        apogee::agent::Tool read;
        read.name = "read_file";
        read.run = [this](std::string_view) {
            ++reads;
            return apogee::agent::ToolOutcome{"read", false};
        };
        registry.add(read);
        apogee::agent::Tool fetch;
        fetch.name = "fetch_url";
        fetch.outbound = true;
        fetch.describe_target = [](std::string_view) { return std::string{"example.org"}; };
        fetch.run = [this](std::string_view) {
            ++fetches;
            return apogee::agent::ToolOutcome{"fetched", false};
        };
        registry.add(fetch);
    }

    TableTools(const TableTools&) = delete;
    TableTools& operator=(const TableTools&) = delete;
    TableTools(TableTools&&) = delete;
    TableTools& operator=(TableTools&&) = delete;
    ~TableTools() = default;
};

enum class Present : std::uint8_t { Nobody, SaysYes, SaysNo };

}  // namespace

TEST_CASE("the composition, exhaustively: config x agent x grants x the person present",
          "[commands][task][policy][ceiling][composition]") {
    using apogee::harness::AgentToolPolicy;
    using apogee::harness::PermissionLevel;
    const std::array<std::optional<AgentToolPolicy>, 4> agents{
        std::nullopt, AgentToolPolicy::All, AgentToolPolicy::ReadOnly, AgentToolPolicy::None};
    const std::array<std::optional<PermissionLevel>, 4> levels{
        std::nullopt, PermissionLevel::Ask, PermissionLevel::Allow, PermissionLevel::Deny};
    const std::array<bool, 2> grants{false, true};
    const std::array<Present, 3> people{Present::Nobody, Present::SaysYes, Present::SaysNo};
    int rows = 0;
    int refused_rows = 0;
    int ran_unattended = 0;
    for (const auto& policy : agents) {
        for (const auto& level : levels) {
            for (const bool granted : grants) {
                for (const Present present : people) {
                    ++rows;
                    TableTools tools;
                    apogee::harness::Config config;
                    if (level.has_value()) {
                        config.permissions.levels["write_file"] = *level;
                    }
                    t::AutonomyPolicy autonomy;
                    if (granted) {
                        autonomy.grants = {"write_file"};
                    }
                    apogee::harness::AgentConfig agent;
                    if (policy.has_value()) {
                        autonomy.agent = "helper";
                        agent.tools = *policy;
                    }
                    int asked = 0;
                    const apogee::commands::PersonPrompt person =
                        [present, &asked](std::shared_ptr<apogee::commands::SessionApprovals>)
                        -> apogee::agent::ConfirmFn {
                        if (present == Present::Nobody) {
                            return nullptr;
                        }
                        return [present, &asked](const apogee::agent::GateRequest&) {
                            ++asked;
                            return present == Present::SaysYes;
                        };
                    };
                    const apogee::commands::TaskGate composed = apogee::commands::compose_task_gate(
                        config, tools.registry, autonomy, policy.has_value() ? &agent : nullptr,
                        person);
                    INFO("agent " << (policy.has_value() ? apogee::harness::to_string(*policy)
                                                         : "none")
                                  << ", level "
                                  << (level.has_value() ? apogee::harness::to_string(*level)
                                                        : "unset")
                                  << ", granted " << granted << ", present "
                                  << static_cast<int>(present) << ": " << composed.refusal);

                    const bool agent_keeps = !policy.has_value() || *policy == AgentToolPolicy::All;
                    const bool config_denies = level == PermissionLevel::Deny;
                    const bool config_allows = level == PermissionLevel::Allow;

                    // The ceiling: a grant the config or the agent would not
                    // allow is refused before anything runs.
                    const bool refuse = granted && (!agent_keeps || config_denies);
                    CHECK(composed.refusal.empty() == !refuse);
                    if (refuse) {
                        ++refused_rows;
                        continue;
                    }

                    const t::TurnRecorder recorder;
                    const t::WatchedGate watched =
                        recorder.watch(composed.gate.permission, composed.standing, autonomy.grants,
                                       composed.gate.confirm);
                    const apogee::agent::DispatchContext context{.permission = watched.permission,
                                                                 .confirm = watched.confirm};
                    const apogee::agent::ToolOutcome write = apogee::agent::dispatch(
                        composed.tools, {.id = "1", .name = "write_file", .arguments = "{}"},
                        context);
                    (void)apogee::agent::dispatch(
                        composed.tools, {.id = "2", .name = "read_file", .arguments = "{}"},
                        context);
                    (void)apogee::agent::dispatch(
                        composed.tools, {.id = "3", .name = "fetch_url", .arguments = "{}"},
                        context);

                    // What may run: what config x agent allow, and -- only
                    // when someone is there to say so -- the person's yes.
                    const bool offered = agent_keeps;
                    const bool may_run = offered && !config_denies &&
                                         (config_allows || granted || present == Present::SaysYes);
                    CHECK((tools.writes == 1) == may_run);
                    CHECK(write.is_error == !may_run);
                    if (!offered) {
                        CHECK(write.content.starts_with("Error: no tool named 'write_file'"));
                    }
                    // Never wider than config x agent with nobody present.
                    if (present == Present::Nobody && tools.writes == 1) {
                        ++ran_unattended;
                        CHECK(agent_keeps);
                        CHECK((config_allows || (granted && !config_denies)));
                    }
                    // A read never asks; a website is never granted -- with
                    // nobody present only a listed host is reached.
                    CHECK(tools.reads == (policy == AgentToolPolicy::None ? 0 : 1));
                    CHECK(tools.fetches ==
                          (policy != AgentToolPolicy::None && present == Present::SaysYes ? 1 : 0));

                    // Every decision recorded, with whose authority.
                    const std::vector<t::Permit> allowed = recorder.take_allowed();
                    const std::vector<t::Denial> denied = recorder.take_denials();
                    const auto permit_of = [&allowed](std::string_view tool) {
                        return std::ranges::find_if(allowed, [tool](const t::Permit& permit) {
                            return permit.tool == tool;
                        });
                    };
                    const auto denial_of = [&denied](std::string_view tool) {
                        return std::ranges::find_if(denied, [tool](const t::Denial& denial) {
                            return denial.tool == tool;
                        });
                    };
                    if (may_run) {
                        REQUIRE(permit_of("write_file") != allowed.end());
                        CHECK(permit_of("write_file")->by == (config_allows ? t::kByConfig
                                                              : granted     ? t::kByGrant
                                                                            : t::kByPerson));
                        CHECK(permit_of("write_file")->target == "out.txt");
                    } else if (offered) {
                        REQUIRE(denial_of("write_file") != denied.end());
                        CHECK(denial_of("write_file")->by == (config_denies ? t::kByConfig
                                                              : present == Present::Nobody
                                                                  ? t::kByNobody
                                                                  : t::kByPerson));
                    } else {
                        CHECK(permit_of("write_file") == allowed.end());
                        CHECK(denial_of("write_file") == denied.end());
                    }
                    // The prompt was put only where the gate would ask.
                    const bool gate_asks = offered && !config_denies && !config_allows && !granted;
                    const int prompts_for_write = gate_asks && present != Present::Nobody ? 1 : 0;
                    const int prompts_for_fetch =
                        policy != AgentToolPolicy::None && present != Present::Nobody ? 1 : 0;
                    CHECK(asked == prompts_for_write + prompts_for_fetch);
                }
            }
        }
    }
    CHECK(rows == 96);
    // Granted under a read-only or a no-tools agent (2 x 4 levels x 3 people),
    // and granted against a deny under no agent or an `all` one (2 x 3).
    CHECK(refused_rows == 30);
    // Allowed by the config (2 agents x 2 grant states) or granted at ask or
    // unset (2 agents x 2 levels): nothing else ran with nobody present.
    CHECK(ran_unattended == 8);
}
