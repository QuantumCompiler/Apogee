# ADR 0001 — Four layers, dependencies point down, regardless of language and framework

**Status:** Accepted · **Date:** 2026-10-21

## Context

The repo's law, third application: the CLI proved the shape and build-enforces it ([its ADR 0001](../../cli/adrs/layer-enforcement.md)); the GUI adopted it for Swift with its own teeth ([gui ADR 0001](../../gui/adrs/layer-enforcement.md), the user's call that the law holds "regardless of the language and framework"). The mod is TypeScript inside Claude's plugin host — a third language, the same drift risk — and the user's call (2026-10-21, with the mod spike) extends the same law to it.

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
