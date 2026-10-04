# ADR 0006 — Older versions keep working

**Status:** Accepted · **Date:** 2026-10-03

## Context

An installed Apogee owns durable state and external parties: a hand-edited `config.yaml`, a model store under `~/.apogee/models`, saved chats and attachment indexes, secrets, training runs — and, since machine mode became the front-end contract, **other people's drivers** speaking `protocol_version: 1` at the binary's pipes. The integration spike proved the tolerance rules live (a naive host kept working while the protocol grew), and the model store already carries the precedent for layout change done right: the legacy flat layout is *refused by name* with `apogee models migrate` offered — never silently reinterpreted, never broken.

## Decision

**A new Apogee version serves the state and the counterparties of the old one.**

- **Config:** an older config loads; new keys arrive with defaults and are absent-safe; a key's meaning never silently changes. Removing or repurposing one is a deliberate, documented act with a stated migration.
- **The machine protocol grows additively** within a `protocol_version`: new event types and new fields only, never a changed meaning for an existing one — the tolerance rules (ignore unknown types and fields) are what make this retrofittable in both directions, and they are the contract's floor.
- **On-disk state migrates, loudly:** stores, sessions, caches and indexes are either read compatibly or met with a named migration command; "rebuilt silently" is acceptable only for disposable cache state that documents itself as such.
- **Behavior contracts named in records** (MILESTONES' shipped guarantees, the reference docs) hold until a dated revision says otherwise; breaking one is a bug even when the code change was intentional.

## Consequences

- "Delete the old format reader" waits for a major-version conversation; until then compatibility code is load-bearing, not cruft.
- A driver, script or config written against version N is the test case for version N+1.
- Compatibility work is visible in estimates: a format change costs its migration.

## Enforcement

`cli.machine_schema_conformance` pins the protocol; the schema artifact and additivity guarantees are queued to make the promise an artifact ([the machine-mode items, 28d–28h](../../backlog/v0.1.5/machine-handshake.md)); the store's refuse-and-name-the-migration pattern (`find_legacy`/`legacy_refusal`) is the template for any layout change; config loading keeps its absent-safe defaults by test. The release notes' *Known limitations* section owns any deliberate exception, dated.
