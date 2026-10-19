#include "contracts/assets.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

#include "contracts/config_edit.h"
#include "contracts/sha256.h"

// The bundled agents' texts are compiled in by scripts/generate_agent_assets.py
// (assets_agents.cpp); this file holds what is done with them.

namespace apogee::harness {

const BundledAgent* find_bundled_agent(std::string_view name) noexcept {
    // Case-insensitively, as every config section is compared.
    for (const BundledAgent& agent : bundled_agents()) {
        if (!CaseInsensitiveLess{}(agent.name, name) && !CaseInsensitiveLess{}(name, agent.name)) {
            return &agent;
        }
    }
    return nullptr;
}

const BundledPersona* find_bundled_persona(std::string_view name) noexcept {
    for (const BundledPersona& persona : bundled_personas()) {
        if (persona.name == name) {
            return &persona;
        }
    }
    return nullptr;
}

std::string bundled_prompt_relative_path(std::string_view name) {
    return "prompts/" + std::string{name} + ".txt";
}

std::string bundled_schema_relative_path(std::string_view name) {
    return "schemas/" + std::string{name} + "-output.json";
}

AgentConfig bundled_agent_config(const BundledAgent& agent) {
    AgentConfig config;
    config.description = std::string{agent.description};
    config.prompts = {bundled_prompt_relative_path(agent.name)};
    config.schemas = {bundled_schema_relative_path(agent.name)};
    // Read-only, so a non-interactive run never blocks on a prompt; the
    // reports nest under analyses/<name>/ so the three never glob together.
    config.tools = AgentToolPolicy::ReadOnly;
    config.save_subdir = std::string{agent.name};
    return config;
}

std::optional<AgentConfig> resolve_agent(const Config& config, std::string_view name,
                                         const BundledAgent** bundled_out) {
    const BundledAgent* bundled = find_bundled_agent(name);
    if (bundled_out != nullptr) {
        *bundled_out = bundled;
    }
    if (const AgentConfig* entry = config.find_agent(name); entry != nullptr) {
        return *entry;
    }
    if (bundled != nullptr) {
        return bundled_agent_config(*bundled);
    }
    return std::nullopt;
}

std::vector<NamedAgent> all_agents(const Config& config) {
    std::vector<NamedAgent> out;
    for (const BundledAgent& bundled : bundled_agents()) {
        NamedAgent agent;
        agent.name = std::string{bundled.name};
        if (const AgentConfig* entry = config.find_agent(bundled.name); entry != nullptr) {
            agent.config = *entry;
            agent.overrides_bundled = true;
        } else {
            agent.config = bundled_agent_config(bundled);
            agent.bundled = true;
        }
        out.push_back(std::move(agent));
    }
    for (const auto& [name, entry] : config.agents) {
        if (find_bundled_agent(name) != nullptr) {
            continue;  // listed above, in the bundled slot it overrides
        }
        out.push_back(NamedAgent{name, entry, false, false});
    }
    return out;
}

std::filesystem::path resolve_agent_path(const std::filesystem::path& home, std::string_view path) {
    const std::filesystem::path candidate{expand_env_and_home(path)};
    if (candidate.is_absolute()) {
        return candidate;
    }
    return home / candidate;
}

std::string bundled_kit_relative_path(std::string_view name) {
    return "training/kits/" + std::string{name} + ".yaml";
}

const BundledRegime* find_bundled_regime(std::string_view name) noexcept {
    for (const BundledRegime& regime : bundled_regimes()) {
        if (regime.name == name) {
            return &regime;
        }
    }
    return nullptr;
}

std::string bundled_regime_relative_path(std::string_view name) {
    return "training/regimes/" + std::string{name} + ".yaml";
}

const BundledSymphony* find_bundled_symphony(std::string_view name) noexcept {
    for (const BundledSymphony& starter : bundled_symphonies()) {
        if (starter.name == name) {
            return &starter;
        }
    }
    return nullptr;
}

std::string bundled_symphony_relative_path(std::string_view name) {
    return "symphonies/" + std::string{name} + ".yaml";
}

const BundledRecipe* find_bundled_recipe(std::string_view name) noexcept {
    for (const BundledRecipe& recipe : bundled_recipes()) {
        if (recipe.name == name) {
            return &recipe;
        }
    }
    return nullptr;
}

std::string bundled_recipe_relative_path(std::string_view name) {
    return "recipes/" + std::string{name} + ".yaml";
}

const BundledReviewSkill* find_bundled_review_skill(std::string_view name) noexcept {
    for (const BundledReviewSkill& skill : bundled_review_skills()) {
        if (skill.name == name) {
            return &skill;
        }
    }
    return nullptr;
}

std::string bundled_review_skill_relative_path(std::string_view name) {
    return "review-skills/" + std::string{name} + ".md";
}

std::string bundled_script_relative_path(std::string_view name) {
    return "training/scripts/" + std::string{name};
}

namespace {

[[nodiscard]] std::optional<std::string> read_bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The bundled converter file named `name` (`convert/...`), or null.
[[nodiscard]] const BundledScript* find_converter_file(std::string_view name) {
    for (const BundledScript& file : bundled_converter_files()) {
        if (file.name == name) {
            return &file;
        }
    }
    return nullptr;
}

/// Each regular file under the tree `dir`, named as `bundled_converter_files`
/// names them. A `__pycache__` is Python's, not anyone's edit, and skipped.
[[nodiscard]] std::vector<std::pair<std::filesystem::path, std::string>> converter_tree_files(
    const std::filesystem::path& dir) {
    std::vector<std::pair<std::filesystem::path, std::string>> out;
    std::error_code code;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        if (it->is_directory(code) && it->path().filename() == "__pycache__") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(code)) {
            out.emplace_back(it->path(), dir.filename().generic_string() + "/" +
                                             it->path().lexically_relative(dir).generic_string());
        }
    }
    return out;
}

