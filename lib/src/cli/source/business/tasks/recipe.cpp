#include "tasks/recipe.h"

#include <nlohmann/json.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "contracts/assets.h"
#include "tasks/policy.h"
#include "tasks/task.h"

namespace apogee::tasks {
namespace {

[[noreturn]] void fail(std::string_view origin, const std::string& why) {
    throw std::runtime_error(std::string{origin} + ": " + why);
}

[[nodiscard]] bool blank(std::string_view text) {
    return std::ranges::all_of(text, [](unsigned char c) { return std::isspace(c) != 0; });
}

[[nodiscard]] bool valid_name(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-';
    });
}

[[nodiscard]] std::string scalar(const YAML::Node& node, std::string_view origin,
                                 std::string_view key) {
    if (!node.IsDefined() || node.IsNull()) {
        return {};
    }
    if (!node.IsScalar()) {
        fail(origin, std::string{key} + ": expected text");
    }
    return node.as<std::string>();
}

[[nodiscard]] std::vector<std::string> texts(const YAML::Node& node, std::string_view origin,
                                             std::string_view key) {
    std::vector<std::string> out;
    if (!node.IsDefined() || node.IsNull()) {
        return out;
    }
    if (!node.IsSequence()) {
        fail(origin, std::string{key} + ": expected a list");
    }
    for (const YAML::Node& item : node) {
        const std::string text = scalar(item, origin, key);
        if (blank(text)) {
            fail(origin, std::string{key} + ": an entry is empty");
        }
        out.push_back(text);
    }
    return out;
}

/// The variable names `goal` holds, each `{{...}}` trimmed; throws on an
/// unclosed `{{` or an empty name.
[[nodiscard]] std::vector<std::string> variables(std::string_view goal, std::string_view origin) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while ((at = goal.find("{{", at)) != std::string_view::npos) {
        const std::size_t close = goal.find("}}", at + 2);
        if (close == std::string_view::npos) {
            fail(origin, "goal: a '{{' is never closed -- a goal reads only {{input}}");
        }
        std::string_view inside = goal.substr(at + 2, close - at - 2);
        while (!inside.empty() && std::isspace(static_cast<unsigned char>(inside.front())) != 0) {
            inside.remove_prefix(1);
        }
        while (!inside.empty() && std::isspace(static_cast<unsigned char>(inside.back())) != 0) {
            inside.remove_suffix(1);
        }
        out.emplace_back(inside);
        at = close + 2;
    }
    return out;
}

/// `goal` with every `{{input}}` replaced by `input`.
[[nodiscard]] std::string render(std::string_view goal, std::string_view input) {
    std::string out;
    std::size_t at = 0;
    while (true) {
        const std::size_t open = goal.find("{{", at);
        if (open == std::string_view::npos) {
            out.append(goal.substr(at));
            return out;
        }
        const std::size_t close = goal.find("}}", open + 2);
        out.append(goal.substr(at, open - at));
        out.append(input);
        at = close + 2;
    }
}

constexpr std::array kKeys{"name",    "description",   "input", "goal",  "tools",
                           "require", "require_files", "allow", "agent", "rounds"};

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The `.yaml` and `.yml` files directly under `dir`, by name.
[[nodiscard]] std::map<std::string, std::filesystem::path> recipe_files(
    const std::filesystem::path& dir) {
    std::map<std::string, std::filesystem::path> out;
    std::error_code code;
    for (std::filesystem::directory_iterator it{dir, code};
         !code && it != std::filesystem::directory_iterator{}; it.increment(code)) {
        const std::filesystem::path& path = it->path();
        const std::string extension = path.extension().string();
        if ((extension == ".yaml" || extension == ".yml") && it->is_regular_file(code)) {
            out.emplace(path.stem().string(), path);
        }
    }
    return out;
}

