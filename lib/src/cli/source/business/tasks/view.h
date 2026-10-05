#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "tasks/ledger.h"
#include "tasks/task.h"

/// What every surface shows of a task (27j): one view, built from the ledger
/// once, rendered three ways -- `task status` for a person, its JSON document
/// for a host (`--output-format json`, which is also what `GET
/// /v1/admin/tasks/{id}` serves, byte for byte), and machine mode's task
/// events for a front-end watching a run. Nothing computes a task's state a
/// second way: the status a person reads, each check's state, the rounds
/// used, whether a process runs it now, are decided here and only here.
///
/// **A view type, by construction** (CLAUDE.md -> secrets are unleakable by
/// construction): a task's declared answer (27i) is in its ledger and in
/// `task status`, and in no type below. `PolicyView` says a task answers its
/// questions (`on_question: answer`) and not with what; `QuestionView` says a
/// question was answered and by whom, never what the answer said -- so a
/// served view or an event a host logs cannot carry it, rather than merely
/// not doing so. Nor does anything below name a path into the private
/// layout: the ledger's own path, the session's file. The folder a task works
/// in and a `--require-file` path are the user's, and are shown as the human
/// view shows them.
namespace apogee::tasks {

/// `task list` shows the newest this many unless `--all`; the admin list
/// mirrors it (27j's confirmed default).
inline constexpr std::size_t kListLimit = 50;

/// One acceptance check, as the newest completed round left it.
struct CheckView {
    /// `require` or `require_file`.
    std::string kind;
    std::string value;
    /// What it asks, as a person reads it: `the answer contains "42"`.
    std::string description;
    /// Whether a completed round has evaluated it yet.
    bool ran = false;
    bool passed = false;
    /// Why, in a few words; empty before it ran.
    std::string detail;
};

/// A gated call let through or refused, and on whose authority.
struct UseView {
    std::string tool;
    std::string target;
    /// `config`, `grant` or `person` let it through; `config`, `nobody` or
    /// `person` refused it -- empty in a ledger from before 27i.
    std::string by;
};

/// A question the model asked that was answered, and by whom -- `declared`
/// or `person`. Structurally no answer.
struct QuestionView {
    std::string question;
    std::string by;
};

/// One turn: the plan (round 0) or a round of the budget.
struct TurnView {
    int round = 0;
    /// `plan`, `execute` or `correct`.
    std::string kind;
    /// `completed`, `interrupted`, or `in_flight` while it runs -- or when
    /// its process died mid-turn.
    std::string outcome;
    /// Its answer was found in the conversation after a restart.
    bool adopted = false;
    /// The task's checks this round passed; 0 for the plan.
    int checks_passed = 0;
    /// `done`, `not_done` or `unreported`; empty for the plan.
    std::string self_report;
    /// The tool calls that ran.
    int tools = 0;
    std::vector<UseView> allowed;
    std::vector<UseView> denied;
    std::vector<QuestionView> answered;
    std::int64_t tokens = 0;
    bool tokens_estimated = false;
    std::string started_at;
    std::string ended_at;
};

/// What the task was handed before it ran (27i) -- never the declared
/// answer's text.
struct PolicyView {
    /// The agent whose policy it runs under; empty for none.
    std::string agent;
    std::vector<std::string> grants;
    /// `fail`, or `answer`: every question gets the declared answer.
    std::string on_question;
};

struct TaskView {
    std::string id;
    /// The ledger's status.
    std::string status;
    /// The process running it now -- only while it is planning or running.
    std::optional<long> process;
    /// Planning or running by its ledger, with no process running it: killed
    /// mid-turn, and resumable.
    bool interrupted = false;
    std::string goal;
    /// The chat the task drives, by id.
    std::string conversation;
    /// Where it was started: its tools work there, and it resumes only there.
    std::string folder;
    bool tools = false;
    /// Without tools there is nothing to grant or ask, so nothing to show.
    std::optional<PolicyView> policy;
    int rounds_used = 0;
    int rounds_budget = 0;
    std::string created_at;
    std::string updated_at;
    /// Why it stopped; empty while it runs.
    std::string reason;
    std::vector<CheckView> checks;
    /// The newest completed round's self-report: `done`, `not_done`, or
    /// `unreported` -- before a round has completed too.
    std::string self_report;
    std::string plan;
    /// The plan first, then the rounds, in order.
    std::vector<TurnView> turns;
};

/// One task in a listing.
struct TaskSummary {
    std::string id;
    std::string status;
    int rounds_used = 0;
    int rounds_budget = 0;
    std::string goal;
};

/// A listing: the tasks shown, newest first, and how many there are.
struct TaskListView {
    std::vector<TaskSummary> shown;
    /// Every task, shown or not: past `shown`'s size is what `--all` adds.
    std::size_t total = 0;
};

/// `task`'s view. `holder` is the task lock's holder now (`lock_holder`):
/// it says whether a process runs the task.
[[nodiscard]] TaskView make_task_view(const Task& task, const std::optional<LockHolder>& holder);
[[nodiscard]] TurnView make_turn_view(const Round& round);
/// Each check as the newest completed round left it -- `ran` false for
/// every one before a round has completed.
[[nodiscard]] std::vector<CheckView> make_check_views(const Task& task);
[[nodiscard]] TaskSummary make_task_summary(const Task& task);
/// `tasks`, newest first as `list_tasks` gives them: the newest `kListLimit`
/// unless `all`.
[[nodiscard]] TaskListView make_task_list(const std::vector<Task>& tasks, bool all);

[[nodiscard]] nlohmann::json to_json(const TaskView& view);
[[nodiscard]] nlohmann::json to_json(const TurnView& view);
[[nodiscard]] nlohmann::json to_json(const CheckView& view);
[[nodiscard]] nlohmann::json to_json(const TaskSummary& summary);
/// The listing as one document -- `task list --output-format json` and `GET
/// /v1/admin/tasks` alike: `{"object": "list", "data": [summary...],
/// "total": N}`.
[[nodiscard]] nlohmann::json to_json(const TaskListView& list);

}  // namespace apogee::tasks
