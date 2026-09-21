#include "commands/version_command.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <string>

#include "version/version.h"

namespace apogee::commands {

std::string_view VersionCommand::name() const noexcept {
    return "version";
}

std::string_view VersionCommand::summary() const noexcept {
    return "Print version, build, and target information";
}

void VersionCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->callback([&context]() {
        std::cout << version::full() << "\n";
        if (!context.config_path.empty()) {
            std::cout << "config: " << context.config_path << "\n";
        }
    });
}

}  // namespace apogee::commands
