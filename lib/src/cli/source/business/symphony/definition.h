#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "contracts/symphony_walk.h"

/// A symphony's definition (27q): where it comes from, whether it is sound,
/// and how its templates read.
///
/// **A symphony is a declarative definition** -- a name, a description, an
/// input contract and ordered stages, each stage a suite ROLE, a prompt
/// template and optionally a schema and caps (`harness::SymphonySpec`, which
/// `contracts/config` parses from a `symphonies:` entry and a spec file
/// alike). This file is the deterministic half around the stage calls:
/// which definitions exist and in what order they shadow one another, the
/// template grammar, and the validation a definition passes before any
/// model is asked anything -- table-tested, the calls being the only
/// nondeterminism (`symphony/runner`).
///
/// **Template grammar.** `{{input}}` is the play's input text and
/// `{{<stage>}}` an EARLIER stage's answer, whitespace inside the braces
/// allowed. Nothing else in a prompt is touched, and anything else between
/// `{{` and `}}` -- an unknown name, a later stage, the stage itself, an
/// unclosed `{{` -- is refused when the definition is validated, never at
/// play time: there is no escape for a literal `{{`, so a prompt that needs
/// one says it some other way.
///
/// **Composition** (27r). A stage may play another symphony by name: a chain
/// is a symphony, defined, listed, created and played as one. What a play
/// stage reaches is walked here, against every source at once (the
/// catalog), before any model is asked anything: a loop is refused naming
/// it, a nesting past `symphony_caps.depth` naming the path, a played name
/// nothing defines naming the stage -- and each symphony reached is held to
/// its own rules and to what it is given.
namespace apogee::symphony {

/// The caps a stage takes when its definition sets none. A play's brief is
/// the whole of what the member reads -- the input among it -- so they are
/// roomier than a consult's (27f); the member's window stays the ceiling.
inline constexpr std::int64_t kStageBriefTokens = 4096;
inline constexpr std::int64_t kStageAnswerTokens = 1024;

/// The template variable the play's input is.
inline constexpr std::string_view kInputVariable = "input";

/// Where a definition comes from.
enum class Source : std::uint8_t {
    /// A shipped starter: its seeded file under `symphonies/`, or the
    /// compiled-in text while that is absent.
    Shipped,
    /// A `symphonies:` entry in the config.
    Config,
    /// A spec file: one under `symphonies/` played by its name, or one named
    /// by its path.
    File,
};

/// `shipped`, `config`, `file`.
[[nodiscard]] std::string_view to_string(Source source) noexcept;

/// One definition, and where it came from.
struct Definition {
    harness::SymphonySpec spec;
    Source source = Source::Config;
    /// The file it was read from; empty for a config entry and for a
    /// starter played from its compiled-in text.
    std::filesystem::path path;
    /// A config entry standing in for a starter or a spec file of its name.
    bool overrides = false;
};

/// One piece of a template: text as written, or a variable's name.
struct Segment {
    bool variable = false;
    std::string text;

    bool operator==(const Segment&) const = default;
};

/// `text` read into its segments. On a malformed variable -- an unclosed
/// `{{`, an empty or unnamed one -- returns nothing and says why in `error`.
[[nodiscard]] std::vector<Segment> parse_template(std::string_view text, std::string& error);

/// `text` with every variable replaced by its value in `values`. A variable
/// with no value is left as it was written -- validation has already
/// refused any a definition could reach.
[[nodiscard]] std::string render_template(std::string_view text,
                                          const std::map<std::string, std::string>& values);

/// The variables `text` names, in order, each once.
[[nodiscard]] std::vector<std::string> template_variables(std::string_view text);

/// The template stage `index` (0-based) of `spec` is rendered from: a role
/// stage's prompt; a play stage's `input:`, or when it has none (27r) the
/// previous stage's answer -- `{{<previous>}}`, or `{{input}}` for the first
/// stage -- so stages that each play a symphony chain without a word.
[[nodiscard]] std::string stage_template(const harness::SymphonySpec& spec, std::size_t index);

/// Every reason `spec` cannot be played on its own terms, in the order its
/// stages are read, each naming the stage: a malformed template, a variable
/// naming no earlier stage, a schema that is not a valid JSON Schema, a
/// symphony that never reads its input. Empty for a sound definition. The
/// parse's own rules -- roles, names, keys -- are `contracts/config`'s,
/// already held. What a play stage reaches is the catalog's to say: see the
/// overload below, which every surface calls.
[[nodiscard]] std::vector<std::string> validate(const harness::SymphonySpec& spec);

/// Every definition by name, each name once, a config entry winning over a
/// starter or a file of its name (and marked `overrides`): the starters
/// first, an entry standing in for one in its place -- its seeded file under
/// `dir` (the data directory's `symphonies/`) when there is one, else its
/// compiled-in text -- then the config's other entries, then the other spec
/// files under `dir`, each group by name. A file that does not parse is
/// left out of the list and said in `problems`.
struct Catalog {
    std::vector<Definition> definitions;
    std::vector<std::string> problems;
    /// How deep a play may nest (27r): the config's `symphony_caps.depth`.
    std::int64_t max_depth = harness::kSymphonyDepth;

    /// The definition of `name` (compared case-insensitively), or nullptr.
    [[nodiscard]] const Definition* find(std::string_view name) const noexcept;

    /// `find`, as the walk asks it.
    [[nodiscard]] harness::SymphonyLookup lookup() const;
};

[[nodiscard]] Catalog catalog(const harness::Config& config, const std::filesystem::path& dir);

/// `spec`'s plays walked through `catalog` (27r): its loop, its nesting past
/// the cap, a played name nothing defines -- the first, with its path --
/// and, when sound, how deep it nests and how many member calls one play of
/// it makes.
[[nodiscard]] harness::SymphonyWalk walk(const harness::SymphonySpec& spec, const Catalog& catalog);

/// Every reason `spec` cannot be played with the symphonies `catalog`
/// holds: its own (above), then the walk's (a loop, the depth, a name
/// nothing defines, each with its path), then each symphony it reaches --
/// one that cannot be played itself, one given an image it does not take or
/// not given one it does -- named by the symphony and stage that hold it.
/// What create, edit, play, show and list ask.
[[nodiscard]] std::vector<std::string> validate(const harness::SymphonySpec& spec,
                                                const Catalog& catalog);

/// What `find_definition` answers.
struct Found {
    std::optional<Definition> definition;
    /// Why there is none: no such name (with the ones there are), a file
    /// that cannot be read or does not parse.
    std::string error;
    /// Every definition the name was looked up among -- what the found
    /// one's play stages resolve against (27r).
    Catalog catalog;
};

/// The definition `name_or_path` names: a path to a spec file when it
/// looks like one (a separator, or a `.yaml`/`.yml` ending) and exists,
/// else a name, looked up as `catalog` orders them. `config`'s backends are
/// named to the parser, so a spec file's stage naming one is refused by it.
[[nodiscard]] Found find_definition(const harness::Config& config, const std::filesystem::path& dir,
                                    std::string_view name_or_path);

/// The spec files' directory for the config at `config_path`: its data
/// directory's `symphonies/` (`harness::home_for_config`), so a `--config`
/// tree stays hermetic as the agents' files do. One derivation, for the
/// command line and the admin plane alike.
[[nodiscard]] std::filesystem::path directory_for(const std::filesystem::path& config_path);

/// The roles a definition's stages play, in order: `utility → chat`; a
/// stage that plays a symphony as `play:<name>` (27r).
[[nodiscard]] std::string role_chain(const harness::SymphonySpec& spec);

}  // namespace apogee::symphony
