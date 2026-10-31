#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "backends/provider_cache.h"
#include "cli/command.h"
#include "contracts/config.h"

/// `apogee providers` -- what this machine has, and registering it (28b).
///
/// `providers scan` runs the explicit detection (`backends/provider_probe.h`):
/// each provider in the knowledge table with its tier and the evidence for it
/// in plain words, and which configured backend reaches it. `--refresh` asks
/// every installed CLI its version again, fingerprint or not. `--register`
/// then writes one entry per detected provider that has none, through the
/// one config editor (`cli/provider_offer.h`) -- an explicit yes, typed.
/// Nothing is written without it. `providers register <id>` (M33) is that
/// pass for one provider over the last scan -- the core the shell's
/// Providers view runs on `r` (`register_provider`).
namespace apogee::commands {

/// One provider as `providers scan` reports it (37b): the one row function
/// the human scan, its JSON document and the shell's Providers view all draw.
struct ProviderRow {
    std::string provider;  ///< the knowledge table's id: `claude`
    std::string type;      ///< the backend type that reaches it: `claude-cli`
    std::string label;     ///< what a person calls it: `Claude CLI`
    std::string tier;      ///< `credentials found`, `verified 2026-10-06`, ...
    /// Which configured backends reach it (`backend: claude`), `not
    /// registered` when it could be, else empty.
    std::string backend;
    /// The evidence, a line each: where the binary is and its version, the
    /// credentials found, the last turn answered.
    std::vector<std::string> evidence;
};

/// The rows for `statuses` (one per provider, in table order) against
/// `config`, which may be null (no config file yet).
[[nodiscard]] std::vector<ProviderRow> provider_rows(
    const std::vector<backends::ProviderStatus>& statuses, const harness::Config* config);

/// The scan as a person reads it: a row per provider -- id, tier, the backend
/// that reaches it -- with its evidence beneath. `config` may be null (no
/// config file yet).
[[nodiscard]] std::string render_provider_scan(const std::vector<backends::ProviderStatus>& rows,
                                               const harness::Config* config);

/// `providers scan --output-format json` (37b): `{"object": "list", "data":
/// [{"provider", "type", "label", "tier", "backend", "evidence": [...]}]}`.
[[nodiscard]] nlohmann::json provider_scan_document(const std::vector<ProviderRow>& rows);

/// What the last explicit scan found, read from the provider cache -- no
/// probe, nothing started (37b): what the shell's Providers view draws.
[[nodiscard]] std::vector<backends::ProviderStatus> last_provider_scan();

/// The explicit scan `providers scan` runs (version probes and status
/// commands per the fingerprint rule, the cache written back) -- on the
/// user's word only, as the Providers view's `s` is.
[[nodiscard]] std::vector<backends::ProviderStatus> scan_providers_now(
    const std::filesystem::path& config_path, bool refresh);

/// What registering one provider did (M33): `render_registration`'s lines,
/// and the rosters fetched on the user's word (M13), a line each -- the
/// `registration` and `rosters` of `providers scan --register`'s document.
struct ProviderRegistration {
    std::vector<std::string> registration{};
    std::vector<std::string> rosters{};

    /// Both, a line each: what the Providers view's `r` and `providers
    /// register` say.
    [[nodiscard]] std::string said() const;
};

/// `providers scan --register` for one provider (37b): the registration core
/// over the last scan's status for `provider` alone, the entry written through
/// the one editor -- `models.default` with it when none is set, said -- and
/// its type's roster fetched: the command's own words for whatever happened, a
/// refusal included. The shell's Providers view runs it on `r`, and
/// `providers register <id>` from the command line (M33).
[[nodiscard]] ProviderRegistration register_provider(const std::filesystem::path& config_path,
                                                     std::string_view provider);

class ProvidersCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
