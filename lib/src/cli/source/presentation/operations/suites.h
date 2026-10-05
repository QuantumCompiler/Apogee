#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "contracts/config.h"

/// The write-time rules a `suites:` entry must satisfy (27d), decided ONCE for
/// `config add-suite`/`set-suite` and their admin twins, so a suite one
/// surface refuses the other refuses with the same words. The loader holds
/// the shape -- role names, positive windows, toolset words, one backend
/// pinned one way -- and these add what only a write can ask: that every
/// member names a backend this config has.
namespace apogee::commands {

/// Why `member` cannot be `role`'s member of a suite in `config`, or empty:
/// `role` one of `harness::suite_role_names()`, a configured backend, a
/// positive window, toolset words of `harness::suite_toolset_names()`.
[[nodiscard]] std::string validate_suite_member(const harness::Config& config,
                                                std::string_view role,
                                                const harness::SuiteMember& member);

/// Whether generation on a backend is billed per call, as its provider states
/// it (`LLMProvider::generation_is_metered`, never a list of types) -- asked
/// of a provider the surface builds from the config being written, since
/// nothing in this layer builds one. `unknown` says why it could not be
/// asked; unknown is metered.
struct MeteredAnswer {
    bool metered = true;
    std::string unknown;
};

using MeteredProbe =
    std::function<MeteredAnswer(const harness::Config& config, std::string_view backend)>;

/// Why `suite`'s `consultable:` and `consult_caps:` cannot be written (27f),
/// or empty: each consultable role one that can answer
/// (`harness::consultable_role_names()`), listed once, with a member in the
/// suite whose backend `metered` says is local and unmetered -- a consult runs
/// on the model's initiative, which never spends -- and every cap positive.
/// A null probe can tell nothing, and unknown is metered.
[[nodiscard]] std::string validate_suite_consult(const harness::Config& config,
                                                 const harness::SuiteConfig& suite,
                                                 const MeteredProbe& metered);

/// Why `suite` cannot be written as `name`, or empty: a name that is not
/// `off`, at least one member, every member valid, and its consultable
/// members and caps valid (`validate_suite_consult`, through `metered`).
[[nodiscard]] std::string validate_suite(const harness::Config& config, std::string_view name,
                                         const harness::SuiteConfig& suite,
                                         const MeteredProbe& metered = {});

/// Why a run cannot go ahead under `config`'s active suite, or empty: a member
/// naming a backend the config does not have. The resolver returns that name
/// as it returns any (roles.h never validates), and a surface putting the
/// suite to use refuses it here, in the existing words -- "no backend named"
/// -- rather than let the router fall back to `models.default` and answer
/// from a model nobody chose (27d). Empty with no suite active.
[[nodiscard]] std::string validate_active_suite(const harness::Config& config);

/// Why the suite `name` cannot be deleted from `config`, or empty: it is the
/// default suite, which the loader would then refuse to find.
[[nodiscard]] std::string validate_suite_delete(const harness::Config& config,
                                                std::string_view name);

}  // namespace apogee::commands
