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
/// named in `problems`.
[[nodiscard]] std::vector<Task> list_tasks(const std::filesystem::path& root,
                                           std::vector<std::string>* problems = nullptr);

// --- requests from another process ---------------------------------------------

enum class Request : std::uint8_t { None, Halt, Cancel };

[[nodiscard]] std::string_view to_string(Request request) noexcept;

/// Leaves `request` for the task `id`'s running process. The error text, or
/// empty.
[[nodiscard]] std::string write_request(const std::filesystem::path& root, std::string_view id,
                                        Request request);
[[nodiscard]] Request read_request(const std::filesystem::path& root, std::string_view id);
void clear_request(const std::filesystem::path& root, std::string_view id);

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
    /// made.
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
