#include "tasks/recipe.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/assets.h"
#include "support/env_guard.h"
#include "tasks/task.h"

/// Task recipes (40c): the one parser and what it refuses, the goal's one
/// variable, and the source ladder -- a file under `recipes/` over the
/// shipped starter of its name, whose seeded file is read while it is there
/// and its compiled-in text while it is not.
namespace {

namespace fs = std::filesystem;
namespace t = apogee::tasks;
using Catch::Matchers::ContainsSubstring;

constexpr std::string_view kFull = R"YAML(name: tidy
description: Tidy a folder.
input:
  description: The folder.
goal: "Tidy {{ input }} and say done."
tools: true
require: ["tidied"]
require_files: [TIDY.md]
allow: [write_file, delete_file, write_file]
agent: careful
rounds: 3
)YAML";

/// What parsing `text` as the recipe `name` refuses with, or "" when it
/// parses.
std::string refusal(std::string_view text, std::string_view name = "r") {
    try {
        (void)t::parse_recipe(text, "r.yaml", name);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return {};
}

void write(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

}  // namespace

TEST_CASE("a recipe file parses into every field", "[tasks][recipe]") {
    const t::Recipe recipe = t::parse_recipe(kFull, "tidy.yaml", "tidy");
    CHECK(recipe.name == "tidy");
    CHECK(recipe.description == "Tidy a folder.");
    CHECK(recipe.input == "The folder.");
    CHECK(recipe.goal == "Tidy {{ input }} and say done.");
    CHECK(recipe.takes_input());
    CHECK(recipe.tools);
    CHECK(recipe.require == std::vector<std::string>{"tidied"});
    CHECK(recipe.require_files == std::vector<std::string>{"TIDY.md"});
    // The suggested grants as --allow takes them: sorted, each once.
    CHECK(recipe.allow == std::vector<std::string>{"delete_file", "write_file"});
    CHECK(recipe.agent == "careful");
    CHECK(recipe.rounds == 3);

    // The least a recipe is: a goal. Tools off, no checks, the default budget.
    const t::Recipe bare = t::parse_recipe("goal: Say hello.\n", "x", "bare");
    CHECK_FALSE(bare.tools);
    CHECK_FALSE(bare.takes_input());
    CHECK(bare.require.empty());
    CHECK_FALSE(bare.rounds.has_value());
}

TEST_CASE("what the parser refuses, by name and reason", "[tasks][recipe]") {
    CHECK(refusal("goal: x\n").empty());
    CHECK_THAT(refusal("goal: [x\n"), ContainsSubstring("r.yaml: not valid YAML"));
    CHECK_THAT(refusal(""), ContainsSubstring("the recipe is empty"));
    CHECK_THAT(refusal("- goal: x\n"), ContainsSubstring("expected a mapping at the top level"));
    CHECK_THAT(refusal("goal: x\nsteps: 3\n"),
               ContainsSubstring("'steps' is not a recipe key (name, description, input"));
    CHECK_THAT(refusal("description: no goal\n"), ContainsSubstring("a recipe needs a goal"));
    CHECK_THAT(refusal("goal: '   '\n"), ContainsSubstring("a recipe needs a goal"));
    // The goal's one variable is the input; anything else is refused at parse.
    CHECK_THAT(refusal("goal: 'Do {{folder}}'\ninput: {description: d}\n"),
               ContainsSubstring("'{{folder}}' is not a variable -- a goal reads only {{input}}"));
    CHECK_THAT(refusal("goal: 'Do {{input'\ninput: {description: d}\n"),
               ContainsSubstring("a '{{' is never closed"));
    CHECK_THAT(refusal("goal: 'Do {{}}'\n"), ContainsSubstring("'{{}}' is not a variable"));
    CHECK_THAT(refusal("goal: x\ninput: {description: d}\n"),
               ContainsSubstring("input: the goal never reads {{input}}"));
    CHECK_THAT(refusal("goal: 'Do {{input}}'\n"),
               ContainsSubstring("the goal reads {{input}}, so say what it is"));
    CHECK_THAT(refusal("goal: 'Do {{input}}'\ninput: {description: ' '}\n"),
               ContainsSubstring("say what the input is"));
    // Named by its file: a name key that disagrees, or a name that is no name.
    CHECK_THAT(refusal("name: other\ngoal: x\n"),
               ContainsSubstring("names itself 'other' -- a recipe runs by its file's name, 'r'"));
    CHECK_THAT(refusal("goal: x\n", "two words"), ContainsSubstring("is not a recipe name"));
    // A grant and an agent are over tools: without them, nothing would ask.
    CHECK_THAT(refusal("goal: x\nallow: [write_file]\n"),
               ContainsSubstring("allow: a grant needs tools (tools: true)"));
    CHECK_THAT(refusal("goal: x\nagent: careful\n"), ContainsSubstring("agent: an agent's policy"));
    CHECK_THAT(refusal("goal: x\ntools: maybe\n"),
               ContainsSubstring("tools: expected true or false"));
    CHECK_THAT(refusal("goal: x\nrounds: 0\n"),
               ContainsSubstring("rounds: 0 is out of range (1 to"));
    CHECK_THAT(refusal("goal: x\nrounds: 33\n"), ContainsSubstring("out of range"));
    CHECK_THAT(refusal("goal: x\nrounds: many\n"), ContainsSubstring("expected a whole number"));
    // A check is written as it is, and an empty one checks nothing.
    CHECK_THAT(refusal("goal: x\nrequire: ['']\n"),
               ContainsSubstring("require: an entry is empty"));
    CHECK_THAT(refusal("goal: x\nrequire_files: x\n"),
               ContainsSubstring("require_files: expected a list"));
    CHECK(refusal("goal: x\nrequire: []\n").empty());
}

TEST_CASE("a goal expands with its input, and only an input it takes", "[tasks][recipe]") {
    const t::Recipe recipe = t::parse_recipe(kFull, "tidy.yaml", "tidy");
    std::string error;
    CHECK(t::expand_goal(recipe, std::string{"docs/"}, error) == "Tidy docs/ and say done.");
    CHECK(error.empty());
    // The input is taken as written: braces in it are text, never read again.
    CHECK(t::expand_goal(recipe, std::string{"{{input}}"}, error) ==
          "Tidy {{input}} and say done.");
    (void)t::expand_goal(recipe, std::nullopt, error);
    CHECK(error == "the recipe 'tidy' takes an input -- The folder. -- given with --input");
    (void)t::expand_goal(recipe, std::string{"  "}, error);
    CHECK_THAT(error, ContainsSubstring("takes an input"));

    const t::Recipe none = t::parse_recipe("goal: Say hello.\n", "x", "hello");
    CHECK(t::expand_goal(none, std::nullopt, error) == "Say hello.");
    CHECK(error.empty());
    (void)t::expand_goal(none, std::string{"x"}, error);
    CHECK(error == "the recipe 'hello' takes no input, and --input gave one");
}

TEST_CASE("the shipped recipes parse, each its checks and suggestions as specced",
          "[tasks][recipe][assets]") {
    REQUIRE(apogee::harness::bundled_recipes().size() == 3);
    for (const apogee::harness::BundledRecipe& starter : apogee::harness::bundled_recipes()) {
        INFO(starter.name);
        const t::Recipe recipe =
            t::parse_recipe(starter.text, "starter " + std::string{starter.name}, starter.name);
        CHECK_FALSE(recipe.description.empty());
        CHECK(recipe.takes_input());
    }
    const auto recipe = [](std::string_view name) {
        return t::parse_recipe(apogee::harness::find_bundled_recipe(name)->text, "x", name);
    };
    CHECK(recipe("digest-folder").require_files == std::vector<std::string>{"DIGEST.md"});
    CHECK(recipe("digest-folder").allow == std::vector<std::string>{"write_file"});
    CHECK(recipe("changelog-draft").require == std::vector<std::string>{"## Unreleased"});
    CHECK_FALSE(recipe("changelog-draft").tools);
    CHECK(recipe("capture-decisions").allow ==
          std::vector<std::string>{"run_command", "write_file"});
    CHECK(recipe("capture-decisions").require == std::vector<std::string>{"Captured:"});
    CHECK(apogee::harness::bundled_recipe_relative_path("digest-folder") ==
          "recipes/digest-folder.yaml");
}

TEST_CASE("the ladder: a file over the starter of its name, seeded or edited, nearest wins",
          "[tasks][recipe][ladder]") {
    const apogee::testing::TempDir home{"recipes-" + std::to_string(std::random_device{}())};
    const fs::path dir = home.path() / "recipes";

    SECTION("no directory: the starters alone, from their compiled-in text") {
        const t::RecipeBook book = t::recipe_book(dir);
        REQUIRE(book.recipes.size() == apogee::harness::bundled_recipes().size());
        CHECK(book.problems.empty());
        for (const t::RecipeEntry& entry : book.recipes) {
            CHECK(entry.source == t::RecipeSource::Shipped);
            CHECK(entry.path.empty());
        }
        CHECK(book.names() == "capture-decisions, changelog-draft, digest-folder");
    }
    SECTION("seeded, edited, the user's own, and a broken file") {
        // A seeded starter, byte for byte: seeded, read from its file.
        write(dir / "digest-folder.yaml",
              apogee::harness::find_bundled_recipe("digest-folder")->text);
        // An edited starter stands in for it as the user's.
        write(dir / "changelog-draft.yaml", "description: mine\ngoal: Write it.\n");
        // A recipe of the user's own, and one that does not parse.
        write(dir / "tidy.yaml", kFull.substr(kFull.find('\n') + 1));
        write(dir / "broken.yml", "goal: [\n");
        write(dir / "notes.txt", "not a recipe");
        const t::RecipeBook book = t::recipe_book(dir);
        std::vector<std::string> names;
        for (const t::RecipeEntry& entry : book.recipes) {
            names.push_back(entry.recipe.name + ":" + std::string{t::to_string(entry.source)});
        }
        // The starters by name, then the user's own by name.
        CHECK(names == std::vector<std::string>{"capture-decisions:shipped", "changelog-draft:user",
                                                "digest-folder:seeded", "tidy:user"});
        CHECK(book.find("changelog-draft")->recipe.description == "mine");
        CHECK(book.find("changelog-draft")->path == dir / "changelog-draft.yaml");
        CHECK(book.find("digest-folder")->path == dir / "digest-folder.yaml");
        CHECK(book.find("tidy")->recipe.allow ==
              std::vector<std::string>{"delete_file", "write_file"});
        CHECK(book.find("broken") == nullptr);
        REQUIRE(book.problems.size() == 1);
        CHECK_THAT(book.problems.front(), ContainsSubstring("broken.yml: not valid YAML"));
    }
    SECTION("a starter's file that does not parse is said, never silently replaced") {
        write(dir / "digest-folder.yaml", "goal: 'Do {{what}}'\n");
        const t::RecipeBook book = t::recipe_book(dir);
        CHECK(book.find("digest-folder") == nullptr);
        REQUIRE(book.problems.size() == 1);
        CHECK_THAT(book.problems.front(),
                   ContainsSubstring("digest-folder.yaml: goal: '{{what}}'"));
    }
}

TEST_CASE("the recipes' documents: the book's rows, and one recipe whole", "[tasks][recipe]") {
    const apogee::testing::TempDir home{"recipe-docs-" + std::to_string(std::random_device{}())};
    write(home.path() / "tidy.yaml", kFull.substr(kFull.find('\n') + 1));
    const t::RecipeBook book = t::recipe_book(home.path());
    const nlohmann::json listed = t::to_json(book);
    CHECK(listed.at("object") == "list");
    REQUIRE(listed.at("data").size() == 4);
    CHECK(listed.at("problems").empty());
    const nlohmann::json& tidy = listed.at("data").back();
    CHECK(tidy.at("name") == "tidy");
    CHECK(tidy.at("source") == "user");
    CHECK(tidy.at("path") == (home.path() / "tidy.yaml").string());
    CHECK(tidy.at("allow") == nlohmann::json{"delete_file", "write_file"});
    CHECK(tidy.at("rounds") == 3);
    CHECK_FALSE(tidy.contains("goal"));  // the list leaves the goal to `show`
    CHECK(listed.at("data").front().at("path").is_null());

    const nlohmann::json shown = t::to_json(*book.find("tidy"));
    CHECK(shown.at("object") == "task.recipe");
    CHECK(shown.at("goal") == "Tidy {{ input }} and say done.");
    CHECK(shown.at("input") == "The folder.");
    CHECK(shown.at("agent") == "careful");
    CHECK(t::to_json(*book.find("changelog-draft")).at("rounds") == t::kDefaultRounds);
}
