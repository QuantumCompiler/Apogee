#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "harness/config.h"

/// The bundled agents: three review workflows compiled into the binary --
/// `security-review`, `release-notes`, `merge-request` -- and materialised
/// as files under the data directory by the ONE seeding path, so both
/// installers get them for free and a user's edits survive an update.
///
/// **Compiled in, like the config template.** Nothing has to be found on
/// disk for `apogee analyze --agent security-review` to run: the prompt and
/// schema texts are here, byte-identical to the shipped files under
/// `lib/src/cli/assets/`, and a test fails the build if they drift.
///
/// **Seeded skip-if-present.** `seed_bundled_assets` writes each file only
/// when it is absent, which is what makes the on-disk copy the user's: an
/// edited prompt is read back on the next run and never overwritten by a
/// re-seed (Ommi's `refreshAssets` rule, kept).
///
/// **A config entry of the same name wins.** The bundled definition is the
/// default the file may override -- to pin a model, say -- and a bundled
/// agent whose files are missing falls back to the compiled-in text rather
/// than failing, so a fresh `config init` with no `check --fix` still runs.
namespace apogee::harness {

struct BundledAgent {
    std::string_view name;
    std::string_view description;
    /// The shipped `prompts/<name>.txt`, byte for byte.
    std::string_view prompt;
    /// The shipped `schemas/<name>-output.json`, byte for byte.
    std::string_view schema;
};

/// The three, in the order `analyze --list` shows them.
[[nodiscard]] std::span<const BundledAgent> bundled_agents() noexcept;

[[nodiscard]] const BundledAgent* find_bundled_agent(std::string_view name) noexcept;

/// The entry a bundled agent runs as when the config does not override it:
/// its files by relative path, a read-only tool policy, and its own
/// `save_subdir`.
[[nodiscard]] AgentConfig bundled_agent_config(const BundledAgent& agent);

/// `prompts/<name>.txt` and `schemas/<name>-output.json`, relative to the
/// data directory -- the paths the bundled entries and the scaffold write.
[[nodiscard]] std::string bundled_prompt_relative_path(std::string_view name);
[[nodiscard]] std::string bundled_schema_relative_path(std::string_view name);

/// The agent `name` resolves to: the config's entry when it has one, else
/// the bundled definition of that name, else nothing. `bundled_out`, when
/// non-null, receives the bundled definition the entry shadows or is, so a
/// caller can fall back to the compiled-in text for a missing file.
[[nodiscard]] std::optional<AgentConfig> resolve_agent(const Config& config, std::string_view name,
                                                       const BundledAgent** bundled_out = nullptr);

/// Every runnable agent by name: the bundled three first (unless the config
/// overrides one, in which case the entry is listed in its place), then the
/// rest of the config's entries in the map's order.
struct NamedAgent {
    std::string name;
    AgentConfig config;
    bool bundled = false;
    /// A config entry standing in for a bundled agent of the same name.
    bool overrides_bundled = false;
};

[[nodiscard]] std::vector<NamedAgent> all_agents(const Config& config);

/// Resolves an agent's prompt or schema path: `${ENV}` and `~` expanded,
/// and a relative path taken against `home` -- the data directory the
/// config lives in, so a `--config` temp tree stays hermetic.
[[nodiscard]] std::filesystem::path resolve_agent_path(const std::filesystem::path& home,
                                                       std::string_view path);

/// Whether `file` under `root` is a bundled prompt or schema whose bytes are
/// still exactly the shipped text -- Apogee's, not the user's. `uninstall`
/// asks this so a fresh install's seeded files do not read as user data;
/// an edited one does.
[[nodiscard]] bool is_unmodified_bundled_asset(const std::filesystem::path& root,
                                               const std::filesystem::path& file);

struct AssetSeedResult {
    /// Files written, relative to the root. Absent files only.
    std::vector<std::string> created;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

/// Writes every bundled prompt and schema under `root` that is not already
/// there. Called by `seed_data_directory`, so `apogee check --fix` -- and
/// through it both installers -- is the only path that materialises them.
[[nodiscard]] AssetSeedResult seed_bundled_assets(const std::filesystem::path& root);

}  // namespace apogee::harness
