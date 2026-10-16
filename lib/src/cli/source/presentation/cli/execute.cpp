#include "cli/execute.h"

#include <CLI/CLI.hpp>

#include <memory>
#include <string>
#include <utility>

#include "cli/chat_session.h"
#include "harness/roles.h"

namespace apogee::commands {

ExecuteCommand::ExecuteCommand(MachineBudgetSource machine) : machine_{std::move(machine)} {}

std::string_view ExecuteCommand::name() const noexcept {
    return "execute";
}

std::string_view ExecuteCommand::summary() const noexcept {
    return "Open a session under a suite, its symphonies playable (/play), or bare on one model";
}

void open_execute_session(const RootContext& context, const SessionFlags& flags,
                          const MachineBudgetSource& machine, SessionOutput* front) {
    run_session(context, flags, machine, SessionMode::Execute, front);
}

std::vector<ExecuteDoor> execute_doors(const harness::Config& config) {
    std::vector<ExecuteDoor> doors;
    const std::string& fallback = config.models.default_suite;
    for (const std::string& name : config.suite_names()) {
        ExecuteDoor door{.label = "Execute under suite " + name, .suite = name};
        if (name == fallback) {
            door.label += " (default)";
            doors.insert(doors.begin(), std::move(door));
        } else {
            doors.push_back(std::move(door));
        }
    }
    // The bare door: what `apogee execute` with nothing named opens on --
    // never with a default suite, which it opens under instead.
    if (fallback.empty()) {
        if (const std::string bare = harness::resolve_chat_backend(config, {}); !bare.empty()) {
            doors.push_back(ExecuteDoor{.label = "Execute on " + bare + " (no suite)"});
        } else if (doors.empty()) {
            doors.push_back(unconfigured_execute_door());
        }
    }
    return doors;
}

ExecuteDoor unconfigured_execute_door() {
    return ExecuteDoor{.label = "Execute (no suite or default backend yet)"};
}

void ExecuteCommand::bind(CLI::App& root, const RootContext& context) {
    // The session core, as execute (27s): chat's flags, chat's session --
    // opened with a suite, its symphonies playable, or since 41a bare on one
    // model as chat runs.
    auto flags = std::make_shared<SessionFlags>();
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    bind_session_flags(*cmd, flags, SessionMode::Execute);
    cmd->callback([&context, flags, machine = machine_]() {
        open_execute_session(context, *flags, machine);
    });
}

}  // namespace apogee::commands
