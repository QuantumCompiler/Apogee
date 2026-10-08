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
    /// `config` is the config file's text, at `config/<file>`: the older
    /// `config.yaml` unless a test names the layout's `config.json` (28i).
    explicit CliHome(const std::string& config, std::string file = "config.yaml");

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

    /// The same, with stdout in `out` and stderr in `err` apart -- for a
    /// command whose stdout must carry one thing only (a JSON document,
    /// machine mode's events).
    int run(const std::vector<std::string>& args, std::string* out, std::string* err) const;

    /// The same with no `--config`: the run finds its config the way an
    /// install does -- `config/config.json`, or the `config.yaml` an older
    /// install holds (28i).
    int run_default(const std::vector<std::string>& args, std::string* out, std::string* err) const;

private:
    int run_with(const std::vector<std::string>& args, bool named, std::string* out,
                 std::string* err) const;

    std::string file_;
    TempDir root_{"cli-home-" + std::to_string(std::random_device{}())};
    EnvGuard guard_{"APOGEE_HOME", root_.path().string()};
};

}  // namespace apogee::testing
