#include "cli/tui_cmd.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

    [[nodiscard]] ShellEntry entry() {
        return ShellEntry{.interactive = [this]() { return interactive; },
                          .refusal = [this]() { return refusal; },
                          .run =
                              [this](const apogee::commands::RootContext& /*context*/) {
                                  ++opened;
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
    CHECK(shell.render_text(90, 14).find("show a view by its number") != std::string::npos);
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
