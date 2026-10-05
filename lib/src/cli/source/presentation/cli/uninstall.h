#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "contracts/layout.h"

/// `apogee uninstall` — remove the binary, the data directory, and completions.
///
/// It lives in the product rather than in the installer scripts for the same
/// reason `check` does: there are two installers (bash and PowerShell) and one
/// of them cannot run on the platform the other targets. A removal implemented
/// twice in shell would drift exactly the way the install layout used to, and
/// the failure mode is worse — an uninstall that misses a path leaves state
/// behind that a later install inherits.
///
/// The plan machinery here is shared with `apogee reset` (`cli/reset.h`, M9):
/// the one walk of the layout's rows, the one test of what counts as the
/// user's own data, the one warning that names it, and the one confirmation
/// discipline. Two destructive verbs with two ideas of "your data" would be the
/// install-drift bug class again, in the one prompt that must never be misread.
namespace apogee::commands {

/// One row of the data directory as a destructive plan sees it.
struct PlannedRow {
    /// The row's path relative to the root, exactly as `layout.h` declares it.
    std::string name;

    /// Whether it exists under the root now. An absent row is nothing to remove.
    bool present = false;

    /// Whether it holds the user's own work: a `user_data` row with anything in
    /// it besides Apogee's own unedited bundled files. Named in the prompt.
    bool user_data = false;

    /// Spared by the plan (`reset --keep`): not removed, not recreated, not
    /// rewritten. Uninstall keeps nothing row by row; `--keep-data` keeps all.
    bool kept = false;
};

/// Walks `rows` under `home`: what exists, and what holds the user's data.
///
/// `rows` is the layout's one declaration. It is a parameter only so a test can
/// prove that a row added to the layout reaches every plan with no change to
/// any command -- nothing here, or in a caller, may carry a list of its own.
[[nodiscard]] std::vector<PlannedRow> plan_rows(
    const std::filesystem::path& home,
    std::span<const harness::LayoutEntry> rows = harness::data_directories());

/// The warning a destructive plan ends with when it reaches the user's own
/// data: each line (a row's `name/`, with any note) under "YOUR OWN DATA", then
/// "This cannot be undone." Empty for no lines -- a warning about nothing
/// trains the user to click through the one that matters.
[[nodiscard]] std::string describe_user_data(const std::vector<std::string>& lines);

/// How a destructive command's confirmation came out.
enum class Confirmation : std::uint8_t {
    /// `--yes`, or `yes` typed at the terminal.
    Proceed,
    /// Anything else typed at the terminal: nothing is done, and not an error.
    Cancelled,
    /// No terminal to ask and no `--yes`: refused, an error.
    Refused,
};

/// The confirmation discipline uninstall and reset share, verbatim: `--yes`
/// skips it; a terminal is asked to type `yes`; and anything that is not a
/// terminal is refused with the remediation, because a piped removal with no
/// way to ask must not decide on the user's behalf that the answer is yes.
/// `interactive` is whether stdin is a terminal -- passed in, so the rule is
/// testable without one. `command` names the verb in the refusal.
[[nodiscard]] Confirmation confirm_removal(std::string_view command, bool yes, bool interactive,
                                           std::istream& in, std::ostream& out, std::ostream& err);

/// Removes one planned path -- a tree when `recursive` -- and records it in
/// `removed`, or the path and the reason in `errors`: a partial failure is
/// reported, never hidden, and the removals after it still run. A symlink is
/// removed as itself, never followed. An empty path is nothing to remove.
void remove_planned(const std::filesystem::path& path, bool recursive,
                    std::vector<std::string>& removed, std::vector<std::string>& errors);

/// What an uninstall would remove, gathered before anything is deleted so the
/// user can be shown the whole list and answer once.
struct UninstallPlan {
    std::filesystem::path binary;
    std::filesystem::path data_directory;
    std::vector<std::filesystem::path> completions;

    /// Directories inside the data tree that hold the user's own work —
    /// conversations they had, models they supplied. Non-empty means the
    /// prompt must say so explicitly.
    std::vector<std::string> user_data;

    [[nodiscard]] bool touches_user_data() const noexcept {
        return !user_data.empty();
    }
};

/// Builds the plan for `home`, inspecting what actually exists.
///
/// `binary` may be empty when the platform will not report it; the plan then
/// simply has nothing to remove there, rather than guessing a path — deleting
/// the wrong file is the one outcome worse than leaving one behind.
[[nodiscard]] UninstallPlan plan_uninstall(const std::filesystem::path& home,
                                           const std::filesystem::path& binary);

/// Renders the plan as the confirmation prompt's body.
[[nodiscard]] std::string describe_plan(const UninstallPlan& plan);

/// Executes `plan`. Returns what was removed; `errors` collects paths that
/// could not be removed, so a partial failure is reported rather than hidden.
[[nodiscard]] std::vector<std::string> execute_uninstall(const UninstallPlan& plan,
                                                         std::vector<std::string>& errors);

class UninstallCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
