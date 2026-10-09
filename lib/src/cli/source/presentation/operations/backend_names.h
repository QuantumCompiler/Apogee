#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "harness/roles.h"

/// The backends a request names, resolved once for every surface: a helper
/// role's, the named utility, and a model spelled as a configured key (A4:
/// moved out of `cli/helpers`).
namespace apogee::commands {

/// The backend a helper `role` runs on for a conversation on `conversation`
/// (26b): the role's pointer, else the conversation's own backend -- through
/// the one resolver.
[[nodiscard]] std::string helper_backend(const harness::Config& config, harness::ModelRole role,
                                         std::string_view conversation);

/// The utility backend when the config names one -- its pointer, or the
/// active suite's member (27d) -- else empty: for the chore
/// that happens only when a utility model is set -- summarising a large tool
/// result -- and is never pushed onto the chat model.
[[nodiscard]] std::string named_utility(const harness::Config& config);

/// Whether `model` names something the config actually defines.
///
/// Mirrors the router's first two rungs -- a backend key, or a backend entry's
/// `model:` field -- and deliberately NOT its third, the fallback to
/// `models.default`.
///
/// That fallback is right for an unspecified model and wrong for an explicit
/// one. `apogee complete -m sonnnet` is a typo, and silently answering from a
/// different backend is the worst possible response: the user gets a real
/// answer from a model they did not choose, with nothing to indicate it. So an
/// explicit model is checked here before the router ever sees it -- by
/// `complete`, and by `serve` for a request's `model` field, through this one
/// function so the two cannot disagree about what "configured" means.
[[nodiscard]] bool names_a_configured_backend(const harness::Config& config,
                                              std::string_view model);

/// The backend KEY `model` names, as written in the config: the key itself, or
/// the key of the entry whose `model:` field it matches (literal or
/// normalized). Empty when nothing matches. `names_a_configured_backend` is
/// this test made boolean; `serve` needs the key, because the set of backends
/// it serves is a set of keys.
[[nodiscard]] std::string configured_backend_key(const harness::Config& config,
                                                 std::string_view model);

/// The roster resolution (M13): what a model name that is neither a backend
/// key nor an entry's `model:` field means, read from the cached vendor
/// rosters -- zero network, the cache only.
struct RosterResolution {
    /// The backend key to run on, set only when exactly one configured
    /// vendor type's roster lists the name (that type's first entry, by key
    /// order -- deterministic).
    std::string backend;
    /// The roster id to pin for the run, verbatim.
    std::string model;
    /// Every configured type whose roster lists the name -- the refusal
    /// names them all when there is more than one.
    std::vector<std::string> owners;
};

/// Resolves `model` against the cached rosters of the *configured* provider
/// types. One owner fills `backend`/`model`; several fill only `owners`
/// (the caller refuses naming them); none returns everything empty and the
/// caller's existing error stands.
[[nodiscard]] RosterResolution resolve_roster_model(const harness::Config& config,
                                                    std::string_view model);

}  // namespace apogee::commands
