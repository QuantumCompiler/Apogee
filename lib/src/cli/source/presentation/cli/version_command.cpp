#include "cli/version_command.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <string>

#include "contracts/paths.h"
#include "version/version.h"

namespace apogee::commands {

std::string version_report(const RootContext& context) {
    std::string out{version::full()};
    const harness::RootResolution root = harness::current_root();
    out += "\nchannel: ";
    out += harness::channel_name(root.channel);
    out += "\nroot: ";
    out += root.ok() ? root.root.string() + " (" + harness::root_reason(root) + ")"
                     : "unresolved -- " + root.error;
    if (!context.config_path.empty()) {
        out += "\nconfig: " + context.config_path;
    } else if (root.ok() && root.flag.has_value() && !root.flag->channel.has_value()) {
        out += "\nconfig: " + root.config.string();
    }
    return out;
}

std::string_view VersionCommand::name() const noexcept {
    return "version";
}

std::string_view VersionCommand::summary() const noexcept {
    return "Print version, build, and target information";
}

void VersionCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->callback([&context]() { std::cout << version_report(context) << "\n"; });
}

}  // namespace apogee::commands
