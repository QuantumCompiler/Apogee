#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

/// The model-roster cache (M13): each cloud provider type's live model
/// catalogue as its vendor last listed it, with the date it was fetched.
///
/// The roster is the vendor's answer verbatim -- no model id lives in
/// source, no client-side curation -- fetched only on the user's word (a
/// registration, `providers scan --refresh`) and read everywhere else:
/// `models list`'s folded rows, completion, the bare-model-name resolution.
/// The file is `cache/model-rosters.json` (`harness::roster_cache_path()`),
/// **disposable state** in the provider cache's exact idiom: absent,
/// truncated, corrupt or another schema reads as empty, never throws, and
/// the next fetch rebuilds it; a failed write costs the record, never the
/// command. Keyed by backend *type* ("anthropic"), because the catalogue is
/// the vendor's, the same for every entry of that type.
namespace apogee::backends {

/// One model as the vendor listed it.
struct RosterModel {
    std::string id;    ///< the vendor's id, verbatim -- what `-m` takes
    std::string name;  ///< the vendor's display name, else the id

    bool operator==(const RosterModel&) const = default;
};

/// One provider type's roster.
struct ProviderRoster {
    std::vector<RosterModel> models;
    /// YYYY-MM-DD, shown wherever the roster is: the day it was fetched -- or,
    /// for a built-in list, the day it was last checked against its source
    /// (ADR 0009).
    std::string fetched_at;
    /// Carried in the binary rather than fetched (35): a CLI that prints no
    /// list of its own. Never written to the cache.
    bool built_in = false;

    bool operator==(const ProviderRoster&) const = default;
};

/// Every cached roster, by backend type.
struct RosterCache {
    std::map<std::string, ProviderRoster> rosters;

    [[nodiscard]] const ProviderRoster* roster_for(std::string_view type) const noexcept;
};

/// Reads the cache at `harness::roster_cache_path()`. Never throws; anything
/// unreadable is an empty cache.
[[nodiscard]] RosterCache load_roster_cache();

/// Writes the cache atomically. A failure is returned as its reason, never
/// thrown -- the caller says it and moves on.
[[nodiscard]] std::string save_roster_cache(const RosterCache& cache);

/// The rosters Apogee carries for the vendor CLIs that print no model list
/// (35, the user's call, relaxing M13's "nothing hardwired" for these two):
/// Claude Code's is Anthropic's published model ids, its Gemini sibling's the
/// CLI's own aliases. Each is `built_in`, with no fetch date.
[[nodiscard]] RosterCache built_in_rosters();

/// Every roster a model name is read from (35): the cache's, and each built-in
/// one whose type the cache has none for -- a fetched roster always wins.
/// What completion, resolution, the listings and `/models` read; only a fetch
/// reads and writes the cache itself (`load_roster_cache`).
[[nodiscard]] RosterCache known_rosters();

}  // namespace apogee::backends
