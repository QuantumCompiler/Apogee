#include "backends/provider_status.h"

#include <string>
#include <system_error>

#include "platform/child_process.h"
#include "platform/platform.h"
#include "secrets/resolve.h"
#include "secrets/store.h"

namespace apogee::backends {

namespace {

class HostExistenceView final : public ExistenceView {
public:
    [[nodiscard]] std::string find_program(std::string_view program) const override {
        if (program.find('/') == std::string_view::npos &&
            program.find('\\') == std::string_view::npos) {
            return platform::find_on_path(program);
        }
        std::error_code error;
        const std::filesystem::path path{program};
        return std::filesystem::is_regular_file(path, error) ? path.string() : std::string{};
    }

    [[nodiscard]] bool exists(const std::filesystem::path& path) const override {
        std::error_code error;
        return std::filesystem::exists(path, error);
    }

    [[nodiscard]] std::optional<BinaryFingerprint> fingerprint(
        const std::filesystem::path& path) const override {
        std::error_code error;
        const std::filesystem::path real = std::filesystem::canonical(path, error);
        if (error) {
            return std::nullopt;
        }
        const auto modified = std::filesystem::last_write_time(real, error);
        if (error) {
            return std::nullopt;
        }
        return BinaryFingerprint{real.string(),
                                 static_cast<std::int64_t>(modified.time_since_epoch().count())};
    }

    [[nodiscard]] std::optional<std::filesystem::path> home() const override {
        const auto home = platform::home_directory();
        if (!home) {
            return std::nullopt;
        }
        return std::filesystem::path{*home};
    }
};

/// Where a resolved key came from, in the words every surface uses.
[[nodiscard]] std::optional<std::string> source_words(const secrets::KeyResolution& resolution) {
    switch (resolution.source) {
        case secrets::KeySource::Config:
            return std::string{"config"};
        case secrets::KeySource::Store:
            return std::string{"the store"};
        case secrets::KeySource::Environment:
            return resolution.variable;
        case secrets::KeySource::None:
            break;
    }
    return std::nullopt;
}

/// `codex login status`, as a person would type it.
[[nodiscard]] std::string status_command(const ProviderFacts& facts) {
    std::string out{facts.binary};
    for (const std::string_view word : facts.status_arguments) {
        out += ' ';
        out += word;
    }
    return out;
}

}  // namespace

std::unique_ptr<ExistenceView> host_existence_view() {
    return std::make_unique<HostExistenceView>();
}

KeyPresence host_key_presence(const secrets::CredentialStore* store,
                              const secrets::EnvSnapshot& env) {
    return [store, env](harness::BackendType type) -> std::optional<std::string> {
        if (!secrets::takes_api_key(type)) {
            return std::nullopt;
        }
        harness::BackendConfig entry;
        entry.type = type;
        return source_words(secrets::resolve_api_key(entry, store, env));
    };
}

void credentials_from_evidence(const ProviderFacts& facts, const ExistenceView& view,
                               ProviderStatus& status) {
    if (facts.evidence_paths.empty()) {
        status.credential_evidence =
            "the " + std::string{facts.label} + " leaves no login evidence Apogee can check";
        return;
    }
    const std::optional<std::filesystem::path> home = view.home();
    if (!home.has_value()) {
        status.credential_evidence = "no home directory to look in";
        return;
    }
    for (const std::string_view relative : facts.evidence_paths) {
        if (view.exists(*home / std::filesystem::path{relative})) {
            status.credentials = CredentialState::Found;
            status.credential_evidence = "~/" + std::string{relative} + " exists";
            return;
        }
    }
    status.credentials = CredentialState::NotFound;
    status.credential_evidence = "no ~/" + std::string{facts.evidence_paths.front()};
}

std::string describe_status(const ProviderStatus& status) {
    std::string words;
    switch (status.tier()) {
        case ProviderTier::Verified:
            words = "verified -- answered a turn on " + status.verified->date;
            break;
        case ProviderTier::CredentialsFound:
            words = "credentials found (" + status.credential_evidence + ")";
            break;
        case ProviderTier::Installed:
            words = "installed";
            if (!status.credential_evidence.empty()) {
                words += " (" + status.credential_evidence + ")";
            }
            break;
        case ProviderTier::NotFound:
            return status.installed_evidence;
    }
    if (!status.binary.empty()) {
        if (!status.version.empty()) {
            words += "; " + status.version;
        } else {
            words += status.version_probed ? "; version unknown" : "; version not asked yet";
        }
    }
    return words;
}

std::string not_on_path(std::string_view program) {
    return "'" + std::string{program} + "' was not found on PATH";
}

std::optional<ProviderStatus> backend_provider_status(const harness::BackendConfig& entry,
                                                      const ExistenceView& view,
                                                      const secrets::CredentialStore* store,
                                                      const secrets::EnvSnapshot& env,
                                                      const ProviderCache& cache) {
    const ProviderFacts* facts = provider_for_type(entry.type);
    if (facts == nullptr) {
        return std::nullopt;
    }
    ProviderStatus status;
    status.id = std::string{facts->id};
    status.type = facts->type;
    if (const auto record = cache.verified.find(facts->id); record != cache.verified.end()) {
        status.verified = record->second;
    }
    const auto row = cache.providers.find(facts->id);
    const ProviderStatus* cached = row == cache.providers.end() ? nullptr : &row->second;

    if (facts->binary.empty()) {
        // The entry's own key, through the one chain -- its `api_key` first.
        if (const std::optional<std::string> source =
                source_words(secrets::resolve_api_key(entry, store, env))) {
            status.installed = true;
            status.installed_evidence = "key from " + *source;
            status.credentials = CredentialState::Found;
            status.credential_evidence = status.installed_evidence;
        } else {
            status.installed_evidence = "no key resolves";
            status.credentials = CredentialState::NotFound;
            status.credential_evidence = status.installed_evidence;
        }
        return status;
    }

    const std::string program = entry.binary.empty() ? std::string{facts->binary} : entry.binary;
    status.binary = view.find_program(program);
    if (status.binary.empty()) {
        status.installed_evidence = not_on_path(program);
        return status;
    }
    status.installed = true;
    status.fingerprint = view.fingerprint(status.binary);
    const bool same_binary =
        cached != nullptr && cached->installed && cached->binary == status.binary;
    if (same_binary && cached->version_probed && status.fingerprint.has_value() &&
        cached->fingerprint == status.fingerprint) {
        status.version_probed = true;
        status.version = cached->version;
        status.installed_evidence = cached->installed_evidence;
    } else {
        status.installed_evidence = status.binary + ", version not asked yet";
    }
    if (!facts->status_arguments.empty()) {
        if (same_binary) {
            status.credentials = cached->credentials;
            status.credential_evidence = cached->credential_evidence;
        } else {
            status.credential_evidence = "`" + status_command(*facts) + "` not run yet";
        }
    } else {
        credentials_from_evidence(*facts, view, status);
    }
    return status;
}

}  // namespace apogee::backends
