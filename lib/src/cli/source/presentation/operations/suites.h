#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "contracts/config.h"
#include "symphony/definition.h"

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
/// on the model's initiative, which never spends -- and every cap positive;
/// then `validate_suite_validation`. A null probe can tell nothing, and
/// unknown is metered.
[[nodiscard]] std::string validate_suite_consult(const harness::Config& config,
                                                 const harness::SuiteConfig& suite,
                                                 const MeteredProbe& metered);

/// Why `suite`'s `validate:` block cannot be written (27g), or empty: its
/// verifier -- named, or the utility member by default -- a role that can
/// check (`harness::consultable_role_names()`) with a member in the suite
/// whose backend `metered` says is local and unmetered, since a check runs on
/// Apogee's initiative; `answers` one of `harness::answer_check_names()`. No
/// block is always writable. `validate_suite_consult` ends with it, so every
/// path that holds a consultable member holds the verifier too.
[[nodiscard]] std::string validate_suite_validation(const harness::Config& config,
                                                    const harness::SuiteConfig& suite,
                                                    const MeteredProbe& metered);

/// Why `suite`'s `orchestrate: true` cannot be written as `name` (27t), or
/// empty: every member a symphony it would offer reaches -- each symphony of
/// `symphonies` that can be played and projected as a tool, its stages and
/// those of every symphony it plays, each role resolved through the one chain
/// under the suite -- local and unmetered by `metered`'s word, since a play
/// the model starts runs on its initiative, which never spends. The refusal
/// names the symphony, its stage, the role and the backend. Off is always
/// writable; on with no catalog to read (`symphonies` null) cannot be told,
/// and unknown is metered.
[[nodiscard]] std::string validate_suite_orchestrate(const harness::Config& config,
                                                     std::string_view name,
                                                     const harness::SuiteConfig& suite,
                                                     const MeteredProbe& metered,
                                                     const symphony::Catalog* symphonies);

/// Why `suite` cannot be written as `name`, or empty: a name that is not
/// `off`, at least one member, every member valid, its consultable members
/// and caps valid (`validate_suite_consult`, through `metered`), and its
/// orchestration (`validate_suite_orchestrate`, over `symphonies`).
[[nodiscard]] std::string validate_suite(const harness::Config& config, std::string_view name,
                                         const harness::SuiteConfig& suite,
                                         const MeteredProbe& metered = {},
                                         const symphony::Catalog* symphonies = nullptr);

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
