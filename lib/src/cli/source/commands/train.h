#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "harness/config.h"
#include "training/python_env.h"

/// `apogee train` -- fine-tuning local models. Created by the datasets item
/// with the one subcommand the whole track stands on, `setup`; the run item
/// adds `run|eval|promote|rollback|versions|status`.
///
/// **`setup` builds the Python environment Apogee owns** (`training/venv/`,
/// never the system Python) from `training.python` or `python3` on PATH,
/// and installs the requirement sets asked for: `--with prepare` for Parquet
/// in `datasets prepare`, `--trainer mlx|peft|auto` for a trainer's stack.
/// It is the ONE thing that downloads Python packages, and it runs only when
/// asked -- seeding the data directory never creates the environment, and a
/// command that finds it missing asks on a terminal or refuses on a pipe,
/// naming this command.
namespace apogee::commands {

class TrainCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

/// Which trainer stack `--trainer auto` picks on this host: `mlx` on
/// macOS/arm64, `peft` where `nvidia-smi` is on PATH, else empty with the
/// reason.
[[nodiscard]] std::string detect_trainer(std::string& reason);

/// The environment a command needs, created after a yes on a terminal when
/// absent. On a pipe, or on a no, throws the CLI's user error naming
/// `apogee train setup`. `purpose` names the command asking.
[[nodiscard]] training::PythonEnv require_python_env(const harness::Config& config,
                                                     std::string_view purpose);

/// What `train setup` does, shared with the prompt above.
struct SetupRequest {
    /// `auto`, `mlx`, `peft`, or empty for no trainer stack.
    std::string trainer;
    std::vector<training::RequirementSet> with;
};

/// Runs a setup, printing each step. Throws the CLI's user error on failure.
void run_train_setup(const harness::Config& config, const SetupRequest& request);

}  // namespace apogee::commands
