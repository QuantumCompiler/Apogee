#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
#include "training/script_runner.h"

/// The Python environment Apogee owns: `<APOGEE_HOME>/training/venv/`.
///
/// **Never the system Python.** Training execution is Python -- `mlx_lm` on
/// Apple Silicon, `transformers`/`peft`/`trl`/`bitsandbytes` on CUDA, the
/// `datasets` library for Parquet -- and a C++ harness cannot assume any of
/// it is installed, nor install it into someone's system site-packages. So
/// the drivers run under a virtual environment created here, seeded from an
/// interpreter the user names (`training.python`) or `python3` on PATH, and
/// every package lands inside it.
///
/// **Created only when asked.** `apogee train setup` creates it explicitly;
/// the first command that needs it asks on a terminal and refuses on a pipe,
/// naming the command. Seeding the data directory never creates it: a fresh
/// install downloads nothing unasked, and a `pip install` is a download.
///
/// **What is installed is recorded.** `apogee.json` inside the environment
/// names the interpreter it was seeded from and every requirement set
/// installed since, so `check` and `train setup` can say what is there
/// without importing anything.
namespace apogee::training {

/// A named group of packages, installed together. Pinned as version FLOORS
/// rather than exact versions: an exact pin ages into an uninstallable one
/// the day it is yanked, and the drivers are written against the libraries'
/// stable surfaces.
enum class RequirementSet : std::uint8_t {
    /// `datasets`, for Parquet in `prepare_dataset.py`.
    Prepare,
    /// `mlx-lm`, the Apple Silicon trainer's stack (the run item).
    Mlx,
    /// The CUDA trainer's stack (the run item).
    Peft,
    /// The GGUF converter's stack (the run item).
    Convert,
};

[[nodiscard]] std::string_view to_string(RequirementSet set) noexcept;
[[nodiscard]] std::optional<RequirementSet> requirement_set_from_string(
    std::string_view name) noexcept;
/// Every set's spelling, in declaration order.
[[nodiscard]] std::span<const std::string_view> requirement_set_names() noexcept;
/// The pip requirement specifiers `set` installs.
[[nodiscard]] std::span<const std::string_view> packages_for(RequirementSet set) noexcept;

/// What the environment holds, read from `apogee.json` and the filesystem.
struct PythonEnvStatus {
    /// The interpreter exists.
    bool exists = false;
    std::filesystem::path interpreter;
    /// The interpreter it was seeded from.
    std::string base_python;
    std::string created_at;
    /// Requirement sets installed, by name.
    std::vector<std::string> sets;
    /// A record that could not be read, or a directory with no record.
    std::string error;

    [[nodiscard]] bool has(RequirementSet set) const noexcept;
};

class PythonEnv {
public:
    explicit PythonEnv(std::filesystem::path venv_dir,
                       CommandRunner runner = default_command_runner());

    [[nodiscard]] const std::filesystem::path& dir() const noexcept {
        return dir_;
    }

    /// `<dir>/bin/python`, or `<dir>/Scripts/python.exe` on Windows.
    [[nodiscard]] std::filesystem::path interpreter() const;

    [[nodiscard]] bool exists() const;

    [[nodiscard]] PythonEnvStatus status() const;

    /// Creates the environment from `base_python` (`python -m venv`). The
    /// error text, or empty. An existing environment is left alone.
    [[nodiscard]] std::string create(const std::filesystem::path& base_python,
                                     const harness::CancellationToken& cancellation = {});

    /// Installs `set` into the environment with its own pip and records it.
    /// The error text, or empty.
    [[nodiscard]] std::string install(RequirementSet set,
                                      const harness::CancellationToken& cancellation = {});

    /// The record's path: `<dir>/apogee.json`.
    [[nodiscard]] std::filesystem::path record_path() const;

private:
    std::filesystem::path dir_;
    CommandRunner runner_;
};

/// The interpreter an environment is seeded from: `configured` (with `${ENV}`
/// and `~` expanded) when non-empty, else `python3` then `python` on PATH.
/// Empty with `error` set when none is found or the configured one is not
/// there.
[[nodiscard]] std::filesystem::path locate_base_python(std::string_view configured,
                                                       std::string& error);

}  // namespace apogee::training
