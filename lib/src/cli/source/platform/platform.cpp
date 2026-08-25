#include "platform/platform.h"

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

std::string host_target() {
    std::string target{to_string(host_os())};
    target += "-";
    target += to_string(host_architecture());
    return target;
}

}  // namespace apogee::platform
