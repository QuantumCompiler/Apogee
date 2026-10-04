# Architecture A2: the move into four layers

**What / why.** With the contracts carved ([A1](../../assistant/MILESTONES.md#milestone-aa--the-four-layers)), the measured graph obeys the four-layer model — and this item makes the directories say so: every package `git mv`'d into `lib/src/cli/source/<layer>/<module>/`, the tests tree mirrored as `tests/<layer>/<module>/`, and **nothing else**. The layer assignment, from the spike (2026-10-03):

| Layer | Modules |
|---|---|
| `presentation/` | `commands`, `httpserver`, `markdown`, `render` |
| `business/` | `harness`, `agentloop`, `agent`, `tools`, `knowledge`, `graph`, `training`, `scaffold`, `models`, `mcp` |
| `data/` | `contracts`, `backends`, `embedstore`, `logger`, `secrets`, `modelstore`, `transport` |
| `infrastructure/` | `platform`, `ansi`, `events`, `version` |

The migration-cost killer is the include rule the user chose: **short paths stay** — each layer directory joins the include path, so `#include "agentloop/loop.h"` is byte-stable and roughly 95% of include lines never change; the layer lives in the tree and (after [A4](arch-build-enforcement.md)) in the link graph, not in the spelling. Layers are **per application** (the user's call): this reshapes `lib/src/cli/` only, and each future GUI app repeats the shape inside its own self-contained build.

**Core constraint(s).**
- **Pure renames, provably.** This diff contains file moves, path-list updates (CMake source lists, include dirs, script globs), and the documentation sweep — zero semantic edits. `git diff -M` shows 100% renames for source and tests; any real change found in review belongs to A1 or A3 and gets evicted. That purity is what makes a repo-wide diff reviewable at all.
- **The quiet window is mandatory here:** no open worktrees, no in-flight branches when this lands — every parallel session conflicts with a tree-wide move. Whoever takes it picks a release boundary at that moment (the standing-category placement deliberately left timing to the taker); the mutation-runs-in-worktrees workflow resumes after.
- **The checks that name paths move in the same commit:** the no-listen symbol check's allow-listed object (`serve.cpp`'s new object path), `tests/layering.cmake`'s scan roots, `cli.one_key_resolver`'s scopes, the lint scope in `.clang-tidy`/Makefile, `shell_completion_check` and the PTY scripts' binary-relative paths — a check pointed at a path that no longer exists passes vacuously, which is worse than failing.
- **The documentation sweep is in-change, not a follow-up** (the house rule): CLAUDE.md's Codebase Map and "Where new source code goes", DEVELOPER.md's tree and sections, and the **Seam + files** paths in every pending backlog document — the 2026-08-24 app-rooted rewrite is the exact mechanical precedent, script included.
- **One build target still:** `apogee_core` survives this item unchanged; splitting it into module libraries is [A4](arch-build-enforcement.md)'s job, so a build break here can only be a path, never a link rule.

**Seam + files.** `lib/src/cli/source/` and `tests/` wholesale per the table; `source/CMakeLists.txt` (source lists + the four `target_include_directories` entries); `tests/CMakeLists.txt` and the `.cmake`/`.sh`/`.py` checks' path references; the docs named above. No `.h`/`.cpp` content changes beyond nothing at all.

**Reference (Ommi).** No analog as an event — Ommi was born with its layout. The precedent is Apogee's own 2026-08-24 layout decision and its same-day mechanical rewrite of every seam path, which this item repeats at larger scale with better tooling (the rename-only property is checkable; it wasn't then).

**Decisions made** (dated):
- 2026-10-03 *(A1, at build)* — `transport` sits in **Data**, not Infrastructure: `http_client` speaks the contracts' cancellation token and errors (see A1's record).
- 2026-10-03 *(A1, at build)* — `mcp` sits in **Business**, not Data: it registers into and serves the agent's `ToolRegistry`, which stays in `agent/` (see A1's record).
- 2026-10-03 — The user's calls, recorded at specing: short include paths (per-layer include dirs; the build graph carries the layer); layers per application; the move isolated from all judgment diffs (A1 before, A3 after) so it reviews as renames.

**Open calls:**
- [default: the four include dirs are added root-first (`source/presentation` … `source/infrastructure`), and the flat `source/` include dir is removed in the same commit so a stale short path cannot resolve two ways] Include resolution.
- [default: `tests/` root keeps the cross-cutting e2e scripts and `support/` where they are; only per-package test dirs move under their layers] The tests root.
- [default: the backlog-doc path sweep is a committed script run (the 2026-08-24 pattern), its diff reviewed as text, so the 25-odd documents cannot drift one by one] Sweep mechanics.

**Guardrail(s).**
- `git diff -M --summary` over the landed commit: renames and path-list files only — asserted in review, recorded in the milestone.
- The full suite green before and after with identical test names (`tests/test_names.cmake`'s inventory unchanged modulo paths).
- The layering measurement re-run over the moved tree: still zero violations, now with layer-qualified roots.
- Every path-naming check re-verified against its planted violation after the move (no-listen symbol attribution especially).
- A fresh-clone build (`cicd.sh --fresh`) on the moved tree, proving no stale include dir or cached path survives only locally.

**Acceptance criteria:**
- [ ] The tree reads `source/{presentation,business,data,infrastructure}/<module>/` exactly per the table, tests mirrored; `rg '#include "(business|data|presentation|infrastructure)/'` finds nothing (short paths held).
- [ ] The move commit is 100% renames + path lists + docs; the suite and a fresh-clone build are green on every preset that built before.
- [ ] CLAUDE.md, DEVELOPER.md and every pending backlog document describe the layered tree; no document anywhere names a flat `source/<pkg>/` path.
- [ ] The no-listen, layering, one-key-resolver and completion checks all still fail their planted violations post-move.

**Scope note.** **Architecture item A2**; build after [A1](../../assistant/MILESTONES.md#milestone-aa--the-four-layers), **in a quiet window at a release boundary of the taker's choosing**. Out of scope: any module content changes ([A3](arch-commands-modules.md)); CMake target splits ([A4](arch-build-enforcement.md)); the GUI apps (they adopt the shape when they exist).
