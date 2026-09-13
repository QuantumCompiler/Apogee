#include "harness/config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace apogee::harness {
namespace {

/// ASCII-only case folding.
///
/// Deliberately not locale-aware: config keys are identifiers, and a
/// locale-sensitive fold would make the same file load differently on two
/// machines -- the Turkish dotless-i problem, which is a real class of bug in
/// config loaders that reach for tolower() with the default locale.
char fold(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

/// The one place a backend type name is spelled.
///
/// Widening the enum is a row here plus a case in the switch below -- the
/// loader dispatches through this table, so no other file learns the new name.
constexpr std::array<std::pair<std::string_view, BackendType>, 9> kBackendTypeNames{{
    {"anthropic", BackendType::Anthropic},
    {"openai", BackendType::OpenAI},
    {"google", BackendType::Google},
    {"llamacpp", BackendType::LlamaCpp},
    {"claude-cli", BackendType::ClaudeCli},
    {"codex-cli", BackendType::CodexCli},
    {"gemini-cli", BackendType::GeminiCli},
    {"ollama-cli", BackendType::OllamaCli},
    {"mock", BackendType::Mock},
}};

constexpr std::array<std::pair<std::string_view, StatusMode>, 3> kStatusModeNames{{
    {"line", StatusMode::Line},
    {"verbose", StatusMode::Verbose},
    {"quiet", StatusMode::Quiet},
}};

std::string accepted_backend_types() {
    std::string out;
    for (const auto& [name, unused] : kBackendTypeNames) {
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    }
    return out;
}

[[noreturn]] void fail(std::string_view origin, std::string_view message) {
    std::ostringstream out;
    out << origin << ": " << message;
    throw ConfigError(out.str());
}

/// Reads a scalar as a string, expanding ${ENV} references.
/// Returns "" for a null/missing node so an empty key is not an error.
std::string scalar(const YAML::Node& node, std::string_view origin, std::string_view key) {
    if (!node.IsDefined() || node.IsNull()) {
        return {};
    }
    if (!node.IsScalar()) {
        fail(origin, std::string{key} + ": expected a single value");
    }
    return expand_env(node.Scalar());
}

std::optional<std::int64_t> integer(const YAML::Node& node, std::string_view origin,
                                    std::string_view key) {
    if (!node.IsDefined() || node.IsNull()) {
        return std::nullopt;
    }
    try {
        return node.as<std::int64_t>();
    } catch (const YAML::Exception&) {
        fail(origin, std::string{key} + ": expected a whole number, got '" + node.Scalar() + "'");
    }
}

std::optional<double> number(const YAML::Node& node, std::string_view origin,
                             std::string_view key) {
    if (!node.IsDefined() || node.IsNull()) {
        return std::nullopt;
    }
    try {
        return node.as<double>();
    } catch (const YAML::Exception&) {
        fail(origin, std::string{key} + ": expected a number, got '" + node.Scalar() + "'");
    }
}

BackendConfig parse_backend(const YAML::Node& node, std::string_view origin,
                            const std::string& name) {
    const std::string where = "backends." + name;
    if (!node.IsMap()) {
        fail(origin, where + ": expected a block of settings");
    }

    BackendConfig backend;

    const std::string type_name = scalar(node["type"], origin, where + ".type");
    if (type_name.empty()) {
        fail(origin,
             where + ": missing required 'type' (one of: " + accepted_backend_types() + ")");
    }
    const std::optional<BackendType> type = backend_type_from_string(type_name);
    if (!type.has_value()) {
        fail(origin, where + ": unknown type '" + type_name +
                         "' (accepted types: " + accepted_backend_types() + ")");
    }
    backend.type = *type;

    backend.api_key = scalar(node["api_key"], origin, where + ".api_key");
    backend.model = scalar(node["model"], origin, where + ".model");
    backend.model_path = scalar(node["model_path"], origin, where + ".model_path");

    backend.mmproj_path = scalar(node["mmproj_path"], origin, where + ".mmproj_path");
    backend.system_prompt = scalar(node["system_prompt"], origin, where + ".system_prompt");
    backend.context_size = integer(node["context_size"], origin, where + ".context_size");
    backend.max_tokens = integer(node["max_tokens"], origin, where + ".max_tokens");
    backend.temperature = number(node["temperature"], origin, where + ".temperature");
    backend.binary = scalar(node["binary"], origin, where + ".binary");
    backend.mode = scalar(node["mode"], origin, where + ".mode");
    backend.host = scalar(node["host"], origin, where + ".host");
    backend.idle_unload_seconds =
        integer(node["idle_unload_seconds"], origin, where + ".idle_unload_seconds");

    return backend;
}

}  // namespace

bool CaseInsensitiveLess::operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                        [](char a, char b) noexcept { return fold(a) < fold(b); });
}

