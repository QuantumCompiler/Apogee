# ADR 0001 — Four layers, dependencies point down

**Status:** Accepted · **Date:** 2026-10-03

## Context

The 2026-10-03 architecture spike measured the real include graph — 22 packages, 81 edges — and found it 93% conformant to a four-layer model, with exactly six violating edge-types, five sharing one root cause: the shared contracts living in a Business package. The carve (A1) moved them to a Data-floor `contracts/` module and the measured graph reached zero upward edges; the move (A2) made the layers literal directories — 433 source and 194 test files under `source/<layer>/<package>/`, include lines unchanged because the layer roots are the include roots (both shipped 2026-10-03, [Milestone AA](../../assistant/MILESTONES.md#milestone-aa--the-four-layers)).

## Decision

The CLI is four layers, lowest to highest:

1. **Infrastructure** — platform, ANSI, events, versioning, transport: what everything stands on.
2. **Data** — contracts (the provider interface, message IR, errors, config engine, layout), backends, stores, logger, secrets: how state and the outside world are reached.
3. **Business** — the harness, agent loop, tools, knowledge, graph, training, models: what Apogee does.
4. **Presentation** — commands, HTTP server, machine mode, rendering: how it is shown and driven.

**A package may depend only on packages in its own layer or in layers below it.** Dependencies point down, never up. A lower layer that seems to need something above it is a signal that an interface belongs lower — the contracts carve is the standing example — never a license for an upward include.

## Consequences

- Adding a package means declaring its layer first; the directory *is* the declaration.
- Shared types sink: anything two layers both need lives at the lower one's floor (usually `contracts/`).
- A change that would point an edge upward is an architecture change — it goes through the planning docs, not through a clever include.

## Enforcement

The `harness.layering` test holds the four-layer map and fails on an undeclared or upward edge (shipped with A1). [A4](../../backlog/architecture/arch-build-enforcement.md) makes the law compile-time: one static library per module, the link graph as the dependency declaration, the grep test rescoped to its named scalpel allowances. The summary card at each layer root restates this rule in brief and links here.
