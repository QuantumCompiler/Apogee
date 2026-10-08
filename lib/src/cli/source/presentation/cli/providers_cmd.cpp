#include "cli/providers_cmd.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "backends/provider_probe.h"
#include "backends/provider_table.h"
#include "cli/helpers.h"
#include "cli/provider_offer.h"
#include "contracts/config.h"
#include "contracts/paths.h"
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
    });
}

}  // namespace apogee::commands
