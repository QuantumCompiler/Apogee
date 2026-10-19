#include "modelstore/pipeline_info.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <set>
#include <system_error>

#include "modelstore/snapshot.h"

namespace apogee::models {
namespace {

/// The variants `diffusers` publishes weights under, beside or instead of
/// the plain ones. A name ending in anything else is a plain file.
constexpr std::array<std::string_view, 5> kVariants{"fp16", "bf16", "fp32", "fp8", "int8"};

constexpr std::string_view kSafetensors = ".safetensors";

[[nodiscard]] nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return nlohmann::json{};
    }
    const nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
    return parsed.is_discarded() ? nlohmann::json{} : parsed;
}

[[nodiscard]] std::string string_at(const nlohmann::json& parent, const char* key) {
    const auto found = parent.find(key);
    return found != parent.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

[[nodiscard]] bool known_variant(std::string_view token) {
    return std::ranges::find(kVariants, token) != kVariants.end();
}

/// A component's entry in the index: `[library, class]`, either of them
/// null in a component the checkpoint does not ship. Nullopt for anything
/// else -- a key that is one of the pipeline's own settings.
struct Declared {
    std::string library;
    std::string class_name;
    bool present = false;
};

[[nodiscard]] std::optional<Declared> declared(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 2) {
        return std::nullopt;
    }
    const nlohmann::json& library = value.at(0);
    const nlohmann::json& class_name = value.at(1);
    if ((!library.is_string() && !library.is_null()) ||
        (!class_name.is_string() && !class_name.is_null())) {
        return std::nullopt;
    }
    Declared out;
    out.library = library.is_string() ? library.get<std::string>() : std::string{};
    out.class_name = class_name.is_string() ? class_name.get<std::string>() : std::string{};
    out.present = !out.class_name.empty();
    return out;
}

[[nodiscard]] bool any_file(const std::filesystem::path& dir,
                            std::initializer_list<std::string_view> names) {
    std::error_code code;
    return std::ranges::any_of(names, [&](std::string_view name) {
        return std::filesystem::is_regular_file(dir / std::string{name}, code);
    });
}

[[nodiscard]] bool has_tokenizer(const std::filesystem::path& dir) {
    if (any_file(dir, {"tokenizer.json", "tokenizer_config.json", "tokenizer.model", "spiece.model",
                       "vocab.json", "vocab.txt"})) {
        return true;
    }
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (entry.is_regular_file(code) && entry.path().extension() == ".tiktoken") {
            return true;
        }
    }
    return false;
}

/// A model component's weights: at least one shard, every shard each of its
/// weight indexes names present, and each one whole -- the problem, or
/// empty.
[[nodiscard]] std::string check_weights(const std::filesystem::path& dir,
                                        const PipelineComponent& component) {
    const std::string prefix = component.name + "/";
    if (component.shards.empty()) {
        return prefix + " holds no weights (*.safetensors)";
    }
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        const std::string name = entry.path().filename().string();
        const bool index =
            name.ends_with(".safetensors.index.json") ||
            (name.find(".safetensors.index.") != std::string::npos && name.ends_with(".json"));
        if (!entry.is_regular_file(code) || !index) {
            continue;
        }
        const nlohmann::json parsed = read_json(entry.path());
        const auto map = parsed.is_object() ? parsed.find("weight_map") : parsed.end();
        if (!parsed.is_object() || map == parsed.end() || !map->is_object()) {
            continue;
        }
        std::set<std::string> named;
        for (const auto& [tensor, shard] : map->items()) {
            if (shard.is_string()) {
                named.insert(shard.get<std::string>());
            }
        }
        for (const std::string& shard : named) {
            if (!std::filesystem::is_regular_file(dir / shard, code)) {
                return prefix + shard + " is missing (" + name + " names it)";
            }
        }
    }
    for (const MlxShard& shard : component.shards) {
        if (const std::string problem = safetensors_shard_problem(dir / shard.name, shard.bytes);
            !problem.empty()) {
            return prefix + problem;
        }
    }
    return {};
}

