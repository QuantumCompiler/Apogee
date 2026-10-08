#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// The config's move from YAML to JSONC (28i), and its catching up with a
/// newer template -- both explicit acts, run only on the user's word
/// (`config migrate`, `config upgrade`), never by an installer or a first
/// run (ADR install-mode-stability).
namespace apogee::harness {

/// A YAML config converted.
struct Migration {
    /// The JSONC text: two-space indentation, keys in the order written,
    /// every comment carried as a `//` comment where it stood.
    std::string text;
    /// How many comments were carried -- every one the YAML held.
    std::size_t comments = 0;
};

/// The legacy YAML config as JSONC, losslessly: every key in its order,
/// every value as YAML read it (`${ENV}` references literally, never
/// expanded), every comment -- whole-line and trailing -- carried as `//` at
/// its place, blank lines kept. Verified before it is returned: the JSONC
/// holds exactly the YAML's values and exactly its comments. Throws
/// ConfigError when the YAML cannot be carried exactly -- a quoted value or
/// flow list spread over several lines, a key that is not a plain scalar --
/// naming the line, rather than guessing.
[[nodiscard]] Migration migrate_config_text(std::string_view yaml);

/// What `config migrate` did on disk.
struct MigrationReport {
    std::filesystem::path from;    ///< the YAML config read
    std::filesystem::path to;      ///< the JSONC config written
    std::filesystem::path backup;  ///< the original, byte for byte
    std::size_t comments = 0;
    std::size_t keys = 0;  ///< top-level sections carried
};

/// Converts the YAML config at `path` to JSONC beside it: `<stem>.json`
/// written atomically, then the original moved to `<file>.bak` -- the JSONC
/// first, so an interruption leaves the original in place. Refuses, touching
/// nothing, when the JSONC file already exists (both named: which is current
/// is the user's call), when the backup name is taken, when the file is
/// already JSONC, or when it does not load. Throws ConfigEditError.
[[nodiscard]] MigrationReport migrate_config_file(const std::filesystem::path& path);

/// The options -- dotted paths -- `template_text` writes as keys and
/// `content` does not: a missing section counts once, never once per key in
/// it. Only a JSONC config is compared (the template's own format); a YAML
/// one gives nothing -- it migrates first.
[[nodiscard]] std::vector<std::string> missing_template_options(std::string_view content,
                                                                std::string_view template_text);

/// `missing_template_options` against this build's template.
[[nodiscard]] std::vector<std::string> missing_template_options(std::string_view content);

/// A config caught up with a newer template.
struct Upgrade {
    std::string text;
    /// The options inserted, as dotted paths, in the order inserted.
    std::vector<std::string> added;
};

/// `content` with every option `template_text` has and it lacks inserted
/// where the template places it -- under the same parent, after the option
/// the template puts before it -- with the comment block that teaches it,
/// re-indented to the file's own. Nothing present is reordered, rewritten or
/// removed; a current config comes back unchanged with nothing added.
[[nodiscard]] Upgrade upgrade_config_text(std::string_view content, std::string_view template_text);

}  // namespace apogee::harness
