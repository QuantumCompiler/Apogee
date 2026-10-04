# Side model calls narrated in the thinking block

**What / why.** A turn is no longer one model call. Around the chat model, a single question can now run an embedder (retrieval over the chat's collection and attachments), the utility role (rewriting a follow-up into a standalone search query, summarising an oversized tool result, compacting), the rerank judge, the vision or transcription role reading an attachment, and — on `/capture` — the extraction clerk. Today that machinery is invisible or a status-line blip, while the chat model's own reasoning gets a first-class home: the thinking block. The user's ask (2026-09-30): **fold the other model calls into that block** — one dim, rolling, collapsing narrative of everything machine-side that happened before the answer: `· embedding — 1 query → embedder (local)`, `· utility — rewriting the follow-up into a search query`, `· rerank — judging 12 chunks · 0.6 s`, `· vision — describing photo.jpg`. The turn reads as one story in one place, and the status line goes back to being transient state rather than a scrolling log of side work.

**Core constraint(s).**
- **Narration is thinking-shaped: transient, never persisted.** Side-call lines live and die with the thinking display — they collapse with it, never enter saved history, never appear in `result` text or the answer stream. The reasoning-persistence rule extends to them verbatim.
- **CLI rendering only; the wire is untouched.** This is `CliReporter`'s presentation choice. Machine mode maps the same narration onto the **existing** `tool_status` event (whose documented meaning is already "described in `text` for display") — no new event types, `cli.machine_schema_conformance` stays green, and protocol growth remains the machine-mode items' business (28d–28h). The SSE adapter maps to its existing meta-frame the same way.
- **One painter.** Entries enter the thinking block through `ThinkingView`'s own write path — no second rolling-window implementation, no competing erase arithmetic. The never-the-last-column and width-per-repaint rules apply because the view already enforces them.
- **The seam stays shared and the layers stay blind:** `agentloop/` (and the chat command's clerk path) emit a side-call event through the Reporter; what each adapter renders is its own business. `agentloop/` learns nothing about views — the event carries role, description, and (when truly known) elapsed and tokens; **no timing lies**: counts and durations render only when the layer reports them.
- **In-turn only — the division of labor with [M1](../../assistant/MILESTONES.md#milestone-g--the-terminal-ux-layer) (shipped 2026-10-03):** the thinking block narrates work *inside a turn that owns the terminal*; slow commands outside a turn are the busy line's job; genuinely background work (the async chat title, attachment indexing between turns) stays off both the block and the transcript, keeping the status line it has today.
- **Quiet means quiet:** whatever suppresses the thinking display today (`--quiet`, pipes — where none of this ever rendered) suppresses the narration identically; nothing new leaks to a non-TTY.

**Seam + files.**
- `agentloop/reporter.h`: one new event — `on_side_call(role, detail)` with a paired completion carrying elapsed/tokens-when-known — defaulted no-op, so every adapter compiles unchanged and opts in (the header's own "a local change rather than a rewrite" doctrine).
- Emission sites: `agentloop/rag.cpp` (the embed and the retrieval round), the query-rewrite and tool-result-summary paths (the utility role's in-turn work), `agentloop/rerank*` (the judge), the attachment read path where the vision/transcription role runs in-turn, and `cli/chat.cpp`'s `/capture` clerk.
- `views/cli_reporter.cpp` + `views/thinking_view.h/.cpp`: side-call lines join the rolling window interleaved with reasoning in arrival order; a labeled-line entry point beside `write()` so the view, not the reporter, owns the dim styling and the collapse.
- `machine/json_reporter.cpp` / `httpserver/sse_reporter`: the mapping onto the existing display-prose events; byte-level goldens prove no vocabulary change.
- Tests: `tests/presentation/views/cli_reporter_test.cpp` goldens (a scripted turn with embed + rewrite + rerank narrates in order inside the block and collapses with it); transcript-purity assertions (saved session and `result` free of narration); the machine-mode e2e and conformance untouched; the PTY chat check extended one case.

**Reference (Ommi).** No analog — Ommi surfaced none of its side work; the typed thinking display is Apogee's own (Milestones D and G), and the helper-role side-request architecture it narrates is 26b's. This item is those two shipped pieces meeting.

**Decisions made** (dated):
- 2026-09-30 — Asked for by the user: the other model calls (embedder, extractor, and the rest) folded into the thinking block; **end of v0.1.3**, lettered **26n** per the release-prefix rule.
- 2026-09-30 — One new Reporter event mapped per adapter, rather than reusing `on_tool_status` at the source: the CLI needs to tell side calls from tool runs to place them in the block, while machine mode deliberately collapses both into its existing display-prose event — the distinction exists exactly where it is needed and nowhere else.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): line shape `· <role> — <what it is doing>`, completed in place with `· <elapsed>` and `· <N> tokens` when reported; label truncated against the live width.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): with no model reasoning, the block still appears when there are side calls to narrate — an all-narration block, collapsing the same way.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the first-cut set is embed, retrieval, rewrite, summarise, rerank, in-turn vision/transcription and the `/capture` clerk; mid-turn compaction announces itself the same way.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): `auto_rag` collection selection stays a status-line notice — one-shot state, not a side call.

**Guardrail(s).**
- Reporter goldens: the scripted turn's block interleaves reasoning and side-call lines in arrival order; collapse leaves the same residue the thinking view leaves today (none).
- Transcript purity: the saved session, the `result` event text, and a piped run are byte-free of narration — asserted with a distinctive side-call label.
- Conformance: machine mode's vocabulary unchanged in both directions; the SSE golden unchanged but for prose text.
- The no-op default: a Reporter adapter that ignores the new event behaves exactly as before (compiled-and-asserted on the mock reporter).

**Acceptance criteria:**
- [ ] A chat turn with retrieval, a utility rewrite and rerank on a TTY shows each as a labeled line inside the thinking block, in order, completing with elapsed (and tokens when known), and the whole block collapses as it does today.
- [ ] A turn on a non-reasoning model with side calls still gets the block, narration only.
- [ ] The saved transcript, the `result` text, piped output and machine mode carry none of it; `cli.machine_schema_conformance` passes unmodified.
- [ ] `/capture` in chat narrates the clerk in the block instead of a bare status print.

**Scope note.** Item **26n**, earmarked for **v0.1.3** (the end — the user's call); gated on nothing pending. Interplay, not gates: [26i](thinking-control.md) shares the block (its off-switch meets the all-narration default above); [M1](../../assistant/MILESTONES.md#milestone-g--the-terminal-ux-layer) owns the outside-a-turn case. Out of scope: narrating background work that never owns the terminal (titles, between-turn indexing); any machine-mode or SSE vocabulary change; cost/billing accounting beyond what a call already reports.
