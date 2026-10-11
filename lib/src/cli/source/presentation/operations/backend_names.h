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

/// What a model name given to a session means (33): a backend the config
/// defines, a vendor roster's model pinned on its sole configured owner, or
/// -- `<backend>:<model>` (34) -- any model pinned on a named entry.
struct SessionModel {
    /// The backend KEY to run on, as the file spells it; empty when nothing
    /// matched or the name is refused.
    std::string backend;
    /// The roster model to pin on `backend` -- empty for a name the config
    /// itself defines.
    std::string pinned;
    /// Whose roster `pinned` is on: its owner's type -- empty for a pin named
    /// `<backend>:<model>`, which no roster need list.
    std::string roster;
    /// Why the name is refused: a roster model two configured types list, or
    /// a model pinned on an entry that runs a file rather than a name.
    std::string refusal;
};

/// Resolves `name` as launch `-m`, `/model` and `complete -m` all take it --
/// one function, so no two of them can disagree. A backend key or an entry's
/// `model:` first (`configured_backend_key`), then a roster model with exactly
/// one configured owner (`resolve_roster_model`) -- two owners refuse naming
/// both and the way to pin one -- then `<backend>:<model>` (34), split at the
/// first colon whose left side is a configured key, so a model may hold
/// colons of its own: the model the vendor's to resolve, verbatim, refused on
/// an entry that runs the weights at its `model_path` (`names_its_model`); the
/// entry's own model, or none, is the entry itself. Nothing matching returns
/// everything empty and the caller's own refusal stands. Read against the
/// config as the FILE has it: a pin is never mistaken for the entry's own
/// model.
[[nodiscard]] SessionModel resolve_session_model(const harness::Config& config,
                                                 std::string_view name);

/// How a pin is said, after its model: `anthropic's roster, on backend
/// 'claude'`, or `on backend 'claude'` for one no roster lists.
[[nodiscard]] std::string roster_pin_note(const SessionModel& model);

}  // namespace apogee::commands
