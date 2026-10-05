#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "agent/tool.h"
#include "cli/command.h"
#include "cli/permissions.h"
#include "cli/suite_residency.h"
#include "contracts/config.h"
#include "logger/session.h"
#include "tasks/policy.h"

/// `apogee task` -- a goal in, the application drives it (27h).
///
/// `task run "<goal>" --require …` creates a task, and with it an ordinary
/// chat session; the runner (`tasks/runner`) plans, drives rounds, checks the
/// stated acceptance after each and composes the next from what failed,
/// until the task is done, its budget spent, the breaker trips, a turn fails
/// or someone halts or cancels it -- and the process exits. Each turn is the
/// chat's own (`cli/chat_turn.h`), so everything a chat gets a task's turns
/// get. With nobody at the terminal there is no confirm function, so an
/// `ask`-level tool denies as on any pipe, and a question fails the task --
/// unless the task was handed the authority beforehand (27i): `--allow
/// <tool>` grants, `--on-question answer:` one declared answer, `--agent`
/// an agent's policy to run under, all held no wider than the config and
/// that agent allow, and every use recorded. At a terminal the person there
/// is still asked about anything not granted: policy adds to the human path,
/// never replaces it. `status`, `list`, `resume`, `halt` and `cancel` read
/// and steer the ledger under `tasks/`; one task runs at a time, under its
/// lock.
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

/// The terminal's permission prompt over the answers a gate remembers, or a
/// function returning null -- nobody is there to ask.
using PersonPrompt = std::function<agent::ConfirmFn(std::shared_ptr<SessionApprovals>)>;

/// What one task's turns may do, composed (27i).
struct TaskGate {
    /// The tools the model is offered: the task's, narrowed by its agent's
    /// policy (Milestone X's filter, `apply_tool_policy`).
    agent::ToolRegistry tools;
    /// Chat's own composition -- the config's levels, then the session's
    /// answers -- with the task's grants seeded as the `session` answer given
    /// at launch, exactly as `chat --allow` seeds one (26o); the confirm the
    /// person at the terminal, or null.
    ToolGate gate;
    /// The config's levels alone, nothing granted or answered: what an
    /// allowed call's authority is told by.
    agent::PermissionChecker standing;
    /// Why the grants would take the task past the config or its agent,
    /// naming the rule (`tasks::grant_refusal`); empty when they do not, and
    /// then nothing above is to be used.
    std::string refusal;
};

/// The ceiling, then the gate, for a task whose tools before any agent's
/// policy are `available` (`tools.disabled` and the suite's toolset
/// applied). `agent` is the entry the task runs under, or null.
[[nodiscard]] TaskGate compose_task_gate(const harness::Config& config,
                                         const agent::ToolRegistry& available,
                                         const tasks::AutonomyPolicy& policy,
                                         const harness::AgentConfig* agent,
                                         const PersonPrompt& person);

}  // namespace apogee::commands
