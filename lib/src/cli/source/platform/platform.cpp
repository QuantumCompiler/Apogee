#include "platform/platform.h"

#include <cstdlib>

#if defined(_WIN32)
#include <io.h>
#else
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

std::string host_target() {
    std::string target{to_string(host_os())};
    target += "-";
    target += to_string(host_architecture());
    return target;
}

}  // namespace apogee::platform
