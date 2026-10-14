#include "operations/renames.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <regex>
#include <set>
#include <system_error>
#include <utility>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/layout.h"
#include "logger/session.h"

namespace apogee::commands {

namespace {

/// The model's directories, in each root that holds one.
[[nodiscard]] std::vector<std::filesystem::path> model_dirs(const models::StoreRoots& roots,
                                                            std::string_view model) {
    std::vector<std::filesystem::path> out;
    for (const std::filesystem::path& root : {roots.models, roots.safetensors}) {
        if (root.empty() || model.empty()) {
            continue;
        }
        const std::filesystem::path dir = root / std::string{model};
        std::error_code code;
        if (std::filesystem::is_directory(dir, code) && std::ranges::find(out, dir) == out.end()) {
            out.push_back(dir);
        }
    }
    return out;
}

/// Whether anything under `dirs` is still being written: a pull's or a
/// conversion's `.partial`.
[[nodiscard]] bool being_written(const std::vector<std::filesystem::path>& dirs) {
    for (const std::filesystem::path& dir : dirs) {
        std::error_code code;
        for (auto it = std::filesystem::recursive_directory_iterator{dir, code};
             !code && it != std::filesystem::recursive_directory_iterator{}; it.increment(code)) {
            if (it->path().extension() == ".partial") {
                return true;
            }
        }
    }
    return false;
}

/// `path` as written, its store directory `model` renamed `renamed`: the one
/// path segment swapped, so a `~` or a `${VAR}` the user wrote stays theirs.
[[nodiscard]] std::string rebased(std::string path, std::string_view model,
                                  std::string_view renamed) {
    for (const char separator : {'/', '\\'}) {
        const std::string segment = separator + std::string{model} + separator;
        if (const std::size_t at = path.find(segment); at != std::string::npos) {
            path.replace(at + 1, model.size(), renamed);
            return path;
        }
    }
    return path;
}

/// Moves each of `moves`, in order; on the first failure the moved ones are
/// put back and the reason returned.
[[nodiscard]] std::string move_all(
    const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& moves,
    std::size_t& done) {
    for (done = 0; done < moves.size(); ++done) {
        if (const std::string error = models::move_path(moves[done].first, moves[done].second);
            !error.empty()) {
            return error;
        }
    }
    return {};
}

void move_back(const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& moves,
               std::size_t done) {
    while (done > 0) {
        --done;
        (void)models::move_path(moves[done].second, moves[done].first);
    }
}

/// The chats a rename touched, said.
void say_sweep(RenameOutcome& out, std::string_view old_name, std::string_view new_name) {
    const logger::SessionSweep sweep = logger::rename_session_backend(old_name, new_name);
    if (sweep.renamed > 0) {
        out.said.push_back(
            std::to_string(sweep.renamed) +
            (sweep.renamed == 1 ? " saved chat now names '" : " saved chats now name '") +
            std::string{new_name} + "'");
    }
    for (const std::string& chat : sweep.failed) {
        out.said.push_back("chat " + chat + " could not be rewritten -- it still names '" +
                           std::string{old_name} + "', and resumes saying so");
    }
}

}  // namespace

models::StoreRoots store_roots_of(const harness::Config& config) {
    const std::filesystem::path models = harness::models_dir();
    return models::StoreRoots{
        .models = models,
        .safetensors =
            config.paths.hf_dir.empty()
                ? models
                : std::filesystem::path{harness::expand_env_and_home(config.paths.hf_dir)}};
}

std::string model_base_name(std::string_view model) {
    const std::size_t separator = model.rfind("--");
    return std::string{separator == std::string_view::npos ? model : model.substr(separator + 2)};
}

std::optional<std::string> derived_level(std::string_view backend, std::string_view model) {
    const std::string base = model_base_name(model);
    if (base.empty() || backend.size() <= base.size() + 1 || !backend.starts_with(base) ||
        backend[base.size()] != '-') {
        return std::nullopt;
    }
    const std::string_view level = backend.substr(base.size() + 1);
    if (!std::ranges::all_of(
            level, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; })) {
        return std::nullopt;
    }
    return std::string{level};
}

