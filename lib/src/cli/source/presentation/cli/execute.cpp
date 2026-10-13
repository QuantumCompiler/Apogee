#include "cli/execute.h"

#include <CLI/CLI.hpp>

#include <memory>
#include <string>
#include <utility>

#include "cli/chat_session.h"

namespace apogee::commands {

ExecuteCommand::ExecuteCommand(MachineBudgetSource machine) : machine_{std::move(machine)} {}

std::string_view ExecuteCommand::name() const noexcept {
    return "execute";
}

std::string_view ExecuteCommand::summary() const noexcept {
    return "Open a session under a suite, its symphonies playable (/play), or bare on one model";
}

void ExecuteCommand::bind(CLI::App& root, const RootContext& context) {
    // The session core, as execute (27s): chat's flags, chat's session --
    // opened with a suite, its symphonies playable, or since 41a bare on one
    // model as chat runs.
    auto flags = std::make_shared<SessionFlags>();
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    bind_session_flags(*cmd, flags, SessionMode::Execute);
    cmd->callback([&context, flags, machine = machine_]() {
        run_session(context, *flags, machine, SessionMode::Execute);
    });
}

}  // namespace apogee::commands
