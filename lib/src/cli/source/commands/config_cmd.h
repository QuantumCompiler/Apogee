#pragma once

#include <string_view>

#include "commands/command.h"

namespace apogee::commands {

/// `apogee config` -- inspect and edit the config file.
///
/// Every mutating subcommand routes through harness/config_edit.h rather than
/// writing YAML itself. That is the Core constraint the config engine exists
/// to establish: one mutation path, shared by every surface, so an edit made
/// over HTTP later is byte-identical to the same edit made here. A command
/// that formatted its own YAML would be the first crack in it.
class ConfigCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
