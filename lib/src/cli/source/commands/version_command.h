#pragma once

#include <string_view>

#include "commands/command.h"

namespace apogee::commands {

/// `apogee version` -- prints the same line as `apogee --version`.
///
/// The skeleton's one real subcommand: it exists so the registration path is
/// exercised end to end by a command that actually does something, rather than
/// proven by a fixture that only tests wire up.
class VersionCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
