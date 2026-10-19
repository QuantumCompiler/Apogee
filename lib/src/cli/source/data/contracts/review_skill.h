#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// A review skill (48b): one lens a review runs over a change.
///
/// **A skill is a markdown file.** A front matter between `---` lines holds
/// its `description` and, optionally, its `applies` hints; the body below is
/// the hunt prompt a model reads with the diff. It is a prompt, not an
/// agent: no tools, no model, and no schema of its own -- every skill's
/// answer is held to the ONE finding contract (`review_finding_schema_text`),
/// the shipped `code-review` agent's findings, owned there once.
///
///     ---
///     description: Browser code -- XSS sinks, state races, ...
///     applies:
///       extensions: [.js, .tsx]
///       paths: ["**/components/**"]
///       languages: [typescript]
///     ---
///     You are reviewing one change through one lens: ...
///
/// **Applies-to hints are data, matched deterministically**: a skill applies
/// to a changed file when the file's extension is one it lists (directly or
/// through a language), or its path matches one of its globs; a skill with
/// no hints applies everywhere. Picking which skills a review runs is the
/// review's plan (48c); this only answers "does it apply to this file".
///
/// **Where skills come from** -- the recipes' ladder (40c): a file under the
/// data directory's `review-skills/`, run by its file's name, over the
/// shipped skill of that name, whose seeded file is read while it is there
/// and its compiled-in text while it is not. A dropped file is a new lens.
namespace apogee::harness {

/// A language a skill may name, and the extensions it stands for.
struct ReviewLanguage {
    std::string_view name;
    std::span<const std::string_view> extensions;
};

/// The languages `applies.languages` accepts, by name.
[[nodiscard]] std::span<const ReviewLanguage> review_skill_languages() noexcept;

struct ReviewSkillApplies {
    /// Lowercase, each with its leading dot.
    std::vector<std::string> extensions;
    /// Globs over repository-relative paths: `*` within a segment, `**`
    /// across segments, `?` one character.
    std::vector<std::string> paths;
    std::vector<std::string> languages;

    /// No hints at all: the skill applies to every file.
    [[nodiscard]] bool everywhere() const noexcept {
        return extensions.empty() && paths.empty() && languages.empty();
    }
};

struct ReviewSkill {
    /// The file's name, without `.md`.
    std::string name;
    std::string description;
    ReviewSkillApplies applies;
    /// The hunt prompt: the body after the front matter, trimmed.
    std::string prompt;
};

/// Where a skill comes from.
enum class ReviewSkillSource : std::uint8_t {
    /// A shipped skill read from its compiled-in text: no file is there.
    Shipped,
    /// A shipped skill's seeded file, byte for byte the compiled-in text.
    Seeded,
    /// A file of the user's: one of their own, or a shipped skill's edited.
    User,
};

/// `shipped`, `seeded`, `user`.
[[nodiscard]] std::string_view to_string(ReviewSkillSource source) noexcept;

/// `text` read as a skill from `origin` (a file's path, or `shipped <name>`),
/// named `name` -- its file's name, which a `name:` key must agree with.
/// Throws `std::runtime_error` naming `origin` and what is wrong: no front
/// matter, front matter that is not YAML or holds an unknown key, no
/// description, an extension without its dot, a path glob that is absolute
/// or climbs out (`..`), a language not in `review_skill_languages()`, an
/// empty prompt.
[[nodiscard]] ReviewSkill parse_review_skill(std::string_view text, std::string_view origin,
                                             std::string_view name);

/// Whether `glob` matches the repository-relative `path` (`/`-separated).
[[nodiscard]] bool review_glob_match(std::string_view glob, std::string_view path);

/// Whether `skill` applies to the changed file `path`.
[[nodiscard]] bool skill_applies(const ReviewSkill& skill, std::string_view path);

/// One skill, and where it came from.
struct ReviewSkillEntry {
    ReviewSkill skill;
    ReviewSkillSource source = ReviewSkillSource::Shipped;
    /// The file it was read from; empty for a shipped skill's compiled-in text.
    std::filesystem::path path;
};

/// Every skill by name, each once: the shipped skills by name (a file of a
/// shipped name standing in for it, `seeded` while it is the shipped bytes
/// and `user` once edited), then the other `.md` files under the directory
/// by name. A file that does not parse is left out and said in `problems`.
struct ReviewSkillBook {
    std::vector<ReviewSkillEntry> skills;
    std::vector<std::string> problems;

    [[nodiscard]] const ReviewSkillEntry* find(std::string_view name) const noexcept;
    /// The names, in the book's order, `, `-joined.
    [[nodiscard]] std::string names() const;
    /// The skills that apply to at least one of `paths`, in the book's order.
    [[nodiscard]] std::vector<const ReviewSkillEntry*> applicable(
        const std::vector<std::string>& paths) const;
};

/// The book over `dir`, the data directory's `review-skills/`
/// (`review_skills_dir()`).
[[nodiscard]] ReviewSkillBook review_skill_book(const std::filesystem::path& dir);

/// THE finding contract every skill's answer is held to, as JSON Schema
/// text: `{"findings": [...]}`, each finding exactly the shipped
/// `code-review` agent's -- `severity`, `kind`, `file`, `location`, `claim`,
/// `suggested_fix`, `confidence` -- read from that agent's compiled-in
/// schema, never restated. An empty list is a valid answer.
[[nodiscard]] const std::string& review_finding_schema_text();

/// One skill as `review skills show --output-format json`'s document,
/// `{"object": "review.skill", ...}`: its fields, its hints, its source and
/// path, and its prompt.
[[nodiscard]] nlohmann::json to_json(const ReviewSkillEntry& entry);

/// The book as `review skills list --output-format json`'s document:
/// `{"object": "list", "data": [...], "problems": [...]}`, each skill
/// without its prompt.
[[nodiscard]] nlohmann::json to_json(const ReviewSkillBook& book);

}  // namespace apogee::harness
