#include "cli/tui_parity.h"

#include <algorithm>
#include <array>

namespace apogee::commands {
namespace {

using enum ShellSurfaceKind;

/// THE table: every root subcommand of `apogee`, today's truth (2026-10-10,
/// the parity spike). A backfill names the track-37 item that draws it; that
/// item flips its row to a view when it ships.
constexpr std::array kSurfaces{
    // --- views: drawn on the shell today --------------------------------------
    ShellSurface{"chat", View, "Session"},
    ShellSurface{"chats", View, "Chats"},
    ShellSurface{"models", View, "Models"},
    ShellSurface{"config", View, "Config (and Suites)"},
    ShellSurface{"version", View, "Home"},
    ShellSurface{"tui", View, "the shell itself -- the door it is opened by"},
    ShellSurface{"system", View, "System (and the monitor bar)"},
    ShellSurface{"check", View, "Check"},
    ShellSurface{"providers", View, "Providers"},
    ShellSurface{"knowledge", View, "Knowledge"},
    ShellSurface{"embed", View, "Collections"},
    ShellSurface{"graph", View, "Graph"},
    ShellSurface{"execute", View, "Session (the picker's execute door)"},
    ShellSurface{"symphonies", View, "Symphonies"},
    ShellSurface{"task", View, "Tasks (a run narrated through the progress seam)"},
    // --- backfills: track 37 draws them ---------------------------------------
    ShellSurface{"train", Backfill, "37f"},
    ShellSurface{"datasets", Backfill, "37f"},
    ShellSurface{"agents", Backfill, "37g"},
    ShellSurface{"mcp", Backfill, "37g"},
    ShellSurface{"auth", Backfill, "37g"},
    // --- runner-covered: one-shot scripting surfaces, the exec line's (37h) ---
    ShellSurface{"complete", RunnerCovered,
                 "a one-shot completion for scripts: the session view is the shell's "
                 "conversation, and the exec line (37h) runs the command as typed"},
    ShellSurface{"analyze", RunnerCovered,
                 "a one-shot agent workflow over an input: the exec line (37h) runs it as typed"},
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
        const ShellSurface* surface = find_shell_surface(command);
        if (surface == nullptr) {
            violations.push_back("'" + command +
                                 "' has no shell classification -- draw it in a view, name the "
                                 "item that will, or record why not (ADR 0010)");
        } else if (surface->detail.empty()) {
            violations.push_back("'" + command + "' is classified with nothing said");
        }
    }
    for (const ShellSurface& surface : kSurfaces) {
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
    // A carve-out is a refusal: the exec line must say why.
    for (const ShellSurface& surface : kSurfaces) {
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
