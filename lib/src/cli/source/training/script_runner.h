#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "harness/cancellation.h"
#include "platform/child_process.h"

/// The C++ side of the training track's Python boundary.
///
/// Every Python driver Apogee ships -- `prepare_dataset.py` today, the
/// trainers with the run item -- speaks ONE line protocol on stdout: one JSON
/// object per line, `{"message": "..."}` for a note, `{"error": "..."}` for a
/// fatal problem followed by a non-zero exit, and each script's own terminal
/// record (`{"rows_written", ...}`). This file runs a script under the
/// environment's interpreter and turns that stream into events.
///
/// Three rules, each a closed gap from the reference implementation:
///
/// **stdout is framed by the one framer.** A pipe hands bytes over in
/// whatever sizes the kernel felt like; `backends/jsonl_framer.h` has been
/// tested against every chunk boundary, and a second splitter would be a
/// second copy of that bug class -- the same allowance `mcp/` has.
///
/// **A non-JSON line is a message, never dropped.** Ommi's reader silently
/// `continue`d on a parse error, so a driver's stack trace vanished. Here it
/// reaches the caller as text.
///
/// **The exit code is carried, and `{"error"}` is its own event.** Ommi
/// discarded `cmd.Wait()`'s status and its progress struct had no `error`
/// field, so an error line decoded as a blank progress tick and a crashed
/// trainer was indistinguishable from success. A crashed script is a failed
/// run here, with the code and the stderr tail on the outcome.
///
/// stderr is captured as a bounded tail and never inherited: a driver's
/// warnings must not land on the user's terminal mid-status-line, and they
/// must not enter the JSONL stream either.
namespace apogee::training {

/// One line the script emitted, classified.
struct ScriptEvent {
    enum class Kind : std::uint8_t {
        /// `{"message": "..."}`, or any line that is not a JSON object.
        Message,
        /// `{"error": "..."}` -- fatal; the script exits non-zero after it.
        Error,
        /// Any other JSON object: the script's own terminal record.
        Record,
    };
    Kind kind = Kind::Message;
    /// The message or error text; for a record, the raw line.
    std::string text;
    /// The parsed object for a record; null otherwise.
    nlohmann::json record;
};

/// Classifies one complete line. Exposed because it is the contract: a test
/// pins each shape rather than inferring it from a run.
[[nodiscard]] ScriptEvent classify_script_line(std::string_view line);

using ScriptEventSink = std::function<void(const ScriptEvent&)>;

/// How a child is started. Injectable so every test here is hermetic --
/// a scripted child in place of a real interpreter.
using Spawner = std::function<std::unique_ptr<platform::ChildProcess>(
    const platform::ChildCommand& command, std::string& error)>;

/// `platform::start_child`.
[[nodiscard]] Spawner default_spawner();

struct ScriptRequest {
    /// The environment's interpreter -- never the system Python.
    std::filesystem::path interpreter;
    std::filesystem::path script;
    std::vector<std::string> arguments;
    /// Extra variables for the child, over the inherited environment.
    std::vector<std::pair<std::string, std::string>> environment;
};

struct ScriptOutcome {
    /// Exit status zero and no `{"error"}` line.
    bool ok = false;
    /// nullopt when the child never started or was killed.
    std::optional<int> exit_code;
    /// The last `{"error"}` text; a synthesised one for a bad exit with none;
    /// the spawn failure when it never started.
    std::string error;
    /// The last lines the script wrote to stderr, for the failure report.
    std::string stderr_tail;
    bool cancelled = false;

    /// `error`, with the stderr tail appended when there is one.
    [[nodiscard]] std::string describe() const;
};

/// Runs `request` to completion, delivering every event as it arrives.
/// Cancellation terminates the child and reports `cancelled`.
[[nodiscard]] ScriptOutcome run_script(const ScriptRequest& request,
                                       const ScriptEventSink& on_event,
                                       const harness::CancellationToken& cancellation,
                                       const Spawner& spawn = default_spawner());

/// A command run to completion with both streams captured -- what creating
/// the environment and installing into it need.
struct CommandResult {
    std::optional<int> exit_code;
    std::string out;
    std::string err;
    /// Why the child could not be started, or empty.
    std::string start_error;
    bool cancelled = false;

    [[nodiscard]] bool ok() const noexcept {
        return start_error.empty() && !cancelled && exit_code.has_value() && *exit_code == 0;
    }
};

[[nodiscard]] CommandResult run_command(const platform::ChildCommand& command,
                                        const harness::CancellationToken& cancellation,
                                        const Spawner& spawn = default_spawner());

using CommandRunner =
    std::function<CommandResult(const platform::ChildCommand&, const harness::CancellationToken&)>;

/// `run_command` over `default_spawner()`.
[[nodiscard]] CommandRunner default_command_runner();

/// The last `lines` lines of `text`, for a failure report.
[[nodiscard]] std::string tail_of(std::string_view text, std::size_t lines = 8);

}  // namespace apogee::training
