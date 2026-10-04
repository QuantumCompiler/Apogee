# Machine mode: the handshake and the stability promise

**What / why.** A host embedding Apogee today flies blind until after the first turn: the `session` event carries only `model` and `protocol_version`, the child speaks first, and there is no way to declare what the host is or ask what this binary supports (spike wall W1, [Milestone M](../../assistant/MILESTONES.md#2026-09-25--the-integration-spike-a-naive-host-embeds-the-binary-backlog-item-27-for-v014)). This item gives the contract a front door: an **optional `hello` line** the host may send first, and a `session` event grown with a **`capabilities` object** — the event types this build emits, the inbound types it accepts, whether tools/ask are live, and the schema version — plus, in [machine-mode.md](../../reference/machine-mode.md), the **stability promise in writing**: what `protocol_version: 1` guarantees a third party, that additions are the growth mechanism (new event types *and* new fields on existing events), and what a breaking change owes integrators. The spike proved this is retrofittable in both directions at zero cost: an unknown inbound line is already ignored (verified live against v0.1.2), so `hello` to an old binary is harmless, and rule 1 keeps every existing driver working under the additions.

**Core constraint(s).**
- **Additive only.** `session` keeps every field it has; `capabilities` is a new field, `hello` a new inbound type. A `protocol_version: 1` driver that never sends `hello` sees today's behaviour exactly. No version bump.
- **The child still speaks first.** `session` is emitted unprompted as today; `hello` refines it (a host that sent one may get capabilities scoped to what it asked for), never gates it — a contract where the child waited for `hello` would hang every existing driver.
- **The conformance discipline extends:** the new field and inbound type are documented in machine-mode.md and pinned by `cli.machine_schema_conformance` in both directions, like everything else in the protocol.
- **Field-tolerance becomes explicit.** Rule 1 covers unknown *types*; the stability promise must also state that drivers ignore unknown *fields* on known events — the additions this ring makes (capabilities here, `turn` in [28f](machine-turn-control.md)) depend on it.
- **No secrets in the handshake:** capabilities name what the binary can do, never a key, a path into the private layout, or a config value.

**Seam + files.**
- `machine/json_reporter.cpp` / `render/json_report.h/.cpp`: `capabilities` on the session event — built from what is actually wired (tools registry present, ask available, input format), not a hardcoded list.
- `cli/chat.cpp` / `cli/complete.cpp`: accept and parse the optional `hello` first line on stream-json stdin; an absent or malformed one is ignored per the inbound-tolerance rule.
- [machine-mode.md](../../reference/machine-mode.md): the `hello` line, the `capabilities` field, and a new **"The stability promise"** section (guarantees, additivity of types *and* fields, deprecation policy).
- `tests/scripts/py/schema_conformance.py` + the machine-mode e2e: pin both additions; `tests/scripts/py/naive_host_driver.py` grows a hello-aware path proving old-binary compatibility semantics stay.

**Reference (Ommi).** No analog — machine mode is Apogee's divergence. Prior art: MCP's `initialize` (capability negotiation between peer processes) and LSP's `initialize`, both of which put capabilities in the *reply* to a client hello; Apogee inverts it (child announces, host refines) to stay compatible with drivers that predate the handshake.

**Decisions made** (dated):
- 2026-09-25 — Split from the integration spike (item 27): wall W1, with the compatibility mechanism verified live (unknown inbound lines ignored by v0.1.2).
- 2026-09-25 — **The push channel is parked, the user's call**, with the spike's evidence: v1's "events arrive in response to turns" held comfortably for an embedding host. If ever wanted, `capabilities` is where a host and binary would negotiate it — nothing in this item's design forecloses that.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): `capabilities` = events, accepts, tools, ask, schema (28g's version string).
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): `hello` carries client {name, version} and optional `wants`; logged for diagnostics, no behavior change in the first cut.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): a mid-conversation `hello` is ignored with a dim stderr note — never a session-ending error.

**Guardrail(s).**
- The e2e drives one child with `hello` and one without; identical turn behaviour, `capabilities` present in both.
- `cli.machine_schema_conformance` fails on the new field or type going undocumented, in both directions.
- A golden `session` fixture asserts today's fields are byte-stable under the addition.
- `naive_host_driver.py` re-run: W1 closes; every other measurement unchanged.

**Acceptance criteria:**
- [ ] A driver that sends nothing new sees today's stream plus a `capabilities` field it may ignore; the machine-mode e2e passes unmodified except where it asserts the new field.
- [ ] A host can read, from the `session` event alone, which event types this build emits, which inbound types it accepts, and whether tools and `ask_user` are live.
- [ ] `hello` from a host is accepted first-line, recorded, and harmless everywhere else — including against the *previous* release's binary (ignored, by its own inbound tolerance).
- [ ] machine-mode.md carries the stability promise: v1's guarantees, type *and* field additivity, and the deprecation policy, conformance-pinned where mechanical.

**Scope note.** Item **28d** (27a at birth; 28a, then 29a, through the day's renumbers; 28d since 2026-10-03's collapse into v0.1.5 — the user's calls), earmarked for **v0.1.5** — first of the integration ring; gated on nothing pending. Out of scope: any push channel (parked, the user's call); behaviour changes negotiated by `hello` (later items may add them); the schema artifact itself ([28g](machine-schema-artifact.md)).
