# Architecture A4: build-enforced layering

**What / why.** The track's payoff. Today the layering is law by grep (`harness.layering` scans includes and fails by name); after [A2](../../assistant/MILESTONES.md#milestone-aa--the-four-layers)/[A3](../../assistant/MILESTONES.md#milestone-aa--the-four-layers) put every module in its layer, this item makes the law **structural**: each module becomes its own static library (`apogee_data_backends`, `apogee_business_agentloop`, `apogee_presentation_views`, …) whose `target_link_libraries` *is* its dependency rule — a layer links only itself and below, a module links only what its allow-list names, and an illegal include **fails to compile** instead of failing a test later. The enforcement endgame is the user's recorded call (2026-10-03): **both** — the link graph carries the coarse four-layer law at compile time, and the grep test narrows to the scalpel rules only it can express (the named single-header allowances like the JSONL framer exemption, "`markdown/` includes only `ansi/`", the guarded closures' no-Harness-reach rules). Side benefits worth naming: incremental builds recompile the module that changed, not `apogee_core`; and a new module cannot *exist* without declaring its dependencies, which is the review conversation happening at the right moment.

**Core constraint(s).**
- **The executable stays empty and the policy stays absolute:** `apogee` remains a thin face; `apogee_assert_link_policy` extends from "nothing links `apogee`" to walking the full module graph — every `apogee_<layer>_<module>` target's links checked against the declared layer order and module allow-lists, failing configure by name. `apogee_core` survives as an INTERFACE target aggregating the modules, so `main.cpp`, the tests and the link policy's consumers keep one name.
- **Tests link modules, not the world:** each `tests/<layer>/<module>/` suite links its module plus what that module links — a test that needs more is a layering finding, not a CMake inconvenience. The cross-cutting e2e checks keep linking the aggregate.
- **The checks that read the link keep reading truth:** the no-listen symbol scan already walks the link graph transitively ("a new in-process dependency is scanned without anyone remembering") — it must enumerate the new module libraries and still attribute `listen`/`accept` to `serve.cpp`'s object alone, verified against the planted violation.
- **No cycle, ever, including inside a layer:** module-level links are a DAG by CMake's own refusal plus the policy walk — the `backends ↔ models` class of tangle becomes unrepresentable, which is the whole point.
- **Build times are measured, not assumed:** per-module libs usually help incrementals and cost a little at link; the item records clean and incremental timings before/after on the dev host, so the claim in this paragraph is either confirmed or honestly retracted in the milestone.

**Seam + files.**
- `source/CMakeLists.txt` → per-module `CMakeLists.txt` (or one generated table — whichever keeps the dependency declarations greppable in ONE place), the layer/module link map as data.
- `cmake/ApogeeLinkPolicy.cmake`: the graph walk against the declared map; the map is the same file the layering grep test reads its coarse rules from — one declaration, two enforcers.
- `tests/layering.cmake`: rescoped to the scalpel rules; its coarse four-layer scan retires in favor of the link policy (kept only as the map's mutation check).
- `tests/CMakeLists.txt`: per-module test targets; `Makefile`, presets, lint scope, `cicd.sh` untouched in interface (same entry points, same target names for consumers).
- `.github` untouched: CI calls `cicd.sh`, which builds the same aggregate.

**Reference (Ommi).** The closing of a loop: Ommi got this for free from Go's import cycles ("the reverse edge is an import cycle and the build fails"), and CLAUDE.md's harness-never-includes-backends section has always named that free-ness as what C++ lacks. This item buys it back with the linker.

**Decisions made** (dated):
- 2026-10-03 — The user's call: dual enforcement — link graph for the coarse law, the grep test for the named fine-grained allowances; neither alone covers both.

**Open calls:**
- [default: STATIC libraries, not OBJECT — simplest attribution for the symbol scan and ordinary link semantics; revisit only if link time measurably regresses] Library kind.
- [default: one `cmake/modules.cmake` table declaring every module's layer and links; per-module CMakeLists generated-or-trivial so the table stays the single source] Where the map lives.
- [default: the compile-fail proof is a planted illegal include per layer boundary, verified failing then removed — the house verified-violation pattern] Proving it bites.

**Guardrail(s).**
- The planted violations: one upward include per boundary (D→B, B→P, I→D) fails at configure/compile naming the module, before any test runs.
- The symbol scan against its planted `listen()` on the new graph; `cli.install_parity`, the fresh-clone build, and the full suite green.
- Build timings recorded (clean + one-file incremental, llama on and off) in the milestone.
- The map's mutation check: deleting a module's declared link fails something (the rescoped grep test's one remaining coarse duty).

**Acceptance criteria:**
- [ ] Every module is a library with declared links; an illegal include anywhere fails the build naming module and rule, demonstrated per boundary and removed.
- [ ] `make`, `make test`, `cicd.sh --test|--fresh` and CI behave identically from the outside; test names unchanged.
- [ ] A one-file edit in `views/` relinks without recompiling `backends/` (the incremental claim, shown in the recorded timings).
- [ ] The grep test's remaining rules are exactly the named allowances, each still failing its planted violation; the coarse scan lives in the link policy alone.

**Scope note.** **Architecture item A4** — the track's closer; build after [A2](../../assistant/MILESTONES.md#milestone-aa--the-four-layers) and [A3](../../assistant/MILESTONES.md#milestone-aa--the-four-layers) (the module set must be final before it becomes the link law). Out of scope: any dependency *change* (the graph is frozen as measured; this item enforces, never edits); shared/dynamic libraries; per-module versioning.
