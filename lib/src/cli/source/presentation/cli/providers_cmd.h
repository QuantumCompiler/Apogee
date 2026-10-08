#pragma once

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
/// Nothing is written without it.
namespace apogee::commands {

/// The scan as a person reads it: a row per provider -- id, tier, the backend
/// that reaches it -- with its evidence beneath. `config` may be null (no
/// config file yet).
[[nodiscard]] std::string render_provider_scan(const std::vector<backends::ProviderStatus>& rows,
                                               const harness::Config* config);

class ProvidersCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
