#pragma once

#include <string>
#include <string_view>

#include "cli/command.h"

namespace apogee::commands {

/// What `apogee version` and `apogee --version` print: the version line --
/// first, alone on it, as the release scripts read it -- then which channel
/// this build is, the data directory the run resolved and the rung of the
/// chain that chose it (M10), and the config file when a flag named one. "Which
/// Apogee am I talking to" is never archaeology.
[[nodiscard]] std::string version_report(const RootContext& context);

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
