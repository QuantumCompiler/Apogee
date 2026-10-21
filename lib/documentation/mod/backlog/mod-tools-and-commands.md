# Tools, slash commands and the status line

**What / why.** The mod's working surfaces, all over contracts the spike measured live (2026-10-21): **the tools** — Apogee's MCP server registered into the session (the 17 read-only tools answer today; the registration JSON is what `mcp presets` prints), plus **`apogee_complete`** offered via `$.tool.register` running `apogee complete -m <backend> "<prompt>"` one-shot — the CLI path now, switching to 54b's served tool when the CLI's opt-in callable set ships (cross-referenced, never gated); **the `/apogee` slash set** — check, models, train status, promote, rollback — each the real command with its JSON document rendered, the destructive two (promote, rollback) confirming before `--yes`; and **the status line** — the active backend, promoted version and run state from `models status` + `train status`, polled gently, stale said. The async law binds everything long: a training run or big completion launches, toasts, and polls — a turn is never blocked (the spike's W4).

**Core constraint(s).** The ⚠ client invariant (documents in, commands out, tolerant reads); handler logic in Business, registration in Presentation (ADR 0001); no `--yes` without the confirmation; completions bounded (a max-tokens default said in the tool's description); the tool descriptions honest about spend (local and unmetered, or not offered). Hermetic tests over scripted process output.

**Seam + files.** `src/business/` (the client: reads, the complete runner, poll logic), `src/presentation/` (tool/command/status registration), `src/data/` (the document types). Tests: handler matrices over scripted documents; the confirm gates; the async pattern (launch → toast → poll) on a scripted clock; argv pins per command.

**Reference.** The mod spike (2026-10-21) — the server handshake, the measured documents, the one-shot complete; the CLI's [54a/54b](../../cli/backlog/v0.2.1/mcp-serve-public.md) siblings — referenced with fallbacks; the GUI's jobs/read-back disciplines mirrored in miniature.

**Decisions made** (dated):
- 2026-10-21 — `apogee_complete` ships on the CLI path first; 54b's served tool replaces the plumbing, not the surface.

**Open calls:**
- [default: the slash set's exact roster and output rendering at build per the prototype of least surprise (the command's own document, formatted); poll cadence conservative.]

**Guardrail(s).** Handler matrices; confirm pins; the async matrix; argv pins; `tsc`/validate clean; no live binary required by any test.

**Acceptance criteria:**
- [ ] Claude can call `apogee_complete` against a configured backend and the registered read tools; results return within the turn or as the async pattern.
- [ ] The `/apogee` set runs the real commands with documents rendered and the destructive two confirmed; the status line tracks backend/version/state and says staleness.

**Scope note.** Earmarked for v0.2.1; gated on [`mod-scaffold.md`](mod-scaffold.md) (1a). Out of scope: the pane (1c), RAG beyond the server's `search_documents`, any CLI change.
