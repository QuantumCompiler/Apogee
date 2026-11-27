# ADR 0001 — Four layers, dependencies point down, regardless of language and framework

**Status:** Accepted · **Date:** 2026-10-21

## Context

The repo's law, third application: the CLI proved the shape and build-enforces it ([its ADR 0001](../cli/layer-enforcement.md)); the GUI adopted it for Swift with its own teeth ([gui ADR 0001](../gui/layer-enforcement.md), the user's call that the law holds "regardless of the language and framework"). The mod is TypeScript inside Claude's plugin host — a third language, the same drift risk — and the user's call (2026-10-21, with the mod spike) extends the same law to it.

## Decision

The mod is four layers, lowest to highest, with the same downward law:

1. **Infrastructure** — the process runner over `$.process.run`, binary discovery: what everything stands on.
2. **Data** — the CLI's documents as tolerant types, the mod's own state atoms: how the outside world is represented.
3. **Business** — the Apogee client: reads, supervised runs, the logic behind tools, commands and the pane's state.
4. **Presentation** — the pane, status line, toasts and slash registration: the only layer that touches `$.ui`.

**A package may depend only on its own layer or below.** The host's `$` APIs are confined to their layers: `$.process` is Infrastructure's, `$.ui` is Presentation's, atoms are Data's — a Business file that renders or spawns is the drift this record exists to refuse. The composition root (`hooks/register.tsx`) wires the layers and holds nothing itself, as `main.cpp` and `ApogeeMain` do.

## Consequences

- Adding a package means declaring its layer; the directory (`src/<layer>/<package>/`) states the declaration.
- Handler logic is Business and testable without the host: Presentation registers, Business decides.
- A change that would point an import upward is an architecture change — the planning docs, never a clever import.

## Enforcement

Lands with the scaffold's first build (the queue's 1a, honoring the law's teeth-at-birth precedent): a layering check over the TypeScript import graph — each file's imports held to its layer's row, an upward or undeclared edge failing by name, an empty graph refused — the CLI's `harness.layering` and the GUI's `tests/architecture/layering` as the reference implementations. Until then, this record and review hold the line.

**Landed 2026-11-21** (1a, the scaffold): `lib/src/mod/tests/architecture/layering/check.mjs`, run by node (`npm run layering` over the tree, its planted violations under `npm run test:node`), never under `claude plugin test`, whose tests have no file system. It reads the graph with the TypeScript compiler API. The import rules: every file of `src/` sits at `src/<layer>/<package>/`; every layer has a package and every package its tests at `tests/<layer>/<package>/`; every file of the plugin has a place the check holds, so `hooks/` holds `hooks/register.tsx` alone, `types/` holds declarations (`.d.ts`) alone, and a layer's file that is not TypeScript (the engine loads `.js`, `.jsx`, `.mjs` and `.cjs` too) fails by name, since the check cannot read it; an upward edge (an import, a re-export or a type-level import, the composition root's included) fails by name; the contract imports nothing of the mod but itself, so no edge is laundered through it; an undeclared edge fails, which is anything from outside the mod but `'claude-code'`, or `'claude-code/testing'` and the tests from inside the plugin; an `import()` fails; and an empty graph is refused. The `$` nouns, by the type checker's word on which property is the engine's, however the engine is named, destructured or indexed: `process`, `fs` and `env` are Infrastructure's; `state`, `store` and the atom library Data's, whether it is imported or re-exported by name or `'claude-code'` is taken whole (`* as`, `export *`); `ui`, `command` and `tool` Presentation's; `http` no layer's, since the mod is a client of the binary and opens no port; and the composition root and the contract reach none. Reading `fs`, `env`, `store`, `tool` and `http` into the Decision's list is that item's call, recorded with it ([MILESTONES](../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)).

**Grown 2026-11-21** (1b, the tools, slash commands and status line; [MILESTONES](../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)): `$.clock` joins Infrastructure's nouns, since time and timers reach the host as processes do. `src/infrastructure/timer/` wraps it for the layers above: the status line's poll and the async law's window. Its planted test fails a Business and a Presentation file that reach it. Every other noun 1b reaches already had its row: `process` (`run`, and `spawn` for a supervised run), `state`, `ui` (`ask`, `status`, `toast`), `command` and `tool`.
