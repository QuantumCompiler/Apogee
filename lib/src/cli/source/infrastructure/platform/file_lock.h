#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

/// An advisory lock on a file, across processes and threads (55f).
///
/// `flock` on POSIX, `LockFileEx` on Windows: an exclusive lock on a file
/// opened for the purpose, which the system lets go of when its holder closes
/// the file or dies -- so a killed process never strands one, as it can a PID
/// lock file (`pid_lock.h`), whose holder's id must be read before it is taken
/// over. Each acquire opens the file afresh, so two threads of one process
/// exclude each other as two processes do.
///
/// **The file is a sibling the caller names** (the config's is
/// `config.json.lock` beside it), created when absent and never removed:
/// removed while another process waits on its handle, a third would create a
/// new one and take a lock on it beside the second.
///
/// Advisory: it orders only the code that asks for it. Every config edit asks
/// (`contracts/config_edit.h`'s `edit_config_file`).
namespace apogee::platform {

class FileLock {
public:
    /// Why `acquire` did not take the lock.
    struct Refusal {
        /// Another holder kept it for the whole wait.
        bool timed_out = false;
        /// What went wrong, in a sentence: the wait it gave up after, or why
        /// the file could not be opened or locked.
        std::string error{};
    };

    /// Takes the lock on `path` -- opening it, created when absent -- waiting
    /// up to `wait` for another holder to let go, polling with a short
    /// back-off. nullopt with `refusal` filled when the wait runs out or the
    /// file cannot be opened or locked; never a silent pass.
    [[nodiscard]] static std::optional<FileLock> acquire(const std::filesystem::path& path,
                                                         std::chrono::milliseconds wait,
                                                         Refusal& refusal);

    ~FileLock();
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    /// Lets the lock go now, closing the file; safe to call twice.
    void release() noexcept;

private:
    /// The open, locked file -- a descriptor or a handle, by platform -- which
    /// never leaves this class.
    struct Held;

    FileLock(std::filesystem::path path, std::unique_ptr<Held> held);
    std::filesystem::path path_;
    std::unique_ptr<Held> held_;
};

}  // namespace apogee::platform
