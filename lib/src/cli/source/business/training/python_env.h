#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
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
    /// `mlx-vlm`: an `mlx` backend over a vision model reads images through
    /// it (27c). Installed on demand, never with `mlx`: only a vision model
    /// needs it, and without it such a model's images go to the vision role.
    MlxVlm,
    /// `torch` and `diffusers` with what a diffusion pipeline loads through
    /// (49b): what `video_generate.py` runs a pipeline on. Installed on
    /// demand -- asked for at a terminal, named on a pipe -- or by `train
    /// setup --with video`; several gigabytes, so never with anything else.
    Video,
    /// `kokoro` and what it speaks through (50a): what `voice_say.py` runs a
    /// voice model on. Installed on demand, as the video set is.
    Voice,
};

[[nodiscard]] std::string_view to_string(RequirementSet set) noexcept;
[[nodiscard]] std::optional<RequirementSet> requirement_set_from_string(
    std::string_view name) noexcept;
/// Every set's spelling, in declaration order.
[[nodiscard]] std::span<const std::string_view> requirement_set_names() noexcept;
/// The pip requirement specifiers `set` installs.
[[nodiscard]] std::span<const std::string_view> packages_for(RequirementSet set) noexcept;

/// Why `set` is not for a host with these facts (51) -- the table's own
/// column, so a set added to it says there where it runs: `mlx` and
/// `mlx-vlm` are Apple silicon's, `peft` an NVIDIA GPU's, every other set
/// any host's. Empty when it is for this one.
[[nodiscard]] std::string set_unfit_reason(RequirementSet set, bool apple_silicon, bool nvidia_smi);

/// A Python's version, as `sys.version_info` states it (M19).
struct PythonVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;

    /// `3.12.13`; empty for the zero value, which means unknown.
    [[nodiscard]] std::string str() const;

    [[nodiscard]] bool known() const noexcept {
        return major > 0;
    }

    /// `3.12.13` or `3.12`; nullopt for anything else.
    [[nodiscard]] static std::optional<PythonVersion> parse(std::string_view text);
    auto operator<=>(const PythonVersion&) const = default;
};

/// The Pythons a set's packages run on (M19): the `requires_python` PyPI
/// publishes for each package's current release, carried with the date they
/// were read -- ADR 0009's discipline for facts that go stale -- so a choice
/// is made before an environment exists, not after pip refuses. Python 3
/// minors; `below` 0 is no upper bound.
struct PythonRange {
    int at_least = 0;
    int below = 0;

    [[nodiscard]] bool contains(const PythonVersion& version) const noexcept;
    /// `>=3.11`, `>=3.10,<3.13`.
    [[nodiscard]] std::string describe() const;
};

/// One set's range, from the sets' table.
[[nodiscard]] PythonRange python_range(RequirementSet set) noexcept;
/// The range every one of `sets` runs on: their intersection.
[[nodiscard]] PythonRange python_range(std::span<const RequirementSet> sets) noexcept;
/// When the ranges were last read against PyPI.
[[nodiscard]] std::string_view python_ranges_reviewed() noexcept;

/// What the environment holds, read from `apogee.json` and the filesystem.
struct PythonEnvStatus {
    /// The interpreter exists.
    bool exists = false;
    std::filesystem::path interpreter;
    /// The interpreter it was seeded from.
    std::string base_python;
    /// Its version: the record's `base_version`, else `pyvenv.cfg`'s
    /// `version` (written by `python -m venv`) -- read, never asked of Python.
    PythonVersion base_version;
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

    /// Creates the environment from `base_python` (`python -m venv`),
    /// recording its version when known. The error text, or empty. An
    /// existing environment is left alone.
    [[nodiscard]] std::string create(const std::filesystem::path& base_python,
                                     const harness::CancellationToken& cancellation = {},
                                     const PythonVersion& version = {});

