#include "cli/providers_cmd.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "backends/factory.h"
#include "backends/model_roster.h"
#include "backends/provider_probe.h"
#include "backends/provider_table.h"
#include "cli/helpers.h"
#include "cli/provider_offer.h"
#include "contracts/config.h"
#include "contracts/paths.h"
#include "harness/harness.h"
#include "platform/platform.h"
#include "secrets/store.h"

namespace apogee::commands {

namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee providers: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

[[nodiscard]] std::string padded(std::string text, std::size_t width) {
    if (text.size() < width) {
        text.append(width - text.size(), ' ');
    }
    return text;
}

[[nodiscard]] std::string tier_words(const backends::ProviderStatus& status) {
    std::string words{backends::to_string(status.tier())};
    if (status.tier() == backends::ProviderTier::Verified && status.verified.has_value()) {
        words += " " + status.verified->date;
    }
    return words;
}

/// Which configured backends reach `status`'s provider, said for the row.
[[nodiscard]] std::string registration_words(const backends::ProviderStatus& status,
                                             const harness::Config* config) {
    if (config == nullptr) {
        return {};
    }
    std::string names;
    for (const auto& [name, entry] : config->backends) {
        if (entry.type == status.type) {
            names += names.empty() ? "" : ", ";
            names += name;
        }
    }
    if (!names.empty()) {
        return "backend: " + names;
    }
    return offerable(status) ? "not registered" : "";
}

struct ScanFlags {
    bool register_found = false;
    bool refresh = false;
};


/// The rosters (M13): each catalogue-capable provider type's live model
/// list, fetched on the user's word -- right after a registration, or on an
/// explicit --refresh -- and cached disposably. Never on a plain scan, and
/// never on a hot path: the cache is what everything else reads. One fetch
/// per vendor type; a failure keeps the cached roster, said with its date.
void fetch_rosters(const std::filesystem::path& config_path, bool refresh_all) {
    harness::Config config;
    try {
        config = harness::load_config(config_path);
    } catch (const harness::ConfigError&) {
        return;  // the scan already said what is wrong with the config
    }
    harness::Harness harness{config};
    backends::BuildOptions build_options;
    build_options.config_path = config_path;
    (void)backends::build_providers(harness, build_options);

    backends::RosterCache cache = backends::load_roster_cache();
    bool changed = false;
    std::set<std::string> asked;
    for (const auto& [name, backend] : config.backends) {
        const std::string type{harness::to_string(backend.type)};
        if (!asked.insert(type).second) {
            continue;  // the catalogue is the vendor's: one fetch per type
        }
        harness::CatalogListing* catalog = harness.catalog_for(name);
        if (catalog == nullptr) {
            continue;
        }
        const backends::ProviderRoster* cached = cache.roster_for(type);
        if (!refresh_all && cached != nullptr) {
            continue;  // a registration fetches the missing; --refresh re-fetches all
        }
        try {
            const std::vector<harness::ModelInfo> models =
                catalog->list_catalog(harness::CancellationToken{});
            backends::ProviderRoster roster;
            roster.fetched_at = platform::utc_time(std::chrono::system_clock::now(), "%Y-%m-%d");
            for (const harness::ModelInfo& model : models) {
                roster.models.push_back(backends::RosterModel{model.id, model.name});
            }
            const std::size_t count = roster.models.size();
            cache.rosters.insert_or_assign(type, std::move(roster));
            changed = true;
            std::cout << "fetched " << type << "\'s models: " << count << " on the roster\n";
        } catch (const std::exception& e) {
            std::cout << "could not fetch " << type << "\'s models: " << e.what()
                      << (cached != nullptr
                              ? " -- the roster from " + cached->fetched_at + " stands"
                              : std::string{})
                      << "\n";
        }
    }
    if (changed) {
        if (const std::string error = backends::save_roster_cache(cache); !error.empty()) {
            std::cout << "could not keep the rosters: " << error << "\n";
        }
    }
}

}  // namespace

std::string render_provider_scan(const std::vector<backends::ProviderStatus>& rows,
                                 const harness::Config* config) {
    constexpr std::size_t kIdWidth = 11;
    constexpr std::size_t kTierWidth = 22;
    const std::string indent(kIdWidth, ' ');
    std::string out;
    for (const backends::ProviderStatus& status : rows) {
        std::string line = padded(status.id, kIdWidth);
        const std::string registered = registration_words(status, config);
        line += registered.empty() ? tier_words(status) : padded(tier_words(status), kTierWidth);
        line += registered;
        out += line + "\n";
        if (!status.installed_evidence.empty()) {
            out += indent + status.installed_evidence + "\n";
        }
        if (status.installed && !status.credential_evidence.empty() &&
            status.credential_evidence != status.installed_evidence) {
            out += indent + status.credential_evidence + "\n";
        }
        if (status.tier() == backends::ProviderTier::Verified && status.verified.has_value()) {
            out += indent + "answered a turn on " + status.verified->date + " (" +
                   status.verified->backend + ")\n";
        }
    }
    return out;
}

std::string_view ProvidersCommand::name() const noexcept {
    return "providers";
}

std::string_view ProvidersCommand::summary() const noexcept {
    return "See which providers this machine has, and register them as backends";
}

void ProvidersCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    auto flags = std::make_shared<ScanFlags>();
    CLI::App* scan = cmd->add_subcommand(
        "scan",
        "Detect each provider -- installed, credentials found, verified -- with the evidence");
    scan->add_flag("--register", flags->register_found,
                   "Add one backend for each detected provider that has none, through the config "
                   "editor");
    scan->add_flag("--refresh", flags->refresh,
                   "Ask every installed CLI its version again, even when the binary is unchanged");
    scan->callback([&context, flags]() {
        std::filesystem::path config_path;
        try {
            config_path = harness::resolve_config_path(context.config_path);
        } catch (const std::exception& e) {
            fail(e.what());
        }
        std::optional<harness::Config> config;
        std::error_code missing;
        if (std::filesystem::exists(config_path, missing)) {
            try {
                config = harness::load_config(config_path);
            } catch (const harness::ConfigError& e) {
                fail(e.what());
            }
        }

        const secrets::CredentialStore store{secrets::credentials_path(config_path)};
        backends::ScanOptions options;
        options.refresh = flags->refresh;
        const backends::ScanReport report = backends::scan_host_providers(options, &store);
        std::cout << render_provider_scan(report.providers, config ? &*config : nullptr);

        if (!config.has_value()) {
            if (flags->register_found) {
                fail("no config file at " + config_path.string() +
                     " -- run 'apogee config init' first, then register");
            }
            std::cout << "\nno config file yet -- 'apogee config init' writes one\n";
            return;
        }
        const RegistrationPlan plan = plan_registration(report.providers, *config);
        if (!flags->register_found) {
            if (flags->refresh) {
                // An explicit refresh re-fetches every roster (M13).
                fetch_rosters(config_path, /*refresh_all=*/true);
            }
            if (plan.registers() > 0) {
                std::cout << "\nregister the detected ones as backends: apogee providers scan "
                             "--register\n";
            }
            return;
        }
        try {
            apply_registration(config_path, plan);
        } catch (const std::exception& e) {
            fail(e.what());
        }
        std::cout << "\n" << render_registration(plan, /*applied=*/true);
        // Registration brings the models (M13): the rosters of the types now
        // configured, fetched on this -- the user's -- word.
        fetch_rosters(config_path, /*refresh_all=*/flags->refresh);
    });
}

}  // namespace apogee::commands
