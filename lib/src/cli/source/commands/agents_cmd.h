#pragma once

#include <string_view>

#include "commands/command.h"

/// `apogee agents create|list|edit|delete` -- first-class management of the
/// workflows `apogee analyze --agent` runs. `create` goes through the shared
/// scaffold core (`scaffold/agent.h`), the same function the admin plane's
/// `POST /v1/admin/agents` calls, so an agent made here is byte-identical to
/// one made over HTTP; every config write is the one config mutation path.
namespace apogee::commands {

class AgentsCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