[[nodiscard]] nlohmann::json fields(const RecipeEntry& entry) {
    const Recipe& recipe = entry.recipe;
    nlohmann::json out{
        {"name", recipe.name},
        {"description", recipe.description},
        {"source", to_string(entry.source)},
        {"path", entry.path.empty() ? nlohmann::json{} : nlohmann::json(entry.path.string())},
        {"input", recipe.input},
        {"tools", recipe.tools},
        {"require", recipe.require},
        {"require_files", recipe.require_files},
        {"allow", recipe.allow},
        {"agent", recipe.agent},
        {"rounds", recipe.rounds.value_or(kDefaultRounds)}};
    return out;
}

}  // namespace

std::string_view to_string(RecipeSource source) noexcept {
    switch (source) {
        case RecipeSource::Shipped:
            return "shipped";
        case RecipeSource::Seeded:
            return "seeded";
        case RecipeSource::User:
            return "user";
    }
    return "shipped";
}

bool Recipe::takes_input() const {
    // Parsed recipes hold only `{{input}}`, so any variable is the input.
    return goal.find("{{") != std::string::npos;
}

Recipe parse_recipe(std::string_view text, std::string_view origin, std::string_view name) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string{text});
    } catch (const YAML::Exception& e) {
        fail(origin, std::string{"not valid YAML -- "} + e.what());
    }
    if (!root.IsDefined() || root.IsNull()) {
        fail(origin, "the recipe is empty");
    }
    if (!root.IsMap()) {
        fail(origin, "expected a mapping at the top level");
    }
    for (const auto& pair : root) {
        const std::string key = pair.first.as<std::string>();
        if (std::ranges::find(kKeys, key) == kKeys.end()) {
            fail(origin, "'" + key +
                             "' is not a recipe key (name, description, input, goal, tools, "
                             "require, require_files, allow, agent, rounds)");
        }
    }

    Recipe recipe;
    recipe.name = std::string{name};
    if (const std::string named = scalar(root["name"], origin, "name");
        !named.empty() && named != name) {
        fail(origin, "names itself '" + named + "' -- a recipe runs by its file's name, '" +
                         std::string{name} + "'");
    }
    if (!valid_name(recipe.name)) {
        fail(origin, "'" + recipe.name + "' is not a recipe name (letters, digits, '_' and '-')");
    }
    recipe.description = scalar(root["description"], origin, "description");
    if (const YAML::Node input = root["input"]; input.IsDefined() && !input.IsNull()) {
        if (!input.IsMap()) {
            fail(origin, "input: expected a mapping with a description");
        }
        recipe.input = scalar(input["description"], origin, "input.description");
        if (blank(recipe.input)) {
            fail(origin, "input: say what the input is (input.description)");
        }
    }
    recipe.goal = scalar(root["goal"], origin, "goal");
    if (blank(recipe.goal)) {
        fail(origin, "a recipe needs a goal");
    }
    bool reads_input = false;
    for (const std::string& variable : variables(recipe.goal, origin)) {
        if (variable != kRecipeInput) {
            fail(origin, "goal: '{{" + variable + "}}' is not a variable -- a goal reads only " +
                             "{{input}}");
        }
        reads_input = true;
    }
    if (!recipe.input.empty() && !reads_input) {
        fail(origin, "input: the goal never reads {{input}}");
    }
    if (reads_input && recipe.input.empty()) {
        fail(origin, "the goal reads {{input}}, so say what it is (input.description)");
    }
    if (const YAML::Node tools = root["tools"]; tools.IsDefined() && !tools.IsNull()) {
        try {
            recipe.tools = tools.as<bool>();
        } catch (const YAML::Exception&) {
            fail(origin, "tools: expected true or false");
        }
    }
    recipe.require = texts(root["require"], origin, "require");
    recipe.require_files = texts(root["require_files"], origin, "require_files");
    recipe.allow = normalized_grants(texts(root["allow"], origin, "allow"));
    recipe.agent = scalar(root["agent"], origin, "agent");
    if (const YAML::Node rounds = root["rounds"]; rounds.IsDefined() && !rounds.IsNull()) {
        int value = 0;
        try {
            value = rounds.as<int>();
        } catch (const YAML::Exception&) {
            fail(origin, "rounds: expected a whole number");
        }
        if (value < 1 || value > kMaxRounds) {
            fail(origin, "rounds: " + std::to_string(value) + " is out of range (1 to " +
                             std::to_string(kMaxRounds) + ")");
        }
        recipe.rounds = value;
    }
    if (!recipe.tools && !recipe.allow.empty()) {
        fail(origin, "allow: a grant needs tools (tools: true) -- without tools, nothing is asked");
    }
    if (!recipe.tools && !recipe.agent.empty()) {
        fail(origin,
             "agent: an agent's policy is over the tools a task's turns may call, so it "
             "needs tools: true");
    }
    return recipe;
}

