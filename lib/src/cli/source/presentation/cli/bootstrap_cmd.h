#pragma once

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>
#include <vector>

#include "cli/command.h"

/// `apogee bootstrap` (51): a fresh install that arrives ready.
///
/// Every requirement set of `train setup`'s one table this host is for --
/// the table's own column says which -- then the providers this machine has
/// and the models its store holds, registered. Each step is the shipped
/// command itself, run as a captured child of this binary (the shell's
/// precedent, `cli/tui_child`): `train setup --with <set>`, `providers scan
/// --register`, `config scan --register`. Bootstrap composes them and
/// reimplements none, so a set added to the table joins with no edit here.
///
/// **Never fatal.** A step that cannot finish -- offline, nothing to
/// register, no child processes on this platform -- is skipped with its
/// reason and the rest still run; the install stays green, and the last
/// line names `apogee bootstrap`, which runs it all again any time.
/// `--minimal`, or `APOGEE_MINIMAL_INSTALL` in the environment, skips it
/// whole, said. Every install path runs it right after `check --fix`.
namespace apogee::commands {

/// One step, and how it ended.
struct BootstrapStep {
    /// What it sets up, in words: `the 'video' set`, `the providers`.
    std::string name;
    /// The command it runs, `apogee` left off; empty for a set this host is
    /// not for.
    std::vector<std::string> words;
    /// `done` or `skipped` -- or empty, not yet run.
    std::string status{};
    std::string reason{};
};

/// Every step a bootstrap takes on a host with these facts, in order: each
/// set of the table -- one this host is not for already skipped, saying
/// why -- then the providers, then the model store.
[[nodiscard]] std::vector<BootstrapStep> bootstrap_plan(bool apple_silicon, bool nvidia_smi);

/// What a bootstrap needs from outside.
struct BootstrapSeams {
    bool apple_silicon = false;
    bool nvidia_smi = false;
    /// Runs `apogee <words...>` to its end, each line it writes handed on;
    /// its exit code, -1 when it could not start (said through `line`).
    std::function<int(const std::vector<std::string>& words,
                      const std::function<void(std::string)>& line)>
        run;
    /// A line of the narration.
    std::function<void(const std::string&)> say;
};

struct BootstrapResult {
    /// Skipped whole, and what asked for it: `--minimal` or
    /// `APOGEE_MINIMAL_INSTALL`.
    bool minimal = false;
    std::string minimal_by{};
    std::vector<BootstrapStep> steps;
};

/// One bootstrap: skipped whole when `minimal_by` is not empty, else every
/// step of the plan run in order, each said as it runs.
[[nodiscard]] BootstrapResult run_bootstrap(const BootstrapSeams& seams,
                                            const std::string& minimal_by = {});

/// `--output-format json`: `{"object": "apogee.bootstrap", "minimal", "steps":
/// [{"name", "command", "status", "reason"}]}`.
[[nodiscard]] nlohmann::json bootstrap_document(const BootstrapResult& result);

/// Whether the environment asks for a minimal install: `APOGEE_MINIMAL_INSTALL`
/// set to anything but empty, `0` or `false`.
[[nodiscard]] bool minimal_install_env();

class BootstrapCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
