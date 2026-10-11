#include "cli/tui_parity.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "cli/registry.h"
#include "cli/root.h"
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

TEST_CASE("the table is today's truth: the views, the backfills, the exec line's refusals",
          "[cli][tui][parity]") {
    const auto kind = [](std::string_view command) {
        const apogee::commands::ShellSurface* surface =
            apogee::commands::find_shell_surface(command);
        REQUIRE(surface != nullptr);
        return surface->kind;
    };
    for (const char* view :
         {"chat", "chats", "models", "config", "version", "tui", "system", "check", "providers"}) {
        INFO(view);
        CHECK(kind(view) == ShellSurfaceKind::View);
    }
    for (const char* pending : {"knowledge", "embed", "graph", "execute", "symphonies", "task",
                                "train", "datasets", "agents", "mcp", "auth"}) {
        INFO(pending);
        CHECK(kind(pending) == ShellSurfaceKind::Backfill);
        CHECK(apogee::commands::find_shell_surface(pending)->detail.starts_with("37"));
    }
    CHECK(kind("complete") == ShellSurfaceKind::RunnerCovered);
    CHECK(kind("analyze") == ShellSurfaceKind::RunnerCovered);
    for (const char* carved : {"serve", "uninstall", "reset"}) {
        INFO(carved);
        CHECK(kind(carved) == ShellSurfaceKind::CarvedOut);
    }
    CHECK(apogee::commands::find_shell_surface("nonsense") == nullptr);

    // The exec line's refusals: the doors, the `$EDITOR` verbs, the three.
    std::vector<std::string_view> refused;
    for (const apogee::commands::ShellRefusal& refusal : apogee::commands::shell_refusals()) {
        refused.push_back(refusal.words);
        CHECK_FALSE(refusal.reason.empty());
    }
    CHECK(refused == std::vector<std::string_view>{"chat", "execute", "tui", "agents edit",
                                                   "symphonies edit", "reset", "uninstall",
                                                   "serve"});
}
