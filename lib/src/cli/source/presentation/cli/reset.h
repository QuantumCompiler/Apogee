#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "cli/uninstall.h"
#include "contracts/layout.h"

/// `apogee reset` — the data directory back to a verified first-run state,
/// selectively (maintenance item M9).
///
/// Uninstall's contract is "Apogee is gone"; reset's is "Apogee starts over".
/// They are two verbs because overloading one with both blurs the one prompt
/// that must never be misread. Reset touches the data directory alone -- the
/// binary, the completions and the shell files are uninstall's business.
///
/// Everything it knows about the directory comes from the layout's one
/// declaration (`contracts/layout.h`), read at runtime: the rows it plans, the
/// rows `--keep` accepts and tab-completes, and -- by asking `check --fix` --
/// the skeleton it leaves. A row added to the layout is planned, keepable and
/// recreated with no change here. It shares uninstall's plan machinery: the
/// row walk, the user-data test, the warning, and the confirmation.
namespace apogee::commands {

/// What a reset would do, gathered before anything is deleted so the user is
/// shown the whole plan and answers once.
struct ResetPlan {
    /// The data directory. Nothing outside it is touched.
    std::filesystem::path home;

    /// Every row of the layout, in its order, each kept or not.
    std::vector<PlannedRow> rows;

    /// What sits at the top of the data directory that no row declares (the
    /// chat line editor's `chat_history`, a stray file). A first-run directory
    /// holds none of it, so it goes too -- named, like everything removed, and
    /// warned as the user's own, since nothing says it is not.
    std::vector<std::string> unlisted;

    /// The row holding the secrets store, when the store exists and that row
    /// is being removed: the user's stored API keys, named apart because their
    /// loss is a consequence to state loudly, never to discover. Empty when the
    /// store is absent or kept.
    std::string secrets_row;

    /// The store's file name, as the warning names it.
    std::string secrets_file;

    /// Whether running the plan removes anything at all.
    [[nodiscard]] bool removes_anything() const noexcept;
};

/// The rows `--keep` accepts, and completes to: every row of the layout, in
/// its order. Read from the declaration -- never a list of their own.
[[nodiscard]] std::vector<std::string> keepable_rows(
    std::span<const harness::LayoutEntry> rows = harness::data_directories());

/// Builds the plan for resetting `home`, keeping the rows `keep` names.
///
/// `secrets_store` is where the credential store lives for this root; it is
/// named in the plan when it exists inside a row being removed. `rows` is the
/// layout's declaration, a parameter only so a test can prove a new row
/// reaches the plan with no change to the command. Throws
/// `std::invalid_argument` for a `keep` that names no row: keeping nothing in
/// silence where the user asked to keep something is the one wrong answer.
[[nodiscard]] ResetPlan plan_reset(
    const std::filesystem::path& home, const std::vector<std::string>& keep,
    const std::filesystem::path& secrets_store,
    std::span<const harness::LayoutEntry> rows = harness::data_directories());

/// Renders the plan as the confirmation prompt's body: every row marked kept,
/// removed or absent, everything outside the layout named, then the user's own
/// data -- the secrets store by name -- warned exactly as uninstall warns it.
[[nodiscard]] std::string describe_plan(const ResetPlan& plan);

/// Executes `plan`: removes every row it does not keep and everything outside
/// the layout, and nothing else -- a kept row is not opened, let alone
/// rewritten. Returns what was removed; `errors` collects what could not be,
/// with the reason, and the removals after a failure still run. The skeleton
/// is the caller's to recreate, through `check --fix` (`run_check_pass`).
[[nodiscard]] std::vector<std::string> execute_reset(const ResetPlan& plan,
                                                     std::vector<std::string>& errors);

class ResetCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
