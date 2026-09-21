#include "support/env_guard.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

namespace apogee::testing {
namespace {

std::optional<std::string> read_env(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr) {
        return std::string{value};
    }
    return std::nullopt;
}

void write_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void clear_env(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

void restore(const char* name, const std::optional<std::string>& previous) {
    if (previous.has_value()) {
        write_env(name, previous->c_str());
    } else {
        clear_env(name);
    }
}

}  // namespace

EnvGuard::EnvGuard(std::string name, const std::string& value) : name_{std::move(name)} {
    previous_ = read_env(name_.c_str());
    write_env(name_.c_str(), value.c_str());
}

EnvGuard::~EnvGuard() {
    restore(name_.c_str(), previous_);
}

EnvUnsetGuard::EnvUnsetGuard(std::string name) : name_{std::move(name)} {
    previous_ = read_env(name_.c_str());
    clear_env(name_.c_str());
}

EnvUnsetGuard::~EnvUnsetGuard() {
    restore(name_.c_str(), previous_);
}

TempDir::TempDir(const std::string& label) {
    // A per-process counter rather than a random name: reproducible across
    // runs, and unique enough because the directory is removed immediately.
    static std::atomic<unsigned> counter{0};
    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(ec) /
        ("apogee-test-" + label + "-" + std::to_string(counter.fetch_add(1)));
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    path_ = root;
}

TempDir::~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

}  // namespace apogee::testing
