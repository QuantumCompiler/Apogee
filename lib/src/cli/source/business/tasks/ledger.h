#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platform/pid_lock.h"
#include "tasks/task.h"

/// Where tasks live, and the one lock they run under (27h).
///
/// **One directory per task under the `tasks/` layout row**: `task.json`,
/// the ledger, rewritten through a temp file and a rename on every
/// transition -- the pipeline manifest's pattern (Milestone Z) -- so a
/// `kill -9` at any moment leaves the last transition on disk and never a
/// truncated file; and `request`, a halt or cancel another process left for
/// the running task to read. Beside the directories, `task.lock`: the PID
/// lock (`platform::PidLock`, the cycle's) one running task holds, naming
/// the task, so a second `task run` is refused naming the first.
namespace apogee::tasks {

inline constexpr std::string_view kTaskIdPrefix = "task-";
inline constexpr std::string_view kLedgerFileName = "task.json";
inline constexpr std::string_view kRequestFileName = "request";
inline constexpr std::string_view kLockFileName = "task.lock";

/// `tasks/<id>/`.
[[nodiscard]] std::filesystem::path task_dir(const std::filesystem::path& root,
                                             std::string_view id);
[[nodiscard]] std::filesystem::path ledger_path(const std::filesystem::path& root,
                                                std::string_view id);

/// `task-YYYYMMDD-HHMMSS` in UTC, with `-2`, `-3`, ... while a task of that
/// id exists under `root`.
[[nodiscard]] std::string new_task_id(
    const std::filesystem::path& root,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

/// Letters, digits, `-` and `_` only: an id is a directory name and never
/// reaches outside `tasks/`.
[[nodiscard]] bool valid_task_id(std::string_view id) noexcept;

/// The time now, as every record stamps it.
[[nodiscard]] std::string now_timestamp();

/// `at`, as every record stamps it. A new task's id and its `created_at` are
/// both made from one reading of the clock, so a task listed as created in a
/// second carries that second's id -- the order `list_tasks` relies on.
[[nodiscard]] std::string timestamp(std::chrono::system_clock::time_point at);

/// Writes the ledger through a temp file and a rename, creating the
/// directory. The error text, or empty.
[[nodiscard]] std::string save_task(const std::filesystem::path& root, const Task& task);

/// nullopt with `error` set when there is no such task or its ledger cannot
/// be read.
[[nodiscard]] std::optional<Task> load_task(const std::filesystem::path& root, std::string_view id,
                                            std::string& error);

/// Every task, newest first. A ledger that cannot be read is skipped and
/// named in `problems` -- except a directory with no ledger that a stop sent
/// while a new run started made (57u): the run's while the lock's live
/// holder names it, or one holding that unread stop alone once the run
/// ended before it started (killed during its setup), neither a task yet nor
/// a problem. The second stays on disk: removed, its id could be minted
/// again in the same second, after a later one's `-2`, and list out of turn.
[[nodiscard]] std::vector<Task> list_tasks(const std::filesystem::path& root,
                                           std::vector<std::string>* problems = nullptr);

// --- requests from another process ---------------------------------------------

enum class Request : std::uint8_t { None, Halt, Cancel };

[[nodiscard]] std::string_view to_string(Request request) noexcept;

/// Leaves `request` for the task `id`'s running process. The error text, or
/// empty.
///
/// **A request is addressed to the lock's holder** (57u): one standing when
/// `TaskLock::acquire` takes the lock was left for a process that has ended,
/// and is cleared there; one written after it -- while the run is still
/// starting, its providers built and its MCP servers connecting -- is for
/// this process, and the runner reads it before any model call.
[[nodiscard]] std::string write_request(const std::filesystem::path& root, std::string_view id,
                                        Request request);
[[nodiscard]] Request read_request(const std::filesystem::path& root, std::string_view id);
void clear_request(const std::filesystem::path& root, std::string_view id);

/// Whether task `id`'s ledger is on disk: false while a new run is still
/// starting, though the lock already names it.
[[nodiscard]] bool ledger_written(const std::filesystem::path& root, std::string_view id);

/// What a run refused before it ran leaves, removed (57u): the request it
/// never read, and the task's directory when that leaves it empty -- a new
/// task refused before its ledger was written, which `task list` must never
/// see. A ledger, and anything else in the directory, stays.
void discard_unread_request(const std::filesystem::path& root, std::string_view id);

// --- the lock ---------------------------------------------------------------------

/// Who holds the task lock.
struct LockHolder {
    long pid = 0;
    /// The task it runs.
    std::string task_id;
    /// Whether that process is running now.
    bool running = false;
};

/// The lock's holder, or nullopt when there is no lock.
[[nodiscard]] std::optional<LockHolder> lock_holder(const std::filesystem::path& root);

/// The task running now: the lock's holder, while its process runs.
[[nodiscard]] std::optional<std::string> running_task(const std::filesystem::path& root);

/// One task runs at a time. A lock whose process is gone -- a task killed
/// mid-round -- is taken over, since `task resume` is how that task comes
/// back; a live holder is refused by name.
class TaskLock {
public:
    /// nullopt with `error` set when another task runs, or the lock cannot be
    /// made. Once the lock is this process's, the named task's standing
    /// request is cleared (57u): it was left for a process that ended before
    /// reading it, and this run is a new decision -- 27h's rule, its point
    /// moved from the run's start to the lock changing hands, so a stop sent
    /// while this run is still starting is kept for it. Another task's
    /// directory is never touched: a new run killed during its setup leaves
    /// the stop it never read, which `list_tasks` passes over, and keeps its
    /// id from being minted again.
    [[nodiscard]] static std::optional<TaskLock> acquire(const std::filesystem::path& root,
                                                         std::string_view task_id,
                                                         std::string& error);

    /// Removes the lock now; safe to call twice.
    void release() noexcept {
        lock_.release();
    }

private:
    explicit TaskLock(platform::PidLock lock);
    platform::PidLock lock_;
};

}  // namespace apogee::tasks