std::string_view to_string(BackendType value) noexcept {
    for (const auto& [name, type] : kBackendTypeNames) {
        if (type == value) {
            return name;
        }
    }
    return "unknown";
}

std::optional<BackendType> backend_type_from_string(std::string_view name) noexcept {
    for (const auto& [candidate, type] : kBackendTypeNames) {
        if (candidate == name) {
            return type;
        }
    }
    return std::nullopt;
}

std::span<const std::string_view> backend_type_names() noexcept {
    static const std::array<std::string_view, kBackendTypeNames.size()> names = [] {
        std::array<std::string_view, kBackendTypeNames.size()> out{};
        for (std::size_t i = 0; i < kBackendTypeNames.size(); ++i) {
            out[i] = kBackendTypeNames[i].first;
        }
        return out;
    }();
    return names;
}

std::string_view to_string(StatusMode value) noexcept {
    for (const auto& [name, mode] : kStatusModeNames) {
        if (mode == value) {
            return name;
        }
    }
    return "line";
}

std::optional<StatusMode> status_mode_from_string(std::string_view name) noexcept {
    for (const auto& [candidate, mode] : kStatusModeNames) {
        if (candidate == name) {
            return mode;
        }
    }
    return std::nullopt;
}

const BackendConfig* Config::find_backend(std::string_view name) const noexcept {
    const auto it = backends.find(name);
    return it == backends.end() ? nullptr : &it->second;
}

std::vector<std::string> Config::backend_names() const {
    std::vector<std::string> names;
    names.reserve(backends.size());
    for (const auto& [name, unused] : backends) {
        names.push_back(name);
    }
    return names;
}

const EmbeddingConfig* Config::find_embedding(std::string_view name) const noexcept {
    const auto it = embeddings.find(name);
    return it == embeddings.end() ? nullptr : &it->second;
}

std::vector<std::string> Config::embedding_names() const {
    std::vector<std::string> names;
    names.reserve(embeddings.size());
    for (const auto& [name, unused] : embeddings) {
        names.push_back(name);
    }
    return names;
}

std::string expand_env(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '$') {
            out.push_back(input[i]);
            continue;
        }
        // "$$" is a literal '$' -- the escape hatch for a value that really
        // does contain one.
        if (i + 1 < input.size() && input[i + 1] == '$') {
            out.push_back('$');
            ++i;
            continue;
        }
        if (i + 1 >= input.size() || input[i + 1] != '{') {
            out.push_back('$');
            continue;
        }
        const std::size_t close = input.find('}', i + 2);
        if (close == std::string_view::npos) {
            // Unterminated: pass through untouched rather than guessing where
            // the name ends.
            out.push_back('$');
            continue;
        }
        const std::string name{input.substr(i + 2, close - (i + 2))};
        // An undefined variable expands to empty, not an error: a config
        // naming ${ANTHROPIC_API_KEY} must still load on a machine with no
        // key. "Local by default, cloud by choice."
        if (const char* value = std::getenv(name.c_str()); value != nullptr) {
            out += value;
        }
        i = close;
    }

    return out;
}

