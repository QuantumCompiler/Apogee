#include "cli/tui_parity.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cli/registry.h"
#include "cli/root.h"
#include "cli/tui_runner.h"
#include "support/fake_command.h"

/// The shell is a mode (ADR 0010, 37a): every root subcommand the binary
/// registers has its place in the full-screen shell classified -- drawn by a
/// view, backfilled by a named item, covered by the exec line, or carved out
/// with the reason the exec line refuses it by. Adding a subcommand without
/// placing it fails here, which is the only way the table stays true.
namespace {

using apogee::commands::ShellSurfaceKind;

[[nodiscard]] std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        out += line + "\n";
    }
    return out;
}

}  // namespace

TEST_CASE("every root subcommand has its place in the shell classified", "[cli][tui][parity]") {
    const apogee::commands::RootCommand root{apogee::commands::default_registry()};
    REQUIRE(root.app().get_subcommands({}).size() >= 20);  // the real set, not a stub
    const std::vector<std::string> violations = apogee::commands::shell_law_violations(root.app());
    INFO(joined(violations));
    CHECK(violations.empty());
}

TEST_CASE("an unclassified subcommand breaks the law, named; an empty registry never passes",
          "[cli][tui][parity]") {
    // The planted violation: a command no row places.
    apogee::commands::CommandRegistry registry = apogee::commands::default_registry();
    registry.add(std::make_unique<apogee::testing::FakeCommand>("frobnicate", "A planted one"));
    const apogee::commands::RootCommand planted{std::move(registry)};
    const std::vector<std::string> found = apogee::commands::shell_law_violations(planted.app());
    REQUIRE(found.size() == 1);
    CHECK(found.front().starts_with("'frobnicate' has no shell classification"));

    // Nothing registered: a violation, never a vacuous pass -- and every row
    // then names a command that does not exist.
    const apogee::commands::RootCommand empty{apogee::commands::CommandRegistry{}};
    const std::vector<std::string> none = apogee::commands::shell_law_violations(empty.app());
    REQUIRE_FALSE(none.empty());
    CHECK(none.front() == "no subcommands registered -- the law would pass vacuously");
}

TEST_CASE("the table is today's truth: the views, no backfill left, the exec line's refusals",
          "[cli][tui][parity]") {
    const auto kind = [](std::string_view command) {
        const apogee::commands::ShellSurface* surface =
            apogee::commands::find_shell_surface(command);
        REQUIRE(surface != nullptr);
        return surface->kind;
    };
    for (const char* view :
         {"chat",  "chats",     "models",    "config", "version", "tui",     "system",
          "check", "providers", "knowledge", "embed",  "graph",   "execute", "symphonies",
          "task",  "train",     "datasets",  "agents", "mcp",     "auth"}) {
        INFO(view);
        CHECK(kind(view) == ShellSurfaceKind::View);
    }
    // Track 37 drew every backfill (37g the last): none is left pending.
    for (const apogee::commands::ShellSurface& surface : apogee::commands::shell_surfaces()) {
        INFO(surface.command);
        CHECK(surface.kind != ShellSurfaceKind::Backfill);
    }
    // `mcp serve` (54a) is a verb of the MCP view's command, run by the exec
    // line as typed; its hidden alias stays plumbing, named for what it is.
    CHECK(apogee::commands::exec_refusal({"mcp", "serve"}).empty());
    CHECK(kind("__mcp-tools") == ShellSurfaceKind::RunnerCovered);
    CHECK(std::string_view{apogee::commands::find_shell_surface("__mcp-tools")->detail}.find(
              "alias of mcp serve") != std::string_view::npos);
    CHECK(kind("complete") == ShellSurfaceKind::RunnerCovered);
    CHECK(kind("analyze") == ShellSurfaceKind::RunnerCovered);
    for (const char* carved : {"serve", "uninstall", "reset", "acp"}) {
        INFO(carved);
        CHECK(kind(carved) == ShellSurfaceKind::CarvedOut);
    }
    // `acp` (56a) is an agent host's stdio protocol: refused by word prefix,
    // so a verb under it is refused with it.
    CHECK(apogee::commands::exec_refusal({"acp"}).starts_with(
        "refused: 'acp' -- speaks the Agent Client Protocol to the host that started it"));
    CHECK_FALSE(apogee::commands::exec_refusal({"acp", "registration"}).empty());
    CHECK(apogee::commands::find_shell_surface("nonsense") == nullptr);

    // The exec line's refusals: the doors, the `$EDITOR` verbs, the three.
    std::vector<std::string_view> refused;
    for (const apogee::commands::ShellRefusal& refusal : apogee::commands::shell_refusals()) {
        refused.push_back(refusal.words);
        CHECK_FALSE(refusal.reason.empty());
    }
    CHECK(refused == std::vector<std::string_view>{"chat", "execute", "tui", "agents edit",
                                                   "symphonies edit", "reset", "uninstall", "serve",
                                                   "acp"});
}

TEST_CASE("every view names its door; a door missing, unsaid or misplaced is named",
          "[cli][tui][parity][door]") {
    // ADR 0012 (44d): a view is a promise a fresh install keeps, so each
    // names the view it is reached by -- `cli.tui_doors` walks them all.
    std::vector<std::string> doorless;
    for (const apogee::commands::ShellSurface& surface : apogee::commands::shell_surfaces()) {
        if (surface.kind == ShellSurfaceKind::View && surface.door.view.empty()) {
            doorless.emplace_back(surface.command);
        }
    }
    CHECK(doorless.empty());
    const apogee::commands::ShellSurface* execute = apogee::commands::find_shell_surface("execute");
    REQUIRE(execute != nullptr);
    CHECK(execute->door.view == "Session");
    CHECK(execute->door.seen == "Execute");
    CHECK(execute->door.condition.empty());  // unconditional since 44b

    const apogee::commands::RootCommand root{apogee::commands::default_registry()};
    std::vector<apogee::commands::ShellSurface> planted(apogee::commands::shell_surfaces().begin(),
                                                        apogee::commands::shell_surfaces().end());
    const auto row = [&planted](std::string_view command) -> apogee::commands::ShellSurface& {
        return *std::ranges::find(planted, command, &apogee::commands::ShellSurface::command);
    };
    // The regression that motivated it: the execute door gone from the row.
    row("execute").door = {};
    std::vector<std::string> found = apogee::commands::shell_law_violations(root.app(), planted);
    REQUIRE(found.size() == 1);
    CHECK(found.front().starts_with("'execute' is classified as a view with no door"));
    // A condition with nothing said where the door is absent.
    row("execute").door = {.view = "Session", .condition = "a suite or a default backend"};
    found = apogee::commands::shell_law_violations(root.app(), planted);
    REQUIRE(found.size() == 1);
    CHECK(found.front() ==
          "'execute''s door is conditional on a suite or a default backend, with nothing said "
          "where it is absent");
    // A door on a row that is no view.
    row("execute").door = {.view = "Session", .seen = "Execute"};
    row("serve").door = {.view = "Session"};
    found = apogee::commands::shell_law_violations(root.app(), planted);
    REQUIRE(found.size() == 1);
    CHECK(found.front() == "'serve' names a door, and is no view");
}
