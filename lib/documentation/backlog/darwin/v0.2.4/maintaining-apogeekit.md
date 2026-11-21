# APP-7e · Maintaining ApogeeKit: the ADR and the contributor rules

| Field | Value |
|---|---|
| **Type** | 🛠️ Task |
| **Project** | Apple apps |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | ApogeeKit (darwin ADRs, the layering check, contributor docs) |
| **Labels** | `adr` `contributor-docs` `apogeekit` `public-interface-check` |
| **Epic** | Track 7 · apps as clients (7d–7e) |
| **Blocked by** | [7d](apogeekit-swift-package.md) |
| **Blocks** | — |
| **Reporter** | The user (2026-11-14) |
| **Created** | 2026-11-14 |

---

## Summary

[7d](apogeekit-swift-package.md) carves the Mac app's client core into **ApogeeKit**, a Swift package other apps embed and the Mac app depends on, but nothing yet stops the package drifting: what it publishes, what lives in it, and whether it is the only copy. This item gives the package the same law the app's other rules have: a darwin ADR, enforcement in the layering check and a public-interface check, and the contributor rules.

## Description

### User story

**As** a contributor to Apogee (or an agent working on it),
**I want** ApogeeKit's rules written down as an ADR, held by checks that fail when a change breaks them, and stated in the darwin contributor guide,
**so that** the package's public interface, its contents and its place as the one client core cannot drift without anyone noticing.

### Background

The user's call, 2026-11-14: *"Make sure there is a task to update adrs / claude documentation about how to maintain this integration."*

[7d](apogeekit-swift-package.md) carves the Mac app's client core into **ApogeeKit**, a Swift package that other apps embed (Taylor'd Portfolio first), and that the Mac app itself depends on. Once the package exists, three things can drift without anyone noticing:
- **What it publishes.** A public type renamed in a refactor of the Mac app breaks every outside app that depends on it. The app's own build and tests stay green.
- **What lives in it.** A Business change that imports SwiftUI, or a view-model helper that drifts into the package, would make it depend on a UI framework. An app shouldn't get that.
- **Whether it's the only copy.** A convenience written in the app instead of the package grows a second client core. That is the drift the GUI's [machine-mode-client rule](../../../assistant/darwin/CLAUDE.md) exists to prevent.

The app's other laws are held by ADRs and the layering check (the GUI's [0001](../../../adrs/gui/layer-enforcement.md) and [0003](../../../adrs/gui/parity.md), darwin's [0002](../../../adrs/darwin/presentation-mvvm.md) and [0004](../../../adrs/darwin/module-schemes-and-previews.md)). This item gives the package the same: an ADR, enforcement, and the contributor rules. Its CLI sibling, [60e](../../cli/v0.2.4/maintaining-the-app-integration.md), writes the integration's law on the harness side.

### Evidence