[[nodiscard]] bool is_retired(std::span<const std::string_view> retired, const std::string& name,
                              std::string_view bytes) {
    const std::string key = name + " " + models::sha256_hex(bytes);
    return std::ranges::binary_search(retired, std::string_view{key});
}

}  // namespace

ConverterTreeState inspect_converter_tree(const std::filesystem::path& dir,
                                          std::span<const std::string_view> retired) {
    ConverterTreeState state;
    std::set<std::string> present;
    for (const auto& [path, name] : converter_tree_files(dir)) {
        present.insert(name);
        const std::optional<std::string> bytes = read_bytes(path);
        const BundledScript* bundled = find_converter_file(name);
        if (bytes.has_value() && bundled != nullptr && *bytes == bundled->text) {
            continue;
        }
        if (bytes.has_value() && is_retired(retired, name, *bytes)) {
            ++state.stale;
        } else {
            ++state.edited;
        }
    }
    for (const BundledScript& file : bundled_converter_files()) {
        if (!present.contains(std::string{file.name})) {
            ++state.missing;
        }
    }
    return state;
}

void refresh_converter_tree(const std::filesystem::path& root, const std::filesystem::path& dir,
                            AssetSeedResult& result, std::span<const std::string_view> retired) {
    bool removed_any = false;
    for (const auto& [path, name] : converter_tree_files(dir)) {
        const std::optional<std::string> bytes = read_bytes(path);
        if (!bytes.has_value() || !is_retired(retired, name, *bytes)) {
            continue;  // current, or the user's
        }
        const BundledScript* bundled = find_converter_file(name);
        const std::string relative = path.lexically_relative(root).generic_string();
        if (bundled != nullptr && *bytes == bundled->text) {
            continue;
        }
        if (bundled == nullptr) {
            std::error_code code;
            std::filesystem::remove(path, code);
            if (code) {
                result.error = "could not remove " + path.string() + ": " + code.message();
                return;
            }
            result.removed.push_back(relative);
            removed_any = true;
            continue;
        }
        try {
            write_file_atomically(path, bundled->text);
        } catch (const std::exception& e) {
            result.error = e.what();
            return;
        }
        result.updated.push_back(relative);
    }
    if (!removed_any) {
        return;
    }
    // Directories a removal emptied, deepest first; never the tree itself.
    std::vector<std::filesystem::path> dirs;
    std::error_code code;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        if (it->is_directory(code)) {
            dirs.push_back(it->path());
        }
    }
    std::ranges::sort(dirs, std::ranges::greater{});
    for (const std::filesystem::path& empty : dirs) {
        if (std::filesystem::is_empty(empty, code)) {
            std::filesystem::remove(empty, code);
        }
    }
}

