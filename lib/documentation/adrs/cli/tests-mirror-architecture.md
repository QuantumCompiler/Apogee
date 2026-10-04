# ADR 0004 — Tests live in the layer of what they test

**Status:** Accepted · **Date:** 2026-10-03

## Context

A2 mirrored the test tree with the source tree — 194 test files into `tests/<layer>/<package>/` in the same change as the source moves — because a test tree with its own geography answers "where is this tested?" with a search instead of a path. The layers only pay off if the tests keep them honest: a test that reaches across layers freely is an undeclared dependency wearing a lab coat.

## Decision

**The test tree follows the architecture, and a test is written in the layer of the thing it tests.**

- `tests/<layer>/<package>/` mirrors `source/<layer>/<package>/`; a new package brings its test directory in the same change.
- A unit test lives beside its subject's layer and obeys the same include law — a Data test does not reach into Business to fabricate its fixtures; it uses its own layer and below.
- A behavior spanning layers is tested **in the highest layer involved** (the command test drives the loop; the loop test drives the store), which keeps every test's dependencies pointing down exactly as ADR [0001](layer-enforcement.md) demands of the code.
- Shared test scaffolding sinks like shared code does: fixtures two layers need live at the lower layer's test floor.

## Consequences

- The test tree is a map of coverage: an empty `tests/<layer>/<package>/` is a visible hole, not a silent one.
- Moving a package moves its tests in the same rename set — A2's precedent, now the rule.
- A test that cannot be written without an upward include is exposing a design fault in the subject, not a missing allowance in the law.

## Enforcement

The mirrored-tree shape is checked structurally (the presence test [A5](../../backlog/architecture/arch-adrs-layer-context.md) adds, beside the layering test); the include law applies to `tests/` exactly as to `source/` under the layering map and A4's link graph — test targets link their layer's libraries and below, nothing else.
