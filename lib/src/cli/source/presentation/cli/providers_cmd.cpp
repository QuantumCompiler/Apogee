#include "cli/providers_cmd.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
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
/// Returns what it did, a line each, for the caller to say.
[[nodiscard]] std::vector<std::string> fetch_rosters(const std::filesystem::path& config_path,
                                                     bool refresh_all) {
    std::vector<std::string> said;
    harness::Config config;
    try {
        config = harness::load_config(config_path);
    } catch (const harness::ConfigError&) {
        return said;  // the scan already said what is wrong with the config
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
            said.push_back("fetched " + type + "'s models: " + std::to_string(count) +
                           " on the roster");
        } catch (const std::exception& e) {
            said.push_back("could not fetch " + type + "'s models: " + e.what() +
                           (cached != nullptr
                                ? " -- the roster from " + cached->fetched_at + " stands"
                                : std::string{}));
        }
    }
    if (changed) {
        if (const std::string error = backends::save_roster_cache(cache); !error.empty()) {
            said.push_back("could not keep the rosters: " + error);
        }
    }
    return said;
}

void say(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
        std::cout << line << "\n";
    }
}

}  // namespace

std::vector<ProviderRow> provider_rows(const std::vector<backends::ProviderStatus>& statuses,
                                       const harness::Config* config) {
    std::vector<ProviderRow> rows;
    for (const backends::ProviderStatus& status : statuses) {
        ProviderRow row;
        row.provider = status.id;
        row.type = std::string{harness::to_string(status.type)};
        if (const backends::ProviderFacts* facts = backends::provider_for_type(status.type);
            facts != nullptr) {
            row.label = std::string{facts->label};
        }
        row.tier = tier_words(status);
        row.backend = registration_words(status, config);
        if (!status.installed_evidence.empty()) {
            row.evidence.push_back(status.installed_evidence);
        }
        if (status.installed && !status.credential_evidence.empty() &&
            status.credential_evidence != status.installed_evidence) {
            row.evidence.push_back(status.credential_evidence);
        }
        if (status.tier() == backends::ProviderTier::Verified && status.verified.has_value()) {
            row.evidence.push_back("answered a turn on " + status.verified->date + " (" +
                                   status.verified->backend + ")");
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string render_provider_scan(const std::vector<backends::ProviderStatus>& statuses,
                                 const harness::Config* config) {
    constexpr std::size_t kIdWidth = 11;
    constexpr std::size_t kTierWidth = 22;
    const std::string indent(kIdWidth, ' ');
    std::string out;
    for (const ProviderRow& row : provider_rows(statuses, config)) {
        std::string line = padded(row.provider, kIdWidth);
        line += row.backend.empty() ? row.tier : padded(row.tier, kTierWidth);
        line += row.backend;
        out += line + "\n";
        for (const std::string& evidence : row.evidence) {
            out += indent + evidence + "\n";
        }
    }
    return out;
}

nlohmann::json provider_scan_document(const std::vector<ProviderRow>& rows) {
    nlohmann::json data = nlohmann::json::array();
    for (const ProviderRow& row : rows) {
        data.push_back(nlohmann::json{{"provider", row.provider},
                                      {"type", row.type},
                                      {"label", row.label},
                                      {"tier", row.tier},
                                      {"backend", row.backend},
                                      {"evidence", row.evidence}});
    }
    return nlohmann::json{{"object", "list"}, {"data", std::move(data)}};
}

std::vector<backends::ProviderStatus> last_provider_scan() {
    return cached_statuses(backends::load_provider_cache());
}

std::vector<backends::ProviderStatus> scan_providers_now(const std::filesystem::path& config_path,
                                                         bool refresh) {
    const secrets::CredentialStore store{secrets::credentials_path(config_path)};
    backends::ScanOptions options;
    options.refresh = refresh;
    return backends::scan_host_providers(options, &store).providers;
}

std::string register_provider(const std::filesystem::path& config_path, std::string_view provider) {
    const harness::Config config = harness::load_config(config_path);
    // The registration core over the last scan, for this provider alone: the
    // plan, its default and its words are the command's.
    std::vector<backends::ProviderStatus> chosen;
    for (backends::ProviderStatus& status : last_provider_scan()) {
        if (status.id == provider) {
            chosen.push_back(std::move(status));
        }
    }
    const RegistrationPlan plan = plan_registration(chosen, config);
    apply_registration(config_path, plan);
    std::string said = render_registration(plan, /*applied=*/true);
    // Registration brings the models (M13), on this -- the user's -- word.
    if (plan.registers() > 0) {
        for (const std::string& line : fetch_rosters(config_path, /*refresh_all=*/false)) {
            said += line + "\n";
        }
    }
    return said;
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
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    add_read_format(scan, format);
    scan->callback([&context, flags, format]() {
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
        const bool json = *format == ReadFormat::Json;
        // One document on stdout (37b): the rows, and what registering and
        // fetching did, each beside them rather than printed around them.
        nlohmann::json document =
            provider_scan_document(provider_rows(report.providers, config ? &*config : nullptr));
        const auto lines_of = [](const std::string& text) {
            std::vector<std::string> lines;
            std::istringstream in{text};
            for (std::string line; std::getline(in, line);) {
                lines.push_back(line);
            }
            return lines;
        };
        if (!json) {
            std::cout << render_provider_scan(report.providers, config ? &*config : nullptr);
        }

        if (!config.has_value()) {
            if (flags->register_found) {
                fail("no config file at " + config_path.string() +
                     " -- run 'apogee config init' first, then register");
            }
            if (json) {
                write_document(std::cout, document);
                return;
            }
            std::cout << "\nno config file yet -- 'apogee config init' writes one\n";
            return;
        }
        const RegistrationPlan plan = plan_registration(report.providers, *config);
        if (!flags->register_found) {
            std::vector<std::string> fetched;
            if (flags->refresh) {
                // An explicit refresh re-fetches every roster (M13).
                fetched = fetch_rosters(config_path, /*refresh_all=*/true);
            }
            if (json) {
                if (flags->refresh) {
                    document["rosters"] = fetched;
                }
                write_document(std::cout, document);
                return;
            }
            say(fetched);
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
        // Registration brings the models (M13): the rosters of the types now
        // configured, fetched on this -- the user's -- word.
        const std::string registered = render_registration(plan, /*applied=*/true);
        if (json) {
            document["registration"] = lines_of(registered);
            document["rosters"] = fetch_rosters(config_path, /*refresh_all=*/flags->refresh);
            write_document(std::cout, document);
            return;
        }
        std::cout << "\n" << registered;
        say(fetch_rosters(config_path, /*refresh_all=*/flags->refresh));
    });
}

}  // namespace apogee::commands
