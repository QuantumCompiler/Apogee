#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/// A PID lock file: one holder at a time, across processes.
///
/// The shape the training cycle proved (Milestone Z) and the task runner
/// reuses (27h): the file is created exclusively -- `O_CREAT|O_EXCL`,
/// `CREATE_NEW` on Windows, through `create_exclusive_file` -- holding the
/// holder's process id on its first line and, on its second, a label saying
/// what it holds the lock for (a task's id), and removed on release. Two
/// processes racing for it cannot both win, and the filesystem is the source
/// of truth: whoever holds the file holds the lock.
///
/// **A lock a dead process left.** By default it is refused like a live one,
/// and the caller names the way out -- the cycle's rule. With `take_stale`
/// the caller may take it over when its process is no longer running: what a
/// killed task needs, since `task resume` is the documented way back from a
/// `kill -9` and must not first ask the user to delete a file. A reused id
/// reads as running (`process_running`), which errs toward refusing.
namespace apogee::platform {

class PidLock {
public:
    /// Who holds a lock file.
    struct Holder {
        /// 0 when the file names none it can read.
        long pid = 0;
        /// The second line, or empty.
        std::string label;
        /// Whether that process is running now.
        bool running = false;
    };

    /// What `acquire` found when it could not take the lock.
    struct Refusal {
        /// Set when the file exists: who holds it. Unset when the lock could
        /// not be made at all (`error` says why).
        std::optional<Holder> holder;
        std::string error;
    };

    /// Creates `path` (and its directory) exclusively, holding this process's
    /// id and `label`. nullopt with `refusal` filled when the file exists --
    /// or, with `take_stale`, when it exists and its holder is running -- or
    /// cannot be made.
    [[nodiscard]] static std::optional<PidLock> acquire(const std::filesystem::path& path,
                                                        std::string_view label, bool take_stale,
                                                        Refusal& refusal);

    /// Who holds `path`, or nullopt when there is no such file.
    [[nodiscard]] static std::optional<Holder> holder(const std::filesystem::path& path);

    ~PidLock();
    PidLock(PidLock&& other) noexcept;
    PidLock& operator=(PidLock&& other) noexcept;
    PidLock(const PidLock&) = delete;
    PidLock& operator=(const PidLock&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    /// Removes the file now; safe to call twice.
    void release() noexcept;

private:
    explicit PidLock(std::filesystem::path path);
    std::filesystem::path path_;
};

}  // namespace apogee::platform
