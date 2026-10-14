#pragma once

#include <CLI/CLI.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The shell is a mode (ADR 0010, `adrs/cli/the-shell-is-a-mode.md`; 37a): every root
/// subcommand's place in the full-screen shell, classified once, here, so a
/// subcommand shipped without its shell surface classified fails a test
/// (`tui_parity_test`) instead of drifting -- the HTTP parity table's idiom
/// (`parity_test.cpp`) at the shell's unit, the root subcommand. Per-verb
/// coverage lives in each view's own parity tests.
///
/// It is also the exec line's one source of refusals (37h): a command the
/// shell will not run names the reason recorded here, never a second list.
namespace apogee::commands {

/// How a root subcommand reaches the shell.
enum class ShellSurfaceKind : std::uint8_t {
    /// A view draws it: `detail` names the view.
    View,
    /// A pending item will draw it: `detail` names the item.
    Backfill,
    /// No curated view: the exec line (37h) is its surface, every verb and
    /// flag running there as in a script. `detail` says why no view.
    RunnerCovered,
    /// No surface at all: the exec line refuses it, for the reason
    /// `shell_refusals` records. `detail` says what it is.
    CarvedOut,
};

/// How a `View` row's door is reached on a fresh install (ADR 0012, 44d):
/// its view, by the title the shell's keys reach it by -- the Go-to menu, or
/// its number -- and what that view shows for this door. `cli.tui_doors`
/// walks every one on a real terminal, reading this table.
struct ShellDoor {
    /// The view's title, as the tab strip and the Go-to menu name it.
    std::string_view view;
    /// What must be on that view for this door, beyond its title: a
    /// picker's row (`New chat`, `Execute`). Empty: the view is the door.
    std::string_view seen;
    /// Where the door is deliberately conditional, the condition -- `seen`
    /// is then the words the shell says it in where the door is absent.
    /// Empty: the door stands on what a fresh install has.
    std::string_view condition;
};

/// One root subcommand's classification.
struct ShellSurface {
    std::string_view command;
    ShellSurfaceKind kind;
    std::string_view detail;
    /// A view's door (44d); empty for every other class.
    ShellDoor door{};
};

/// Every root subcommand, classified -- today's truth, flipped by each item
/// that draws a backfilled row.
[[nodiscard]] std::span<const ShellSurface> shell_surfaces() noexcept;

/// The row for `command` (a root subcommand's name), or null.
[[nodiscard]] const ShellSurface* find_shell_surface(std::string_view command) noexcept;

/// A command the exec line refuses, by the words it starts with -- a root
/// subcommand, or a subcommand and its verb -- and the reason it says.
struct ShellRefusal {
    std::string_view words;
    std::string_view reason;
};

/// Every refusal: the doors the shell already is (`chat`, `execute`, `tui`),
/// the `$EDITOR` verbs (the editor needs the terminal the shell holds), and
/// the three that would pull the ground from under it (`reset`, `uninstall`,
/// `serve` -- the user's call, 2026-10-10).
[[nodiscard]] std::span<const ShellRefusal> shell_refusals() noexcept;

/// What breaks the law over `root`'s registered subcommands, one line each,
/// naming the command: an unclassified subcommand, a row naming none that
/// exists, a refusal naming words no command answers to, a classification
/// with nothing said -- and since 44d (ADR 0012) a view with no door to reach
/// it by, a recorded condition with nothing said for it, a door on a row
/// that is no view. Empty when the law holds; a root with no subcommands at
/// all is itself a violation, so the check never passes vacuously.
[[nodiscard]] std::vector<std::string> shell_law_violations(const CLI::App& root);

/// The same law over `surfaces` in place of the table: how a planted row is
/// shown to be named.
[[nodiscard]] std::vector<std::string> shell_law_violations(const CLI::App& root,
                                                            std::span<const ShellSurface> surfaces);

}  // namespace apogee::commands