namespace {

/// The seeded driver named `name` -- a trainer's, an MLX one or the
/// code-execution server -- or null.
[[nodiscard]] const BundledScript* find_seeded_script(std::string_view name) {
    for (const std::span<const BundledScript> scripts :
         {bundled_training_scripts(), bundled_mlx_scripts(), bundled_mcp_scripts()}) {
        for (const BundledScript& script : scripts) {
            if (script.name == name) {
                return &script;
            }
        }
    }
    return nullptr;
}

}  // namespace

SeededScript inspect_seeded_script(const std::filesystem::path& root, std::string_view name,
                                   std::span<const std::string_view> retired) {
    const std::optional<std::string> bytes = read_bytes(root / bundled_script_relative_path(name));
    if (!bytes.has_value()) {
        return SeededScript::Missing;
    }
    const BundledScript* bundled = find_seeded_script(name);
    if (bundled != nullptr && *bytes == bundled->text) {
        return SeededScript::Current;
    }
    return is_retired(retired, std::string{name}, *bytes) ? SeededScript::Stale
                                                          : SeededScript::Edited;
}

void refresh_seeded_scripts(const std::filesystem::path& root, AssetSeedResult& result,
                            std::span<const std::string_view> retired) {
    for (const std::span<const BundledScript> scripts :
         {bundled_training_scripts(), bundled_mlx_scripts(), bundled_mcp_scripts()}) {
        for (const BundledScript& script : scripts) {
            if (inspect_seeded_script(root, script.name, retired) != SeededScript::Stale) {
                continue;  // current, the user's, or seeding's to write
            }
            const std::string relative = bundled_script_relative_path(script.name);
            try {
                write_file_atomically(root / relative, script.text);
            } catch (const std::exception& e) {
                result.error = e.what();
                return;
            }
            result.updated.push_back(relative);
        }
    }
}

std::string bundled_mlx_driver_relative_path() {
    return bundled_script_relative_path("mlx_generate.py");
}

std::string bundled_mlx_converter_relative_path() {
    return bundled_script_relative_path("mlx_convert.py");
}

std::string bundled_code_exec_server_relative_path() {
    return bundled_script_relative_path("code_exec_server.py");
}

std::string bundled_converter_relative_dir() {
    return "training/scripts/convert";
}

std::vector<BundledFile> bundled_files() {
    std::vector<BundledFile> files;
    for (const BundledAgent& agent : bundled_agents()) {
        files.push_back({bundled_prompt_relative_path(agent.name), agent.prompt});
        files.push_back({bundled_schema_relative_path(agent.name), agent.schema});
    }
    for (const BundledKit& kit : bundled_kits()) {
        files.push_back({bundled_kit_relative_path(kit.name), kit.text});
    }
    for (const BundledRegime& regime : bundled_regimes()) {
        files.push_back({bundled_regime_relative_path(regime.name), regime.text});
    }
    for (const BundledSymphony& starter : bundled_symphonies()) {
        files.push_back({bundled_symphony_relative_path(starter.name), starter.text});
    }
    for (const BundledRecipe& recipe : bundled_recipes()) {
        files.push_back({bundled_recipe_relative_path(recipe.name), recipe.text});
    }
    for (const BundledReviewSkill& skill : bundled_review_skills()) {
        files.push_back({bundled_review_skill_relative_path(skill.name), skill.text});
    }
    for (const BundledScript& script : bundled_training_scripts()) {
        files.push_back({bundled_script_relative_path(script.name), script.text});
    }
    for (const BundledScript& script : bundled_mlx_scripts()) {
        files.push_back({bundled_script_relative_path(script.name), script.text});
    }
    for (const BundledScript& script : bundled_mcp_scripts()) {
        files.push_back({bundled_script_relative_path(script.name), script.text});
    }
    for (const BundledScript& file : bundled_converter_files()) {
        files.push_back({bundled_script_relative_path(file.name), file.text});
    }
    return files;
}

