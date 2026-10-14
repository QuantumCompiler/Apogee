#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "cli/chat_completer.h"
#include "cli/command.h"

namespace CLI {
class App;
}  // namespace CLI

namespace apogee::tui {
class Shell;
}  // namespace apogee::tui

/// The full-screen shell's doors (32b): bare `apogee` at an interactive
/// terminal, and `apogee tui`, the spelled way in.
///
/// **Strictly additive.** A bare `apogee` with a pipe on either side --
/// `apogee </dev/null`, `apogee | cat`, a script, CI -- prints exactly the
/// help it always printed, and every subcommand parses, prints and exits as
/// it did: the gate errs toward the raw CLI, never toward a screen nobody
/// can see. A terminal that cannot host a screen (`TERM=dumb`; a Windows
/// console without virtual-terminal sequences) is said on stderr and gets
/// the help.
///
/// This is the composition root's half: the shell's views are built here,
/// over the cores the commands run -- `tui/` draws them and reaches down,
/// never back into `cli/`.
namespace apogee::commands {

struct SessionFlags;

/// What the shell opens first (44c): with `flags`, a session -- `apogee tui`
/// given the session table's flags, parsed through the one table the modes
/// share and opened through `mode`'s own opening (ADR 0011); without, the
/// picker, as a bare `apogee` and a flagless `apogee tui` open it.
struct ShellLaunch {
    std::shared_ptr<const SessionFlags> flags;
    SessionMode mode = SessionMode::Chat;
};

/// The mode session flags open (44c): execute when they name a suite (never
/// `off`) or ask to orchestrate -- what only execute's opening does with a
/// suite -- else chat.
[[nodiscard]] SessionMode launch_mode(const SessionFlags& flags);

/// How the shell is reached, each part replaceable in a test.
struct ShellEntry {
    /// Whether this run is at an interactive terminal: stdin and stdout both
    /// one (`platform::is_terminal`, which a process feeding or capturing a
    /// standard stream answers as a pipe).
    std::function<bool()> interactive;
    /// Why the terminal cannot host the screen, or empty when it can
    /// (`platform::full_screen_refusal`).
    std::function<std::string()> refusal;
    /// Builds the shell over `context` and runs it full screen, its first
    /// session `launch`'s; its exit code.
    std::function<int(const RootContext&, const ShellLaunch&)> run;
};

/// The real doors: the platform's answers, and the terminal's loop.
[[nodiscard]] ShellEntry default_shell_entry();

/// The shell's views, built over the cores: Home -- `apogee version`'s
/// report, which Apogee this is and where its data lives -- and Keys, the
/// shell's own keys; both under Setup (38).
void add_shell_views(tui::Shell& shell, const RootContext& context);

/// Bare `apogee`: the shell when `entry` says this is an interactive terminal
/// that can host it; otherwise `app`'s help on stdout, exactly as before --
/// after the terminal's refusal on stderr when that was the reason.
[[nodiscard]] int open_bare(const ShellEntry& entry, const RootContext& context, CLI::App& app);

/// `apogee tui`: the shell, or a refusal saying why not (exit 1). Since 44c
/// it takes the session table `chat` and `execute` share -- bound by the same
/// call, so a flag the modes grow is `tui`'s with no change here -- and with
/// any of them given opens the shell inside that session.
class TuiCommand final : public Command {
public:
    explicit TuiCommand(ShellEntry entry = default_shell_entry());

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;

private:
    ShellEntry entry_;
};

}  // namespace apogee::commands
