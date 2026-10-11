#pragma once

#include <nlohmann/json_fwd.hpp>

#include <memory>
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
/// it as `task cancel` does. The ledger stays the one record: the table is
/// read on show, on a slow tick while a run is live, and once it ends.
namespace apogee::commands {

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
