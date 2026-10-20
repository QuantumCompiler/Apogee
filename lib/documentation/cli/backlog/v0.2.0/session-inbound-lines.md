# The driver's new lines: detach, set, compact — and a session that is never written

**What / why.** Four session abilities the design draws have no door in the protocol, per the contract board: **(G2)** attach has its inbound line, detach has none, so the app hides Detach; **(G5)** changing model, thinking, effort, temperature or the system prompt mid-session means ending the child and restarting with `--resume` — a local model loads again for a settings tweak; **(G6)** there is no compact — not as a driver line, not as a verb over a saved chat; **(G7)** a private session does not exist — the app's fallback is `--no-recall` plus `chats delete` on close, which still writes the transcript to disk for the session's lifetime. This item adds the doors, each reaching a core that already exists: an inbound **`{"type":"detach","path":…}`** answered by the same notice an attach gets (the detach the terminal's `/detach` already performs); an inbound **`{"type":"set",…}`** for the run settings the session's own slash commands already move (`/model`, `/system`, think/effort/temperature), applied from the next turn and said in a `notice`; an inbound **`{"type":"compact"}`** plus the verb **`chats compact <id>`**, both driving the one compaction the loop already owns; and **`chat --private`** — no transcript written, no recall summary, said at session start, the machine `session` event carrying `private: true`.

**Core constraint(s).** **Every line reaches the one core** (ADR 0011's spirit in the protocol): `detach` is `/detach`'s core, `set` is the slash commands' own setters through the one command table, `compact` is the loop's compaction — never a second implementation reachable only from machine mode. **Additive** (ADR 0006): `capabilities.accepts` gains the new lines, so a driver can feature-test; older binaries' refusal of an unknown line is already defined. **Private is absolute and honest**: nothing of the conversation lands under `sessions/` or the recall index — not written-then-deleted; the one session opening carries the switch so every front end gets it (ADR 0011), and the TUI/terminal banner says it. **`chats compact` is classified** (ADR 0010) and completes (ADR 0007); mode parity per ADR 0002 — the admin plane's served sessions get none of this (recall is CLI-only by 26l's law; a served compact is a recorded out), said.

**Seam + files.** `presentation/machine/driver_input` (+ the three lines), `presentation/machine/protocol` (declared; `accepts` grown), `presentation/cli/chat_session` + the command table (the cores reached; `--private` through the one opening), `business/agentloop/` (compaction's entry for a saved chat), `presentation/cli/chat_history` (`chats compact`), `logger/` (the never-write switch), `machine-mode.md`. Tests: each line driven over the mock backend (the effect asserted on the next turn; the notice golden); `--private` leaving zero files and zero recall rows (the absence pinned); `chats compact` on a fixture session; capabilities' accepts golden; the TUI parity row.

**Reference.** The contract board's G2/G5/G6/G7 rows and fallbacks; 28f (`driver_input`, the cancel precedent for an inbound line reaching a turn); 26l (recall's CLI-only law); the session core (27s) — the one opening `--private` rides.

**Decisions made** (dated):
- 2026-10-20 — Specced from the GUI design spike, the user's call.
- 2026-10-20 — Every inbound line is an existing core's door; `--private` means never-written, not written-then-deleted.

- 2026-10-20 — **`set`'s keys** are exactly the session's own slash-settable run settings, refused by name otherwise; shapes at build (the default, confirmed at grooming).
- 2026-10-20 — **`chats compact` asks** before rewriting a saved session on a terminal, `--yes` for scripts — the house's destructive-verb manners (the default, confirmed at grooming).

**Open calls:** none — groomed 2026-10-20.

**Guardrail(s).** Per-line effect tests over the mock backend; the private absence pin (filesystem and recall index swept); accepts/schema conformance; the one-core rule held by tests that drive both doors (slash and line) and assert one behavior; no network, no models.

**Acceptance criteria:**
- [ ] `detach`, `set` and `compact` lines work in a driven chat with their notices; `chats compact <id>` compacts a saved session, asked first on a terminal.
- [ ] `chat --private` runs a whole session leaving no transcript and no recall row, every front end saying so; `capabilities.accepts` announces all four doors.
- [ ] machine-mode.md and the schema agree; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 52); ungated. Out of scope: served-session twins (recall and compaction stay CLI-side by the standing laws), the GUI's use of the lines, new run settings beyond what slash commands already move.
