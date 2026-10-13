#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// A task recipe (40c): a named goal template with its acceptance checks and
/// suggested grants baked in -- what makes a task pre-shipped.
///
/// **A recipe is a hand-typed `task run` with a name.** It holds what the
/// command line would: the goal (a template over one `{{input}}`), the
/// `--require` and `--require-file` checks, `--tools`, `--rounds`, `--agent`
/// -- and the `--allow` grants it would need, which it only SUGGESTS: the
/// launch prints them and the person at the terminal consents to each, as
/// typing `--allow` would; with nobody there, nothing is granted that was
/// not given on the line. Expanded, a recipe run is the twin of the same
/// invocation typed by hand, its ledger carrying the recipe's name as
/// provenance and nothing else different.
///
/// **Where recipes come from** -- the symphonies' ladder (27q), without a
/// config section: a file under the data directory's `recipes/`, run by its
/// file's name, over the shipped starter of that name, whose seeded file is
/// read while it is there and its compiled-in text while it is not.
///
/// **Template grammar.** `{{input}}` (whitespace inside the braces allowed)
/// is `--input`'s text; nothing else in a goal is touched, and any other
/// `{{...}}` -- or an unclosed `{{` -- is refused at parse. The checks are
/// written as they are, never templated.
namespace apogee::tasks {

/// The one template variable a goal reads.
inline constexpr std::string_view kRecipeInput = "input";

/// Where a recipe comes from.
enum class RecipeSource : std::uint8_t {
    /// A shipped starter played from its compiled-in text: no file is there.
    Shipped,
    /// A shipped starter's seeded file, byte for byte the compiled-in text.
    Seeded,
    /// A file of the user's: one of their own, or a starter's file edited.
    User,
};

/// `shipped`, `seeded`, `user`.
[[nodiscard]] std::string_view to_string(RecipeSource source) noexcept;

/// One recipe, as its file says it.
struct Recipe {
    std::string name;
    std::string description;
    /// What `--input` is for; empty when the goal reads no input.
    std::string input;
    /// The goal template.
    std::string goal;
    /// `--tools`: whether the task's turns are offered tools.
    bool tools = false;
    /// `--require`, each the text the answer must contain.
    std::vector<std::string> require;
    /// `--require-file`, each a path where the task runs.
    std::vector<std::string> require_files;
    /// `--allow`, SUGGESTED: printed at launch and consented, never assumed.
    std::vector<std::string> allow;
    /// `--agent`, or empty.
    std::string agent;
    /// `--rounds`, or the command's default.
    std::optional<int> rounds;

    /// Whether the goal reads `{{input}}`.
    [[nodiscard]] bool takes_input() const;
};

/// `text` read as a recipe from `origin` (a file's path, or `starter <name>`
/// for compiled-in text), named `name` -- its file's name, which a `name:`
/// key must agree with. Throws `std::runtime_error` saying what is wrong,
/// naming `origin`: not YAML, an unknown key, no goal, a variable other than
/// `{{input}}`, an `input:` the goal never reads, a grant or an agent with
/// no tools, a round budget out of range, a check that is blank.
[[nodiscard]] Recipe parse_recipe(std::string_view text, std::string_view origin,
                                  std::string_view name);

/// The goal `recipe` expands to with `input`, or -- in `error` -- why it
/// cannot: an input a goal needs and was not given, or one it takes none of.
[[nodiscard]] std::string expand_goal(const Recipe& recipe, const std::optional<std::string>& input,
                                      std::string& error);

/// One recipe, and where it came from.
struct RecipeEntry {
    Recipe recipe;
    RecipeSource source = RecipeSource::Shipped;
    /// The file it was read from; empty for a starter's compiled-in text.
    std::filesystem::path path;
};

/// Every recipe by name, each once: the shipped starters by name (a file of
/// a starter's name standing in for it, `seeded` while it is the shipped
/// bytes and `user` once edited), then the other files under `dir` by name.
/// A file that does not parse is left out and said in `problems`.
struct RecipeBook {
    std::vector<RecipeEntry> recipes;
    std::vector<std::string> problems;

    /// The recipe named `name`, or nullptr.
    [[nodiscard]] const RecipeEntry* find(std::string_view name) const noexcept;
    /// The names, in the book's order, `, `-joined.
    [[nodiscard]] std::string names() const;
};

/// The book over `dir`, the data directory's `recipes/` (`harness::recipes_dir()`).
[[nodiscard]] RecipeBook recipe_book(const std::filesystem::path& dir);

/// One recipe as the `task recipes show --output-format json` document,
/// `{"object": "task.recipe", ...}`: every field the file holds, its source
/// and its path.
[[nodiscard]] nlohmann::json to_json(const RecipeEntry& entry);

/// The book as `task recipes --output-format json`'s document, the list
/// idiom: `{"object": "list", "data": [...], "problems": [...]}`, `data` in the
/// book's order, each recipe without its goal text.
[[nodiscard]] nlohmann::json to_json(const RecipeBook& book);

}  // namespace apogee::tasks
