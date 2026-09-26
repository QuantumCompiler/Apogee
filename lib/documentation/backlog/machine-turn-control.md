# Machine mode: turn ids and cancel

**What / why.** Two walls from the integration spike ([Milestone M](../assistant/MILESTONES.md#2026-09-25--the-integration-spike-a-naive-host-embeds-the-binary-backlog-item-27-for-v014)). **W3:** events belong to "the current turn" by position only — no id ties a `result` to the `user` line that caused it, so a host must serialize turns and can attribute nothing after a race or a reconnect-to-log. **W4:** there is no way out of an in-flight turn short of killing the child or closing stdin (which fails the turn *and* ends the session) — yet every host has a Stop button, and the terminal already has Ctrl-C. This item adds both, additively: a **`turn` field** (a monotonic integer, assigned when a `user` line is accepted) on every turn-scoped event, and an inbound **`{"type":"cancel"}`** that aborts the in-flight turn the way Ctrl-C does — the half-turn rolled back per the loop's existing discipline, a `result` closing the turn with a cancellation `finish_reason`, the child alive for the next turn.

**Core constraint(s).**
- **Additive under the stability promise.** `turn` is a new field on known events and `cancel` a new inbound type — both covered by [27a](machine-handshake.md)'s written promise (drivers ignore unknown fields and the binary ignores unknown inbound types, the latter verified live in the spike). No `protocol_version` bump.
- **Cancel is Ctrl-C, not a new semantics.** It reuses the loop's existing cancellation path (`options.cancellation`) and its rollback rule: an aborted turn must not leave an assistant message whose tool calls were never answered — that history is rejected outright by several providers. Whatever Ctrl-C guarantees today, `cancel` guarantees, and nothing more.
- **A cancelled turn still ends in one `result`** — a driver's turn-accounting invariant (one `result` per accepted `user` line) must survive cancellation, or every host grows a special case.
- **The conformance discipline extends:** the field, the inbound type, and the cancellation `finish_reason` value are documented in [machine-mode.md](../reference/machine-mode.md) and pinned by `cli.machine_schema_conformance` both directions.
- **A `cancel` with nothing in flight is ignored** — the inbound-tolerance rule, not an error; racing a cancel against a turn that just finished must be harmless.

**Seam + files.**
- `commands/json_reporter.cpp` / `render/json_report.h/.cpp`: the `turn` counter, stamped on `answer_*`, `thinking*`, `tool_status`, `question`, `result`, `error`; the cancellation `finish_reason` on `result`.
- `commands/chat.cpp`: the stdin reader accepts `cancel` and trips the same cancellation the interrupt handler trips; a queued `user` line after a `cancel` starts the next turn normally.
- `agentloop/loop.cpp`: no new mechanism — the existing `cancellation` seam and half-turn rollback are the implementation; this item only gives machine mode a way to reach them.
- [machine-mode.md](../reference/machine-mode.md): both additions, plus the turn-accounting rule stated for drivers.
- Tests: `tests/schema_conformance.py`, the machine-mode e2e (a cancelled long turn: `result` arrives, session survives, next turn works), `tests/naive_host_driver.py` re-run — W3 and W4 close.

**Reference (Ommi).** No analog (Ommi had no machine mode). Prior art: JSON-RPC's `id` (what correlation buys) and LSP's `$/cancelRequest` (cancel as a notification against an id) — adopted here in the JSONL idiom the spike recommended over reframing: the id is a field, the cancel is a typed line, and both arrive without breaking a v1 driver.

**Decisions made** (dated):
- 2026-09-25 — Split from the integration spike (item 27), walls W3 and W4.
- 2026-09-25 — Cancel targets **the in-flight turn**, not a turn id: turns serialize in v1, so `cancel` carrying an id would imply pipelining the protocol does not have. The `turn` field is for *attribution* (logs, races, accounting), and leaves room for an id-addressed cancel if turns ever pipeline.

**Open calls:**
- [default: `finish_reason: "cancelled"` on the closing `result`, with `text` carrying whatever answer streamed before the abort] What the cancelled `result` says.
- [default: `turn` starts at 1 and also appears on `session` as the *next* turn's number, so a resuming host knows where the count stands] Numbering.
- [default: a `cancel` that lands while a `question` is outstanding fails that turn exactly as closing stdin does today — the documented semantics, reached without ending the session] Cancel versus a blocking question.

**Guardrail(s).**
- E2e: a slow scripted turn (`delay_ms`) is cancelled mid-stream — one `result` with the cancellation reason, the half-turn absent from the persisted session, the *next* turn answered by the same child.
- The turn-accounting invariant asserted: N accepted `user` lines ⇒ N `result` events, cancelled or not.
- A `cancel` with nothing in flight: no event, no error, next turn unaffected.
- Golden fixtures re-run: pre-existing events differ only by the added field.

**Acceptance criteria:**
- [ ] Every turn-scoped event carries `turn`, and a driver can attribute any event to its `user` line by number alone.
- [ ] `{"type":"cancel"}` during a streaming answer yields a `result` with the cancellation reason; the session survives; the rolled-back half-turn never reaches the persisted session or a later request.
- [ ] A v1 driver (the reference driver, unmodified) completes its run against the new binary untouched by either addition.
- [ ] machine-mode.md documents both, and `cli.machine_schema_conformance` fails on either going undocumented.

**Scope note.** Item **27c**, earmarked for **v0.1.4**; build after [27a](machine-handshake.md) (the stability promise this rides, and `capabilities` advertising `cancel` among accepted inbound types). Out of scope: pipelined turns (the `turn` field permits a future case for them; this item does not make one); cancelling `complete` (one-shot — the host kills the process, documented as such).
