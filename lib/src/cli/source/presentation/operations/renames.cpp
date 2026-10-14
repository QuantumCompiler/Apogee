#include "operations/renames.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
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

RenameOutcome rename_stored_model(const std::filesystem::path& config_path,
                                  const models::StoreRoots& roots, std::string_view old_model,
                                  std::string_view new_model) {
    RenameOutcome out;
    const models::StoreTarget target = models::resolve_store_target(roots, old_model);
    if (!target.error.empty() || target.outside || !target.format.empty() ||
        model_dirs(roots, target.model).empty()) {
        out.refusal = "no stored model named '" + std::string{old_model} +
                      "' -- 'apogee models list --all' shows them";
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
