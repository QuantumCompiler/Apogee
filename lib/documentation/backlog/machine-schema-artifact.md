# Machine mode: the schema artifact

**What / why.** Spike wall W2 ([Milestone M](../assistant/MILESTONES.md#2026-09-25--the-integration-spike-a-naive-host-embeds-the-binary-backlog-item-27-for-v014)): an integrator's event vocabulary is hand-transcribed from prose — the naive host's `DOCUMENTED_EVENTS` set *is* that transcription — and nothing ships that a host can validate a stream against, generate types from, or diff between releases. This item makes the contract machine-readable: **`apogee __machine-schema`** prints a JSON Schema describing the protocol — every outbound event with its fields, every accepted inbound type — from the same vocabulary `cli.machine_schema_conformance` already pins to `json_reporter.cpp`, so the schema, the prose doc, and the code cannot drift apart; the same discipline, one artifact wider. The schema carries its own version (the `schema` field [27a](machine-handshake.md)'s `capabilities` names), ships in the release archives beside the completion stubs, and [machine-mode.md](../reference/machine-mode.md) points integrators at it for validation and codegen.

**Core constraint(s).**
- **One vocabulary, three views.** The prose doc, the conformance test, and the schema all read the same declaration; a fourth hand-maintained list would reintroduce the drift this exists to close. The schema is generated from (or checked against) the declaration the conformance test uses — never written by hand.
- **The schema documents the promise, not just the shape:** unknown-type and unknown-field tolerance are expressed structurally (open enums for `type`, `additionalProperties: true` on events), so a validator built from it accepts exactly what the stability promise tells drivers to accept. A schema that rejected a legal future event would break integrators on upgrade — the opposite of its job.
- **A double-underscore command:** `__machine-schema` follows `__complete` and `__mcp-tools` — hidden from help, stable in behaviour, stdout is the artifact and nothing else.
- **Install parity:** the schema file ships in every release archive identically (the same rule as the completion stubs); nothing is generated at install time.
- **Absent is not zero, in schema too:** optionality is modelled honestly (`usage` absent when the provider reported none, `kind` only on permission questions), because a codegen'd client turns each of these into a type.

**Seam + files.**
- `commands/` (the hidden command) + `render/json_report.h/.cpp`: the event declaration becomes data one place can render as schema — whatever form the conformance test reads today is the source of truth to extend, not duplicate.
- `tests/schema_conformance.py`: grows the third direction — the emitted schema must name exactly the vocabulary the code emits and the doc documents; a fixture stream (the machine-mode e2e's own output) must validate against it.
- Release packaging (`cicd.sh` staging / the archive list): `machine-schema.json` beside the binary and stubs.
- [machine-mode.md](../reference/machine-mode.md): a "Validating and generating" section; `tests/naive_host_driver.py` swaps its hand-written vocabulary for a schema check — W2 closes in the probe itself.

**Reference (Ommi).** No analog. Prior art: LSP's published metaModel and MCP's TypeScript-source schema — both prove integrators build clients from the artifact, not the prose; JSON Schema chosen here because validators exist in every language a host will be written in.

**Decisions made** (dated):
- 2026-09-25 — Split from the integration spike (item 27), wall W2.
- 2026-09-25 — JSON Schema as the format (validator ubiquity), emitted by the binary (`__machine-schema`) so the artifact can never describe a build other than the one that printed it; the copy in the archive is a convenience, the command is the truth.

**Open calls:**
- [default: one document with a definition per event type, discriminated on `type`; draft 2020-12] Schema dialect and layout.
- [default: the schema version is a date (`2026-09-25`-style), bumped whenever the vocabulary grows; `capabilities.schema` and the artifact carry the same string] Versioning of the artifact itself.
- [default: inbound types documented in the same file under a second discriminator, since a host validates both directions] One artifact or two.

**Guardrail(s).**
- The conformance test's third direction: schema ↔ code ↔ doc, any one drifting fails the build naming the missing type or field.
- The e2e's real captured stream validates against the emitted schema with a stock validator.
- A deliberately added-but-undeclared event fails conformance (the mutation the discipline was verified with elsewhere).
- The archive check: `machine-schema.json` present and byte-identical to `__machine-schema`'s output for that build.

**Acceptance criteria:**
- [ ] `apogee __machine-schema` prints a valid JSON Schema; a stock validator accepts the machine-mode e2e's captured stream against it and rejects a stream with a mangled known event.
- [ ] The schema expresses tolerance: a synthetic future event type and an extra field on a known event both validate.
- [ ] `naive_host_driver.py` validates the live stream against the schema instead of a hand-kept set, and still completes its run.
- [ ] The release archive carries the schema; `capabilities.schema` (27a) names the version it carries.

**Scope note.** Item **27d**, earmarked for **v0.1.4**; build after [27a](machine-handshake.md) and [27c](machine-turn-control.md), so the artifact's first published version already includes the handshake, `turn`, and `cancel`. Out of scope: generated client libraries in any language (a host generates its own from the artifact); schemas for the CLI's read-command output (that contract belongs to [27e](machine-readable-reads.md)).
