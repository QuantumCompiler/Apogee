#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// Spawning and talking to a long-lived child process.
///
/// The mechanism the platform header has been reserving for "the vendor-CLI
/// backends' persistent children". It lives here, behind a first-party
/// interface, because the three platforms do genuinely different things and the
/// alternative is `#ifdef` blocks in feature code.
///
/// Two shape decisions come straight from this item's design notes, and both
/// are about a failure that is invisible until it is not:
///
/// **Raw file descriptors, never a `FILE*`.** A pipe wrapped in a fully
/// buffered stdio stream holds up to 4 KiB before handing anything over —
/// reintroducing exactly the chunking that token-level streaming exists to
/// remove. `read_stdout` reads the fd.
///
/// **stdout and stderr stay separate.** Merging them puts the child's
/// diagnostics inside the JSONL stream, which breaks the parser at the worst
/// possible moment. stderr is drained separately and kept as a bounded tail so
/// a spawn failure can be reported with what the child actually said.
namespace apogee::platform {

/// What to run.
struct ChildCommand {
    /// Executable, resolved from PATH when it contains no separator.
    std::string program;
    std::vector<std::string> arguments;

    /// The child inherits the parent environment wholesale. That is a Core
    /// constraint of the vendor-CLI family, not a convenience: a harness that
    /// injects credentials of its own can silently redirect a user's
    /// subscription plan to per-token billing.
    std::vector<std::pair<std::string, std::string>> extra_environment;
};

/// Why a read stopped.
enum class ReadStatus : std::uint8_t {
    /// Bytes were produced.
    Data,
    /// The poll timed out with nothing available. Not an error — it is what
    /// lets a reader loop notice a cancellation flag.
    Timeout,
    /// The child closed this stream.
    Eof,
    /// The read failed.
    Error,
};

/// A running child.
class ChildProcess {
public:
    ChildProcess() = default;
    virtual ~ChildProcess() = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&&) = delete;
    ChildProcess& operator=(ChildProcess&&) = delete;

    /// Writes to the child's stdin and flushes. Returns false once the pipe is
    /// closed or broken.
    ///
    /// Callers must serialise their writes; this does not lock. One writer is
    /// the design, and a mutex here would hide a second one rather than
    /// prevent it.
    [[nodiscard]] virtual bool write_stdin(std::string_view bytes) = 0;

    /// Closes stdin, which is how a turn-stream child is told to finish.
    ///
    /// Preferred over killing: a clean close lets the child emit its terminal
    /// event, which is where the session id and cost accounting live.
    virtual void close_stdin() = 0;

    /// Reads whatever is available from stdout, waiting up to `timeout`.
    [[nodiscard]] virtual ReadStatus read_stdout(std::string& out,
                                                 std::chrono::milliseconds timeout) = 0;

    /// Reads whatever is available from stderr, waiting up to `timeout`.
    [[nodiscard]] virtual ReadStatus read_stderr(std::string& out,
                                                 std::chrono::milliseconds timeout) = 0;

    /// Whether the child has exited, without blocking.
    [[nodiscard]] virtual bool exited() = 0;

    /// Waits up to `timeout` for exit and returns the status, or nullopt on
    /// timeout.
    ///
    /// The budget matters: the CLI drains queued output before exiting rather
    /// than truncating, scaling its wait with the backlog up to roughly 30
    /// seconds. A 5-second kill cuts off tail output under load.
    [[nodiscard]] virtual std::optional<int> wait_for_exit(std::chrono::milliseconds timeout) = 0;

    /// Terminates the child. Last resort — see close_stdin.
    virtual void terminate() = 0;
};

/// Starts `command`, or returns nullptr with `error` filled.
///
/// Returns nullptr on Windows: this mechanism has no implementation there yet.
/// A recorded per-item skip, surfaced as a clear message rather than a silent
/// gap — the same discipline the PTY tests and the `lsof` check follow.
[[nodiscard]] std::unique_ptr<ChildProcess> start_child(const ChildCommand& command,
                                                        std::string& error);

/// Whether this build can spawn a child process at all.
[[nodiscard]] bool supports_child_processes() noexcept;

/// Absolute path of `program` if it is executable on PATH, else empty.
///
/// Used to fail early with a message naming what was not found, rather than
/// letting a spawn fail with a numeric errno the user cannot act on.
[[nodiscard]] std::string find_on_path(std::string_view program);

}  // namespace apogee::platform
