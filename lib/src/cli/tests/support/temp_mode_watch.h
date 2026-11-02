#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <thread>

namespace apogee::testing {

/// Watches `directory` from a thread of its own while a test writes there,
/// stat'ing every temporary file it catches -- a name holding `.tmp.`, as
/// `write_file_atomically` names its own, with a random suffix, so one name
/// is one write -- and counting the distinct files it caught, and those it
/// caught readable or writable beyond their owner (57e). It is the probe that
/// found a secret's temporary file at the umask's mode while it was written,
/// kept as a test: a writer that creates its file wide and narrows it after
/// is caught in the window between.
///
/// A watch proves nothing it did not see, so a test loops its writes until
/// `seen()` reaches a few distinct files (or a cap of writes) and requires it
/// above zero, never passing on a watch that caught no file at all.
class TempModeWatch {
public:
    explicit TempModeWatch(std::filesystem::path directory);
    ~TempModeWatch();

    TempModeWatch(const TempModeWatch&) = delete;
    TempModeWatch& operator=(const TempModeWatch&) = delete;
    TempModeWatch(TempModeWatch&&) = delete;
    TempModeWatch& operator=(TempModeWatch&&) = delete;

    /// Distinct temporary files caught and stat'ed -- one per write caught,
    /// however many times the loop stat'ed it while it lived.
    [[nodiscard]] std::size_t seen() const noexcept {
        return seen_.load();
    }

    /// Of those, the ones caught (at least once) under a mode wider than the
    /// owner's.
    [[nodiscard]] std::size_t wider() const noexcept {
        return wider_.load();
    }

    /// Stops the watch and waits for its thread; the destructor's too.
    void stop();

private:
    void run();

    std::filesystem::path directory_;
    std::atomic<bool> running_{true};
    std::atomic<std::size_t> seen_{0};
    std::atomic<std::size_t> wider_{0};
    std::thread thread_;
};

/// Sets the process's umask to `022` -- the common one, under which a file
/// made without a mode of its own is readable by everyone -- for the life of
/// the object, restoring the one before (POSIX; nothing on Windows). A mode
/// test pins it so a developer's narrower umask cannot hide what it checks.
class UmaskGuard {
public:
    UmaskGuard();
    ~UmaskGuard();

    UmaskGuard(const UmaskGuard&) = delete;
    UmaskGuard& operator=(const UmaskGuard&) = delete;
    UmaskGuard(UmaskGuard&&) = delete;
    UmaskGuard& operator=(UmaskGuard&&) = delete;

private:
    unsigned previous_ = 0;
};

}  // namespace apogee::testing
