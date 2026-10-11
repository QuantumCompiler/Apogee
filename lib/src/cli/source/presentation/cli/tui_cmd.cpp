#include "cli/tui_cmd.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cli/helpers.h"
#include "cli/suite_residency.h"
#include "cli/tui_doctor.h"
#include "cli/tui_knowledge.h"
#include "cli/tui_session.h"
#include "cli/tui_workbench.h"
#include "cli/version_command.h"
#include "platform/platform.h"
#include "platform/system_info.h"
#include "tui/list_view.h"
#include "tui/monitor_bar.h"
#include "tui/pump.h"
#include "tui/session_view.h"
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
    const tui::Theme theme = tui::detect_theme();
    tui::Shell shell{
        tui::ShellOptions{.title = "apogee " + std::string{version::semantic()}, .theme = theme}};
    tui::TerminalPump pump;

    // The machine on the bottom bar (32e): 32a's probe, on the pump's tick,
    // the models the conversation holds beside it.
    struct Held {
        std::mutex mutex;
        std::vector<std::string> backends;
    };

    const auto held = std::make_shared<Held>();
    tui::MonitorBar monitor{pump, theme, platform::host_system(),
                            tui::MonitorOptions{.held = [held]() {
                                const std::lock_guard lock{held->mutex};
                                return held->backends;
                            }}};
    shell.set_bottom_bar(monitor.view());
    // The conversation first: where a bare `apogee` lands (32c).
    tui::SessionView session{pump, theme};
    const std::size_t session_view = shell.add(session.view());
    TuiSessionDriver driver{session, pump, context, machine_budget,
                            [held](std::vector<std::string> backends) {
                                const std::lock_guard lock{held->mutex};
                                held->backends = std::move(backends);
                            }};
    // The workbench beside it (32d): reads drawn, cores called.
    const WorkbenchHooks hooks{
        .open_chat = [&driver](const std::string& chat_id) { return driver.open_chat(chat_id); },
        .use_suite = [&driver](const std::string& suite) { return driver.use_suite(suite); },
        .use_model = [&driver](const std::string& model) { return driver.use_model(model); },
        .show_session = [&shell, session_view]() { shell.activate(session_view); }};
    const std::vector<std::unique_ptr<tui::ListView>> workbench =
        make_workbench(pump, theme, context, hooks);
    for (const std::unique_ptr<tui::ListView>& view : workbench) {
        (void)shell.add(view->view());
    }
    // The doctor beside it (37b): the report, the providers, the machine.
    const std::vector<std::unique_ptr<tui::ListView>> doctor =
        make_doctor_views(pump, theme, context);
    for (const std::unique_ptr<tui::ListView>& view : doctor) {
        (void)shell.add(view->view());
    }
    // The knowledge beside them (37c): records, collections, graphs.
    const std::vector<std::unique_ptr<tui::ListView>> knowledge =
        make_knowledge_views(pump, theme, context);
    for (const std::unique_ptr<tui::ListView>& view : knowledge) {
        (void)shell.add(view->view());
    }
    add_shell_views(shell, context);
    driver.start();
    monitor.start();
    const int code = pump.run(shell);
    driver.stop();
    return code;
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
    std::vector<std::string> keys = tui::Shell::key_lines();
    keys.emplace_back();
    keys.emplace_back("In the session:");
    for (const std::string& line : tui::SessionView::key_lines()) {
        keys.push_back(line);
    }
    shell.add(tui::text_view("Keys", std::move(keys)));
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
