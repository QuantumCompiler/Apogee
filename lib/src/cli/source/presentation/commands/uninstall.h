#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"

/// `apogee uninstall` — remove the binary, the data directory, and completions.
///
/// It lives in the product rather than in the installer scripts for the same
/// reason `check` does: there are two installers (bash and PowerShell) and one
/// of them cannot run on the platform the other targets. A removal implemented
/// twice in shell would drift exactly the way the install layout used to, and
/// the failure mode is worse — an uninstall that misses a path leaves state
/// behind that a later install inherits.
namespace apogee::commands {

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
