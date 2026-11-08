#pragma once

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cli/command.h"
#include "cli/suite_residency.h"
#include "tui/list_view.h"
#include "tui/progress.h"

/// The shell's Task view (37e), the progress seam's first consumer: the tasks
/// as `task list --all --output-format json` states them, Enter `task
/// status`'s card, and the commands' own cores on its keys --
///
/// - `r` runs a new task with the goal typed in the input row, asked first
///   with the policy it runs under, as machine mode runs `task run "<goal>"
///   --tools`: nobody asked anything, a tool that asks denied, a question
///   with no declared answer ending the task, exactly as on any pipe. On a
///   row `task resume` takes, `r` resumes it instead.
/// - `h` halts and `c` cancels, asked first, through `task halt`'s and `task
///   cancel`'s core: the request file a running task's watcher reads.
///
/// A run's narration is its events, worded by `task_event_lines` in their
/// own vocabulary (27j), drawn through `tui/progress`; Ctrl-C there cancels
/// it as `task cancel` does (`TaskRunStop`). The ledger stays the one record:
/// the table is read on show, on a slow tick while a run is live, and once
/// it ends.
namespace apogee::commands {

/// Ctrl-C on the Task view's run (57u), through `task cancel`'s core
/// (`request_task_stop`) on the task the run is for, and only once the run
/// holds the task lock: from its first event naming the task, or before that
/// while this process's lock names it, the run still starting. A resume
/// knows its task from the start, a new run learns it from the lock or that
/// event; either way, a stop pressed before the run took the lock is kept
/// and asked the moment the first event names the task, which the run
/// writes before its first turn and so reads before any model call. Asked
/// any earlier, a resume's stop would find no process running its task and
/// cancel it under the lock itself -- which the resume would then resume.
/// Any thread.
class TaskRunStop {
public:
    /// `id`: the task a resume runs; empty for a new run.
    explicit TaskRunStop(std::string id = {});

    TaskRunStop(const TaskRunStop&) = delete;
    TaskRunStop& operator=(const TaskRunStop&) = delete;
    TaskRunStop(TaskRunStop&&) = delete;
    TaskRunStop& operator=(TaskRunStop&&) = delete;
    ~TaskRunStop() = default;

    /// An event of the run named its task: a stop kept until now is asked.
    void named(const std::string& id);

    /// Ctrl-C: the line for the notice row -- the core's own, or why there
    /// was nothing to stop (the run already over), never silence.
    [[nodiscard]] std::string stop();

private:
    std::mutex mutex_;
    /// The task: a resume's from the start, a new run's once named.
    std::string id_;
    /// An event of the run has named its task: the run holds its lock.
    bool named_ = false;
    /// Pressed before the run took its lock: asked once it is named.
    bool pending_ = false;
};

/// A machine-mode event of a task run as the progress widget says it: the
/// task events in their own words -- the transition's event and status, a
/// round's checks, a grant, the finish's reason -- and a turn's tool
/// narration, answer and error; nothing for the rest (the session line, a
/// streamed delta).
[[nodiscard]] std::vector<std::string> task_event_lines(const nlohmann::json& event);

/// The view's options, its runs drawn through `progress` and admitted against
/// `machine` as `task run`'s are.
[[nodiscard]] tui::ListOptions task_view_options(const RootContext& context,
                                                 std::shared_ptr<tui::Progress> progress,
                                                 MachineBudgetSource machine = machine_budget);

}  // namespace apogee::commands
