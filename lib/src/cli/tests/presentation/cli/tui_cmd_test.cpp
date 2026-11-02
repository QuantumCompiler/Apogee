#include "cli/tui_cmd.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cli/chat_session.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "support/cli_home.h"
#include "tui/shell.h"

/// The shell's doors (32b) behind a fake entry: a bare `apogee` opens the
/// shell only at an interactive terminal that can host it, and prints the
/// help, untouched, everywhere else; `apogee tui` says why when it cannot.
namespace {

using apogee::commands::ShellEntry;

struct Doors {
    bool interactive = true;
    std::string refusal;
    int opened = 0;
    int code = 0;
    /// What the shell was last opened with (44c).
    apogee::commands::ShellLaunch launch;

    [[nodiscard]] ShellEntry entry() {
        return ShellEntry{.interactive = [this]() { return interactive; },
                          .refusal = [this]() { return refusal; },
                          .run =
                              [this](const apogee::commands::RootContext& /*context*/,
                                     const apogee::commands::ShellLaunch& given) {
                                  ++opened;
                                  launch = given;
                                  return code;
                              }};
    }
};

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

[[nodiscard]] Run run(Doors& doors, const std::vector<std::string>& args) {
    apogee::commands::CommandRegistry registry = apogee::commands::default_registry(doors.entry());
    apogee::commands::RootCommand root{std::move(registry), doors.entry()};
    std::vector<const char*> argv{"apogee"};
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    const std::ostringstream out;
    const std::ostringstream err;
    std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(err.rdbuf());
    Run result;
    result.code = root.run(static_cast<int>(argv.size()), argv.data());
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

[[nodiscard]] std::string root_help() {
    const apogee::commands::RootCommand root;
    return root.app().help();
}

}  // namespace

TEST_CASE("a bare apogee off an interactive terminal prints the help, exactly, and opens nothing",
          "[cli][tui]") {
    Doors doors;
    doors.interactive = false;
    const Run bare = run(doors, {});
    CHECK(bare.code == 0);
    CHECK(doors.opened == 0);
    CHECK(bare.out == root_help());
    CHECK(bare.err.empty());
}

TEST_CASE("a bare apogee at an interactive terminal opens the shell, and nothing else is said",
          "[cli][tui]") {
    Doors doors;
    doors.code = 0;
    const Run bare = run(doors, {});
    CHECK(bare.code == 0);
    CHECK(doors.opened == 1);
    CHECK(bare.out.empty());
    CHECK(bare.err.empty());
}

TEST_CASE("a terminal that cannot host the screen gets its reason, then the help", "[cli][tui]") {
    Doors doors;
    doors.refusal = "this console does not take virtual-terminal sequences";
    const Run bare = run(doors, {});
    CHECK(bare.code == 0);
    CHECK(doors.opened == 0);
    CHECK(bare.err ==
          "apogee: no full screen here -- this console does not take virtual-terminal "
          "sequences\n");
    CHECK(bare.out == root_help());
}

TEST_CASE("every subcommand is untouched by the shell's door", "[cli][tui]") {
    // Interactive and able: a named command still runs itself, never the shell.
    Doors doors;
    const Run version = run(doors, {"version"});
    CHECK(version.code == 0);
    CHECK(doors.opened == 0);
    CHECK(version.out.starts_with("apogee "));
    const Run help = run(doors, {"--help"});
    CHECK(help.code == 0);
    CHECK(doors.opened == 0);
    CHECK(help.out == root_help());
}

TEST_CASE("apogee tui opens the shell, or says why it cannot", "[cli][tui]") {
    Doors doors;
    CHECK(run(doors, {"tui"}).code == 0);
    CHECK(doors.opened == 1);

    doors.interactive = false;
    const Run piped = run(doors, {"tui"});
    CHECK(piped.code == 1);
    CHECK(doors.opened == 1);
    CHECK(piped.err.find("needs an interactive terminal") != std::string::npos);

    doors.interactive = true;
    doors.refusal = "this terminal (TERM=dumb) cannot place the cursor";
    const Run dumb = run(doors, {"tui"});
    CHECK(dumb.code == 1);
    CHECK(doors.opened == 1);
    CHECK(dumb.err ==
          "apogee tui: no full screen here -- this terminal (TERM=dumb) cannot place "
          "the cursor\n");
}

TEST_CASE("the shell's first views render the version report and the keys", "[cli][tui]") {
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::commands::add_shell_views(shell, {});
    REQUIRE(shell.size() == 2);
    const std::string home = shell.render_text(90, 14);
    CHECK(home.find("[1 Home] 2 Keys") != std::string::npos);
    CHECK(home.find(" apogee ") != std::string::npos);
    CHECK(home.find("\n channel: ") != std::string::npos);
    CHECK(home.find("\n root: ") != std::string::npos);
    (void)shell.press(apogee::tui::Key::function(2));
    CHECK(shell.render_text(90, 16).find("a view of this category by its number") !=
          std::string::npos);
}

TEST_CASE("tui completes, and its help says what it opens", "[cli][tui][completion]") {
    const apogee::testing::CliHome home{"backends: {}\n"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"__complete", "tu"}, &out, &err) == 0);
    CHECK(out.find("tui") != std::string::npos);
    out.clear();
    REQUIRE(home.run({"tui", "--help"}, &out, &err) == 0);
    CHECK(out.find("full-screen shell") != std::string::npos);
}

// --- the session table on apogee tui (44c) ------------------------------------

