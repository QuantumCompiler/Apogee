#include "cli/machine_schema_cmd.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

#include "machine/protocol.h"

namespace apogee::commands {

std::string_view MachineSchemaCommand::name() const noexcept {
    return "__machine-schema";
}

std::string_view MachineSchemaCommand::summary() const noexcept {
    return "Print machine mode's protocol as a JSON Schema";
}

void MachineSchemaCommand::bind(CLI::App& root, const RootContext& /*context*/) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->group("");  // hidden from --help: it is a protocol, not a feature
    cmd->callback([]() { std::cout << machine_schema().dump(2) << "\n"; });
}

}  // namespace apogee::commands
