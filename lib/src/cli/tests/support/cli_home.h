#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "support/env_guard.h"

namespace apogee::testing {

/// A throwaway install -- `APOGEE_HOME` pointed at a temp directory, a config
/// file in it -- and the real command tree run against it in process, its
/// output captured. For a test of what a command prints and writes, without
/// the binary.
class CliHome {
public:
    /// `config` is the config file's text.
    explicit CliHome(const std::string& config);

    [[nodiscard]] const std::filesystem::path& home() const noexcept {
        return root_.path();
    }

    [[nodiscard]] std::filesystem::path config_path() const;
    [[nodiscard]] std::filesystem::path models() const;
    [[nodiscard]] std::string config_text() const;

    /// `apogee --config <config> <args...>` against this install --
    /// `APOGEE_HOME` is this home's for the run, so two homes in one test do
    /// not read each other's store: its exit code, and what it wrote to
    /// stdout then stderr into `out`.
    int run(const std::vector<std::string>& args, std::string* out) const;

private:
    TempDir root_{"cli-home-" + std::to_string(std::random_device{}())};
    EnvGuard guard_{"APOGEE_HOME", root_.path().string()};
};

}  // namespace apogee::testing