std::string stored_model_at(const models::StoreRoots& roots, const std::filesystem::path& file) {
    if (file.empty()) {
        return {};
    }
    const std::filesystem::path path =
        std::filesystem::path{harness::expand_env_and_home(file.string())}.lexically_normal();
    for (const std::filesystem::path& root : {roots.models, roots.safetensors}) {
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path relative = path.lexically_relative(root.lexically_normal());
        auto part = relative.begin();
        if (relative.empty() || part == relative.end() || *part == "..") {
            continue;
        }
        const std::string model = part->string();
        ++part;
        if (part == relative.end()) {
            continue;
        }
        const std::string format = part->string();
        if (format == models::kGgufFormat || format == models::kSafetensorsFormat ||
            format == models::kMlxFormat) {
            return model;
        }
    }
    return {};
}

RenameOutcome rename_backend_everywhere(const std::filesystem::path& config_path,
                                        const models::StoreRoots& roots, std::string_view old_name,
                                        std::string_view new_name) {
    RenameOutcome out;
    harness::Config config;
    try {
        config = harness::load_config(config_path);
    } catch (const std::exception& e) {
        out.refusal = e.what();
        return out;
    }
    const harness::BackendConfig* entry = config.find_backend(old_name);
    if (entry == nullptr) {
        out.refusal = "no backend named '" + std::string{old_name} + "'";
        return out;
    }

    // The store follows only where the mapping is one to one (46a's rule (a)).
    const std::string model = stored_model_at(roots, entry->model_path);
    std::string renamed_model;
    std::string store_said;
    if (!model.empty()) {
        std::string other;
        for (const auto& [name, backend] : config.backends) {
            if (name != old_name && (stored_model_at(roots, backend.model_path) == model ||
                                     stored_model_at(roots, backend.mmproj_path) == model)) {
                other = name;
                break;
            }
        }
        const std::optional<std::string> level = derived_level(old_name, model);
        const std::string suffix = level.has_value() ? "-" + *level : std::string{};
        const std::string head = model.substr(0, model.size() - model_base_name(model).size());
        if (!other.empty()) {
            store_said = "store '" + model + "' unchanged -- '" + other + "' still points at it";
        } else if (!level.has_value()) {
            store_said = "store '" + model + "' unchanged -- '" + std::string{old_name} +
                         "' is not named after it";
        } else if (!new_name.ends_with(suffix) || new_name.size() <= suffix.size()) {
            store_said = "store '" + model + "' unchanged -- '" + std::string{new_name} +
                         "' is not named after a model the way '" + std::string{old_name} + "' is";
        } else {
            const std::string candidate =
                head + std::string{new_name.substr(0, new_name.size() - suffix.size())};
            if (models::safe_model_name(candidate) != candidate) {
                store_said = "store '" + model + "' unchanged -- '" + candidate +
                             "' cannot name a stored model";
            } else if (!model_dirs(roots, candidate).empty()) {
                store_said =
                    "store '" + model + "' unchanged -- '" + candidate + "' is already stored";
            } else if (being_written(model_dirs(roots, model))) {
                store_said = "store '" + model + "' unchanged -- it is being written";
            } else {
                renamed_model = candidate;
            }
        }
    }

    // The directories first, so the one edit can name where the files are.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
    if (!renamed_model.empty()) {
        for (const std::filesystem::path& dir : model_dirs(roots, model)) {
            moves.emplace_back(dir, dir.parent_path() / renamed_model);
        }
    }
    std::size_t done = 0;
    if (const std::string error = move_all(moves, done); !error.empty()) {
        move_back(moves, done);
        out.refusal = "could not move store '" + model + "': " + error;
        return out;
    }
    const std::string model_path = entry->model_path;
    const std::string mmproj_path = entry->mmproj_path;
    try {
        harness::edit_config_file(config_path, [&](std::string_view content) {
            std::string text{content};
            if (!renamed_model.empty()) {
                text = harness::set_backend_model_path(text, old_name,
                                                       rebased(model_path, model, renamed_model));
                if (stored_model_at(roots, mmproj_path) == model) {
                    text = harness::set_backend_mmproj_path(
                        text, old_name, rebased(mmproj_path, model, renamed_model));
                }
            }
            return harness::rename_backend(text, old_name, new_name);
        });
    } catch (const std::exception& e) {
        move_back(moves, moves.size());
        out.refusal = e.what();
        return out;
    }
    out.said.push_back("renamed backend '" + std::string{old_name} + "' to '" +
                       std::string{new_name} + "' in " + config_path.string() +
                       ", every reference to it with it");
    if (!renamed_model.empty()) {
        out.said.push_back("moved store '" + model + "' to '" + renamed_model +
                           "', its records inside it");
    } else if (!store_said.empty()) {
        out.said.push_back(store_said);
    }
    say_sweep(out, old_name, new_name);
    return out;
}

