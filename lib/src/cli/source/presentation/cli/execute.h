#pragma once

#include <string_view>

#include "cli/command.h"
#include "cli/suite_residency.h"

/// `apogee execute` (27s) -- chat's sibling whose unit is the suite: the
/// session core (`cli/chat_session.h`) opened with a suite instead of a
/// model, its symphonies first-class.
///
/// **A superset of chat for the suite case, never a second loop.** Bare input
/// converses with the suite's root model exactly as `chat --suite` does --
/// the same turn, the same persistence, resume, completion, busy line and
/// permission presets -- because it is the same code: `run_session` with
/// `SessionMode::Execute`. What execute adds is what the mode means: a suite
/// it insists on (named, resumed, else `models.default_suite`; with none it
/// refuses with the way to configure one, and `off` is never one), admitted
/// and warmed exactly as `chat --suite` (27e); a banner naming the suite and
/// its symphony count; and `/play <symphony> [input]` and `/symphonies` --
/// a play's stages narrated as side calls, its output the session's answer
/// and its history.
namespace apogee::commands {

class ExecuteCommand final : public Command {
public:
    /// `machine` is where a suite's admission reads the machine's budget
    /// (27e), as `ChatCommand`'s is.
    explicit ExecuteCommand(MachineBudgetSource machine = machine_budget);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;

private:
    MachineBudgetSource machine_;
};

}  // namespace apogee::commands
