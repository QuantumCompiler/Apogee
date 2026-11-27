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
/// its place, blank lines kept. The text is read as the loader reads it
/// (CLI-M47): past a UTF-8 byte-order mark, which the JSONC does not carry,
/// and its first document alone. Every line is carried once: a comment on a
/// line that writes nothing -- a `---`, `...` or `%YAML` line, a tag or
/// anchor alone, a dash alone, a value carried onto the next line -- goes on
/// a `//` line of its own where the same comment alone on its line would; a
/// container's closing comments stay in it by their indentation, past a
/// block scalar's text or a value carried on. Verified before it is
/// returned: the JSONC holds exactly the YAML's values and every comment,
/// once. Throws ConfigError, naming the line, when the YAML cannot be carried
/// exactly -- a quoted value or flow list spread over several lines, a key
/// that is not a plain scalar or is written twice in one mapping, a later
/// document that holds content (`a second YAML document starts here`), a
/// comment with no place -- and naming the encoding for a UTF-16 or UTF-32
/// text or one holding a NUL byte (`save it as UTF-8 and run it again`);
/// never guessing.
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
/// nothing, when the file is not UTF-8 (named before the loader reads it,
/// CLI-M47), when the JSONC file already exists (both named: which is
/// current is the user's call), when the backup name is taken, when the file
/// is already JSONC, when it does not load, or when `migrate_config_text`
/// refuses it. Throws ConfigEditError, or that refusal's ConfigError.
[[nodiscard]] MigrationReport migrate_config_file(const std::filesystem::path& path);

/// An option the starter config teaches in a comment alone (55g): its
/// example commented out, as the template writes everything a fresh install
/// leaves unset, so no key says a file has it. Declared with the release
/// whose template first carried it, so `config upgrade` can bring its block
/// of comments to a file seeded before, and `check` count it as missing.
struct CommentedOption {
    /// The release whose starter config first carried it.
    std::string_view release{};
    /// What it teaches, as `config upgrade` and `check` name it.
    std::string_view option{};
    /// Its block's first line and its last, trimmed: the block is found in
    /// the template by them -- a bare `//` line just above the first with
    /// it -- and a file holding the first line anywhere has it.
    std::string_view opens{};
    std::string_view closes{};
    /// The key it teaches, when it is one (57f): looked for in the object the
    /// template holds the block in -- `default_video` in `models`, `review`
    /// at the top level -- so a file that writes the key itself, without the
    /// template's comment, has the option. Empty for one that names no one
    /// key (the `backends.*` examples), which only its first line marks.
    std::string_view key{};
};

/// The starter config's commented options, by release, oldest first (55g).
/// A release that adds one to the template declares it here, the test that
/// takes each block out of the template and upgrades it back holding the
/// declaration to the text.
[[nodiscard]] std::vector<CommentedOption> commented_options();

/// The options -- dotted paths -- `template_text` writes as keys and
/// `content` does not: a missing section counts once, never once per key in
/// it. Only a JSONC config is compared (the template's own format); a YAML
/// one gives nothing -- it migrates first. Since 55g also each commented
/// option `template_text` carries that `content` lacks and upgrade can place
/// -- one inside a missing section comes with it, so it counts with it; one
/// whose key the file writes is not lacking (57f), comment or none.
[[nodiscard]] std::vector<std::string> missing_template_options(std::string_view content,
                                                                std::string_view template_text);

/// `missing_template_options` against this build's template.
[[nodiscard]] std::vector<std::string> missing_template_options(std::string_view content);

/// A config caught up with a newer template.
struct Upgrade {
    std::string text;
    /// The options inserted, as dotted paths -- a commented option as it is
    /// declared -- in the order inserted.
    std::vector<std::string> added;
};

/// `content` with every option `template_text` has and it lacks inserted
/// where the template places it -- under the same parent, after the option
/// the template puts before it -- with the comment block that teaches it,
/// re-indented to the file's own. Then (55g) each commented option it lacks:
/// its block in the object the template holds it in, beside the line the
/// template has after it (else before it) when the file has that line once,
/// else last in that object; an object written on one line takes none, and
/// an option whose key that object already writes is the file's (57f), so
/// neither its example goes in nor is it named added. Nothing present is
/// reordered, rewritten or removed; a current config comes back unchanged
/// with nothing added.
[[nodiscard]] Upgrade upgrade_config_text(std::string_view content, std::string_view template_text);

}  // namespace apogee::harness
