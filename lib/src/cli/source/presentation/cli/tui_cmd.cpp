#include "cli/tui_cmd.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cli/helpers.h"
#include "cli/version_command.h"
#include "platform/platform.h"
#include "tui/pump.h"
#include "tui/shell.h"
#include "tui/theme.h"
#include "tui/view.h"
#include "version/version.h"

namespace apogee::commands {

namespace {

[[nodiscard]] std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in{text};
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] int run_full_screen(const RootContext& context) {
    tui::Shell shell{tui::ShellOptions{.title = "apogee " + std::string{version::semantic()},
                                       .theme = tui::detect_theme()}};
    add_shell_views(shell, context);
    tui::TerminalPump pump;
    return pump.run(shell);
}

}  // namespace

ShellEntry default_shell_entry() {
    return ShellEntry{.interactive =
                          []() {
                              return platform::is_terminal(platform::StandardStream::In) &&
                                     platform::is_terminal(platform::StandardStream::Out);
                          },
                      .refusal = []() { return platform::full_screen_refusal(); },
                      .run = [](const RootContext& context) { return run_full_screen(context); }};
}

void add_shell_views(tui::Shell& shell, const RootContext& context) {
    std::vector<std::string> home = lines_of(version_report(context));
    home.emplace_back();
    home.emplace_back("Every command runs as it always has -- `apogee <command>` at a prompt, and");
    home.emplace_back("`apogee --help` lists them. Tab moves between the views; q quits.");
    shell.add(tui::text_view("Home", std::move(home)));
    shell.add(tui::text_view("Keys", tui::Shell::key_lines()));
}

int open_bare(const ShellEntry& entry, const RootContext& context, CLI::App& app) {
    if (entry.interactive && entry.interactive()) {
        const std::string refused = entry.refusal ? entry.refusal() : std::string{};
        if (refused.empty()) {
            return entry.run(context);
        }
        std::cerr << "apogee: no full screen here -- " << refused << "\n";
    }
    std::cout << app.help();
    return 0;
}

TuiCommand::TuiCommand(ShellEntry entry) : entry_{std::move(entry)} {}

std::string_view TuiCommand::name() const noexcept {
    return "tui";
}

std::string_view TuiCommand::summary() const noexcept {
    return "Open the full-screen shell -- what a bare `apogee` opens at a terminal";
}

void TuiCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->callback([this, &context]() {
        if (!entry_.interactive || !entry_.interactive()) {
            std::cerr << "apogee tui: the shell needs an interactive terminal -- stdin and stdout "
                         "both one; every command runs without it\n";
            throw CLI::RuntimeError(kUserError);
        }
        if (const std::string refused = entry_.refusal ? entry_.refusal() : std::string{};
            !refused.empty()) {
            std::cerr << "apogee tui: no full screen here -- " << refused << "\n";
            throw CLI::RuntimeError(kUserError);
        }
        if (const int code = entry_.run(context); code != 0) {
            throw CLI::RuntimeError(code);
        }
    });
}

}  // namespace apogee::commands