    /// Installs `set` into the environment with its own pip and records it.
    /// The error text, or empty. An environment whose Python the set does
    /// not run on is refused before pip, naming the rebuild (M19).
    [[nodiscard]] std::string install(RequirementSet set,
                                      const harness::CancellationToken& cancellation = {});

    /// Why `set` cannot go in this environment as it is -- its Python is
    /// outside the set's range -- naming the rebuild; empty when it can, or
    /// when the version is not known. `install` and every offer to install
    /// ask it first, so nothing is offered that would fail.
    [[nodiscard]] std::string unfit_reason(RequirementSet set) const;

    /// Replaces the environment with one made from `base_python`, every set
    /// the record lists reinstalled in its order (M19). The old one is moved
    /// aside first and put back if anything fails, so a failed rebuild
    /// leaves what was there. Asking the person is the caller's. The error
    /// text, or empty.
    [[nodiscard]] std::string rebuild(const std::filesystem::path& base_python,
                                      const PythonVersion& version,
                                      const harness::CancellationToken& cancellation = {});

    /// The record's path: `<dir>/apogee.json`.
    [[nodiscard]] std::filesystem::path record_path() const;

private:
    std::filesystem::path dir_;
    CommandRunner runner_;
};

/// `<venv>`'s installed package `name` -- its directory in the
/// environment's site-packages (`lib/python*/site-packages/<name>/`, or
/// `Lib/site-packages/<name>/` on Windows) holding an `__init__.py` -- or
/// empty. Read from files, never by importing anything (27a's rule).
[[nodiscard]] std::filesystem::path find_site_package(const std::filesystem::path& venv,
                                                      std::string_view name);

/// The version a package's distribution record names in its own directory
/// name -- `diffusers-0.35.1.dist-info` beside `diffusers/` -- or empty.
[[nodiscard]] std::string installed_package_version(const std::filesystem::path& package);

/// The interpreter an environment is seeded from: `configured` (with `${ENV}`
/// and `~` expanded) when non-empty, else `python3` then `python` on PATH.
/// Empty with `error` set when none is found or the configured one is not
/// there.
/// `source` names where `configured` came from in a refusal.
[[nodiscard]] std::filesystem::path locate_base_python(std::string_view configured,
                                                       std::string& error,
                                                       std::string_view source = "training.python");

/// `interpreter`'s version, asked through `runner` (`-c "import sys; ..."`);
/// the zero value when it does not answer.
[[nodiscard]] PythonVersion probe_python_version(const std::filesystem::path& interpreter,
                                                 const CommandRunner& runner);

/// An interpreter on this machine and its version.
struct PythonCandidate {
    std::filesystem::path path;
    PythonVersion version;
};

/// The interpreters on PATH by name (M19): `python3.<minor>` for 3.10 to
/// 3.30, newest first, then `python3`, then `python` -- each asked its
/// version through `runner`, one reached by two names listed once. No
/// install prefix beyond PATH is searched and nothing is installed.
[[nodiscard]] std::vector<PythonCandidate> python_candidates(const CommandRunner& runner);

/// The interpreter a new environment is made from, and why (M19).
struct PythonChoice {
    std::filesystem::path path;
    PythonVersion version;
    /// Why this one, in the words setup says.
    std::string reason;
    /// Why none: set exactly when `path` is empty.
    std::string error;
};

/// The choice. `configured` (`training.python`, or `--python` for the run)
/// is the person's word, used as given -- one whose version is outside
/// `requested`'s range is refused naming both, never swapped. Otherwise the
/// newest candidate every set in `host` runs on (what this host can install:
/// one environment serves them all, and the bootstrap makes it at its first
/// set); else the newest `requested` runs on, naming what it leaves out;
/// else refused, naming the range, what was found and where an interpreter
/// comes from.
[[nodiscard]] PythonChoice choose_base_python(std::string_view configured,
                                              std::span<const RequirementSet> host,
                                              std::span<const RequirementSet> requested,
                                              const CommandRunner& runner,
                                              std::string_view source = "training.python");

}  // namespace apogee::training
