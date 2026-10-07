#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"

/// The static walk over a symphony's plays (27r): the one walk that finds a
/// loop, a nesting past the cap, and a played name nothing defines, before
/// any model is asked anything.
///
/// **One walk, every lookup.** A `play:` stage names another symphony, and
/// which definitions exist depends on who asks: the config loader knows its
/// own entries (a name it does not know -- a shipped starter, a spec file --
/// is late-bound, a leaf to it), while the Business layer's catalog knows
/// every source. Both walk through this function with their own lookup, so
/// "a definition that reaches itself is refused" is one rule in one place,
/// held at parse, at load, at create and at play start alike.
///
/// **Positions are named the way a play narrates them.** A path is the
/// symphonies from the root down, `outer → inner`; a stage under it is
/// `outer → inner, stage 2 (verify)`, the root's own stages `stage 2
/// (verify)` as 27q's validation names them.
namespace apogee::harness {

/// The definition a `play:` stage names, by name; nullptr for a name the
/// lookup does not know.
using SymphonyLookup = std::function<const SymphonySpec*(std::string_view name)>;

/// What the walk found.
struct SymphonyWalk {
    /// Why the walk refuses the symphony, naming the path through its plays:
    /// a loop, a nesting past the cap, or -- for a complete lookup -- a
    /// played name nothing defines. Empty when it is sound; the walk stops at
    /// the first.
    std::string problem;
    /// How deep it nests: 1 for a symphony that plays none, one more for
    /// each level of plays. A lower bound when a played name was not known.
    std::int64_t depth = 1;
    /// The member calls one play of it makes: each role stage once, each
    /// played symphony's stages as often as it is played. A lower bound when
    /// a played name was not known (each such play counted once).
    std::int64_t stage_calls = 0;
    /// Every symphony reached below the root, each once, in the order the
    /// walk first reached it.
    std::vector<std::string> reached;

    [[nodiscard]] bool ok() const noexcept {
        return problem.empty();
    }
};

/// Walks `spec`'s plays through `lookup`, refusing a loop (direct, mutual,
/// transitive or the symphony's own name) and a nesting deeper than
/// `max_depth` -- each named with its path -- and, when `complete`, a played
/// name `lookup` does not know. With `complete` false such a name is
/// late-bound: a leaf one level down, checked again by whoever knows it.
[[nodiscard]] SymphonyWalk walk_symphony(const SymphonySpec& spec, const SymphonyLookup& lookup,
                                         std::int64_t max_depth, bool complete);

/// `names` joined as a play's path: `outer → inner`.
[[nodiscard]] std::string symphony_path(const std::vector<std::string>& names);

}  // namespace apogee::harness
