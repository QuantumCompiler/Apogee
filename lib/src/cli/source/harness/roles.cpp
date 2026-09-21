#include "harness/roles.h"

namespace apogee::harness {
namespace {

/// Trims ASCII whitespace from both ends.
[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    const auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    while (!value.empty() && is_space(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && is_space(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

/// The role pointer's value, or "" for a role that has none of its own.
[[nodiscard]] std::string_view pointer_for(const Config& config, ModelRole role) noexcept {
    switch (role) {
        case ModelRole::Embedding:
            return config.models.default_embedding;
        case ModelRole::Extraction:
            return config.models.default_extraction;
        case ModelRole::Chat:
            break;
    }
    // Chat has no pointer of its own: `models.default` IS its pointer, and it
    // is the last rung below. Returning it here would make the chain read the
    // same value twice, which is harmless but hides that Chat is the base case.
    return {};
}

}  // namespace

std::string_view to_string(ModelRole role) noexcept {
    switch (role) {
        case ModelRole::Embedding:
            return "default_embedding";
        case ModelRole::Extraction:
            return "default_extraction";
        case ModelRole::Chat:
            break;
    }
    return "default";
}

Resolution resolve_backend(const Config& config, const RoleRequest& request) {
    // The order below IS the contract, and it is table-tested rung by rung in
    // tests/harness/roles_test.cpp. Reordering these four returns is the whole
    // bug this file exists to prevent, so it is asserted rather than reviewed.
    if (const std::string_view value = trim(request.override); !value.empty()) {
        return {.key = std::string{value}, .from = ResolvedFrom::Override};
    }
    if (const std::string_view value = trim(request.entry_backend); !value.empty()) {
        return {.key = std::string{value}, .from = ResolvedFrom::EntryBackend};
    }
    if (const std::string_view value = trim(pointer_for(config, request.role)); !value.empty()) {
        return {.key = std::string{value}, .from = ResolvedFrom::RolePointer};
    }
    if (const std::string_view value = trim(config.models.default_backend); !value.empty()) {
        return {.key = std::string{value}, .from = ResolvedFrom::Default};
    }
    return {};
}

std::string resolve_backend_key(const Config& config, const RoleRequest& request) {
    return resolve_backend(config, request).key;
}

std::string resolve_chat_backend(const Config& config, std::string_view override) {
    return resolve_backend_key(config, RoleRequest{.role = ModelRole::Chat, .override = override});
}

}  // namespace apogee::harness