1. **W6 — there is no package to depend on** (the 2026-11-14 spike's measurement, carried in [7d](apogeekit-swift-package.md)): the lower layers are 8 modules, 56 files and about 8,300 lines, with no UI imports, and there is no `Package.swift`. This is the new ADR's Context.

## Acceptance criteria

- [ ] The darwin ADR exists, is in the index, and names its enforcement by file.
- [ ] **Given** a UI import planted in the package, or a renamed public symbol, **when** the checks run, **then** each fails its check, naming the file and the symbol.
- [ ] The darwin contributor guide states the rule and the per-change duty; DEVELOPER.md has the recipe; the full suite is green.

## Constraints

- **ADRs are append-only.** A new record in the GUI's numbered series, and **darwin's** because it binds the Apple apps' Swift code.
  - It takes the next free number when built.
  - [8a](../v0.2.5/app-builds-for-ios-and-ipados.md) (v0.2.5) plans GUI ADR 0005 for the Apple platforms. Whichever of the two lands second takes the next number, and its draft is renumbered.
- **One client core** (the GUI's machine-mode-client rule). The package is the only implementation of the protocol, the transports, the sessions and the reads. The Mac app consumes it, never a copy beside it.
- **The layers** ([GUI ADR 0001](../../../adrs/gui/layer-enforcement.md)). The package holds only layers below Presentation, and imports no UI framework.
- **Additive** (the CLI's [ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The package's public interface grows only by addition. A removal or a rename needs a deprecation that ships at least one release first. The package's version is tied to the protocol's schema date, as [60d](../../cli/v0.2.4/building-an-app-on-apogee.md)'s promise states.
- **The anti-goal.** Nothing here makes the Mac app need a server; the child-process transport stays in the package for macOS.

## Technical approach

| File | Change |
|---|---|
| `lib/documentation/adrs/darwin/apogeekit.md` (new) | the record **"ApogeeKit is the one client core, and its public interface only grows"**: Context, Decision, Consequences and Enforcement (below) |
| `lib/documentation/adrs/darwin/README.md` | the ADR gains its row |
| `lib/src/darwin/tests/architecture/layering/` | the layering check gains the package's rules (below) |
| a public-interface check, with a committed baseline | the package's public symbols compared with the baseline (below) |
| `lib/documentation/assistant/darwin/CLAUDE.md` | a section, "ApogeeKit", stating the rule and linking the ADR; checklist lines in Implementing a Feature (below) |
| `lib/documentation/assistant/darwin/DEVELOPER.md` | the package in the tree, how to build and test it alone, and a recipe, "Adding to ApogeeKit's public interface" |

**The record, `lib/documentation/adrs/darwin/apogeekit.md`:**
- **Context**: the 2026-11-14 spike's W6 measurement. The lower layers are 8 modules, 56 files and about 8,300 lines, with no UI imports. There is no `Package.swift`.
- **Decision**:
  - what the package holds and what it never holds (`DataPreferences`, anything from Presentation);
  - the Mac app depends on it;
  - its public interface is additive, versioned with the protocol;
  - a change to a public type follows the deprecation rule.
- **Consequences**, and **Enforcement** (below).
- `adrs/darwin/README.md` gains its row.

**Enforcement:**
- **The layering check** (`lib/src/darwin/tests/architecture/layering/`) gains the package's rules:
  - its modules hold to the map;
  - nothing in it imports a UI framework;
  - the app target links the package and declares no second copy of a module the package holds.
- **A public-interface check**: the package's public symbols (a `swift-api-digester` baseline, or the package's own symbol-graph dump) compared with a committed baseline. A removal or rename fails, naming the symbol, unless the baseline marks it deprecated a release earlier. An addition updates the baseline in the same change.

**The checklist lines** in `lib/documentation/assistant/darwin/CLAUDE.md`'s Implementing a Feature:
- a change below Presentation lands in the package;
- a public-interface change is additive and updates the baseline;
- a capability the CLI adds for apps (CLI 60e's census) gets the package's support in the same release.

**Reference.**
- [7d](apogeekit-swift-package.md), the package this item maintains.
- [CLI 60e](../../cli/v0.2.4/maintaining-the-app-integration.md), the integration's law on the harness side, whose census names "the package's support" as one of a capability's duties.
- [CLI 60d](../../cli/v0.2.4/building-an-app-on-apogee.md), the integrator's guide and the compatibility promise this record enforces for the package.
- Darwin [ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md), the precedent for a darwin rule that the layering check enforces.

## Testing

- **The layering check's new rules.** Proven on a planted violation: a UI import in the package.
- **The public-interface check.** Proven on a planted violation: a renamed public type.
- **The docs validator.** Holds the ADR to the index.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What does the public-interface check compare, and where does it run? | `[default]` | `swift-api-digester` against a committed baseline, run in the darwin suite; if it proves unreliable on the CI image, a symbol-graph dump compared as JSON |
| How far ahead of a removal does a deprecation ship, and where is it said? | `[default]` | at least one release before a removal, said in the package's changelog section of the darwin MILESTONES |
| What the package holds and how it is consumed | `(consumed decision)` | [7d](apogeekit-swift-package.md) |
| The compatibility promise's wording | `(consumed decision)` | [CLI 60d](../../cli/v0.2.4/building-an-app-on-apogee.md) |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | A task to update the ADRs and the contributor docs on how to maintain the integration (*"Make sure there is a task to update adrs / claude documentation about how to maintain this integration."*). This is its package half, after 7d. | the user |
| 2026-11-14 | The record is darwin's, not the GUI's shared one: it binds the Apple apps' Swift code. A Linux or Windows app's client core would get its own record when built. | the spec |
| 2026-11-14 | Taylor'd Portfolio's own side is not specced now. | the user |

## Out of scope

- Taylor'd Portfolio's own side (not specced now, the user's call, 2026-11-14).
- Any change to what the package holds. This item writes the rule; [7d](apogeekit-swift-package.md) builds the package.

## Links

- **Blocked by:** [7d](apogeekit-swift-package.md) · **Blocks:** —
- **Sibling:** [CLI 60e](../../cli/v0.2.4/maintaining-the-app-integration.md), the integration's law on the harness side
- **Follows from:** [7d](apogeekit-swift-package.md), the package this item maintains
- **Relates to:** [CLI 60d](../../cli/v0.2.4/building-an-app-on-apogee.md) (the compatibility promise); [8a](../v0.2.5/app-builds-for-ios-and-ipados.md) (plans GUI ADR 0005; the ADR numbering); the GUI's [ADR 0001](../../../adrs/gui/layer-enforcement.md) and [ADR 0003](../../../adrs/gui/parity.md); darwin's [ADR 0002](../../../adrs/darwin/presentation-mvvm.md) and [ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md); the CLI's [ADR 0006](../../../adrs/cli/backwards-compatibility.md); the [machine-mode-client rule](../../../assistant/darwin/CLAUDE.md)
