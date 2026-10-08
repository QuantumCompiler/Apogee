#include "backends/provider_probe.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <system_error>
#include <utility>

#include "contracts/layout.h"
#include "platform/child_process.h"
#include "platform/platform.h"
#include "secrets/resolve.h"
#include "secrets/store.h"

namespace apogee::backends {

namespace {

/// The most of a probe's stdout kept: a version is one line, and a binary
/// that prints a manual instead must not grow the cache.
constexpr std::size_t kProbeOutputLimit = 4096;
/// The longest version string recorded.
constexpr std::size_t kVersionLimit = 120;

[[nodiscard]] ProbeRun run_probe(const std::string& program,
                                 const std::vector<std::string>& arguments,
                                 std::chrono::milliseconds deadline_after) {
    ProbeRun run;
    if (!platform::supports_child_processes()) {
        return run;
    }
    platform::ChildCommand command;
    command.program = program;
    command.arguments = arguments;
    std::string error;
    const std::unique_ptr<platform::ChildProcess> child = platform::start_child(command, error);
    if (child == nullptr) {
        return run;
    }
    child->close_stdin();

    const auto deadline = std::chrono::steady_clock::now() + deadline_after;
    bool out_open = true;
    bool err_open = true;
    std::string piece;
    while (out_open || err_open) {
        if (std::chrono::steady_clock::now() >= deadline) {
            child->terminate();
            run.outcome = ProbeRun::Outcome::TimedOut;
            return run;
        }
        if (out_open) {
            switch (child->read_stdout(piece, std::chrono::milliseconds{50})) {
                case platform::ReadStatus::Data:
                    if (run.output.size() < kProbeOutputLimit) {
                        run.output.append(piece, 0, kProbeOutputLimit - run.output.size());
                    }
                    break;
                case platform::ReadStatus::Timeout:
                    break;
                case platform::ReadStatus::Eof:
                case platform::ReadStatus::Error:
                    out_open = false;
                    break;
            }
        }
        if (err_open) {
            // Captured and dropped: a probe's child never reaches the terminal.
            switch (child->read_stderr(piece, std::chrono::milliseconds{out_open ? 0 : 50})) {
                case platform::ReadStatus::Data:
                case platform::ReadStatus::Timeout:
                    break;
                case platform::ReadStatus::Eof:
                case platform::ReadStatus::Error:
                    err_open = false;
                    break;
            }
        }
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    const std::optional<int> status =
        child->wait_for_exit(std::max(remaining, std::chrono::milliseconds{0}));
    if (!status.has_value()) {
        child->terminate();
        run.outcome = ProbeRun::Outcome::TimedOut;
        return run;
    }
    run.outcome = ProbeRun::Outcome::Exited;
    run.exit_code = *status;
    return run;
}

/// The first non-blank line of `text`, trimmed and bounded.
[[nodiscard]] std::string first_line(std::string_view text) {
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        while (!line.empty() &&
               (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            return std::string{line.substr(0, kVersionLimit)};
        }
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return {};
}

[[nodiscard]] std::vector<std::string> to_strings(std::span<const std::string_view> words) {
    return {words.begin(), words.end()};
}

/// `codex login status`, as a person would type it.
[[nodiscard]] std::string spelled(const ProviderFacts& facts,
                                  std::span<const std::string_view> arguments) {
    std::string out{facts.binary};
    for (const std::string_view word : arguments) {
        out += ' ';
        out += word;
    }
    return out;
}

[[nodiscard]] std::string seconds(std::chrono::milliseconds duration) {
    return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(duration).count()) +
           " s";
}

/// Why a probe answered nothing, in words.
[[nodiscard]] std::string why_not(const ProbeRun& run, std::chrono::milliseconds deadline) {
    switch (run.outcome) {
        case ProbeRun::Outcome::TimedOut:
            return "no answer in " + seconds(deadline);
        case ProbeRun::Outcome::NotStarted:
            return "could not be run";
        case ProbeRun::Outcome::Exited:
            break;
    }
    return "exited " + std::to_string(run.exit_code);
}

void scan_api_type(const ProviderFacts& facts, const KeyPresence& keys, ProviderStatus& status) {
    const std::optional<std::string> source = keys ? keys(facts.type) : std::nullopt;
    if (source.has_value()) {
        status.installed = true;
        status.installed_evidence = "key from " + *source;
        status.credentials = CredentialState::Found;
        status.credential_evidence = "key from " + *source;
        return;
    }
    status.installed_evidence = "no key resolves";
    status.credentials = CredentialState::NotFound;
    status.credential_evidence = "no key resolves";
}

void probe_version(const ProviderFacts& facts, const ProbeRunner& runner,
                   const ProviderStatus* cached, const std::optional<BinaryFingerprint>& now,
                   const ScanOptions& options, ProviderStatus& status, ScanReport& report) {
    status.fingerprint = now;
    const bool unchanged = !options.refresh && cached != nullptr && cached->version_probed &&
                           now.has_value() && cached->fingerprint == now &&
                           cached->binary == status.binary;
    if (unchanged) {
        status.version_probed = true;
        status.version = cached->version;
        status.installed_evidence = cached->installed_evidence;
        return;
    }
    if (facts.version_arguments.empty()) {
        status.installed_evidence = status.binary;
        return;
    }
    ++report.version_probes;
    const ProbeRun run =
        runner(status.binary, to_strings(facts.version_arguments), kVersionProbeDeadline);
    status.version_probed = true;
    if (run.outcome == ProbeRun::Outcome::Exited && run.exit_code == 0) {
        status.version = first_line(run.output);
    }
    status.installed_evidence =
        status.binary + ", " +
        (status.version.empty()
             ? "version unknown (`" + spelled(facts, facts.version_arguments) + "` " +
                   (run.outcome == ProbeRun::Outcome::Exited && run.exit_code == 0
                        ? std::string{"printed nothing"}
                        : why_not(run, kVersionProbeDeadline)) +
                   ")"
             : status.version);
}

void probe_credentials(const ProviderFacts& facts, const ExistenceView& view,
                       const ProbeRunner& runner, const ProviderStatus* cached,
                       const ScanOptions& options, ProviderStatus& status, ScanReport& report) {
    if (!facts.status_arguments.empty()) {
        const std::string command = "`" + spelled(facts, facts.status_arguments) + "`";
        if (!options.status_commands) {
            if (cached != nullptr && cached->installed) {
                status.credentials = cached->credentials;
                status.credential_evidence = cached->credential_evidence;
            } else {
                status.credential_evidence = command + " not run yet";
            }
            return;
        }
        ++report.status_probes;
        const ProbeRun run =
            runner(status.binary, to_strings(facts.status_arguments), kStatusProbeDeadline);
        if (run.outcome == ProbeRun::Outcome::Exited) {
            status.credentials =
                run.exit_code == 0 ? CredentialState::Found : CredentialState::NotFound;
            status.credential_evidence =
                command + (run.exit_code == 0 ? " reports a login" : " reports no login");
        } else {
            status.credential_evidence = command + " " + why_not(run, kStatusProbeDeadline);
        }
        return;
    }
    credentials_from_evidence(facts, view, status);
}

[[nodiscard]] ProviderStatus scan_one(const ProviderFacts& facts, const ExistenceView& view,
                                      const ProbeRunner& runner, const KeyPresence& keys,
                                      const ProviderStatus* cached, const ScanOptions& options,
                                      ScanReport& report) {
    ProviderStatus status;
    status.id = std::string{facts.id};
    status.type = facts.type;
    if (facts.binary.empty()) {
        scan_api_type(facts, keys, status);
        return status;
    }
    status.binary = view.find_program(facts.binary);
    if (status.binary.empty()) {
        status.installed_evidence = "'" + std::string{facts.binary} + "' is not on PATH";
        return status;
    }
    status.installed = true;
    probe_version(facts, runner, cached, view.fingerprint(status.binary), options, status, report);
    probe_credentials(facts, view, runner, cached, options, status, report);
    return status;
}

}  // namespace

ProbeRunner host_probe_runner() {
    return run_probe;
}

ScanReport scan_providers(const ExistenceView& view, const ProbeRunner& runner,
                          const KeyPresence& keys, ProviderCache& cache, const ScanOptions& options,
                          const std::string& now) {
    ScanReport report;
    for (const ProviderFacts& facts : provider_table()) {
        const auto found = cache.providers.find(facts.id);
        const ProviderStatus* cached = found == cache.providers.end() ? nullptr : &found->second;
        ProviderStatus status = scan_one(facts, view, runner, keys, cached, options, report);
        if (const auto record = cache.verified.find(facts.id); record != cache.verified.end()) {
            status.verified = record->second;
        }
        report.providers.push_back(status);
        cache.providers.insert_or_assign(std::string{facts.id}, std::move(status));
    }
    cache.scanned_at = now;
    return report;
}

ScanReport scan_host_providers(const ScanOptions& options, const secrets::CredentialStore* store) {
    const std::filesystem::path path = harness::provider_cache_path();
    ProviderCache cache = load_provider_cache(path);
    const std::unique_ptr<ExistenceView> view = host_existence_view();
    ScanReport report = scan_providers(
        *view, host_probe_runner(), host_key_presence(store, secrets::EnvSnapshot::process()),
        cache, options, platform::utc_time(std::chrono::system_clock::now(), "%Y-%m-%dT%H:%M:%SZ"));
    (void)store_provider_cache(path, cache);
    return report;
}

}  // namespace apogee::backends
