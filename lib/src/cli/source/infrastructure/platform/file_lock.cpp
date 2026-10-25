#include "platform/file_lock.h"

#include <cerrno>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace apogee::platform {
namespace {

/// The longest pause between two tries: short enough that a waiter takes the
/// lock soon after its holder lets go, long enough not to spin.
constexpr std::chrono::milliseconds kLongestPause{50};

/// A wait as a person reads it: whole seconds, else milliseconds.
[[nodiscard]] std::string spoken(std::chrono::milliseconds wait) {
    if (wait.count() >= 1000 && wait.count() % 1000 == 0) {
        return std::to_string(wait.count() / 1000) + " s";
    }
    return std::to_string(wait.count()) + " ms";
}

}  // namespace

#if defined(_WIN32)

struct FileLock::Held {
    explicit Held(HANDLE opened) noexcept : handle{opened} {}

    ~Held() {
        if (handle != INVALID_HANDLE_VALUE) {
            OVERLAPPED whole{};
            (void)UnlockFileEx(handle, 0, 1, 0, &whole);
            (void)CloseHandle(handle);
        }
    }

    Held(const Held&) = delete;
    Held& operator=(const Held&) = delete;
    Held(Held&&) = delete;
    Held& operator=(Held&&) = delete;

    HANDLE handle = INVALID_HANDLE_VALUE;
};

#else

struct FileLock::Held {
    explicit Held(int opened) noexcept : descriptor{opened} {}

    ~Held() {
        if (descriptor >= 0) {
            (void)::flock(descriptor, LOCK_UN);
            (void)::close(descriptor);
        }
    }

    Held(const Held&) = delete;
    Held& operator=(const Held&) = delete;
    Held(Held&&) = delete;
    Held& operator=(Held&&) = delete;

    int descriptor = -1;
};

#endif

FileLock::FileLock(std::filesystem::path path, std::unique_ptr<Held> held)
    : path_{std::move(path)}, held_{std::move(held)} {}

FileLock::~FileLock() {
    release();
}

FileLock::FileLock(FileLock&& other) noexcept
    : path_{std::move(other.path_)}, held_{std::move(other.held_)} {
    other.path_.clear();
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release();
        path_ = std::move(other.path_);
        held_ = std::move(other.held_);
        other.path_.clear();
    }
    return *this;
}

void FileLock::release() noexcept {
    held_.reset();
}

std::optional<FileLock> FileLock::acquire(const std::filesystem::path& path,
                                          std::chrono::milliseconds wait, Refusal& refusal) {
    refusal = Refusal{};
#if defined(_WIN32)
    // Shared for reading, writing and deleting: the lock is the byte range
    // below, never the sharing mode, so a reader of the file is not refused.
    const HANDLE handle = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        refusal.error =
            "cannot open the lock " + path.string() + ": " +
            std::error_code{static_cast<int>(GetLastError()), std::system_category()}.message();
        return std::nullopt;
    }
    auto held = std::make_unique<Held>(handle);
#else
    const int descriptor = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        refusal.error = "cannot open the lock " + path.string() + ": " +
                        std::error_code{errno, std::generic_category()}.message();
        return std::nullopt;
    }
    auto held = std::make_unique<Held>(descriptor);
#endif

    const auto deadline = std::chrono::steady_clock::now() + wait;
    std::chrono::milliseconds backoff{2};
    while (true) {
#if defined(_WIN32)
        OVERLAPPED whole{};
        if (LockFileEx(held->handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                       &whole) != 0) {
            return FileLock{path, std::move(held)};
        }
        const DWORD failure = GetLastError();
        if (failure != ERROR_LOCK_VIOLATION && failure != ERROR_IO_PENDING) {
            refusal.error =
                "cannot lock " + path.string() + ": " +
                std::error_code{static_cast<int>(failure), std::system_category()}.message();
            return std::nullopt;
        }
#else
        if (::flock(held->descriptor, LOCK_EX | LOCK_NB) == 0) {
            return FileLock{path, std::move(held)};
        }
        const int failure = errno;
        if (failure == EINTR) {
            continue;
        }
        if (failure != EWOULDBLOCK) {
            refusal.error = "cannot lock " + path.string() + ": " +
                            std::error_code{failure, std::generic_category()}.message();
            return std::nullopt;
        }
#endif
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            refusal.timed_out = true;
            refusal.error =
                path.string() + " stayed locked for all of the " + spoken(wait) + " this waited";
            return std::nullopt;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        std::this_thread::sleep_for(backoff < left ? backoff : left);
        backoff = backoff * 2 < kLongestPause ? backoff * 2 : kLongestPause;
    }
}

}  // namespace apogee::platform
