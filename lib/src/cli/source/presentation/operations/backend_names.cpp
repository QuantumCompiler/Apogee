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
    const backends::RosterCache cache = backends::known_rosters();
    for (const auto& [type, roster] : cache.rosters) {
        const bool listed =
            std::any_of(roster.models.begin(), roster.models.end(),
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

SessionModel resolve_session_model(const harness::Config& config, std::string_view name) {
    SessionModel out;
    if (std::string key = configured_backend_key(config, name); !key.empty()) {
        out.backend = std::move(key);
        return out;
    }
    if (name.empty()) {
        return out;
    }
    const RosterResolution roster = resolve_roster_model(config, name);
    if (roster.owners.size() > 1) {
        std::string owners;
        for (const std::string& type : roster.owners) {
            owners += (owners.empty() ? std::string{} : " and ") + type;
        }
        out.refusal = "'" + std::string{name} + "' is on " + owners +
                      "'s rosters -- pin it to one entry with 'apogee config add-backend <name> "
                      "--type <type> --model " +
                      std::string{name} + "'";
        return out;
    }
    if (!roster.backend.empty()) {
        out.backend = roster.backend;
        out.pinned = roster.model;
        out.roster = roster.owners.front();
        return out;
    }
    // `<backend>:<model>` (34): the first colon whose left side is a key.
    for (std::size_t colon = name.find(':'); colon != std::string_view::npos;
         colon = name.find(':', colon + 1)) {
        const auto entry = config.backends.find(name.substr(0, colon));
        if (entry == config.backends.end()) {
            continue;
        }
        if (!harness::names_its_model(entry->second.type)) {
            out.refusal = "'" + entry->first +
                          "' runs the weights at its model_path -- a model is pinned by name "
                          "only on an API or vendor-CLI backend";
            return out;
        }
        out.backend = entry->first;
        if (const std::string_view model = name.substr(colon + 1);
            !model.empty() && model != entry->second.model) {
            out.pinned = std::string{model};
        }
        return out;
    }
    return out;
}

std::string roster_pin_note(const SessionModel& model) {
    const std::string on = "on backend '" + model.backend + "'";
    return model.roster.empty() ? on : model.roster + "'s roster, " + on;
}

}  // namespace apogee::commands
