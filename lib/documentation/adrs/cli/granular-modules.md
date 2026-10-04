# ADR 0003 — One concern per module

**Status:** Accepted · **Date:** 2026-10-03

## Context

The architecture spike's six violating edge-types traced to concerns sharing a package: the contracts hid inside the harness, the model store inside `models/`, the wire primitives inside `backends/`. Each carve (A1) dissolved violations without touching behavior — evidence that the monolith, not the code, was the problem. The one remaining judgment split is `commands/` into `cli`/`views`/`machine` ([A3](../../assistant/MILESTONES.md#milestone-aa--the-four-layers)).

## Decision

**A module is one concern, and stays one.** Within every layer:

- A package owns a single responsibility, nameable in a phrase; the directory name is that phrase.
- A package that accretes a second concern **splits** — the split is ordinary maintenance, not an architecture event, because renames are cheap and reviewable (A2's 433-rename precedent: include lines unchanged, pure moves).
- New capability generally means a **new module**, not a bigger one; extending a module is for extending *its* concern.
- Module boundaries are link-visible: one static library per module ([A4](../../assistant/MILESTONES.md#milestone-aa--the-four-layers)), so a dependency is a declared edge, never an accident of a shared target.

## Consequences

- Many small directories over few large ones — short files, shallow includes, per-module tests.
- A reviewer can hold a module in their head; a builder can rebuild one without the world.
- "Where does this go?" has a checkable answer: the module whose phrase describes it, or a new one.

## Chosen trade-off

More directories and build targets, accepted deliberately: the spike showed the real cost sat in tangles, not in file count, and A4 records incremental-build timings so the target count's price is measured rather than argued.

## Enforcement

The link graph (A4) and the layering map (ADR [0001](layer-enforcement.md)); the review rule that a package description needing "and" is a split waiting; the codebase maps in [CLAUDE.md](../../assistant/CLAUDE.md)/[DEVELOPER.md](../../assistant/DEVELOPER.md) list one concern per row — a row that can't be written that way fails the docs pass.
