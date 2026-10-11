#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "cli/command.h"
#include "platform/child_process.h"

/// The shell runs a command as a captured child of its own binary (37f; the
/// design 37h's exec line records): the command itself, never a second
/// implementation, so what it writes and what it leaves -- a run's manifest,
/// a promoted version, a config line -- are the command's by construction.
///
/// The child gets the shell's root -- `--config` when the shell was given
/// one, and the root it resolved as `APOGEE_HOME` -- its three pipes and
/// nothing else (`platform/child_process`), stdin closed at once (a command
/// that would ask reads end-of-input and declines), and its stdout and
/// stderr lines are handed on as they arrive. Stopping it interrupts it as
/// Ctrl-C at a terminal would, so a command that handles that records its
/// own cancellation.
namespace apogee::commands {

/// `binary` run as `apogee <words...>` under the shell's root.
[[nodiscard]] platform::ChildCommand self_command(const RootContext& context,
                                                  const std::filesystem::path& binary,
                                                  const std::vector<std::string>& words);

/// How a run of the shell's binary is stopped from another thread: once it
/// runs, `stop` interrupts it; asked before, it is interrupted as it starts.
class ChildStop {
public:
    void stop();

    /// The run's own side: the child it runs, and none once it has ended.
    void attach(platform::ChildProcess* child);
    void detach();

private:
    std::mutex mutex_;
    platform::ChildProcess* child_ = nullptr;
    bool stopped_ = false;
};

/// Runs `command` to its end, each line it writes -- stdout and stderr alike,
/// as they arrive -- handed to `line`. Returns its exit code: -1 when it
/// ended on a signal, or could not start (said through `line`).
[[nodiscard]] int run_child_lines(const platform::ChildCommand& command,
                                  const std::function<void(std::string)>& line,
                                  ChildStop* stop = nullptr);

/// The same, its lines gathered: a short act's whole answer (a rollback, a
/// delete), with its exit code.
struct ChildOutput {
    int code = 0;
    std::string text;
};

[[nodiscard]] ChildOutput run_child_text(const platform::ChildCommand& command);

/// A finished run's last line, in the scripts' terms: `exit 0`, `exit 2`, or
/// `stopped by a signal`.
[[nodiscard]] std::string exit_line(int code);

}  // namespace apogee::commands
