#pragma once

#include <cstddef>
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

/// Whether `file` under `root` is a bundled asset -- a prompt, a schema, a
/// kit, a script -- whose bytes are still exactly the shipped text: Apogee's,
/// not the user's. A DIRECTORY answers true when everything beneath it does
/// (a fresh install's `training/kits/`). `uninstall` asks this so a fresh
/// install's seeded files do not read as user data; an edited one does.
[[nodiscard]] bool is_unmodified_bundled_asset(const std::filesystem::path& root,
                                               const std::filesystem::path& file);

/// A bundled training kit -- one skill's teacher synthesis spec and inline
/// eval suite -- compiled in like the agents, byte-identical to the shipped
/// `assets/training/kits/<name>.yaml`, and seeded skip-if-present under
/// `training/kits/`. Four ship; the two tool kits wait for the in-text tool
/// protocol (user decision, 2026-09-19).
struct BundledKit {
    std::string_view name;
    std::string_view text;
};

/// The four, in the order `apogee datasets kits` shows them.
[[nodiscard]] std::span<const BundledKit> bundled_kits();

/// A bundled Python driver -- `prepare_dataset.py`, `train_mlx.py`,
/// `train_peft.py` -- compiled in, byte-identical to `assets/training/<name>`,
/// and seeded skip-if-present under `training/scripts/` so a user's edit
/// survives an update and `check` can show the drift.
struct BundledScript {
    /// The path under `training/scripts/`.
    std::string_view name;
    std::string_view text;
};

/// The three drivers, in listing order.
[[nodiscard]] std::span<const BundledScript> bundled_training_scripts();

/// llama.cpp's HuggingFace -> GGUF converter, vendored verbatim at the
/// pinned revision (`third_party/llama.cpp-convert/`, its README naming
/// the revision): the entry script, its `conversion/` package and the chat
/// templates it reads by path. Compiled in like the drivers and seeded
/// under `training/scripts/convert/`; `apogee train promote` runs it under
/// the environment's interpreter. Names are paths under `training/scripts/`.
[[nodiscard]] std::span<const BundledScript> bundled_converter_files();

/// `training/scripts/convert`, relative to the data directory -- the tree
/// the converter files seed into, which the doctor checks as one row.
[[nodiscard]] std::string bundled_converter_relative_dir();

/// Every converter file version an earlier Apogee shipped and this one does
/// not, as `<name> <sha256>` with the name as in `bundled_converter_files`,
/// sorted. From `third_party/llama.cpp-convert/retired-digests.txt`, which
/// `scripts/vendor_llama_convert.py` extends before each re-vendor.
///
/// Why it exists: seeding is skip-if-present, so after a pin bump every
/// existing install would keep converting with the old llama.cpp's converter
/// -- which knows neither the new models nor the tensors the new runtime
/// expects (2026-09-23: Gemma 4 "unified" refused by the old one). A seeded
/// file matching an entry is an earlier Apogee's copy, not the user's edit,
/// and is safe to replace.
[[nodiscard]] std::span<const std::string_view> bundled_converter_retired();

/// A seeded converter tree, file by file, against this build's.
struct ConverterTreeState {
    /// Files this build ships that are not there.
    std::size_t missing = 0;
    /// Files an earlier Apogee shipped, unedited: to be updated, or removed
    /// when this build no longer ships them.
    std::size_t stale = 0;
    /// Anything else that differs: the user's, kept, and reported as drift.
    std::size_t edited = 0;

    [[nodiscard]] bool current() const noexcept {
        return missing == 0 && stale == 0 && edited == 0;
    }
};

/// `dir` is the tree (`<home>/training/scripts/convert`). `retired` is
/// `bundled_converter_retired()` but for tests.
[[nodiscard]] ConverterTreeState inspect_converter_tree(
    const std::filesystem::path& dir,
    std::span<const std::string_view> retired = bundled_converter_retired());

/// `training/kits/<name>.yaml` and `training/scripts/<name>`, relative to the
/// data directory -- the paths seeding writes and the commands read.
[[nodiscard]] std::string bundled_kit_relative_path(std::string_view name);
[[nodiscard]] std::string bundled_script_relative_path(std::string_view name);

/// Every file seeding materialises: the agents' prompts and schemas, the
/// kits, the drivers, the converter. ONE list, so the seeder, the unmodified check and the
/// doctor's drift row cannot disagree about what Apogee ships.
struct BundledFile {
    std::string relative_path;
    std::string_view content;
};

[[nodiscard]] std::vector<BundledFile> bundled_files();

struct AssetSeedResult {
    /// Files written, relative to the root. Absent files only.
    std::vector<std::string> created;
    /// Converter files an earlier Apogee seeded, replaced by this build's.
    std::vector<std::string> updated;
    /// Converter files an earlier Apogee seeded that this build no longer ships.
    std::vector<std::string> removed;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

/// Writes every bundled prompt and schema under `root` that is not already
/// there. Called by `seed_data_directory`, so `apogee check --fix` -- and
/// through it both installers -- is the only path that materialises them.
///
/// The converter tree first has its stale files brought up to this build
/// (`refresh_converter_tree`): skip-if-present protects an edit, and an
/// earlier Apogee's unedited copy is not one.
[[nodiscard]] AssetSeedResult seed_bundled_assets(const std::filesystem::path& root);

/// Replaces each stale file under the converter tree `dir` (see
/// `ConverterTreeState`) with this build's copy, or removes it when this
/// build no longer ships it, recording each under `result` relative to
/// `root`. Edited and missing files are left: the first are the user's, the
/// second are seeding's. `retired` is `bundled_converter_retired()` but for
/// tests.
void refresh_converter_tree(
    const std::filesystem::path& root, const std::filesystem::path& dir, AssetSeedResult& result,
    std::span<const std::string_view> retired = bundled_converter_retired());

}  // namespace apogee::harness