namespace {

/// Every spelling of every option `name` takes, help aside -- the ones a
/// person is shown, or with `hidden` the plumbing no help or completion shows
/// (`group("")`).
[[nodiscard]] std::set<std::string> option_spellings(const CLI::App& root, const std::string& name,
                                                     bool hidden = false) {
    std::set<std::string> out;
    const CLI::App* sub = root.get_subcommand(name);
    for (const CLI::Option* option : sub->get_options()) {
        if (option == sub->get_help_ptr() || option->get_group().empty() != hidden) {
            continue;
        }
        for (const std::string& spelling : option->get_lnames()) {
            out.insert("--" + spelling);
        }
        for (const std::string& spelling : option->get_snames()) {
            out.insert("-" + spelling);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("apogee tui takes the session table: every flag execute takes, nothing of its own",
          "[cli][tui][launch]") {
    // One table (ADR 0011): bound by the call the modes make, so a flag
    // added there is tui's with no change here.
    const apogee::commands::RootCommand root;
    const std::set<std::string> tui = option_spellings(root.app(), "tui");
    CHECK(tui == option_spellings(root.app(), "execute"));
    CHECK(tui.contains("--suite"));
    CHECK(tui.contains("--attach"));
    CHECK(tui.contains("--effort"));
    CHECK(tui.contains("--orchestrate"));
    CHECK(std::ranges::includes(tui, option_spellings(root.app(), "chat")));
    // Execute's one piece of plumbing, `apogee acp`'s handover of a host's MCP
    // servers (56b), is execute's alone: the shell's door never takes it.
    CHECK(option_spellings(root.app(), "execute", true) ==
          std::set<std::string>{"--acp-host-servers"});
    CHECK(option_spellings(root.app(), "tui", true).empty());
}

TEST_CASE("apogee tui's flags open the shell inside that session, the mode the flags' own",
          "[cli][tui][launch]") {
    Doors doors;
    // A suite: execute's opening, every flag carried as parsed.
    REQUIRE(run(doors, {"tui", "--suite", "duo", "--attach", ".", "--effort", "high"}).code == 0);
    REQUIRE(doors.launch.flags != nullptr);
    CHECK(doors.launch.mode == apogee::commands::SessionMode::Execute);
    CHECK(doors.launch.flags->suite == "duo");
    CHECK(doors.launch.flags->attach == std::vector<std::string>{"."});
    CHECK(doors.launch.flags->effort == "high");
    // A model alone: chat's.
    REQUIRE(run(doors, {"tui", "-m", "mocky"}).code == 0);
    REQUIRE(doors.launch.flags != nullptr);
    CHECK(doors.launch.mode == apogee::commands::SessionMode::Chat);
    CHECK(doors.launch.flags->model == "mocky");
    // Orchestrating is execute's; a suite turned off is chat's.
    REQUIRE(run(doors, {"tui", "--orchestrate"}).code == 0);
    CHECK(doors.launch.mode == apogee::commands::SessionMode::Execute);
    REQUIRE(run(doors, {"tui", "--suite", "off"}).code == 0);
    CHECK(doors.launch.mode == apogee::commands::SessionMode::Chat);
    // No flags: the picker, as a bare apogee opens it.
    REQUIRE(run(doors, {"tui"}).code == 0);
    CHECK(doors.launch.flags == nullptr);
    REQUIRE(run(doors, {}).code == 0);
    CHECK(doors.launch.flags == nullptr);
    CHECK(doors.opened == 6);
}

TEST_CASE("apogee tui refuses what the modes refuse in their words, and machine mode's formats",
          "[cli][tui][launch]") {
    Doors doors;
    // A combination the table refuses: the same words as execute's, before
    // any screen.
    const Run tui = run(doors, {"tui", "--effort", "high", "--think-budget", "100"});
    const Run execute = run(doors, {"execute", "--effort", "high", "--think-budget", "100"});
    CHECK(tui.code != 0);
    CHECK(tui.code == execute.code);
    CHECK(tui.err == execute.err);
    CHECK(doors.opened == 0);
    // The shell draws its own screen: machine mode's formats are a pipe's.
    const Run json = run(doors, {"tui", "--output-format", "stream-json"});
    CHECK(json.code == 1);
    CHECK(json.err.find("--output-format and --input-format are machine mode's") !=
          std::string::npos);
    CHECK(doors.opened == 0);
    // Off a terminal, flags or none, the measured refusal.
    doors.interactive = false;
    const Run piped = run(doors, {"tui", "--suite", "duo"});
    CHECK(piped.code == 1);
    CHECK(piped.err.find("needs an interactive terminal") != std::string::npos);
    CHECK(doors.opened == 0);
}

TEST_CASE("apogee tui's session flags complete as the modes' do",
          "[cli][tui][launch][completion]") {
    const apogee::testing::CliHome home{
        "backends:\n  local:\n    type: mock\n    model_path: s.json\nsuites:\n  duo:\n"
        "    members:\n      chat: local\n"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"__complete", "tui", "--sui"}, &out, &err) == 0);
    CHECK(out.find("--suite") != std::string::npos);
    out.clear();
    REQUIRE(home.run({"__complete", "tui", "--suite", ""}, &out, &err) == 0);
    std::string execute;
    REQUIRE(home.run({"__complete", "execute", "--suite", ""}, &execute, &err) == 0);
    CHECK(out == execute);
    CHECK(out.find("duo") != std::string::npos);
}
