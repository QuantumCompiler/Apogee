#pragma once

#include <span>
#include <string_view>

#include "contracts/config.h"

/// The providers' knowledge table (28a): every fact the detector knows about
/// a provider, in one row each, so adding a provider is a row, not a hunt.
///
/// Pure data, apart from the prober (`provider_probe.h`) on purpose: the
/// cache, the registration offers and the doctor read a provider's facts
/// without being able to run a probe, and `cli.no_provider_probes` holds the
/// prober to the few files allowed to spawn one.
namespace apogee::backends {

/// One provider the detector knows.
struct ProviderFacts {
    /// The provider's short name: `claude`, `codex`, `gemini`, `ollama`,
    /// `anthropic`, `openai`, `google` -- the name its cache entry is keyed by.
    std::string_view id;
    /// The backend type that reaches it.
    harness::BackendType type = harness::BackendType::Mock;
    /// What a person calls it: `Claude CLI`, `Anthropic API`.
    std::string_view label;
    /// The executable a vendor-CLI type spawns, resolved from PATH; empty for
    /// an API type, which is detected by its key alone.
    std::string_view binary;
    /// What prints the version. Run only when the binary's fingerprint
    /// changed, or on a refresh.
    std::span<const std::string_view> version_arguments;
    /// Files under the home directory whose existence is the vendor CLI's
    /// evidence of a login. Tested for existence, never opened.
    std::span<const std::string_view> evidence_paths;
    /// An allow-listed status command that answers "logged in?" by its exit
    /// code -- offline, non-interactive, no spend. Empty for none.
    std::span<const std::string_view> status_arguments;
};

/// The table: the four vendor-CLI types, then the three API types (decided
/// 2026-10-03; grows by row).
[[nodiscard]] std::span<const ProviderFacts> provider_table() noexcept;

/// The row named `id`, or null.
[[nodiscard]] const ProviderFacts* find_provider(std::string_view id) noexcept;

/// The row for backend type `type`, or null for a type the table does not
/// cover -- llamacpp, mlx and mock are local, and never detected.
[[nodiscard]] const ProviderFacts* provider_for_type(harness::BackendType type) noexcept;

}  // namespace apogee::backends
