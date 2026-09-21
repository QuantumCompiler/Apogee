#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "harness/config.h"

/// `apogee check` — the install doctor.
///
/// It answers one question: *is this installation in a state where the things
/// you ask of it will work?* Ommi's recorded reason for having it is that its
/// dominant early bug class was silent install drift — a fresh install missing
/// a directory, a config naming a model that moved, a backend with no key — and
/// every one of those surfaces later as a confusing failure in some unrelated
/// command rather than as an install problem.
///
/// Two rules shape the whole file:
///
/// **A fresh, empty install passes.** No API keys and no models is a *valid*
/// state — Apogee runs fully local and bundles nothing — so "you have not
/// configured a cloud backend" is information, never a failure. A doctor that
/// cries wolf on a correct install teaches its user to ignore it.
///
/// **check never edits your config.** It prints the exact command that fixes
/// what it found. `--fix` is allowed to repair the *local install* (a missing
/// directory, a wrong mode); it is not allowed to touch config, because
/// guessing what a dangling `model_path` was meant to point at is not a repair.
namespace apogee::commands {

/// How one check came out.
///
/// `Skipped` exists so a check that cannot run on this platform says so
/// instead of passing. Windows has no `0700`, and reporting "modes OK" there
/// would be a lie that reads exactly like a pass — which is the failure mode
/// this whole command exists to prevent, reproduced inside the tool itself.
enum class Status : std::uint8_t { Ok, Warn, Fail, Skipped };

[[nodiscard]] std::string_view to_string(Status status) noexcept;

struct CheckRow {
    Status status = Status::Ok;
    std::string section;
    std::string name;
    std::string detail;
    /// The exact command that fixes this, when one exists. Printed verbatim so
    /// it can be copied; never run automatically.
    std::string remedy;
};

struct CheckReport {
    std::vector<CheckRow> rows;

    [[nodiscard]] std::size_t count(Status status) const noexcept;

    [[nodiscard]] bool passed() const noexcept {
        return count(Status::Fail) == 0;
    }
};

/// Inputs the checks read, injected so the whole doctor is testable without
/// touching the developer's real installation.
struct CheckInputs {
    /// Root of the data directory to inspect.
    std::filesystem::path home;

    /// Config file path, and the load outcome. `config_error` non-empty means
    /// the file did not parse; `config_missing` distinguishes "not there yet"
    /// (a fresh install, a warning) from "broken" (a failure).
    std::filesystem::path config_path;
    harness::Config config;
    std::string config_error;
    bool config_missing = false;

    /// Path of the running binary, for the version and quarantine rows. Empty
    /// skips those.
    std::filesystem::path executable;

    /// Environment lookup, injected so a test can present a backend as having
    /// a key without setting one in the process.
    std::function<std::string(std::string_view)> env;
};

/// Runs every check and returns the report. Pure with respect to the machine
/// apart from reading `inputs.home` — it writes nothing, which is the point.
[[nodiscard]] CheckReport run_checks(const CheckInputs& inputs);

/// Repairs what is safely repairable: creates missing layout directories and
/// corrects private modes. Returns what it did. **Never touches config.**
[[nodiscard]] std::vector<std::string> apply_fixes(const CheckInputs& inputs);

/// Renders a report for a terminal. `color` gates ANSI.
[[nodiscard]] std::string render_report(const CheckReport& report, bool use_color);

class CheckCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
