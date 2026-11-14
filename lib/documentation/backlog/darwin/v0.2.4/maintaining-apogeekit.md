# Maintaining ApogeeKit: the ADR and the contributor rules

**What / why.** The user's call, 2026-11-14: *"Make sure there is a task to update adrs / claude documentation about how to maintain this integration."* [7d](apogeekit-swift-package.md) carves the Mac app's client core into **ApogeeKit**, a Swift package that other apps embed (Taylor'd Portfolio first), and that the Mac app itself depends on. Once the package exists, three things can drift without anyone noticing:
- **What it publishes.** A public type renamed in a refactor of the Mac app breaks every outside app that depends on it. The app's own build and tests stay green.
- **What lives in it.** A Business change that imports SwiftUI, or a view-model helper that drifts into the package, would make it depend on a UI framework. An app shouldn't get that.
- **Whether it's the only copy.** A convenience written in the app instead of the package grows a second client core. That is the drift the GUI's [machine-mode-client rule](../../../assistant/darwin/CLAUDE.md) exists to prevent.

The app's other laws are held by ADRs and the layering check (the GUI's [0001](../../../adrs/gui/layer-enforcement.md) and [0003](../../../adrs/gui/parity.md), darwin's [0002](../../../adrs/darwin/presentation-mvvm.md) and [0004](../../../adrs/darwin/module-schemes-and-previews.md)). This item gives the package the same: an ADR, enforcement, and the contributor rules. Its CLI sibling, [60e](../../cli/v0.2.4/maintaining-the-app-integration.md), writes the integration's law on the harness side.

**Core constraint(s).**
- **ADRs are append-only**: a new record in the GUI's numbered series, and **darwin's** because it binds the Apple apps' Swift code.
  - It takes the next free number when built.
  - [8a](../v0.2.5/app-builds-for-ios-and-ipados.md) (v0.2.5) plans GUI ADR 0005 for the Apple platforms. Whichever of the two lands second takes the next number, and its draft is renumbered.
- **One client core** (the GUI's machine-mode-client rule): the package is the only implementation of the protocol, the transports, the sessions and the reads. The Mac app consumes it, never a copy beside it.
- **The layers** ([GUI ADR 0001](../../../adrs/gui/layer-enforcement.md)): the package holds only layers below Presentation, and imports no UI framework.
- **Additive** (the CLI's [ADR 0006](../../../adrs/cli/backwards-compatibility.md)): the package's public interface grows only by addition. A removal or a rename needs a deprecation that ships at least one release first. The package's version is tied to the protocol's schema date, as [60d](../../cli/v0.2.4/building-an-app-on-apogee.md)'s promise states.
- **The anti-goal**: nothing here makes the Mac app need a server; the child-process transport stays in the package for macOS.

**Seam + files.**
- `lib/documentation/adrs/darwin/apogeekit.md`, the record **"ApogeeKit is the one client core, and its public interface only grows"**:
  - **Context**: the 2026-11-14 spike's W6 measurement. The lower layers are 8 modules, 56 files and about 8,300 lines, with no UI imports. There is no `Package.swift`.
  - **Decision**:
    - what the package holds and what it never holds (`DataPreferences`, anything from Presentation);
    - the Mac app depends on it;
    - its public interface is additive, versioned with the protocol;
    - a change to a public type follows the deprecation rule.
  - **Consequences**, and **Enforcement** (below).
  - `adrs/darwin/README.md` gains its row.
- **Enforcement**:
  - **The layering check** (`lib/src/darwin/tests/architecture/layering/`) gains the package's rules:
    - its modules hold to the map;
    - nothing in it imports a UI framework;
    - the app target links the package and declares no second copy of a module the package holds.
  - **A public-interface check**: the package's public symbols (a `swift-api-digester` baseline, or the package's own symbol-graph dump) compared with a committed baseline. A removal or rename fails, naming the symbol, unless the baseline marks it deprecated a release earlier. An addition updates the baseline in the same change.
- `lib/documentation/assistant/darwin/CLAUDE.md`:
  - a section, "ApogeeKit", stating the rule and linking the ADR;
  - checklist lines in Implementing a Feature:
    - a change below Presentation lands in the package;
    - a public-interface change is additive and updates the baseline;
    - a capability the CLI adds for apps (CLI 60e's census) gets the package's support in the same release.
- `lib/documentation/assistant/darwin/DEVELOPER.md`: the package in the tree, how to build and test it alone, and a recipe, "Adding to ApogeeKit's public interface".
- `lib/documentation/adrs/darwin/README.md`: the row.

**Reference.**
- [7d](apogeekit-swift-package.md), the package this item maintains.
- [CLI 60e](../../cli/v0.2.4/maintaining-the-app-integration.md), the integration's law on the harness side, whose census names "the package's support" as one of a capability's duties.
- [CLI 60d](../../cli/v0.2.4/building-an-app-on-apogee.md), the integrator's guide and the compatibility promise this record enforces for the package.
- Darwin [ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md), the precedent for a darwin rule that the layering check enforces.

**Decisions made** (dated):
- 2026-11-14: The user's call: a task to update the ADRs and the contributor docs on how to maintain the integration. This is its package half, after 7d.
- 2026-11-14: The record is darwin's, not the GUI's shared one: it binds the Apple apps' Swift code. A Linux or Windows app's client core would get its own record when built.

**Open calls:**
- [default: the public-interface check uses `swift-api-digester` against a committed baseline, run in the darwin suite; if it proves unreliable on the CI image, a symbol-graph dump compared as JSON.]
- [default: a deprecation ships at least one release before a removal, said in the package's changelog section of the darwin MILESTONES.]
- (consumed decision: 7d) what the package holds and how it is consumed; (consumed decision: CLI 60d) the compatibility promise's wording.

**Guardrail(s).** The layering check's new rules and the public-interface check, each proven on a planted violation (a UI import in the package, a renamed public type); the ADR held to the index by the docs validator.

**Acceptance criteria:**
- [ ] The darwin ADR exists, is in the index, and names its enforcement by file.
- [ ] A UI import in the package and a renamed public symbol each fail their checks, naming the file and the symbol.
- [ ] The darwin contributor guide states the rule and the per-change duty; DEVELOPER.md has the recipe; the full suite is green.

**Scope note.** Earmarked for v0.2.4 (track 7); gated on 7d. Out of scope: Taylor'd Portfolio's own side (not specced now, the user's call, 2026-11-14), and any change to what the package holds. This item writes the rule; 7d builds the package.
