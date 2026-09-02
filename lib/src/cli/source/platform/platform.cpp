#include "platform/platform.h"

#include <cstdlib>
#include <cstring>
#include <system_error>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

// The only place in Apogee where a platform `#ifdef` is expected. Everything
// else asks this header instead. An unrecognized platform is a hard compile
// error on purpose: silently degrading to "Unknown" would let an unsupported
// target build and then misbehave at runtime, which is exactly the failure the
// six-target matrix exists to prevent.

namespace apogee::platform {

OperatingSystem host_os() noexcept {
#if defined(__linux__)
    return OperatingSystem::Linux;
#elif defined(__APPLE__)
    return OperatingSystem::MacOS;
#elif defined(_WIN32)
    return OperatingSystem::Windows;
#else
#error "Apogee supports Linux, macOS, and Windows only -- see CLAUDE.md -> Stack & environment"
#endif
}

Architecture host_architecture() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return Architecture::Arm64;
#elif defined(__x86_64__) || defined(_M_X64)
    return Architecture::X64;
#else
#error "Apogee supports x86_64 and arm64 only -- see CLAUDE.md -> Stack & environment"
#endif
}

std::string_view to_string(OperatingSystem os) noexcept {
    switch (os) {
        case OperatingSystem::Linux:
            return "linux";
        case OperatingSystem::MacOS:
            return "macos";
        case OperatingSystem::Windows:
            return "windows";
    }
    return "unknown";
}

std::string_view to_string(Architecture arch) noexcept {
    switch (arch) {
        case Architecture::X64:
            return "x64";
        case Architecture::Arm64:
            return "arm64";
    }
    return "unknown";
}

std::filesystem::path executable_path() {
    // Three genuinely different mechanisms, which is why this lives behind the
    // seam rather than in the one caller that wants it.
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size()) {
            buffer.resize(written);
            return std::filesystem::path{buffer};
        }
        buffer.resize(buffer.size() * 2);  // truncated: ask again with more room
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);  // returns -1 and sets the size needed
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    buffer.resize(std::strlen(buffer.c_str()));
    std::error_code code;
    // Resolve symlinks: a Homebrew-style install is a link into a cellar, and
    // `apogee uninstall` must act on what it is really deleting.
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(buffer, code);
    return code ? std::filesystem::path{buffer} : resolved;
#else
    std::error_code code;
    const std::filesystem::path resolved = std::filesystem::read_symlink("/proc/self/exe", code);
    if (code) {
        return {};
    }
    return resolved;
#endif
}

std::optional<std::string> home_directory() {
#if defined(_WIN32)
    if (const char* profile = std::getenv("USERPROFILE"); profile != nullptr && *profile != '\0') {
        return std::string{profile};
    }
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive != nullptr && *drive != '\0' && path != nullptr && *path != '\0') {
        return std::string{drive} + path;
    }
    return std::nullopt;
#else
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::string{home};
    }
    return std::nullopt;
#endif
}

bool is_terminal(StandardStream stream) noexcept {
#if defined(_WIN32)
    int descriptor = 0;
    switch (stream) {
        case StandardStream::In:
            descriptor = 0;
            break;
        case StandardStream::Out:
            descriptor = 1;
            break;
        case StandardStream::Err:
            descriptor = 2;
            break;
    }
    return _isatty(descriptor) != 0;
#else
    int descriptor = STDIN_FILENO;
    switch (stream) {
        case StandardStream::In:
            descriptor = STDIN_FILENO;
            break;
        case StandardStream::Out:
            descriptor = STDOUT_FILENO;
            break;
        case StandardStream::Err:
            descriptor = STDERR_FILENO;
            break;
    }
    return ::isatty(descriptor) != 0;
#endif
}

std::optional<int> terminal_width() noexcept {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) != 0) {
        const int width = info.srWindow.Right - info.srWindow.Left + 1;
        return width > 0 ? std::optional<int>{width} : std::nullopt;
    }
    return std::nullopt;
#else
    ::winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return static_cast<int>(size.ws_col);
    }
    return std::nullopt;
#endif
}

void discard_pending_input() noexcept {
    if (!is_terminal(StandardStream::In)) {
        // A pipe or a heredoc: those bytes are the input, not typeahead.
        return;
    }
#if defined(_WIN32)
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
#else
    // Ignored on failure: this is an ergonomic nicety, not a precondition.
    static_cast<void>(::tcflush(STDIN_FILENO, TCIFLUSH));
#endif
}

std::string host_target() {
    std::string target{to_string(host_os())};
    target += "-";
    target += to_string(host_architecture());
    return target;
}

}  // namespace apogee::platform