namespace {

/// `models rename` by the name a model's backends share (the user's
/// request): `family` the stored backends `<old>-<level>`.
[[nodiscard]] RenameOutcome rename_model_family(const std::filesystem::path& config_path,
                                                const models::StoreRoots& roots,
                                                const harness::Config& config,
                                                std::string_view old_name,
                                                std::string_view new_name,
                                                const std::vector<std::string>& family) {
    RenameOutcome out;
    if (new_name.empty() || models::safe_model_name(new_name) != new_name) {
        out.refusal = "'" + std::string{new_name} +
                      "' cannot name a model -- one word: no spaces, no / : @ \\";
        return out;
    }
    // Where the family runs from, and who else runs from there.
    std::set<std::string> dirs;
    for (const std::string& name : family) {
        const harness::BackendConfig& backend = *config.find_backend(name);
        for (const std::string& path : {backend.model_path, backend.mmproj_path}) {
            if (const std::string model = stored_model_at(roots, path); !model.empty()) {
                dirs.insert(model);
            }
        }
    }
    std::string outsider;
    for (const auto& [name, backend] : config.backends) {
        if (std::ranges::find(family, name) != family.end()) {
            continue;
        }
        if (dirs.contains(stored_model_at(roots, backend.model_path)) ||
            dirs.contains(stored_model_at(roots, backend.mmproj_path))) {
            outsider = name;
            break;
        }
    }
    // The backends' new names, a taken one left.
    std::vector<std::pair<std::string, std::string>> renames;
    std::vector<std::string> left;
    for (const std::string& name : family) {
        const std::string renamed = std::string{new_name} + "-" + *quant_level(name);
        if (renamed == name) {
            continue;
        }
        const bool taken = std::ranges::any_of(config.backend_names(), [&](const auto& key) {
            return key != name && (key == renamed || harness::fold_collision({key}, renamed));
        });
        if (taken) {
            left.push_back("left backend '" + name + "' as it is -- '" + renamed +
                           "' is already a backend's name");
        } else {
            renames.emplace_back(name, renamed);
        }
    }
    // The directory follows when the family alone runs it.
    const std::string dir = dirs.size() == 1 ? *dirs.begin() : std::string{};
    std::string store_said;
    bool move = false;
    if (dirs.size() > 1) {
        store_said = "its weights sit in " + std::to_string(dirs.size()) +
                     " stored models -- each left as it is";
    } else if (dir == new_name) {
        // Already named after it.
    } else if (!outsider.empty()) {
        store_said = "store '" + dir + "' unchanged -- '" + outsider + "' also runs from it";
    } else if (!model_dirs(roots, new_name).empty()) {
        store_said =
            "store '" + dir + "' unchanged -- '" + std::string{new_name} + "' is already stored";
    } else if (being_written(model_dirs(roots, dir))) {
        store_said = "store '" + dir + "' unchanged -- it is being written";
    } else {
        move = true;
    }
    if (renames.empty() && !move) {
        out.refusal = left.empty() && store_said.empty()
                          ? "'" + std::string{old_name} + "' is already its name"
                          : (left.empty() ? store_said : left.front());
        return out;
    }

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
    if (move) {
        for (const std::filesystem::path& from : model_dirs(roots, dir)) {
            moves.emplace_back(from, from.parent_path() / std::string{new_name});
        }
    }
    std::size_t done = 0;
    if (const std::string error = move_all(moves, done); !error.empty()) {
        move_back(moves, done);
        out.refusal = "could not move '" + dir + "': " + error;
        return out;
    }
    try {
        harness::edit_config_file(config_path, [&](std::string_view content) {
            std::string text{content};
            if (move) {
                for (const std::string& name : family) {
                    const harness::BackendConfig& backend = *config.find_backend(name);
                    if (stored_model_at(roots, backend.model_path) == dir) {
                        text = harness::set_backend_model_path(
                            text, name, rebased(backend.model_path, dir, new_name));
                    }
                    if (stored_model_at(roots, backend.mmproj_path) == dir) {
                        text = harness::set_backend_mmproj_path(
                            text, name, rebased(backend.mmproj_path, dir, new_name));
                    }
                }
            }
            for (const auto& [from, to] : renames) {
                text = harness::rename_backend(text, from, to);
            }
            return text;
        });
    } catch (const std::exception& e) {
        move_back(moves, moves.size());
        out.refusal = e.what();
        return out;
    }
    for (const auto& [from, to] : renames) {
        out.said.push_back("renamed backend '" + from + "' to '" + to +
                           "', every reference to it with it");
    }
    if (move) {
        out.said.push_back("moved store '" + dir + "' to '" + std::string{new_name} +
                           "', its records inside it");
    } else if (!store_said.empty()) {
        out.said.push_back(store_said);
    }
    for (std::string& line : left) {
        out.said.push_back(std::move(line));
    }
    for (const auto& [from, to] : renames) {
        say_sweep(out, from, to);
    }
    return out;
}

}  // namespace

