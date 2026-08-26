#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace apogee::testing {

/// Sets an environment variable for the life of the object, restoring the
/// previous value (or unsetting it) on destruction.
///
/// Config resolution reads APOGEE_HOME from the environment, so exercising it
/// means mutating the environment -- and a test that leaked such a change
/// would silently steer every test that ran after it. RAII makes the restore
/// unmissable, including on the throwing paths these tests deliberately take.
class EnvGuard {
public:
    EnvGuard(std::string name, const std::string& value);
    ~EnvGuard();

    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;
    EnvGuard(EnvGuard&&) = delete;
    EnvGuard& operator=(EnvGuard&&) = delete;

private:
    std::string name_;
    std::optional<std::string> previous_;
};

/// Unsets a variable for the life of the object, restoring it afterwards.
class EnvUnsetGuard {
public:
    explicit EnvUnsetGuard(std::string name);
    ~EnvUnsetGuard();

    EnvUnsetGuard(const EnvUnsetGuard&) = delete;
    EnvUnsetGuard& operator=(const EnvUnsetGuard&) = delete;
    EnvUnsetGuard(EnvUnsetGuard&&) = delete;
    EnvUnsetGuard& operator=(EnvUnsetGuard&&) = delete;

private:
    std::string name_;
    std::optional<std::string> previous_;
};

/// A directory under the system temp root, removed on destruction.
class TempDir {
public:
    explicit TempDir(const std::string& label);
    ~TempDir();

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }

private:
    std::string path_;
};

}  // namespace apogee::testing
