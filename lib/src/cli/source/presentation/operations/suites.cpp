#include "operations/suites.h"

#include <algorithm>
#include <span>
#include <vector>

namespace apogee::commands {
namespace {

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        out += out.empty() ? "" : ", ";
        out += name;
    }
    return out;
}

std::string joined(std::span<const std::string_view> names) {
    std::string out;
    for (const std::string_view name : names) {
        out += out.empty() ? "" : ", ";
        out += name;
    }
    return out;
}

}  // namespace

std::string validate_suite_member(const harness::Config& config, std::string_view role,
                                  const harness::SuiteMember& member) {
    const std::span<const std::string_view> roles = harness::suite_role_names();
    if (std::ranges::find(roles, role) == roles.end()) {
        return "'" + std::string{role} + "' is not a role (accepted: " + joined(roles) + ")";
    }
    const std::string where = std::string{role} + ": ";
    if (member.backend.find_first_not_of(" \t") == std::string::npos) {
        return where + "names no backend";
    }
    if (config.find_backend(member.backend) == nullptr) {
        const std::vector<std::string> known = config.backend_names();
        return where + "no backend named '" + member.backend + "' in this config" +
               (known.empty() ? " (it has no backends yet -- add one with 'apogee config "
                                "add-backend')"
                              : " (known backends: " + joined(known) + ")");
    }
    if (member.context_size.has_value() && *member.context_size < 1) {
        return where + "context_size must be a positive number of tokens";
    }
    if (member.toolset.has_value()) {
        const std::span<const std::string_view> words = harness::suite_toolset_names();
        for (const std::string& word : *member.toolset) {
            if (std::ranges::find(words, word) == words.end()) {
                return where + "'" + word + "' is not a toolset (accepted: " + joined(words) + ")";
            }
        }
    }
    return {};
}

std::string validate_suite(const harness::Config& config, std::string_view name,
                           const harness::SuiteConfig& suite) {
    if (name.empty()) {
        return "a suite needs a name";
    }
    if (const harness::CaseInsensitiveLess less;
        !less(name, harness::kSuiteOff) && !less(harness::kSuiteOff, name)) {
        return "'" + std::string{name} + "' is reserved -- '/suite " +
               std::string{harness::kSuiteOff} + "' means no suite; choose another name";
    }
    if (suite.members.empty()) {
        return "a suite names at least one member -- a backend for one of its roles (" +
               joined(harness::suite_role_names()) + ")";
    }
    for (const auto& [role, member] : suite.members) {
        if (std::string refused = validate_suite_member(config, role, member); !refused.empty()) {
            return refused;
        }
    }
    return {};
}

std::string validate_active_suite(const harness::Config& config) {
    const harness::SuiteConfig* suite = harness::active_suite(config);
    if (suite == nullptr) {
        return {};
    }
    for (const std::string_view role : harness::suite_role_names()) {
        const auto it = suite->members.find(role);
        if (it == suite->members.end() || config.find_backend(it->second.backend) != nullptr) {
            continue;
        }
        const std::vector<std::string> known = config.backend_names();
        return "no backend named '" + it->second.backend + "'" +
               (known.empty() ? std::string{} : " (configured: " + joined(known) + ")") +
               " -- suite " + config.models.default_suite + "'s " + std::string{role} +
               " member; fix it with: apogee config set-suite " + config.models.default_suite +
               " --" + std::string{role} + " <backend>";
    }
    return {};
}

std::string validate_suite_delete(const harness::Config& config, std::string_view name) {
    const harness::SuiteConfig* suite = config.find_suite(name);
    if (suite == nullptr || harness::active_suite(config) != suite) {
        return {};
    }
    return "'" + std::string{name} +
           "' is the default suite (models.default_suite) -- set another first, or none: "
           "apogee config set-default-suite off";
}

}  // namespace apogee::commands
