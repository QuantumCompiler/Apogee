#include "secrets/resolve.h"

#include <array>
#include <cstdlib>
#include <utility>

namespace apogee::secrets {
namespace {

constexpr std::array<std::string_view, 1> kAnthropicVariables{"ANTHROPIC_API_KEY"};
constexpr std::array<std::string_view, 1> kOpenAiVariables{"OPENAI_API_KEY"};
// The Gemini SDKs read GEMINI_API_KEY first; both are honoured, in this order.
constexpr std::array<std::string_view, 2> kGoogleVariables{"GEMINI_API_KEY", "GOOGLE_API_KEY"};
constexpr std::array<std::string_view, 4> kAllVariables{"ANTHROPIC_API_KEY", "OPENAI_API_KEY",
                                                        "GEMINI_API_KEY", "GOOGLE_API_KEY"};

}  // namespace

std::string_view to_string(KeySource source) noexcept {
    switch (source) {
        case KeySource::None:
            return "none";
        case KeySource::Config:
            return "config";
        case KeySource::Store:
            return "store";
        case KeySource::Environment:
            return "environment";
    }
    return "none";
}

bool takes_api_key(harness::BackendType type) noexcept {
    return !conventional_variables(type).empty();
}

std::span<const std::string_view> conventional_variables(harness::BackendType type) noexcept {
    switch (type) {
        case harness::BackendType::Anthropic:
            return kAnthropicVariables;
        case harness::BackendType::OpenAI:
            return kOpenAiVariables;
        case harness::BackendType::Google:
            return kGoogleVariables;
        case harness::BackendType::LlamaCpp:
        case harness::BackendType::ClaudeCli:
        case harness::BackendType::CodexCli:
        case harness::BackendType::GeminiCli:
        case harness::BackendType::OllamaCli:
        case harness::BackendType::Mock:
            return {};
    }
    return {};
}

std::optional<std::string> slot_name(harness::BackendType type) {
    if (!takes_api_key(type)) {
        return std::nullopt;
    }
    return std::string{harness::to_string(type)};
}

std::optional<harness::BackendType> slot_type(std::string_view name) noexcept {
    const std::optional<harness::BackendType> type = harness::backend_type_from_string(name);
    if (!type.has_value() || !takes_api_key(*type)) {
        return std::nullopt;
    }
    return type;
}

EnvSnapshot EnvSnapshot::capture(const Lookup& lookup) {
    EnvSnapshot snapshot;
    for (const std::string_view name : kAllVariables) {
        const std::string value = lookup ? lookup(name) : std::string{};
        if (!value.empty()) {
            snapshot.values_[std::string{name}] = value;
        }
    }
    return snapshot;
}

const EnvSnapshot& EnvSnapshot::process() {
    // Function-local: captured the first time anything resolves, which is
    // before any turn runs, and never again for the life of the process.
    static const EnvSnapshot snapshot = capture([](std::string_view name) {
        const char* value = std::getenv(std::string{name}.c_str());
        return value == nullptr ? std::string{} : std::string{value};
    });
    return snapshot;
}

std::string EnvSnapshot::get(std::string_view name) const {
    const auto it = values_.find(name);
    return it == values_.end() ? std::string{} : it->second;
}

KeyResolution resolve_api_key(const harness::BackendConfig& entry, const CredentialStore* store,
                              const EnvSnapshot& env) {
    KeyResolution resolution;
    if (!takes_api_key(entry.type)) {
        return resolution;
    }
    // Rung 1: the entry itself. Already ${ENV}-expanded by the loader, so a
    // "${OPENAI_API_KEY}" reference that expanded to nothing is empty here
    // and falls through -- which is what a user who wrote it expects.
    if (!entry.api_key.empty()) {
        resolution.key = entry.api_key;
        resolution.source = KeySource::Config;
        return resolution;
    }
    // Rung 2: the stored slot for this provider type.
    if (store != nullptr) {
        if (const std::optional<std::string> slot = slot_name(entry.type)) {
            if (std::optional<std::string> stored = store->key_for(*slot);
                stored.has_value() && !stored->empty()) {
                resolution.key = std::move(*stored);
                resolution.source = KeySource::Store;
                return resolution;
            }
        }
    }
    // Rung 3: the conventional variable, from the snapshot.
    for (const std::string_view variable : conventional_variables(entry.type)) {
        if (std::string value = env.get(variable); !value.empty()) {
            resolution.key = std::move(value);
            resolution.source = KeySource::Environment;
            resolution.variable = std::string{variable};
            return resolution;
        }
    }
    return resolution;
}

std::string no_key_message(std::string_view backend_name, harness::BackendType type) {
    const std::span<const std::string_view> variables = conventional_variables(type);
    const std::string variable =
        variables.empty() ? std::string{"<VAR>"} : std::string{variables.front()};
    const std::string slot = slot_name(type).value_or(std::string{harness::to_string(type)});
    return "no API key found for '" + std::string{backend_name} +
           "'. Either set api_key on "
           "this backend (a \"${" +
           variable + "}\" reference is expanded when the config is read), run 'apogee auth add " +
           slot + "', or export " + variable;
}

}  // namespace apogee::secrets
