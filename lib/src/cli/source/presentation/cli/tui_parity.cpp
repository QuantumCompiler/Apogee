#include "cli/tui_parity.h"

#include <algorithm>
#include <array>

namespace apogee::commands {
namespace {

using enum ShellSurfaceKind;

/// THE table: every root subcommand of `apogee`, today's truth (2026-10-10,
/// the parity spike). A backfill names the track-37 item that draws it; that
/// item flips its row to a view when it ships. Since 44d each view names its
/// door (ADR 0012): the view's title, and what it shows for this door -- read
/// by `cli.tui_doors` (`pty_door_check.py`) as well as compiled, so each row
/// stays one `ShellSurface{...}` with its `ShellDoor{.view = ...}` spelled
/// out.
constexpr std::array kSurfaces{
    // --- views: drawn on the shell today --------------------------------------
    ShellSurface{"chat", View, "Session", ShellDoor{.view = "Session", .seen = "New chat"}},
    ShellSurface{"chats", View, "Chats", ShellDoor{.view = "Chats"}},
    ShellSurface{"models", View, "Models", ShellDoor{.view = "Models"}},
    ShellSurface{"config", View, "Config (and Suites)", ShellDoor{.view = "Config"}},
    ShellSurface{"version", View, "Home", ShellDoor{.view = "Home"}},
    ShellSurface{"tui", View, "the shell itself -- the door it is opened by",
                 ShellDoor{.view = "Session"}},
    ShellSurface{"system", View, "System (and the monitor bar)", ShellDoor{.view = "System"}},
    ShellSurface{"check", View, "Check", ShellDoor{.view = "Check"}},
    ShellSurface{"providers", View, "Providers", ShellDoor{.view = "Providers"}},
    ShellSurface{"knowledge", View, "Knowledge", ShellDoor{.view = "Knowledge"}},
    ShellSurface{"embed", View, "Collections", ShellDoor{.view = "Collections"}},
    ShellSurface{"graph", View, "Graph", ShellDoor{.view = "Graph"}},
    ShellSurface{"execute", View, "Session (the picker's execute door)",
                 ShellDoor{.view = "Session", .seen = "Execute"}},
    ShellSurface{"symphonies", View, "Symphonies", ShellDoor{.view = "Symphonies"}},
    ShellSurface{"task", View, "Tasks (a run narrated through the progress seam)",
                 ShellDoor{.view = "Tasks"}},
    ShellSurface{"train", View, "Train (a run narrated through the progress seam)",
                 ShellDoor{.view = "Train"}},
    ShellSurface{"datasets", View, "Datasets", ShellDoor{.view = "Datasets"}},
    ShellSurface{"agents", View, "Agents (edit stays the $EDITOR's, refused below)",
                 ShellDoor{.view = "Agents"}},
    ShellSurface{"mcp", View, "MCP", ShellDoor{.view = "MCP"}},
    ShellSurface{"auth", View,
                 "Auth, metadata only (a key is stored at a real prompt: add's secret input "
                 "never enters the shell)",
                 ShellDoor{.view = "Auth"}},
    // --- backfills: track 37 draws them -- none left since 37g -----------------
    // --- runner-covered: one-shot scripting surfaces, the exec line's (37h) ---
    ShellSurface{"complete", RunnerCovered,
                 "a one-shot completion for scripts: the session view is the shell's "
                 "conversation, and the exec line (37h) runs the command as typed"},
    ShellSurface{"analyze", RunnerCovered,
                 "a one-shot agent workflow over an input: the exec line (37h) runs it as typed"},
    ShellSurface{"review", RunnerCovered,
                 "reviewing merge requests and the skills they run (48): the exec line (37h) "
                 "runs it as typed; a Review view is a recorded later item"},
    ShellSurface{"video", RunnerCovered,
                 "generating video locally (49) and composing it from a brief and editing it by "
                 "instruction (50c, 50d): the exec "
                 "line (37h) runs it as typed, its narration streamed; a Video view is a "
                 "recorded later item"},
    ShellSurface{"voice", RunnerCovered,
                 "speech from a voice model in the store (50a): the exec line (37h) runs it as "
                 "typed"},
    // --- hidden plumbing: protocols other programs speak, never a person's ----
    ShellSurface{"__complete", RunnerCovered,
                 "the shell stubs' completion protocol -- the exec line's Tab asks the same "
                 "core in-process"},
    ShellSurface{"__mcp-tools", RunnerCovered,
                 "the MCP stdio server a client spawns -- a protocol, not a page"},
    ShellSurface{"__machine-schema", RunnerCovered,
                 "the machine protocol's JSON Schema, for a driver to read"},
    // --- carved out: no surface, refused by the exec line ----------------------
    ShellSurface{"serve", CarvedOut, "a server deployment's daemon"},
    ShellSurface{"uninstall", CarvedOut, "removes Apogee and its data directory"},
    ShellSurface{"reset", CarvedOut, "resets the data directory to a fresh install"},
};

constexpr std::array kRefusals{
    // The doors: the shell already is what they open.
    ShellRefusal{"chat", "the conversation is the Session view -- open a chat from its picker"},
    ShellRefusal{"execute",
                 "the conversation is the Session view -- open an execute session from its "
                 "picker"},
    ShellRefusal{"tui", "this is the shell already"},
    // The `$EDITOR` wall (37g): the editor needs the terminal the shell holds.
    ShellRefusal{"agents edit",
                 "opens your $EDITOR, which needs the terminal the shell holds -- run it at "
                 "a prompt"},
    ShellRefusal{"symphonies edit",
                 "opens your $EDITOR, which needs the terminal the shell holds -- run it at "
                 "a prompt"},
    // The self-destructive three (the user's call, 2026-10-10).
    ShellRefusal{"reset",
                 "deletes the data directory the running shell stands on -- run it at a "
                 "prompt, with the shell closed"},
    ShellRefusal{"uninstall",
                 "removes Apogee and the data directory the running shell stands on -- run it "
                 "at a prompt, with the shell closed"},
    ShellRefusal{"serve",
                 "a daemon the shell would orphan when it closes -- run it at a prompt, or "
                 "under a service manager"},
};

/// The words a command answers to: each subcommand, and each subcommand with
/// its verbs, as `"agents edit"`.
void collect_words(const CLI::App& app, const std::string& prefix, std::vector<std::string>& out) {
    for (const CLI::App* child : app.get_subcommands({})) {
        const std::string words =
            prefix.empty() ? child->get_name() : prefix + " " + child->get_name();
        out.push_back(words);
        collect_words(*child, words, out);
    }
}

}  // namespace

std::span<const ShellSurface> shell_surfaces() noexcept {
    return kSurfaces;
}

const ShellSurface* find_shell_surface(std::string_view command) noexcept {
    const auto found = std::ranges::find(kSurfaces, command, &ShellSurface::command);
    return found == kSurfaces.end() ? nullptr : &*found;
}

std::span<const ShellRefusal> shell_refusals() noexcept {
    return kRefusals;
}

std::vector<std::string> shell_law_violations(const CLI::App& root) {
    return shell_law_violations(root, kSurfaces);
}

std::vector<std::string> shell_law_violations(const CLI::App& root,
                                              std::span<const ShellSurface> surfaces) {
    const auto find_surface = [surfaces](std::string_view command) -> const ShellSurface* {
        const auto found = std::ranges::find(surfaces, command, &ShellSurface::command);
        return found == surfaces.end() ? nullptr : &*found;
    };
    std::vector<std::string> violations;
    std::vector<std::string> commands;
    for (const CLI::App* command : root.get_subcommands({})) {
        commands.push_back(command->get_name());
    }
    if (commands.empty()) {
        violations.emplace_back("no subcommands registered -- the law would pass vacuously");
        return violations;
    }
    for (const std::string& command : commands) {
        const ShellSurface* surface = find_surface(command);
        if (surface == nullptr) {
            violations.push_back("'" + command +
                                 "' has no shell classification -- draw it in a view, name the "
                                 "item that will, or record why not (ADR 0010)");
        } else if (surface->detail.empty()) {
            violations.push_back("'" + command + "' is classified with nothing said");
        }
    }
    for (const ShellSurface& surface : surfaces) {
        if (std::ranges::find(commands, surface.command) == commands.end()) {
            violations.push_back("the table classifies '" + std::string{surface.command} +
                                 "', which no longer exists");
        }
    }
    std::vector<std::string> words;
    collect_words(root, {}, words);
    for (const ShellRefusal& refusal : kRefusals) {
        if (std::ranges::find(words, refusal.words) == words.end()) {
            violations.push_back("the exec line refuses '" + std::string{refusal.words} +
                                 "', which no longer exists");
        }
        if (refusal.reason.empty()) {
            violations.push_back("the exec line refuses '" + std::string{refusal.words} +
                                 "' with no reason");
        }
    }
    // A view is reachable (ADR 0012): it names the door a fresh install is
    // walked to, and a conditional one what is said where it is absent.
    for (const ShellSurface& surface : surfaces) {
        const std::string named = "'" + std::string{surface.command} + "'";
        if (surface.kind == View && surface.door.view.empty()) {
            violations.push_back(named +
                                 " is classified as a view with no door to reach it by -- name "
                                 "the view, and what it shows for it (ADR 0012)");
        }
        if (surface.kind != View && !surface.door.view.empty()) {
            violations.push_back(named + " names a door, and is no view");
        }
        if (!surface.door.condition.empty() && surface.door.seen.empty()) {
            violations.push_back(named + "'s door is conditional on " +
                                 std::string{surface.door.condition} +
                                 ", with nothing said where it is absent");
        }
    }
    // A carve-out is a refusal: the exec line must say why.
    for (const ShellSurface& surface : surfaces) {
        if (surface.kind == CarvedOut &&
            std::ranges::find(kRefusals, surface.command, &ShellRefusal::words) ==
                kRefusals.end()) {
            violations.push_back("'" + std::string{surface.command} +
                                 "' is carved out with no refusal recorded for the exec line");
        }
    }
    return violations;
}

}  // namespace apogee::commands
