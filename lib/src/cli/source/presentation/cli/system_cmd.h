#pragma once

#include <string_view>

#include "cli/command.h"
#include "cli/suite_residency.h"
#include "operations/system_view.h"
#include "platform/system_info.h"

/// `apogee system` -- the machine in one honest snapshot (32a): CPU (model,
/// cores, load, utilization sampled over a named window), memory (the
/// system's, this process's, and the models it holds), the GPU's story where
/// the platform has one, and the model store's disk. One reading per run --
/// no watch loop, no daemon, no child, no model loaded, no config read.
///
/// The memory a model may offload to is the number 26a's window sizing and
/// 27e's suite admission read, taken from the same function they default to
/// (`machine_budget`): what this command shows is what sizing uses, by
/// construction rather than by a second estimate.
namespace apogee::commands {

/// What the command reads, each replaceable in a test.
struct SystemSeams {
    /// The devices' memory for models: `machine_budget`, the one function
    /// chat, execute, task and the suite admission all default to.
    MachineBudgetSource machine = machine_budget;
    /// The OS.
    const platform::SystemSource* source = &platform::host_system();
    /// How the utilization window is waited out; null sleeps.
    platform::Wait wait;
};

/// The view one run of `apogee system` shows: the snapshot, the budget read
/// once, the store under the layout's `models/`.
[[nodiscard]] operations::SystemView read_system_view(const SystemSeams& seams);

class SystemCommand final : public Command {
public:
    explicit SystemCommand(SystemSeams seams = {});

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;

private:
    SystemSeams seams_;
};

}  // namespace apogee::commands
