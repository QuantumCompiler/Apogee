#include "operations/backend_names.h"

#include <algorithm>

#include "backends/model_roster.h"

#include "harness/harness.h"

namespace apogee::commands {

std::string helper_backend(const harness::Config& config, harness::ModelRole role,
                           std::string_view conversation) {
    return harness::resolve_backend_key(
        config, harness::RoleRequest{.role = role, .conversation = conversation});
}

std::string named_utility(const harness::Config& config) {
    const harness::Resolution resolved =
        harness::resolve_backend(config, harness::RoleRequest{.role = harness::ModelRole::Utility});
    return harness::is_named(resolved.from) ? resolved.key : std::string{};
}

std::string configured_backend_key(const harness::Config& config, std::string_view model) {
    if (model.empty()) {
        return {};
    }
    // The key as WRITTEN, found the way the map compares keys -- a request
    // spelling `Claude` must land on the entry named `claude`, and the name
    // handed back must be the file's spelling so later lookups agree.
    if (const auto it = config.backends.find(model); it != config.backends.end()) {
        return it->first;
    }
    const std::string normalized = harness::normalize_route_key(model);
    for (const auto& [name, entry] : config.backends) {
        if (entry.model == model || harness::normalize_route_key(entry.model) == normalized) {
            return name;
        }
    }
    return {};
}

bool names_a_configured_backend(const harness::Config& config, std::string_view model) {
    return !configured_backend_key(config, model).empty();
}

RosterResolution resolve_roster_model(const harness::Config& config, std::string_view model) {
    RosterResolution out;
    const backends::RosterCache cache = backends::load_roster_cache();
    for (const auto& [type, roster] : cache.rosters) {
        const bool listed = std::any_of(
            roster.models.begin(), roster.models.end(),
            [model](const backends::RosterModel& entry) { return entry.id == model; });
        if (!listed) {
            continue;
        }
        // Only a *configured* type's roster owns anything: the first entry of
        // that type, by key order, is the one a sole owner runs on.
        for (const auto& [name, entry] : config.backends) {
            if (harness::to_string(entry.type) == type) {
                if (out.owners.empty() || out.owners.back() != type) {
                    out.owners.push_back(type);
                    if (out.backend.empty()) {
                        out.backend = name;
                    }
                }
                break;
            }
        }
    }
    if (out.owners.size() == 1) {
        out.model = std::string{model};
    } else {
        out.backend.clear();
    }
    return out;
}

}  // namespace apogee::commands
