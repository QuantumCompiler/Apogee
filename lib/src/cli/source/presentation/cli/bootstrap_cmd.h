#pragma once

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "cli/check.h"
#include "cli/command.h"

/// `apogee bootstrap` (51): a fresh install that arrives ready.
///
/// Every requirement set of `train setup`'s one table this host is for --
/// the table's own column says which -- then the providers this machine has
/// and the models its store holds, registered. Each step is the shipped
/// command itself, run as a captured child of this binary (the shell's
/// precedent, `cli/tui_child`): `train setup --with <set>`, `providers scan
/// --register`, `config scan --register`. Bootstrap composes them and
/// reimplements none, so a set added to the table joins with no edit here.
///
/// **Never fatal.** A step that cannot finish -- offline, nothing to
/// register, no child processes on this platform -- is skipped with its
/// reason and the rest still run; the install stays green, and the last
/// line names `apogee bootstrap`, which runs it all again any time.
/// `--minimal`, or `APOGEE_MINIMAL_INSTALL` in the environment, skips it
/// whole, said. Every install path runs it right after `check --fix`.
///
/// Since 2026-10-21 (the user's call) it also registers the code-execution
/// server (47b's, by 47c's preset) after the runtimes, and on macOS with
/// Homebrew it brings an ffmpeg that burns captions -- `check`'s own remedy,
/// Homebrew's ffmpeg-full linked over its ffmpeg -- when the ffmpeg found
/// cannot. That last step is the one that runs another program than this
/// binary, through a seam of its own.
namespace apogee::commands {

/// One step, and how it ended.
struct BootstrapStep {
    /// What it sets up, in words: `the 'video' set`, `the providers`.
    std::string name;
    /// The command it runs, `apogee` left off; empty for a step this host is
    /// not for, or one that runs another program's commands.
    std::vector<std::string> words;
    /// Another program's commands, run in order instead of `words`
    /// (2026-10-21: Homebrew's, for ffmpeg-full). One that must succeed stops
    /// the rest when it fails; one that may fail does not.
    std::vector<RemedyCommand> external{};
    /// `done` or `skipped` -- or empty, not yet run.
    std::string status{};
    std::string reason{};

    /// Whether the step has anything to run here.
    [[nodiscard]] bool runs() const noexcept {
        return !words.empty() || !external.empty();
    }
};

/// What the plan is decided by: facts about this host and its install, read
/// before the first step runs.
struct BootstrapHost {
    bool apple_silicon = false;
    bool nvidia_smi = false;
    /// macOS, where Homebrew's ffmpeg-full is what burns captions.
    bool macos = false;
    /// `brew` found on PATH.
    bool brew = false;
    /// Whether the ffmpeg on PATH has the `subtitles` filter (libass) --
    /// nullopt when no ffmpeg answered.
    std::optional<bool> ffmpeg_subtitles{};
    /// The MCP server the code-execution server is registered as, when it
    /// is -- or, when another server holds its name, that one.
    std::string code_exec_registered_as{};
    /// Whether `code_exec_registered_as` is another server holding the name.
    bool code_exec_name_taken = false;
};

/// Every step a bootstrap takes on a host with these facts, in order: each
/// set of the table -- one this host is not for already skipped, saying
/// why, and an opt-in one (53k: `mlx-video`) skipped naming the command that
/// installs it -- then the code-execution server, the providers, the model
/// store, and ffmpeg with captions.
[[nodiscard]] std::vector<BootstrapStep> bootstrap_plan(const BootstrapHost& host);

/// What a bootstrap needs from outside.
struct BootstrapSeams {
    BootstrapHost host;
    /// Runs `apogee <words...>` to its end, each line it writes handed on;
    /// its exit code, -1 when it could not start (said through `line`).
    std::function<int(const std::vector<std::string>& words,
                      const std::function<void(std::string)>& line)>
        run;
    /// Runs another program, `argv[0]` found on PATH, the same way.
    std::function<int(const std::vector<std::string>& argv,
                      const std::function<void(std::string)>& line)>
        run_program;
    /// A line of the narration.
    std::function<void(const std::string&)> say;
};

struct BootstrapResult {
    /// Skipped whole, and what asked for it: `--minimal` or
    /// `APOGEE_MINIMAL_INSTALL`.
    bool minimal = false;
    std::string minimal_by{};
    std::vector<BootstrapStep> steps;
};

/// One bootstrap: skipped whole when `minimal_by` is not empty, else every
/// step of the plan run in order, each said as it runs.
[[nodiscard]] BootstrapResult run_bootstrap(const BootstrapSeams& seams,
                                            const std::string& minimal_by = {});

/// `--output-format json`: `{"object": "apogee.bootstrap", "minimal", "steps":
/// [{"name", "command", "status", "reason"}]}`. An external step's `command`
/// is its commands as typed, `brew …` (2026-10-21).
[[nodiscard]] nlohmann::json bootstrap_document(const BootstrapResult& result);

/// Whether the environment asks for a minimal install: `APOGEE_MINIMAL_INSTALL`
/// set to anything but empty, `0` or `false`.
[[nodiscard]] bool minimal_install_env();

class BootstrapCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
