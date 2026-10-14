#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "cli/suite_residency.h"
#include "contracts/config.h"

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

class SessionOutput;
struct SessionFlags;

/// Execute's opening (ADR 0011): what `apogee execute` runs, and what every
/// front end's door calls -- the session core in execute mode over `flags`
/// parsed through execute's own table (`bind_session_flags`). Which suite,
/// which model, the project opening, the persona and admission are decided
/// once, in there, never by a door. `front` is where the session writes when
/// it is not the terminal: the full-screen shell's session view.
void open_execute_session(const RootContext& context, const SessionFlags& flags,
                          const MachineBudgetSource& machine, SessionOutput* front = nullptr);

/// A way into execute a front end's picker offers (ADR 0011), from the
/// command's own rungs on `config` as it stands.
struct ExecuteDoor {
    /// What the row says, in the banner's words.
    std::string label;
    /// The suite it opens under (`--suite`), or empty: `apogee execute` with
    /// nothing named, on its own rungs.
    std::string suite;
};

/// The doors execute opens on `config` (44b): one per configured suite, the
/// default first, each `--suite`'s; and the bare door -- `apogee execute`
/// with nothing named -- whenever no suite is the default and a backend
/// answers for the chat role (41a's rungs, suites configured or not). Never
/// empty (ADR 0012): with nothing to open on, the bare door still stands,
/// and choosing it runs execute's own opening, which refuses in the
/// command's words.
[[nodiscard]] std::vector<ExecuteDoor> execute_doors(const harness::Config& config);

/// The door when the config cannot be read at all: execute's own opening,
/// which says why.
[[nodiscard]] ExecuteDoor unconfigured_execute_door();

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
