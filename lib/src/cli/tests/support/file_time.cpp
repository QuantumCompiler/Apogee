#include "support/file_time.h"

#if defined(_WIN32)
#include <windows.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#endif

namespace apogee::testing {

#if defined(_WIN32)
namespace {

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

using Handle = std::unique_ptr<void, HandleCloser>;

/// `flags` FILE_FLAG_BACKUP_SEMANTICS is what lets CreateFileW open a
/// directory at all -- the flag `_wutime` does not pass.
Handle open_existing(const std::filesystem::path& path, DWORD access, DWORD flags) {
    return Handle{CreateFileW(path.c_str(), access,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, flags, nullptr)};
}

[[noreturn]] void fail(const char* what, const std::filesystem::path& path) {
    const auto error = static_cast<int>(GetLastError());
    throw std::filesystem::filesystem_error(what, path,
                                            std::error_code{error, std::system_category()});
}

}  // namespace
#endif

void set_modified_time(const std::filesystem::path& path, std::filesystem::file_time_type time) {
#if defined(_WIN32)
    std::error_code error;
    std::filesystem::last_write_time(path, time, error);
    if (!error) {
        return;
    }

    // A scratch file, outside the tree being aged: creating it beside `path`
    // would touch the time of the directory that holds it.
    static std::atomic<unsigned> counter{0};
    const std::filesystem::path probe =
        std::filesystem::temp_directory_path() /
        ("apogee-mtime-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(counter++));
    std::ofstream{probe} << "";
    std::filesystem::last_write_time(probe, time);

    FILETIME stamp{};
    {
        const Handle file = open_existing(probe, FILE_READ_ATTRIBUTES, 0);
        if (file.get() == INVALID_HANDLE_VALUE ||
            GetFileTime(file.get(), nullptr, nullptr, &stamp) == 0) {
            fail("cannot read a scratch file's time", probe);
        }
    }
    std::filesystem::remove(probe, error);

    const Handle target = open_existing(path, FILE_WRITE_ATTRIBUTES, FILE_FLAG_BACKUP_SEMANTICS);
    if (target.get() == INVALID_HANDLE_VALUE ||
        SetFileTime(target.get(), nullptr, nullptr, &stamp) == 0) {
        fail("cannot set file time", path);
    }
#else
    std::filesystem::last_write_time(path, time);
#endif
}

}  // namespace apogee::testing