/// What `component`'s kind needs in `dir` -- the problem, or empty.
[[nodiscard]] std::string check_component(const std::filesystem::path& dir,
                                          const PipelineComponent& component) {
    const std::string prefix = component.name + "/";
    switch (component.kind) {
        case PipelineComponentKind::Scheduler:
            return any_file(dir, {"scheduler_config.json"})
                       ? std::string{}
                       : prefix + " has no scheduler_config.json";
        case PipelineComponentKind::Tokenizer:
            return has_tokenizer(dir) ? std::string{}
                                      : prefix +
                                            " holds no tokenizer files (tokenizer.json, "
                                            "tokenizer_config.json or a vocabulary)";
        case PipelineComponentKind::Processor:
            return any_file(dir, {"preprocessor_config.json"})
                       ? std::string{}
                       : prefix + " has no preprocessor_config.json";
        case PipelineComponentKind::Model:
            break;
    }
    if (!any_file(dir, {"config.json"})) {
        return prefix + " has no config.json";
    }
    if (!read_json(dir / "config.json").is_object()) {
        return prefix + "config.json is not a JSON object";
    }
    if (config_is_download_record(dir)) {
        return prefix +
               "config.json is an Apogee download record, not the component's configuration";
    }
    return check_weights(dir, component);
}

}  // namespace

std::string_view to_string(PipelineComponentKind kind) noexcept {
    switch (kind) {
        case PipelineComponentKind::Scheduler:
            return "scheduler";
        case PipelineComponentKind::Tokenizer:
            return "tokenizer";
        case PipelineComponentKind::Processor:
            return "processor";
        case PipelineComponentKind::Model:
            break;
    }
    return "model";
}

PipelineComponentKind component_kind(std::string_view class_name) noexcept {
    if (class_name.find("Scheduler") != std::string_view::npos) {
        return PipelineComponentKind::Scheduler;
    }
    if (class_name.find("Tokenizer") != std::string_view::npos) {
        return PipelineComponentKind::Tokenizer;
    }
    if (class_name.ends_with("Processor") || class_name.ends_with("FeatureExtractor")) {
        return PipelineComponentKind::Processor;
    }
    return PipelineComponentKind::Model;
}

std::size_t PipelineInfo::shard_count() const noexcept {
    std::size_t count = 0;
    for (const PipelineComponent& component : components) {
        count += component.shards.size();
    }
    return count;
}

std::string PipelineInfo::variant() const {
    std::string shared;
    bool any = false;
    for (const PipelineComponent& component : components) {
        if (component.kind != PipelineComponentKind::Model || component.shards.empty()) {
            continue;
        }
        if (!any) {
            shared = component.variant;
            any = true;
        } else if (component.variant != shared) {
            return {};
        }
    }
    return shared;
}

std::string weights_variant(std::string_view file_name) {
    if (file_name.ends_with(kSafetensors)) {
        const std::string_view stem = file_name.substr(0, file_name.size() - kSafetensors.size());
        const std::size_t dot = stem.rfind('.');
        if (dot != std::string_view::npos && known_variant(stem.substr(dot + 1))) {
            return std::string{stem.substr(dot + 1)};
        }
        return {};
    }
    constexpr std::string_view index = ".safetensors.index.";
    constexpr std::string_view json = ".json";
    if (const std::size_t at = file_name.find(index);
        at != std::string_view::npos && file_name.ends_with(json)) {
        const std::size_t start = at + index.size();
        const std::size_t end = file_name.size() - json.size();
        if (end > start && known_variant(file_name.substr(start, end - start))) {
            return std::string{file_name.substr(start, end - start)};
        }
    }
    return {};
}

std::vector<std::string> pipeline_components(std::string_view model_index) {
    std::vector<std::string> out;
    const nlohmann::json index = nlohmann::json::parse(model_index, nullptr, false);
    if (!index.is_object()) {
        return out;
    }
    for (const auto& [key, value] : index.items()) {
        if (key.empty() || key.front() == '_') {
            continue;
        }
        if (const std::optional<Declared> component = declared(value);
            component.has_value() && component->present) {
            out.push_back(key);
        }
    }
    std::ranges::sort(out);
    return out;
}

