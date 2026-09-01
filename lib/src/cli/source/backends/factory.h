#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "harness/config.h"
#include "harness/harness.h"

/// Construction of providers from config.
///
/// **Lives in `backends/`, not in a command.** Every surface needs it — the CLI
/// today, `apogee serve` and the admin plane later — and a factory that lived
/// in `commands/` would have to be duplicated the moment a second surface
/// appeared. Duplicating it is exactly how two surfaces come to disagree about
/// what a config entry means, which is the parity bug class this project is
/// built to avoid.
namespace apogee::backends {

/// What happened to one backend entry during construction.
struct BackendStatus {
    std::string name;
    bool constructed = false;
    /// Why it was skipped, when it was. Empty on success.
    std::string reason;
};

struct BuildResult {
    /// One entry per config backend, in config order, whether or not it built.
    std::vector<BackendStatus> statuses;

    [[nodiscard]] std::size_t constructed_count() const noexcept;
    /// Names that failed, with their reasons, joined for a message.
    [[nodiscard]] std::string skipped_summary() const;
};

/// Per-run overrides that shape construction.
///
/// These are flags, not config: `--search` applies to one invocation, so it
/// belongs here rather than as a `web_search:` key someone has to remember to
/// turn off again.
struct BuildOptions {
    /// Enables the provider's own server-side web search where it has one.
    /// Providers without one ignore it -- there is no local search tool, by
    /// decision (2026-08-26): scraping a search results page breaks silently.
    bool web_search = false;
};

/// Constructs a provider for every backend in `config` and registers it on
/// `harness`, then installs the default router.
///
/// **A backend that cannot be built is skipped, not fatal.** A config with an
/// Anthropic entry whose key is unset and a working local entry must still let
/// the local one run — otherwise one unconfigured backend takes down every
/// other. The reason is recorded per entry so a caller can explain the gap
/// when the model the user actually asked for is the one that was skipped.
[[nodiscard]] BuildResult build_providers(harness::Harness& harness,
                                          const BuildOptions& options = {});

/// Constructs one provider from a single entry, or nullptr with `reason` set.
///
/// Adding a backend type is a case here plus a row in `kBackendTypeNames`
/// (`harness/config.cpp`). Nothing else changes.
[[nodiscard]] std::shared_ptr<harness::LLMProvider> make_provider(
    const std::string& name, const harness::BackendConfig& config, std::string& reason,
    const BuildOptions& options = {});

}  // namespace apogee::backends
