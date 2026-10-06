#include "cli/chat.h"

#include <CLI/CLI.hpp>

#include <memory>
#include <string>
#include <utility>

#include "cli/chat_session.h"

namespace apogee::commands {

ChatCommand::ChatCommand(MachineBudgetSource machine) : machine_{std::move(machine)} {}

std::string_view ChatCommand::name() const noexcept {
    return "chat";
}

std::string_view ChatCommand::summary() const noexcept {
    return "Start or resume an interactive conversation";
}

void ChatCommand::bind(CLI::App& root, const RootContext& context) {
    // The session core, as chat: the first consumer of its own
    // generalization (27s).
    auto flags = std::make_shared<SessionFlags>();
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    bind_session_flags(*cmd, flags, SessionMode::Chat);
    cmd->callback([&context, flags, machine = machine_]() {
        run_session(context, *flags, machine, SessionMode::Chat);
    });
}

}  // namespace apogee::commands
