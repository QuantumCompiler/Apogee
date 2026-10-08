#pragma once

#include <string_view>

#include "cli/command.h"

/// `apogee __machine-schema` -- machine mode's protocol as a JSON Schema
/// (28g), printed from the one declaration (`machine/protocol.h`), so the
/// artifact can never describe a build other than the one that printed it.
///
/// A protocol, not a feature: hidden from `--help` like `__complete` and
/// `__mcp-tools`, stable in behaviour, and stdout carries the schema and
/// nothing else. The release archives carry the same bytes as
/// `machine-schema.json`; this command is the truth they copy.
namespace apogee::commands {

class MachineSchemaCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
