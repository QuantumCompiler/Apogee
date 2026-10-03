# Architecture A1: carve the contracts

**What / why.** The four-layer spike (2026-10-03) measured the real dependency graph: 22 packages, 81 distinct edges — and exactly **six edge-types fight** the user's four-layer model (Presentation → Business → Data → Infrastructure, each importing only itself and below). All six share one root cause: the shared *contracts* live in `harness/`, a Business package, so the Data layer's implementors must reach **up** for them — `backends → harness ×44` (`errors.h` 14, `config.h` 10, `provider.h` 9, `types.h` 7, `cancellation.h` 2, `behavior.h` 1, `harness.h` 1), `logger → harness ×6` (`layout.h`, `paths.h`, `types.h`, `config_edit.h`), `secrets → harness ×2`, `mcp → agent ×2` (one type: `agent/tool.h`'s `Tool` struct) — plus one true cycle, `backends ↔ models` (backends read `gguf_inspect`/`kv_cache`; models' HF source wants backends' `http_client`). This item is the only part of the restructure with design judgment in it: **a `contracts` package at the Data floor** (the user's call: Data-homed, named `contracts`) holding the provider interface, the message IR, errors, cancellation, `ModelBehavior`, the config types *and the comment-preserving editor*, `layout.h`/`paths.h`, and the plain `Tool` type — plus two mechanical splits that kill the cycle: **`modelstore`** (store, sidecar, `gguf_inspect`, `kv_cache`, `sha256` → Data) out of `models`, and the **transport primitives** (`http_client`, the SSE parser, the JSONL framer → Infrastructure-destined) out of `backends`. Directories stay flat; when this ships, the measured violations are **zero** and [A2](arch-layer-move.md)'s move becomes pure renames.

**Core constraint(s).**
- **Pure relocation, zero behavior:** every move is a header/unit relocation plus include updates (~60 lines measured); every suite passes unchanged, the config byte-golden suite especially — the editor moves house without its semantics moving a comma.
- **The invariants' prose moves in the same change:** CLAUDE.md's ⚠ sections name paths this item relocates (`harness/config_edit.h` in One-config-mutation-path; `secrets/` "includes only the harness and itself"; the layering test's package lists). Each ⚠ section, the layering allow-lists, and `cli.one_key_resolver`'s file scopes update in the same commit — a guard pointing at a stale path is a guard that passes vacuously.
- **Contracts stay contracts:** nothing in `contracts/` may include upward — it includes `platform/` and itself only, asserted by the layering test from day one of the package's existence. A convenience function that drags business logic down into it is the drift this layer exists to prevent.
- **`harness/` keeps its name and its soul:** the Harness, the router, the roles resolver and capability probes remain — Business, as the spike assigned them. Nothing outside the moved headers changes its include of `harness/`.
- **Code style carries** (`.h`/`.cpp` pairs; moved units stay pairs).

**Seam + files.**
- `contracts/` (new): `provider.h`, `types.h`, `errors.h`, `cancellation.h`, `behavior.h`, `config.h` (types) + `config_edit.h/.cpp`, `layout.h`, `paths.h/.cpp`, `tool.h` (the struct; `agent/` keeps registry, dispatch, the gate).
- `modelstore/` (new): `store.h/.cpp`, `sidecar.h/.cpp`, `gguf_inspect.h/.cpp`, `kv_cache.h/.cpp`, `sha256.h/.cpp`; `models/` keeps acquisition, convert, quantize, the sources.
- `transport/` (new): `backends/http_client.*`, the SSE parser, `jsonl_framer.h` (the mcp/training named allowances follow the file).
- The ~60 include-line updates across `backends/`, `logger/`, `secrets/`, `mcp/`, `models/`, and the callers of moved `models/` units; `tests/` mirrors the three new packages; `tests/layering.cmake` gains the three packages' rules and the four-layer assignment map the spike used.
- Docs in-change: CLAUDE.md ⚠ paths and Codebase Map rows for the three packages; DEVELOPER.md sections.

**Reference (Ommi).** No analog — Ommi's Go import cycles made this layering free ("in Go this is free: the reverse edge is an import cycle and the build fails"), which is exactly why Apogee's C++ needed the guarded-package discipline this item now formalizes into layers. The spike's method (measure, then move only what the measurement names) is the house pattern.

**Decisions made** (dated):
- 2026-10-03 — The four-layer restructure asked for by the user; specced from the spike's measured graph into the standing **Architecture** category (the user's call, created for this track). The six layer decisions recorded that day: Architecture category above Maintenance; **contracts in Data, named `contracts`**; commands into three modules ([A3](arch-commands-modules.md)); short include paths ([A2](arch-layer-move.md)); dual enforcement ([A4](arch-build-enforcement.md)); layers per application.
- 2026-10-03 — A1 before the move: carving while directories are flat keeps this item's diff small and judged, and makes A2 provably judgment-free.

**Open calls:**
- [default: `config.h`'s *loading* (`load_config`/`parse_config`, the only yaml-cpp users) moves with the types into `contracts` — splitting load from types would leave a two-header seam nobody asked for] The config engine moves whole.
- [default: `transport/` is named now and parked in Infrastructure by A2; until A2 it sits beside the other flat packages] Naming ahead of the move.
- [default: `events/` stays put (already a leaf; Infrastructure by assignment, no includes to fix)] Nothing moves that the measurement didn't name.

**Guardrail(s).**
- The spike's graph script, promoted into the layering test: the four-layer assignment with **zero upward edges**, failing by name — run against flat directories now and the moved tree after A2.
- Every existing suite green with no test-logic edits (include paths only); the config byte-golden and `cli.one_key_resolver` checks especially.
- `contracts/` and `modelstore/` and `transport/` join the layering test's guarded set in the same commit they exist.
- Mutation where convention applies on the updated allow-lists (a deleted rule must fail something).

**Acceptance criteria:**
- [ ] The dependency measurement reports zero four-layer violations; `backends/`, `logger/`, `secrets/`, `mcp/` include nothing from `harness/` or `agent/`; the `backends ↔ models` cycle is gone both ways.
- [ ] `make test` green with zero behavioral diffs; the config editor's byte-golden suite passes untouched.
- [ ] Every ⚠ section and guard that named a moved path names the new one, and each guard still fails its planted violation.
- [ ] `harness/` still builds the Harness: router, roles, probes — and nothing else moved.

**Scope note.** **Architecture item A1** — the track's judgment item; gated on nothing pending. A quiet repo is *preferred* (moderate include churn) but not required — [A2](arch-layer-move.md) is the item that demands the window. Out of scope: any directory-layer creation (A2); any CMake target changes (A4); renaming `harness/`.
