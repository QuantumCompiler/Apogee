# ADR 0002 — Presentation modules are MVVM, and share one styles module

**Status:** Accepted · **Date:** 2026-10-20

## Context

[ADR 0001](layer-enforcement.md) fixes the layers and the downward law but says nothing about a Presentation module's *inside* — and that is where a SwiftUI codebase drifts first: views that reach straight into Business services are untestable without UI automation, each module invents its own file layout, and shared visual decisions (type, color, the look of a button) scatter across modules until no two windows agree. The session window's design already landed on the right seam independently (track 1c: "the view-model is a pure reducer over the core's events, so the window's behavior is tested without UI automation or a binary"). The user's call, 2026-10-20: make that shape the law — MVVM inside every Presentation module, and one shared styles module.

## Decision

**Every Presentation-layer module keeps the MVVM shape.** Inside a module:

- **`Views/`** — the SwiftUI views: layout and binding only, no business logic, no protocol types beyond what the view-model exposes.
- **`ViewModels/`** — the module's state and intents: each view-model consumes Business-layer services (the session's stream, the reads) and exposes what the views bind to; view-models carry no SwiftUI types, so they test headlessly.
- **Further directories as the module genuinely needs them** — granular, module-specific files: `Components/` for the module's own reusable pieces, `Models/` for view-local presentation types, and the like. They exist when real files need them, never as empty scaffolding.

**The edge rule inside the layer:** a view binds to its view-model; the view-model consumes Business; a view never reaches Business directly. This is ADR 0001's downward law applied one notch finer.

**One `styles` module** (`source/presentation/styles/`) owns the shared visual vocabulary: typography, colors, spacing, and the common components used across modules. Every other Presentation module may depend on `styles`; `styles` depends on no sibling Presentation module — it is the Presentation layer's own floor. A component born in one module's `Components/` is promoted to `styles` the moment a second module wants it, never copied.

## Consequences

- A new Presentation module starts as `Views/` + `ViewModels/` at minimum; its behavior lives in the view-model, so its tests are scripted-event tests, not UI automation.
- Shared visual changes land once, in `styles`; a review can reject a duplicated component or an inline color by citing this record.
- SwiftUI stays confined to Presentation (ADR 0001), and within Presentation its types stay out of `ViewModels/` — the testability seam is structural, not stylistic.
- The framework's own patterns (property wrappers, environment) live in `Views/` and `styles`; a view-model that needs one is a design smell to resolve downward.

## Enforcement

Joins the scaffold's layering check (track 1a ships it per ADR 0001's enforcement clause): the check holds every Presentation module to the `Views/`/`ViewModels/` shape, refuses a `ViewModels/` file that imports SwiftUI, and holds `styles` dependency-free of its siblings while every other Presentation module may link it. Until the first build, this record and review hold the line; track 1c's view-model tests are the pattern's standing proof.
