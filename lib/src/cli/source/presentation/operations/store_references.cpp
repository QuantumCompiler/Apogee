#include "operations/store_references.h"

#include <algorithm>
#include <array>
#include <sstream>

#include "contracts/config_edit.h"
#include "modelstore/hf_ref.h"
#include "modelstore/mlx_info.h"

namespace apogee::commands {

namespace {

/// `backends.<name>.draft` set to `path`, through the one editor -- what
/// `config set-backend <name> --draft <path>` writes; empty clears it.
[[nodiscard]] std::string set_backend_draft(std::string_view content, std::string_view name,
                                            std::string_view path) {
    return harness::set_backend_field(content, name, "draft", path);
}

/// One field of a backend entry that holds a path into the model store, and
/// the editor's transform that writes it.
struct StoreField {
    std::string_view name;
    /// The entry runs from the folder it names (`StoreReference::weights`).
    bool weights = false;
    /// The scalar it is, or null for the adapter list -- each adapter's path
    /// a reference, the list written whole.
    const std::string harness::BackendConfig::* scalar = nullptr;
    std::string (*write)(std::string_view content, std::string_view name,
                         std::string_view path) = nullptr;
};

/// **The declaration** (55d): every field of a backend entry that can point
/// into a stored set, in the order a walk says them. The first three are
/// M25's `kStorePaths`, absorbed whole.
constexpr std::array<StoreField, 4> kStoreFields{{
    {.name = "model_path",
     .weights = true,
     .scalar = &harness::BackendConfig::model_path,
     .write = &harness::set_backend_model_path},
    {.name = "mmproj_path",
     .weights = true,
     .scalar = &harness::BackendConfig::mmproj_path,
     .write = &harness::set_backend_mmproj_path},
    {.name = "draft",
     .weights = false,
     .scalar = &harness::BackendConfig::draft,
     .write = &set_backend_draft},
    {.name = "adapters", .weights = false, .scalar = nullptr, .write = nullptr},
}};

/// And the one `models:` pointer that names a stored set (49c): the role
/// pointers name backends, never a folder.
constexpr std::string_view kVideoPointer = "default_video";

/// The declaration's row for an entry's `field`, or null.
[[nodiscard]] const StoreField* store_field(std::string_view field) noexcept {
    const auto found = std::ranges::find(kStoreFields, field, &StoreField::name);
    return found == kStoreFields.end() ? nullptr : &*found;
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

/// `value` -- a path a field holds -- expanded, for comparing with the
/// store's own paths.
[[nodiscard]] std::filesystem::path expanded(const std::string& value) {
    return std::filesystem::path{harness::expand_env_and_home(value)}.lexically_normal();
}

/// Whether `path` is `dir` or lies inside it, lexically.
[[nodiscard]] bool within(const std::filesystem::path& path, const std::filesystem::path& dir) {
    const std::filesystem::path relative = path.lexically_relative(dir.lexically_normal());
    return !relative.empty() && *relative.begin() != "..";
}

/// Whether the stored set at `dir` is a video model a `default_video` naming
/// its model can run (53l): a pipeline, or an MLX directory of kind `video`.
[[nodiscard]] bool runs_video(const std::filesystem::path& dir) {
    const std::string format = dir.parent_path().filename().string();
    if (format == models::kPipelineFormat) {
        return true;
    }
    return format == models::kMlxFormat && models::read_mlx_kind(dir).word == models::kMlxVideo;
}

/// One adapter as `config set-backend --adapters` takes it: `<path>`, or
/// `<path>:<scale>` when it is applied at other than 1.
[[nodiscard]] std::string adapter_argument(const harness::AdapterConfig& adapter) {
    if (adapter.scale == 1.0) {
        return adapter.path;
    }
    std::ostringstream scale;
    scale << adapter.scale;
    return adapter.path + ":" + scale.str();
}

}  // namespace

std::string StoreReference::where() const {
    if (backend.empty()) {
        return "models." + field;
    }
    const std::string at = "backends." + backend + "." + field;
    return field == "adapters" ? at + "[" + std::to_string(index) + "]" : at;
}

std::vector<StoreReference> backend_references(std::string_view name,
                                               const harness::BackendConfig& backend) {
    std::vector<StoreReference> out;
    for (const StoreField& field : kStoreFields) {
        if (field.scalar != nullptr) {
            if (const std::string& value = backend.*field.scalar; !value.empty()) {
                out.push_back({.backend = std::string{name},
                               .field = std::string{field.name},
                               .value = value,
                               .index = 0,
                               .weights = field.weights});
            }
            continue;
        }
        for (std::size_t index = 0; index < backend.adapters.size(); ++index) {
            if (!backend.adapters[index].path.empty()) {
                out.push_back({.backend = std::string{name},
                               .field = std::string{field.name},
                               .value = backend.adapters[index].path,
                               .index = index,
                               .weights = field.weights});
            }
        }
    }
    return out;
}

std::vector<StoreReference> store_references(const harness::Config& config) {
    std::vector<StoreReference> out;
    for (const auto& [name, backend] : config.backends) {
        for (StoreReference& reference : backend_references(name, backend)) {
            out.push_back(std::move(reference));
        }
    }
    if (!config.models.default_video.empty()) {
        out.push_back({.backend = {},
                       .field = std::string{kVideoPointer},
                       .value = config.models.default_video,
                       .index = 0,
                       .weights = false});
    }
    return out;
}

std::string stored_model_at(const models::StoreRoots& roots, const std::filesystem::path& file) {
    if (file.empty()) {
        return {};
    }
    const std::filesystem::path path = expanded(file.string());
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
        if (models::is_store_format(format)) {
            return model;
        }
    }
    return {};
}

std::string referenced_model(const models::StoreRoots& roots, const StoreReference& reference) {
    if (!reference.backend.empty()) {
        return stored_model_at(roots, reference.value);
    }
    const models::StoreTarget target = models::resolve_store_target(roots, reference.value);
    return target.error.empty() && !target.outside ? target.model : std::string{};
}

bool points_inside(const models::StoreRoots& roots, const StoreReference& reference,
                   const std::filesystem::path& dir) {
    if (!reference.backend.empty()) {
        return within(expanded(reference.value), dir);
    }
    const models::StoreTarget target = models::resolve_store_target(roots, reference.value);
    if (!target.error.empty() || target.outside) {
        return false;
    }
    // The set it names, or one inside it; else the model (or format) whose
    // one video model it runs.
    return within(target.path, dir) || (within(dir, target.path) && runs_video(dir));
}

std::optional<std::string> repointed_value(const models::StoreRoots& roots,
                                           const StoreReference& reference, std::string_view model,
                                           std::string_view renamed) {
    const std::string& pointer = reference.value;
    if (!reference.backend.empty()) {
        if (stored_model_at(roots, pointer) != model) {
            return std::nullopt;
        }
        return rebased(pointer, model, renamed);
    }
    // `models.default_video` names a pipeline by the model's own name, a
    // handle under it, its repository or a path into it (49c): each spelling
    // moves with the directory, and a bare id needs nothing.
    if (pointer.empty()) {
        return std::nullopt;
    }
    if (pointer == model) {
        return std::string{renamed};
    }
    const std::string prefix = std::string{model} + "/";
    if (pointer.starts_with(prefix)) {
        return std::string{renamed} + "/" + pointer.substr(prefix.size());
    }
    if (const std::optional<models::HfRef> ref = models::parse_hf_ref(pointer);
        ref.has_value() && ref->file.empty() && models::repo_directory_name(*ref) == model) {
        return std::string{renamed};
    }
    if (stored_model_at(roots, pointer) == model) {
        return rebased(pointer, model, renamed);
    }
    return std::nullopt;
}

std::string repoint_references(std::string text, const harness::Config& config,
                               const models::StoreRoots& roots,
                               const std::vector<StoreReference>& references,
                               std::string_view model, std::string_view renamed) {
    // An entry's adapters are one list: each repointed in place, the list
    // written once.
    std::vector<std::string> listed;
    for (const StoreReference& reference : references) {
        const std::optional<std::string> value = repointed_value(roots, reference, model, renamed);
        if (!value.has_value()) {
            continue;
        }
        if (reference.backend.empty()) {
            text = harness::set_default_video(text, *value);
            continue;
        }
        const StoreField* field = store_field(reference.field);
        if (field == nullptr) {
            continue;
        }
        if (field->write != nullptr) {
            text = field->write(text, reference.backend, *value);
            continue;
        }
        if (std::ranges::find(listed, reference.backend) != listed.end()) {
            continue;
        }
        listed.push_back(reference.backend);
        const harness::BackendConfig* backend = config.find_backend(reference.backend);
        if (backend == nullptr) {
            continue;
        }
        std::vector<harness::AdapterConfig> adapters = backend->adapters;
        for (harness::AdapterConfig& adapter : adapters) {
            const StoreReference each{.backend = reference.backend,
                                      .field = reference.field,
                                      .value = adapter.path,
                                      .index = 0,
                                      .weights = false};
            if (const std::optional<std::string> moved =
                    repointed_value(roots, each, model, renamed)) {
                adapter.path = *moved;
            }
        }
        text = harness::set_backend_adapters(text, reference.backend, adapters);
    }
    return text;
}

std::string clear_references(std::string text, const harness::Config& config,
                             const std::vector<StoreReference>& references) {
    std::vector<std::string> listed;
    for (const StoreReference& reference : references) {
        if (reference.weights) {
            continue;
        }
        if (reference.backend.empty()) {
            text = harness::set_default_video(text, "");
            continue;
        }
        const StoreField* field = store_field(reference.field);
        if (field == nullptr) {
            continue;
        }
        if (field->write != nullptr) {
            text = field->write(text, reference.backend, "");
            continue;
        }
        if (std::ranges::find(listed, reference.backend) != listed.end()) {
            continue;
        }
        listed.push_back(reference.backend);
        const harness::BackendConfig* backend = config.find_backend(reference.backend);
        if (backend == nullptr) {
            continue;
        }
        // Every adapter of this entry the delete strands goes; the rest stay.
        std::vector<harness::AdapterConfig> kept;
        for (std::size_t index = 0; index < backend->adapters.size(); ++index) {
            const bool dropped = std::ranges::any_of(references, [&](const StoreReference& each) {
                return each.backend == reference.backend && each.field == reference.field &&
                       each.index == index;
            });
            if (!dropped) {
                kept.push_back(backend->adapters[index]);
            }
        }
        text = harness::set_backend_adapters(text, reference.backend, kept);
    }
    return text;
}

std::string clear_door(const harness::Config& config, const std::vector<StoreReference>& stranded,
                       const StoreReference& reference) {
    if (reference.weights) {
        return {};
    }
    if (reference.backend.empty()) {
        return "apogee config set-default-video off";
    }
    if (reference.field != "adapters") {
        return "apogee config set-backend " + reference.backend + " --" + reference.field + " \"\"";
    }
    // The adapters the entry keeps: every one the delete strands left out,
    // since `--adapters` checks and pins each it is given.
    std::string rest;
    if (const harness::BackendConfig* backend = config.find_backend(reference.backend)) {
        for (std::size_t index = 0; index < backend->adapters.size(); ++index) {
            const bool gone = std::ranges::any_of(stranded, [&](const StoreReference& each) {
                return each.backend == reference.backend && each.field == reference.field &&
                       each.index == index;
            });
            if (!gone) {
                rest += (rest.empty() ? "" : ",") + adapter_argument(backend->adapters[index]);
            }
        }
    }
    return "apogee config set-backend " + reference.backend + " --adapters " +
           (rest.empty() ? std::string{"\"\""} : rest);
}

}  // namespace apogee::commands
