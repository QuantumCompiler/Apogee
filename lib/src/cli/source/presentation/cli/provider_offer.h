#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "backends/provider_cache.h"
#include "contracts/config.h"

/// Registering detected providers (28b): the pass `providers scan --register`
/// runs, and the one-time offer a first interactive session makes.
///
/// **Consent-shaped, always.** Nothing here writes a backend entry without a
/// yes given in this session -- `--register` typed, or the offer answered `y`.
/// The offer appears only on an interactive terminal (never a pipe, machine
/// mode, `serve` or `--quiet`), only while no provider backend is configured,
/// and only once ever: the answer is recorded in the provider cache, never in
/// the config, and `providers scan --register` stays the way back.
///
/// **Detection is read, never run.** This file reads the provider cache and
/// cannot run a probe: it is not among the files `cli.no_provider_probes`
/// lets include the prober, so the offer on chat's startup path costs a file
/// read. What it offers is what the last explicit scan found.
///
/// **One mutation path.** Each entry is written by `append_backend` -- the
/// writer `config add-backend` uses -- through `edit_config_file`, so the
/// file's comments and layout survive and a bad edit never lands. An existing
/// entry is never touched: a provider already registered, or a name already
/// taken, is skipped and said, never suffixed.
namespace apogee::commands {

/// What the pass does, or would do, about one detected provider.
struct RegistrationStep {
    enum class Outcome : std::uint8_t {
        /// An entry is written: `name`, of `type`.
        Register,
        /// A backend of this type exists already -- `detail` names it.
        AlreadyRegistered,
        /// A backend named `name` exists with another type -- `detail` its
        /// type.
        NameTaken,
        /// The type cannot run without a model, so no entry is written.
        NeedsModel,
    };

    std::string provider;
    std::string name;
    harness::BackendType type = harness::BackendType::Mock;
    Outcome outcome = Outcome::Register;
    std::string detail;
};

/// The whole pass, before anything is written.
struct RegistrationPlan {
    std::vector<RegistrationStep> steps;
    /// The entry `models.default` will point at -- set only when it is empty
    /// and something is registered, else empty.
    std::string default_backend;

    /// How many entries the pass writes.
    [[nodiscard]] std::size_t registers() const noexcept;
};

/// Whether a provider's evidence is enough to offer it: a CLI when it is
/// installed (its own first run handles login), an API type only with a key
/// that resolves (decided 2026-10-03).
[[nodiscard]] bool offerable(const backends::ProviderStatus& status) noexcept;

/// Whether any configured backend reaches a provider in the knowledge table.
[[nodiscard]] bool has_provider_backend(const harness::Config& config) noexcept;

/// The pass over `statuses` (one per provider, in table order) against
/// `config`: one step per offerable provider.
[[nodiscard]] RegistrationPlan plan_registration(
    const std::vector<backends::ProviderStatus>& statuses, const harness::Config& config);

/// The cache's providers in knowledge-table order.
[[nodiscard]] std::vector<backends::ProviderStatus> cached_statuses(
    const backends::ProviderCache& cache);

/// Writes the plan's entries -- and `models.default`, when the plan sets it
/// -- in one edit of the config at `path`. Throws as `edit_config_file` does,
/// leaving the file untouched.
void apply_registration(const std::filesystem::path& path, const RegistrationPlan& plan);

/// The plan in words, a line per step, after it was applied (`applied`) or
/// as a dry run.
[[nodiscard]] std::string render_registration(const RegistrationPlan& plan, bool applied);

/// The first-launch offer's inputs.
struct OfferContext {
    /// Both stdin and stdout are a terminal, and stdin is not piped.
    bool interactive = false;
    /// Machine mode, or anything else with no person to answer.
    bool machine = false;
    /// `--quiet`.
    bool quiet = false;
    std::filesystem::path config_path;
    std::filesystem::path cache_path;
    /// The local day, `YYYY-MM-DD`, recorded with the answer.
    std::string today;
};

enum class OfferOutcome : std::uint8_t {
    /// Nothing was asked.
    NotAsked,
    /// Asked and declined: nothing written but the answer.
    Declined,
    /// Asked and accepted: the entries are in the config, which the caller
    /// reloads.
    Registered,
};

/// Asks, once ever, whether to register the providers the cache says this
/// machine has -- when `context` is interactive, no provider backend is in
/// `config`, a scan is cached, the offer was never answered, and something is
/// offerable. Reads the answer from `in`; writes to `out`.
[[nodiscard]] OfferOutcome offer_registration(const OfferContext& context,
                                              const harness::Config& config, std::istream& in,
                                              std::ostream& out);

}  // namespace apogee::commands
