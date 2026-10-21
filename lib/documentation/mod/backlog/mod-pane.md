# The pane: Apogee beside the chat

**What / why.** The visual half (the spike's feature 3): a pane (`$.ui.open` + the render hook) showing **backends and their models** (`models list|status`), **training** (`train status` — the cycle, pipelines, running and recent runs), and **the version ledger** (`train versions`) — with **promote and rollback as buttons**, each confirming with the exact command shown before it runs (the house's destructive manners, the GUI's shown-batch pattern). Every datum is a measured JSON document (the spike, 2026-10-21); the pane re-reads after every action (read back, don't mirror) and polls the training view only while visible. The adapters column (per backend, with switching) is the recorded follow-on once the CLI's track 53 ships — noted, not gated.

**Core constraint(s).** ADR 0001 (render in Presentation; state and logic below); documents re-read after mutations; polls only while visible; absence honest (no binary, no backends — the stated empty states); nothing invented — a number the documents don't carry isn't shown.

**Seam + files.** `src/presentation/pane/` (the Pane component, status atoms), `src/business/` grown (the pane's reads and actions), `types/index.d.ts` (the atoms). Tests: the pane's view logic over scripted documents (each section, empty states, the confirm-then-run flow, visibility-scoped polling).

**Reference.** The mod spike (2026-10-21) — the documents measured live; the GUI's monitor/read-back patterns in miniature; track 53 (CLI) — the adapters follow-on, referenced.

**Decisions made** (dated):
- 2026-10-21 — The pane is reads plus two confirmed actions; anything session-grade (a chat in the pane) is explicitly out (the SPEC's non-goal).

**Open calls:**
- [default: layout and density at build; the ledger section lists the versions document as-is.]

**Guardrail(s).** View-logic matrices over scripted documents; the confirm-shows-the-command pin; visibility-scoped polling; `tsc`/validate clean; hermetic.

**Acceptance criteria:**
- [ ] The pane shows backends/models, training state and the ledger from the real documents, re-read after actions; empty and absent states honest.
- [ ] Promote and rollback run only after their shown-command confirmation; polling stops when hidden.

**Scope note.** Earmarked for v0.2.1; gated on [`mod-tools-and-commands.md`](mod-tools-and-commands.md) (1b). Out of scope: adapters UI (the 53 follow-on), chat surfaces, any CLI change.
