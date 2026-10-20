# ADR 0001 — Four layers, dependencies point down, regardless of language and framework

**Status:** Accepted · **Date:** 2026-10-20

## Context

The CLI proved the shape: its 2026-10-03 architecture spike measured a 22-package include graph 93% conformant to a four-layer model, the carve brought it to zero upward edges, and the law has been build-enforced since ([the CLI's ADR 0001](../../cli/adrs/layer-enforcement.md) and [0008](../../cli/adrs/module-map.md) — one static library per module, the link graph as the declaration, a layering scan with teeth). The GUI opens as three platform applications (`darwin`, `linux`, `windows`) whose languages and UI frameworks are not yet chosen — and may not be the same — which is exactly when structure drifts: without a shared law, three apps grow three architectures and nothing transfers between them. The user's call, 2026-10-20: the GUI mimics the CLI's layered architecture, **regardless of the language and framework used to build it**.

## Decision

Every GUI application is four layers, lowest to highest, with the same downward law as the CLI's:

1. **Infrastructure** — OS mechanisms behind one interface: spawning and holding the `apogee` child process, file-system paths, the platform's own services (notifications, lifecycle). What everything stands on.
2. **Data** — the contracts floor: the machine-mode protocol as types (events, driver lines, the reads' documents, the schema), decoding that tolerates unknowns, and the GUI's own persisted state (window layout, preferences). How the outside world — chiefly the CLI — is reached and represented.
3. **Business** — the client core: the machine-mode session (spawn through Infrastructure, the JSONL exchange, event dispatch, cancellation), the reads, long-running job tracking. What the GUI does.
4. **Presentation** — the windows, views, and rendering, in whatever UI framework the platform's app uses. How it is shown and driven.

**A package may depend only on packages in its own layer or in layers below it.** Dependencies point down, never up. And the rule is framework-proof in both directions: whatever language a platform's app is written in, the four layers and the downward law hold — and the UI framework's own types are confined to Presentation, exactly as the CLI confines FTXUI to its one module. The layer meanings above are shared across all three applications, so a contributor (or an agent) who knows one app's shape knows them all.

## Consequences

- Adding a package means declaring its layer first; the directory structure states the declaration, whatever the language's module system.
- Shared types sink: anything two layers need lives at the lower one's floor — the protocol types are the Data floor's, never Presentation's.
- A framework that wants to own the whole app's structure (view-model conventions, code generation) gets its wishes granted inside Presentation and nowhere else.
- Whether the three apps share a machine-mode client core is decided *within* this shape (a shared Business/Data implementation, or three per-language ones held to the same layer contract) — the first spike's question, not this record's.
- A change that would point an edge upward is an architecture change: it goes through the planning docs, never through a clever import.

## Enforcement

Lands with the first application's build: each app establishes its language's mechanical analog of the CLI's law — a module map as the one declaration and a layering check that fails on an undeclared or upward edge (the CLI's `harness.layering` and link policy are the reference implementation). Until that exists, this record and review hold the line, and the first application's backlog items must name their layer in **Seam + files** from day one.
