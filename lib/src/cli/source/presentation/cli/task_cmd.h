#pragma once

#include <string>
#include <string_view>

#include "cli/command.h"
#include "cli/suite_residency.h"
#include "logger/session.h"

/// `apogee task` -- a goal in, the application drives it (27h).
///
/// `task run "<goal>" --require …` creates a task, and with it an ordinary
/// chat session; the runner (`tasks/runner`) plans, drives rounds, checks the
/// stated acceptance after each and composes the next from what failed,
/// until the task is done, its budget spent, the breaker trips, a turn fails
/// or someone halts or cancels it -- and the process exits. Each turn is the
/// chat's own (`cli/chat_turn.h`), so everything a chat gets a task's turns
/// get, with nobody to ask: no confirm function, so an `ask`-level tool
/// denies as on any pipe, and a question fails the task. `status`, `list`,
/// `resume`, `halt` and `cancel` read and steer the ledger under
/// `tasks/`; one task runs at a time, under its lock.
namespace apogee::commands {

class TaskCommand final : public Command {
public:
    /// `machine` is where a suite's admission reads the machine's budget
    /// (27e), as `chat`'s.
    explicit TaskCommand(MachineBudgetSource machine = machine_budget);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;

private:
    MachineBudgetSource machine_;
};

/// Why `session` may not be deleted -- a live task drives it, named with
/// the way out -- or empty when it may.
[[nodiscard]] std::string task_holds_chat(const logger::Session& session);

}  // namespace apogee::commands
