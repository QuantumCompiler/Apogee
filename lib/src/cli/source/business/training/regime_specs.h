#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"

/// The regime specs a run can name (42a): the training track's "suites" on
/// the symphonies' ladder (27q).
///
/// **A regime by name is, nearest first:** a `training.regimes:` entry in the
/// config; a spec file under the data directory's `training/regimes/` -- a
/// shipped one's seeded copy, or one of the user's own -- run by its file's
/// name; the shipped spec's compiled-in text while no file is there. A
/// shipped spec names kits and tuning only, never a teacher or a student:
/// those are always the user's, named at run time or in their own entry.
namespace apogee::training {

/// Where a regime comes from.
enum class RegimeSource : std::uint8_t {
    /// A shipped spec from its compiled-in text: no file is there.
    Shipped,
    /// A shipped spec's seeded file, byte for byte the compiled-in text.
    Seeded,
    /// The user's: a config entry, a file of their own, or a shipped
    /// spec's file edited.
    User,
};

/// `shipped`, `seeded`, `user`.
[[nodiscard]] std::string_view to_string(RegimeSource source) noexcept;

/// One regime, and where it came from.
struct RegimeEntry {
    harness::RegimeSpec spec;
    RegimeSource source = RegimeSource::Shipped;
    /// The file it was read from; empty for a config entry and for a
    /// shipped spec's compiled-in text.
    std::filesystem::path path;
    /// A `training.regimes:` entry.
    bool config = false;
};

/// Every regime by name, each once: the shipped ones by name (an entry or a
/// file of a shipped name standing in for it), then the config's other
/// entries by name, then the other files under `dir` by name. A file that
/// does not parse is left out and said in `problems`.
struct RegimeBook {
    std::vector<RegimeEntry> regimes;
    std::vector<std::string> problems;

    /// The regime named `name`, or nullptr.
    [[nodiscard]] const RegimeEntry* find(std::string_view name) const noexcept;
    /// The names, in the book's order, `, `-joined.
    [[nodiscard]] std::string names() const;
};

/// The book over `config`'s entries and `dir`, the data directory's
/// `training/regimes/`.
[[nodiscard]] RegimeBook regime_book(const harness::Config& config,
                                     const std::filesystem::path& dir);

/// One regime as `train regime show --output-format json` prints it:
/// `{"object": "training.regime", name, description, source, path, kits,
/// count, iters, temperature, teacher, student, promote_as}` -- the last
/// three empty for anything shipped.
[[nodiscard]] nlohmann::json to_json(const RegimeEntry& entry);

/// The book as `train regime list --output-format json` prints it, the list
/// idiom: `{"object": "list", "data": [...], "problems": [...]}`.
[[nodiscard]] nlohmann::json to_json(const RegimeBook& book);

}  // namespace apogee::training