bool is_unmodified_bundled_asset(const std::filesystem::path& root,
                                 const std::filesystem::path& file) {
    std::error_code code;
    if (std::filesystem::is_directory(file, code)) {
        // A directory is Apogee's only when everything in it is, and it holds
        // something: an empty directory is nobody's and reads as user data
        // no more than a seeded one would.
        bool any = false;
        for (const auto& item : std::filesystem::directory_iterator(file, code)) {
            any = true;
            if (!is_unmodified_bundled_asset(root, item.path())) {
                return false;
            }
        }
        return any && !code;
    }
    for (const BundledFile& bundled : bundled_files()) {
        if (!std::filesystem::equivalent(root / bundled.relative_path, file, code) || code) {
            code.clear();
            continue;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            return false;
        }
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes == bundled.content) {
            return true;
        }
        break;  // this build's file, changed: an edit -- unless an earlier Apogee shipped it
    }
    // An earlier Apogee's unedited driver is still Apogee's (27c).
    const std::filesystem::path scripts =
        (root / bundled_script_relative_path("driver")).parent_path();
    if (file.parent_path().lexically_normal() == scripts.lexically_normal() &&
        inspect_seeded_script(root, file.filename().string()) == SeededScript::Stale) {
        return true;
    }
    // An earlier Apogee's unedited converter file is still Apogee's.
    const std::filesystem::path converter = root / bundled_converter_relative_dir();
    const std::filesystem::path relative = file.lexically_relative(converter);
    if (relative.empty() || relative.begin()->string() == "..") {
        return false;
    }
    const std::optional<std::string> bytes = read_bytes(file);
    return bytes.has_value() &&
           is_retired(bundled_converter_retired(),
                      converter.filename().generic_string() + "/" + relative.generic_string(),
                      *bytes);
}

AssetSeedResult seed_bundled_assets(const std::filesystem::path& root) {
    AssetSeedResult result;
    // Before the skip-if-present pass: an earlier Apogee's converter, or
    // driver, would otherwise be "present" and skipped for good.
    if (const std::filesystem::path converter = root / bundled_converter_relative_dir();
        std::filesystem::is_directory(converter)) {
        refresh_converter_tree(root, converter, result);
        if (!result.ok()) {
            return result;
        }
    }
    refresh_seeded_scripts(root, result);
    if (!result.ok()) {
        return result;
    }
    for (const BundledFile& bundled : bundled_files()) {
        const std::filesystem::path path = root / bundled.relative_path;
        std::error_code code;
        if (std::filesystem::exists(path, code)) {
            // Skip-if-present: the file is the user's once it exists. A
            // re-seed after an edit reads the edit back, never the shipped
            // text -- proven by a test.
            continue;
        }
        std::filesystem::create_directories(path.parent_path(), code);
        if (code) {
            result.error =
                "could not create " + path.parent_path().string() + ": " + code.message();
            return result;
        }
        try {
            write_file_atomically(path, bundled.content);
        } catch (const std::exception& e) {
            result.error = e.what();
            return result;
        }
        result.created.push_back(bundled.relative_path);
    }
    return result;
}

}  // namespace apogee::harness
