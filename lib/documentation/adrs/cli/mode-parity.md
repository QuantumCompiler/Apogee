# ADR 0002 — A change propagates to every mode

**Status:** Accepted · **Date:** 2026-10-03

*2026-10-10: the full-screen shell is a fourth door. Its place for every subcommand is [ADR 0010](the-shell-is-a-mode.md)'s.*

## Context

The CLI is driven three ways: interactively at a terminal, over the HTTP admin plane (`serve`), and over machine mode (JSONL on the child's pipes — the front-end contract). When two surfaces grow separate implementations of the same resolution logic, the copies disagree — a request runs on one backend from the terminal and another over HTTP. Apogee's `roles.h` exists to make that impossible, and the repo already carries the rule in fragments: config edits over HTTP are byte-identical to the CLI's, machine mode's schema is conformance-pinned to the code, served reads mirror CLI reads.

## Decision

**A feature or modification lands in every mode it applies to, in the same change.** Concretely:

- **One core, many renderers.** Behavior lives in one implementation (Business/Data); each mode renders or transports it. A second implementation of the same decision chain is the defect this rule exists to prevent.
- **Mutations are identical everywhere**: an HTTP or machine-driven mutation produces byte-identical state to the CLI command (the config editor is the one writer).
- **Reads have twins**: a fact the CLI can print, the admin plane can serve and machine mode can carry — same facts, mode-appropriate shape.
- A mode a change deliberately does *not* reach is named in the change's record — skipped and said, never silently missing.

## Consequences

- Surfaces stay thin; pushing logic into a command handler or an HTTP route is a smell by definition.
- The cost of a feature includes its machine event and its served read; estimating without them undercounts.
- Parity gaps are bugs with a home: an item in the backlog, not folklore.

## Enforcement

`cli.machine_schema_conformance` pins machine mode to the code. The CLI↔HTTP parity conventions live in the working process ([CLAUDE.md](../../assistant/CLAUDE.md) → Documentation and Status, the parity audit rules); one-core assertions (identical payloads from CLI verb and served/tool twin, golden-compared) are the test pattern shipped items already use. The pre-MR docs pass audits parity by checklist.