Config parse_config(std::string_view content, std::string_view origin) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string{content});
    } catch (const YAML::Exception& e) {
        fail(origin, std::string{"not valid YAML: "} + e.what());
    }

    Config config;

    // An empty file is a valid config: zero backends, zero models. A fresh
    // install with no keys and no models must load and pass `apogee check`.
    if (!root.IsDefined() || root.IsNull()) {
        return config;
    }
    if (!root.IsMap()) {
        fail(origin, "expected a mapping at the top level");
    }

    if (const YAML::Node backends = root["backends"]; backends.IsDefined() && !backends.IsNull()) {
        if (!backends.IsMap()) {
            fail(origin, "backends: expected a mapping of name -> settings");
        }
        for (const auto& entry : backends) {
            const std::string name = entry.first.Scalar();
            if (name.empty()) {
                fail(origin, "backends: an entry has an empty name");
            }
            BackendConfig backend = parse_backend(entry.second, origin, name);
            const auto [it, inserted] = config.backends.emplace(name, std::move(backend));
            if (!inserted) {
                // Reached only when two keys fold to the same name. Ommi
                // merged them (Viper lowercased keys); Apogee names both.
                fail(origin, "backends: '" + name + "' collides with '" + it->first +
                                 "' -- backend names are compared case-insensitively, so these "
                                 "would be the same backend; rename one");
            }
        }
    }

    if (const YAML::Node models = root["models"]; models.IsDefined() && !models.IsNull()) {
        if (!models.IsMap()) {
            fail(origin, "models: expected a mapping");
        }
        config.models.default_backend = scalar(models["default"], origin, "models.default");
        config.models.default_embedding =
            scalar(models["default_embedding"], origin, "models.default_embedding");
        config.models.default_extraction =
            scalar(models["default_extraction"], origin, "models.default_extraction");
    }

    if (const YAML::Node paths = root["paths"]; paths.IsDefined() && !paths.IsNull()) {
        if (!paths.IsMap()) {
            fail(origin, "paths: expected a mapping");
        }
        config.paths.gguf_dir = scalar(paths["gguf_dir"], origin, "paths.gguf_dir");
        config.paths.hf_dir = scalar(paths["hf_dir"], origin, "paths.hf_dir");
        config.paths.mcp_dir = scalar(paths["mcp_dir"], origin, "paths.mcp_dir");
        config.paths.embeddings_dir =
            scalar(paths["embeddings_dir"], origin, "paths.embeddings_dir");
    }

    if (const YAML::Node embeddings = root["embeddings"];
        embeddings.IsDefined() && !embeddings.IsNull()) {
        if (!embeddings.IsMap()) {
            fail(origin, "embeddings: expected a mapping of collection name -> settings");
        }
        for (const auto& entry : embeddings) {
            const std::string name = entry.first.Scalar();
            if (name.empty()) {
                fail(origin, "embeddings: an entry has an empty name");
            }
            const std::string where = "embeddings." + name;
            const YAML::Node node = entry.second;
            EmbeddingConfig collection;
            if (node.IsDefined() && !node.IsNull()) {
                if (!node.IsMap()) {
                    fail(origin, where + ": expected a mapping of settings");
                }
                collection.chunk_size = integer(node["chunk_size"], origin, where + ".chunk_size");
                collection.chunk_overlap =
                    integer(node["chunk_overlap"], origin, where + ".chunk_overlap");
                collection.description =
                    scalar(node["description"], origin, where + ".description");
            }
            const auto [it, inserted] = config.embeddings.emplace(name, std::move(collection));
            if (!inserted) {
                // The same rule as backends, for the same reason: a collection
                // is addressed by name, and two names that fold together would
                // be the same file on a case-insensitive filesystem.
                fail(origin, "embeddings: '" + name + "' collides with '" + it->first +
                                 "' -- collection names are compared case-insensitively, so "
                                 "these would be the same collection; rename one");
            }
        }
    }

    if (const YAML::Node auto_rag = root["auto_rag"]; auto_rag.IsDefined() && !auto_rag.IsNull()) {
        config.auto_rag = scalar(auto_rag, origin, "auto_rag");
    }

    if (const YAML::Node mode = root["status_mode"]; mode.IsDefined() && !mode.IsNull()) {
        const std::string name = scalar(mode, origin, "status_mode");
        const std::optional<StatusMode> parsed = status_mode_from_string(name);
        if (!parsed.has_value()) {
            fail(origin,
                 "status_mode: unknown value '" + name + "' (accepted: line, verbose, quiet)");
        }
        config.status_mode = *parsed;
    }

    if (const YAML::Node color = root["color"]; color.IsDefined() && !color.IsNull()) {
        try {
            config.color = color.as<bool>();
        } catch (const YAML::Exception&) {
            fail(origin, "color: expected true or false, got '" + color.Scalar() + "'");
        }
    }

    return config;
}

Config load_config(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ConfigError(path.string() +
                          ": cannot open config file (run 'apogee config init' to create one)");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw ConfigError(path.string() + ": error reading config file");
    }
    return parse_config(buffer.str(), path.string());
}

}  // namespace apogee::harness
