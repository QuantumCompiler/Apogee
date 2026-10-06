#pragma once

#include <string_view>

#include "embedstore/store.h"

/// The navigation fixture graph (27l): one mixed graph, built the same way
/// every time, that the traversal tables, the toolset's bounds, the CLI's
/// renderings and the admin twins all walk -- so "the CLI and the tools
/// print the same payload" is checked over one committed shape.
///
/// **The code layer** (source member `app`, stored through
/// `sync_code_graph`, every edge `extracted`): files `pkg/app.py`,
/// `pkg/lib.py`, `pkg/util.py`, `pkg/island.py`, `pkg/chain.py` and
/// `pkg/callers.py`; `pkg.app.main` calls `pkg.app.run`, which calls
/// `pkg.lib.helper` (twice) and `pkg.lib.other`, which calls
/// `pkg.lib.helper`; `pkg.lib.Store` with `add` and `Add` (one name, two
/// cases); a second `helper` in `pkg.util` (so `helper` is ambiguous); a
/// call chain `pkg.chain.n0` -> ... -> `pkg.chain.n10` (ten hops); fifteen
/// callers `pkg.callers.c01`..`c15` of `pkg.lib.helper` (past the
/// twelve-per-relation cap); `pkg.island.alone`, reachable from the rest
/// only through the unresolved name `json.dumps` -- which a path never
/// walks through; and `.push_back`, another unresolved name referenced at
/// fourteen lines (past a card's twelve). Every function
/// is `defined_in` its file (a method in its class), and `pkg/app.py`
/// imports `pkg/lib.py`.
///
/// **The prose layer** (chunks under `member` in `chunks`): `Atlas` twice,
/// a `system` and an `organization` (so `Atlas` is ambiguous); `Vault`; the record
/// `kr-0001` as a decision node (shipped, engineering) that `concerns`
/// Vault; `Atlas -[stores readings in]-> Vault` twice; `Vault -[implemented
/// by]-> pkg.lib.Store`, the bridge between the layers; one community over
/// Atlas (system), Vault and the decision, summarised; twelve more chunks
/// mentioning Vault (fourteen in all, past a card's twelve); and `Ledger`,
/// its description past a card's clip, concerned by thirteen proposed
/// records `kr-0101`..`kr-0113` (past a card's twelve decisions).
namespace apogee::testing {

/// Builds the fixture into `graph`, its prose chunks into `chunks` (the same
/// store for a collection's own graph) labelled `member` (`""` for one's
/// own).
void build_navigation_graph(embedstore::Store& graph, embedstore::Store& chunks,
                            std::string_view member);

}  // namespace apogee::testing