std::string expand_goal(const Recipe& recipe, const std::optional<std::string>& input,
                        std::string& error) {
    error.clear();
    if (recipe.takes_input()) {
        if (!input.has_value() || blank(*input)) {
            error = "the recipe '" + recipe.name + "' takes an input -- " + recipe.input +
                    " -- given with --input";
            return {};
        }
        return render(recipe.goal, *input);
    }
    if (input.has_value()) {
        error = "the recipe '" + recipe.name + "' takes no input, and --input gave one";
        return {};
    }
    return recipe.goal;
}

const RecipeEntry* RecipeBook::find(std::string_view name) const noexcept {
    for (const RecipeEntry& entry : recipes) {
        if (entry.recipe.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

std::string RecipeBook::names() const {
    std::string out;
    for (const RecipeEntry& entry : recipes) {
        out += (out.empty() ? "" : ", ") + entry.recipe.name;
    }
    return out;
}

RecipeBook recipe_book(const std::filesystem::path& dir) {
    RecipeBook book;
    std::map<std::string, std::filesystem::path> files = recipe_files(dir);
    std::vector<std::pair<std::string, std::string_view>> starters;
    for (const harness::BundledRecipe& starter : harness::bundled_recipes()) {
        starters.emplace_back(std::string{starter.name}, starter.text);
    }
    std::ranges::sort(starters, {}, &std::pair<std::string, std::string_view>::first);
    for (const auto& [name, text] : starters) {
        // A starter's file stands in for it, read or not: taken out of the
        // files left over, so one that does not parse is said once.
        std::optional<std::filesystem::path> file;
        if (const auto found = files.find(name); found != files.end()) {
            file = found->second;
            files.erase(found);
        }
        RecipeEntry entry;
        try {
            if (file.has_value()) {
                const std::optional<std::string> bytes = read_file(*file);
                if (!bytes.has_value()) {
                    book.problems.push_back(file->string() + ": could not be read");
                    continue;
                }
                entry.recipe = parse_recipe(*bytes, file->string(), name);
                entry.source = *bytes == text ? RecipeSource::Seeded : RecipeSource::User;
                entry.path = *file;
            } else {
                entry.recipe = parse_recipe(text, "starter " + name, name);
                entry.source = RecipeSource::Shipped;
            }
        } catch (const std::runtime_error& e) {
            book.problems.emplace_back(e.what());
            continue;
        }
        book.recipes.push_back(std::move(entry));
    }
    for (const auto& [name, path] : files) {
        const std::optional<std::string> bytes = read_file(path);
        if (!bytes.has_value()) {
            book.problems.push_back(path.string() + ": could not be read");
            continue;
        }
        try {
            book.recipes.push_back(RecipeEntry{.recipe = parse_recipe(*bytes, path.string(), name),
                                               .source = RecipeSource::User,
                                               .path = path});
        } catch (const std::runtime_error& e) {
            book.problems.emplace_back(e.what());
        }
    }
    return book;
}

nlohmann::json to_json(const RecipeEntry& entry) {
    nlohmann::json out = fields(entry);
    out["object"] = "task.recipe";
    out["goal"] = entry.recipe.goal;
    return out;
}

nlohmann::json to_json(const RecipeBook& book) {
    nlohmann::json recipes = nlohmann::json::array();
    for (const RecipeEntry& entry : book.recipes) {
        recipes.push_back(fields(entry));
    }
    return nlohmann::json{
        {"object", "list"}, {"data", std::move(recipes)}, {"problems", book.problems}};
}

}  // namespace apogee::tasks
