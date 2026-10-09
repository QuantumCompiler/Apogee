#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "backends/provider_cache.h"
#include "backends/provider_status.h"
#include "backends/provider_table.h"
#include "contracts/config.h"

/// Provider detection (28a): what this machine has, in the tiers it can
/// actually know.
///
/// Apogee speaks to every provider it has a backend type for, and until this
/// file it could see none of them -- a fresh install registered nothing, and
/// nothing knew whether `claude` was installed, logged in, or gone. The
/// detector establishes, per provider in the knowledge table
/// (`provider_table.h`):
///
///   - **installed** -- proof: the binary found on PATH and its version, or
///     for an API type a key source resolving;
///   - **credentials found** -- evidence: a file the vendor's CLI keeps when
///     it is logged in exists, or an allow-listed status command says so;
///   - **verified** -- a recorded successful real turn, written by the
///     provider backends' completion seam (provider-surfacing, 28c) and only
///     read here.
///
/// "Authenticated" is a word the detector never uses: two of the three CLIs
/// keep their real credential where Apogee never looks, so the most a file's
/// existence can say is that credentials were found.
///
/// **Never on the hot path.** Probing is expensive -- a cold `claude
/// --version` took 2.6 s, `gemini --version` 0.7-0.9 s on every run (the
/// 2026-10-03 spike) -- so no chat, execute, complete, serve, task, symphony
/// or machine-mode startup runs one. Only the files `cli.no_provider_probes`
/// allow-lists may include this header; everything else reads the cache
/// (`provider_cache.h`), which costs a file read.
///
/// **Existence, never contents.** The probe sees the filesystem through
/// `ExistenceView`, which can say whether a path exists and when it changed
/// and cannot read a byte of it -- so reading a vendor's credential file
/// cannot be written here. Status commands come only from the knowledge
/// table, each a known offline, non-interactive, no-spend invocation with a
/// hard deadline. Nothing here touches the network or a credential store of
/// another application's.
namespace apogee::backends {

/// How long a version probe may run before it is killed and recorded as
/// unknown. Generous: a cold `claude --version` took 2.6 s.
inline constexpr std::chrono::milliseconds kVersionProbeDeadline{10'000};
/// How long a status command may run.
inline constexpr std::chrono::milliseconds kStatusProbeDeadline{5'000};

/// How a probe child ended.
struct ProbeRun {
    enum class Outcome : std::uint8_t {
        /// It exited; `exit_code` says how.
        Exited,
        /// It ran past its deadline and was killed.
        TimedOut,
        /// It could not be started (or this platform spawns nothing).
        NotStarted,
    };
    Outcome outcome = Outcome::NotStarted;
    int exit_code = -1;
    /// Its stdout, bounded. Its stderr is captured and dropped: a probe's
    /// child never writes to the terminal.
    std::string output;
};

/// Runs `program` with `arguments`, stdin closed, killing it at `deadline`.
using ProbeRunner =
    std::function<ProbeRun(const std::string& program, const std::vector<std::string>& arguments,
                           std::chrono::milliseconds deadline)>;

/// The real runner, over `platform::start_child`.
[[nodiscard]] ProbeRunner host_probe_runner();

/// What one scan does beyond the cheap checks.
struct ScanOptions {
    /// Ask every installed binary its version again, fingerprint or not.
    bool refresh = false;
    /// Run the allow-listed status commands. Off, a status-command provider's
    /// credential state is the cache's from the last scan that ran them.
    bool status_commands = true;
};

/// What a scan found, and what it spent finding it.
struct ScanReport {
    /// One per knowledge-table row, in table order.
    std::vector<ProviderStatus> providers;
    /// How many version probes and status commands ran -- 0 and 0 on a scan
    /// the cache answered, which is what the fingerprint rule promises.
    int version_probes = 0;
    int status_probes = 0;
};

/// Scans every provider in the table and updates `cache` with what it found:
/// the cheap checks (PATH, evidence files, key presence) every time; a
/// version probe only when a binary's fingerprint differs from the cached
/// one, or `options.refresh`; the status commands per `options`. `now` is
/// the scan's timestamp, as the cache records it.
///
/// Never throws for anything a provider does -- a probe that hangs, fails or
/// cannot start is an `unknown` in its row.
[[nodiscard]] ScanReport scan_providers(const ExistenceView& view, const ProbeRunner& runner,
                                        const KeyPresence& keys, ProviderCache& cache,
                                        const ScanOptions& options, const std::string& now);

/// The whole explicit scan against this machine: the cache read from its
/// layout path, every provider scanned through the host view, runner and key
/// presence (`store` the install's credential store, or null for none), the
/// cache written back. What `providers scan` and `check` call.
[[nodiscard]] ScanReport scan_host_providers(const ScanOptions& options,
                                             const secrets::CredentialStore* store);

}  // namespace apogee::backends
