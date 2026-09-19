#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// **The on-disk contract.** One declaration of what `~/.apogee/` contains,
/// read by every path that creates it and by the one that validates it.
///
/// This exists because Ommi's dominant early bug class was *silent install
/// drift*: `make install` seeded one thing, `install.sh` seeded another, the
/// updater a third, and `check` validated a fourth list — each correct when
/// written, and diverging one commit at a time. Nothing failed loudly; a fresh
/// install simply lacked a directory some command needed months later.
///
/// The fix is structural rather than procedural. The layout is declared **once,
/// here**, and every consumer enumerates it instead of restating it:
///
///   - `seed_data_directory()` creates exactly these entries (both installers
///     call the binary to do it, so neither shell script owns a copy of the
///     list),
///   - `apogee check` validates exactly these entries,
///   - the CI parity gate diffs two installs against each other.
///
/// Adding a directory means adding one row below. A contributor cannot forget
/// to teach the installer or the doctor about it, because neither one knows
/// anything the row does not say.
namespace apogee::harness {

/// One directory in the contract.
struct LayoutEntry {
    /// Path relative to `apogee_home()`.
    std::string_view relative_path;

    /// What lives here. Printed by `apogee check` and by `--explain`, so it is
    /// written for a user reading diagnostic output, not for a maintainer.
    std::string_view purpose;

    /// Whether the directory may hold secrets, and so must not be group- or
    /// world-readable. POSIX only; Windows has no equivalent and the mode
    /// check reports as skipped there rather than silently passing.
    bool private_mode = false;

    /// Whether the directory holds the user's own data — models they supplied,
    /// conversations they had. `apogee uninstall` prompts before removing a
    /// tree containing any of these, and never deletes one under `--yes`
    /// without having said so.
    bool user_data = false;
};

/// Every directory `~/.apogee/` must contain, in creation order.
[[nodiscard]] std::span<const LayoutEntry> data_directories() noexcept;

// --- Resolved paths ---------------------------------------------------------
//
// One accessor per entry. They exist so callers name a concept rather than
// concatenating a string: a literal "sessions" spelled in three files is the
// same drift this file exists to prevent, one level down.

[[nodiscard]] std::filesystem::path sessions_dir();
[[nodiscard]] std::filesystem::path logs_dir();
[[nodiscard]] std::filesystem::path models_dir();
[[nodiscard]] std::filesystem::path embeddings_dir();

/// `<APOGEE_HOME>/notes` -- the notes toolset's directory.
[[nodiscard]] std::filesystem::path notes_dir();

/// `<APOGEE_HOME>/mcp` -- scaffolded MCP servers, one directory each.
[[nodiscard]] std::filesystem::path mcp_servers_dir();

/// `<APOGEE_HOME>/prompts` -- agents' system prompts (`apogee analyze --agent`).
[[nodiscard]] std::filesystem::path prompts_dir();

/// `<APOGEE_HOME>/schemas` -- agents' output schemas.
[[nodiscard]] std::filesystem::path schemas_dir();

/// `<APOGEE_HOME>/analyses` -- saved `analyze` reports, one directory per agent.
[[nodiscard]] std::filesystem::path analyses_dir();

/// `<APOGEE_HOME>/knowledge` -- the knowledge layer's own files: the raw
/// conversations behind captured records live in `raw/` beneath it. Private:
/// a raw conversation may hold anything the user typed.
[[nodiscard]] std::filesystem::path knowledge_dir();

/// `<APOGEE_HOME>/knowledge/raw` -- one `<record id>.md` per archived
/// conversation. Created by the first capture, never seeded, never exported.
[[nodiscard]] std::filesystem::path knowledge_raw_dir();

/// `<APOGEE_HOME>/training` -- the training track's own files: prepared
/// datasets, the kits and Python drivers seeding materialises, and the
/// virtual environment Apogee owns. Private: a dataset mined from the user's
/// sessions holds their own words. The subdirectories are created by first
/// use or by seeding, never restated as rows.
[[nodiscard]] std::filesystem::path training_dir();
/// `training/datasets` -- trainer-ready JSONL, one file per dataset.
[[nodiscard]] std::filesystem::path training_datasets_dir();
/// `training/datasets/raw` -- downloaded dataset files, for `prepare`.
[[nodiscard]] std::filesystem::path training_raw_datasets_dir();
/// `training/kits` -- the seeded training kits, plus the user's own.
[[nodiscard]] std::filesystem::path training_kits_dir();
/// `training/scripts` -- the seeded Python drivers.
[[nodiscard]] std::filesystem::path training_scripts_dir();
/// `training/venv` -- the Python environment `apogee train setup` creates.
[[nodiscard]] std::filesystem::path training_venv_dir();
[[nodiscard]] std::filesystem::path cache_dir();

/// What `seed_data_directory()` did.
struct SeedResult {
    /// Directories -- and bundled asset files -- that did not exist and were
    /// created, relative to the root.
    std::vector<std::string> created;

    /// Non-empty on failure. Seeding is all-or-nothing from the caller's point
    /// of view: a partial layout is exactly the state the parity rule exists to
    /// prevent, so a failure is reported rather than half-applied silently.
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

/// Creates every directory in the contract under `root`, with the right modes.
/// Idempotent.
///
/// **This is the only implementation.** `apogee check --fix` calls it, and both
/// installers call `check --fix` -- so there is exactly one piece of code that
/// knows how to build the tree. An earlier version had a second copy inside the
/// doctor, and a deliberate drift injected into this one went completely
/// undetected: the copy the installers actually reached was still correct. Two
/// implementations of the layout is the bug, even when both are right.
///
/// **Deliberately does not write the config file.** That is `apogee config
/// init`'s job and goes through the one config mutation path; a second writer
/// here would be the same mistake in the one place the repo already has an
/// invariant about it.
///
/// It DOES materialise the bundled agents' prompt and schema files (see
/// `harness/assets.h`), skip-if-present: they are assets landing under the
/// data directory, and the parity rule says every install path seeds them
/// identically -- which is true by construction only when the one seeding
/// implementation does it.
[[nodiscard]] SeedResult seed_data_directory(const std::filesystem::path& root);

/// Seeds the tree at `apogee_home()`.
[[nodiscard]] SeedResult seed_data_directory();

/// Whether this platform can express "private to this user" as a file mode.
///
/// False on Windows, whose ACL model has no `0700`. Callers use it to report a
/// mode check as *skipped on this platform* rather than passing it vacuously —
/// a check that silently succeeds where it cannot run is worse than one that
/// says it did not run.
[[nodiscard]] bool supports_private_modes() noexcept;

}  // namespace apogee::harness