std::optional<std::string> quant_level(std::string_view backend) {
    const std::size_t dash = backend.rfind('-');
    if (dash == std::string_view::npos || dash == 0) {
        return std::nullopt;
    }
    static const std::regex kLevel{"(B?F(16|32)|I?Q[0-9][A-Z0-9]*)"};
    const std::string level{backend.substr(dash + 1)};
    if (!std::regex_match(level, kLevel)) {
        return std::nullopt;
    }
    return level;
}

std::vector<std::string> model_names(const harness::Config& config,
                                     const models::StoreRoots& roots) {
    std::vector<std::string> names;
    std::set<std::string> named;  // the stored models a family covers
    for (const auto& [name, backend] : config.backends) {
        const std::string model = stored_model_at(roots, backend.model_path);
        const std::optional<std::string> level = quant_level(name);
        if (model.empty() || !level.has_value()) {
            continue;
        }
        names.push_back(name.substr(0, name.size() - level->size() - 1));
        named.insert(model);
    }
    for (const std::string& model : models::list_store_models(roots)) {
        if (!named.contains(model)) {
            names.push_back(model);
        }
    }
    std::ranges::sort(names);
    const auto [repeats, end] = std::ranges::unique(names);
    names.erase(repeats, end);
    return names;
}

RenameOutcome rename_stored_model(const std::filesystem::path& config_path,
                                  const models::StoreRoots& roots, std::string_view old_model,
                                  std::string_view new_model) {
    // A model by the name its backends share, first (the user's request).
    std::error_code absent;
    if (std::filesystem::exists(config_path, absent)) {
        try {
            const harness::Config config = harness::load_config(config_path);
            std::vector<std::string> family;
            for (const auto& [name, backend] : config.backends) {
                const std::optional<std::string> level = quant_level(name);
                if (level.has_value() &&
                    std::string_view{name}.substr(0, name.size() - level->size() - 1) ==
                        old_model &&
                    !stored_model_at(roots, backend.model_path).empty()) {
                    family.push_back(name);
                }
            }
            if (!family.empty()) {
                return rename_model_family(config_path, roots, config, old_model, new_model,
                                           family);
            }
        } catch (const std::exception& e) {
            return RenameOutcome{.said = {}, .refusal = e.what()};
        }
    }
    RenameOutcome out;
    const models::StoreTarget target = models::resolve_store_target(roots, old_model);
    if (!target.error.empty() || target.outside || !target.format.empty() ||
        model_dirs(roots, target.model).empty()) {
        out.refusal = "no model named '" + std::string{old_model} +
                      "' -- name it as its backends share it (the backend without its "
                      "-F16 or -Q4KM), or by its stored directory";
        return out;
    }
    const std::string model = target.model;
    if (new_model.empty() || models::safe_model_name(new_model) != new_model) {
        out.refusal = "'" + std::string{new_model} +
                      "' cannot name a stored model -- a directory's name: no spaces, no / : @ \\";
        return out;
    }
    if (new_model == model) {
        out.refusal = "'" + model + "' is already its name";
        return out;
    }
    if (!model_dirs(roots, new_model).empty()) {
        out.refusal = "a stored model named '" + std::string{new_model} + "' already exists";
        return out;
    }
    const std::vector<std::filesystem::path> dirs = model_dirs(roots, model);
    if (being_written(dirs)) {
        out.refusal = "'" + model +
                      "' is being written -- a pull or a conversion is under way; rename it "
                      "once that ends";
        return out;
    }

    // What points into it, and the backends named after it.
    std::optional<harness::Config> config;
    std::error_code missing;
    if (std::filesystem::exists(config_path, missing)) {
        try {
            config = harness::load_config(config_path);
        } catch (const std::exception& e) {
            out.refusal = e.what();
            return out;
        }
    }
    std::vector<std::string> pointing;
    std::vector<std::pair<std::string, std::string>> renames;
    std::vector<std::string> left;
    if (config.has_value()) {
        for (const auto& [name, backend] : config->backends) {
            if (stored_model_at(roots, backend.model_path) != model &&
                stored_model_at(roots, backend.mmproj_path) != model) {
                continue;
            }
            pointing.push_back(name);
            if (const std::optional<std::string> level = derived_level(name, model)) {
                const std::string renamed = model_base_name(new_model) + "-" + *level;
                if (renamed == name) {
                    continue;  // the new name keeps its base: it follows already
                }
                const bool taken =
                    std::ranges::any_of(
                        config->backend_names(),
                        [&](const auto& key) {
                            return key != name &&
                                   harness::fold_collision({key}, renamed).has_value();
                        }) ||
                    config->find_backend(renamed) != nullptr;
                if (taken) {
                    left.push_back("left backend '" + name + "' as it is -- '" + renamed +
                                   "' is already a backend's name");
                } else {
                    renames.emplace_back(name, renamed);
                }
            }
        }
    }

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
    for (const std::filesystem::path& dir : dirs) {
        moves.emplace_back(dir, dir.parent_path() / std::string{new_model});
    }
    std::size_t done = 0;
    if (const std::string error = move_all(moves, done); !error.empty()) {
        move_back(moves, done);
        out.refusal = "could not move '" + model + "': " + error;
        return out;
    }
    if (!pointing.empty()) {
        try {
            harness::edit_config_file(config_path, [&](std::string_view content) {
                std::string text{content};
                for (const std::string& name : pointing) {
                    const harness::BackendConfig& backend = *config->find_backend(name);
                    if (stored_model_at(roots, backend.model_path) == model) {
                        text = harness::set_backend_model_path(
                            text, name, rebased(backend.model_path, model, new_model));
                    }
                    if (stored_model_at(roots, backend.mmproj_path) == model) {
                        text = harness::set_backend_mmproj_path(
                            text, name, rebased(backend.mmproj_path, model, new_model));
                    }
                }
                for (const auto& [from, to] : renames) {
                    text = harness::rename_backend(text, from, to);
                }
                return text;
            });
        } catch (const std::exception& e) {
            move_back(moves, moves.size());
            out.refusal = e.what();
            return out;
        }
    }
    out.said.push_back("renamed stored model '" + model + "' to '" + std::string{new_model} +
                       "', its records inside it");
    if (!pointing.empty()) {
        std::string names;
        for (const std::string& name : pointing) {
            names += (names.empty() ? "" : ", ") + name;
        }
        out.said.push_back("repointed " + std::to_string(pointing.size()) +
                           (pointing.size() == 1 ? " backend: " : " backends: ") + names);
    }
    for (const auto& [from, to] : renames) {
        out.said.push_back("renamed backend '" + from + "' to '" + to +
                           "', every reference to it with it");
    }
    for (std::string& line : left) {
        out.said.push_back(std::move(line));
    }
    for (const auto& [from, to] : renames) {
        say_sweep(out, from, to);
    }
    return out;
}

}  // namespace apogee::commands