bool is_pipeline_dir(const std::filesystem::path& dir) {
    std::error_code code;
    return std::filesystem::is_regular_file(dir / std::string{kModelIndex}, code);
}

PipelineInfo read_pipeline_info(const std::filesystem::path& dir) {
    PipelineInfo info;
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code)) {
        info.problem = "cannot load: " + dir.string() + " is not a directory";
        return info;
    }
    for (auto it = std::filesystem::recursive_directory_iterator(dir, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        if (it->is_regular_file(code)) {
            info.bytes += it->file_size(code);
        }
    }

    // The index first: without it there is no pipeline to describe.
    if (!is_pipeline_dir(dir)) {
        info.problem = "cannot load: no model_index.json in " + dir.string();
        return info;
    }
    const nlohmann::json index = read_json(dir / std::string{kModelIndex});
    if (!index.is_object()) {
        info.problem = "cannot load: model_index.json is not a JSON object";
        return info;
    }
    info.index_read = true;
    info.class_name = string_at(index, "_class_name");
    info.diffusers_version = string_at(index, "_diffusers_version");
    for (const auto& [key, value] : index.items()) {
        if (key.empty() || key.front() == '_') {
            continue;
        }
        const std::optional<Declared> component = declared(value);
        if (!component.has_value()) {
            continue;  // one of the pipeline's settings
        }
        if (!component->present) {
            info.absent.push_back(key);
            continue;
        }
        PipelineComponent entry;
        entry.name = key;
        entry.library = component->library;
        entry.class_name = component->class_name;
        entry.kind = component_kind(component->class_name);
        info.components.push_back(std::move(entry));
    }
    std::ranges::sort(info.components, {}, &PipelineComponent::name);
    std::ranges::sort(info.absent);
    if (info.class_name.empty()) {
        info.problem = "cannot load: model_index.json names no pipeline class (_class_name)";
        return info;
    }
    if (info.components.empty()) {
        info.problem = "cannot load: model_index.json names no components";
        return info;
    }

    // Every component's directory, with what its kind needs, whole.
    std::string problem;
    for (PipelineComponent& component : info.components) {
        const std::filesystem::path at = dir / component.name;
        if (!std::filesystem::is_directory(at, code)) {
            if (problem.empty()) {
                problem = "the " + component.name + " component (" + component.class_name +
                          ") is missing -- no " + component.name + "/ directory";
            }
            continue;
        }
        for (auto it = std::filesystem::recursive_directory_iterator(at, code);
             !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
            if (!it->is_regular_file(code)) {
                continue;
            }
            const std::uintmax_t size = it->file_size(code);
            ++component.files;
            component.bytes += size;
            if (it.depth() == 0 && it->path().extension() == kSafetensors) {
                component.shards.push_back({.name = it->path().filename().string(), .bytes = size});
            }
        }
        std::ranges::sort(component.shards, {}, &MlxShard::name);
        if (!component.shards.empty()) {
            std::set<std::string> variants;
            for (const MlxShard& shard : component.shards) {
                variants.insert(weights_variant(shard.name));
            }
            if (variants.size() == 1) {
                component.variant = *variants.begin();
            }
        }
        if (std::string found = check_component(at, component); problem.empty() && !found.empty()) {
            problem = std::move(found);
        }
    }
    if (!problem.empty()) {
        info.problem = "cannot load: " + problem;
        return info;
    }
    info.complete = true;
    return info;
}

std::string describe(const PipelineInfo& info) {
    std::string names;
    for (const PipelineComponent& component : info.components) {
        names += (names.empty() ? "" : ", ") + component.name;
    }
    std::string out = (info.class_name.empty() ? std::string{"a pipeline"} : info.class_name) +
                      ", " + std::to_string(info.components.size()) +
                      (info.components.size() == 1 ? " component" : " components") +
                      (names.empty() ? "" : " (" + names + ")") + ", " +
                      std::to_string(info.shard_count()) + " shard(s)";
    if (const std::string variant = info.variant(); !variant.empty()) {
        out += ", " + variant + " weights";
    }
    return out;
}

}  // namespace apogee::models
