#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "harness/config.h"

namespace apogee::commands {

/// Every dotted key `config get` answers for `config`: the fixed ones, and
/// each backend's, MCP server's and collection's fields by name -- what
/// completion offers for `config get <TAB>`. Beside `lookup`, which it must
/// agree with; a test asks `config get` for every key listed here.
[[nodiscard]] std::vector<std::string> config_keys(const harness::Config& config);

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
