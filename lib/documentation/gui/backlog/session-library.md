# The session library and the inspector

**What / why.** The design's Sessions screen is a library, not one window: the session list with titles, dates and turns (`chats list --output-format json` — ready, per the contract board), rename and delete (`chats title`, `chats delete` — ready), opening a session as a live child (`chat --resume <id>` — verified present), and the **inspector pane** (`Inspector`/`ScreenSessionSources` boards: the session's model, helpers and roles from `models status --output-format json` — ready). This item builds the library around the shipped window: the list (read again on focus — the design's "read back; don't mirror" decision), open-on-select through the one client core (`--resume`, the child lifecycle per the design's decision 2: one live child per session in view, moving away ends it, returning resumes), rename/delete through the reads runner, and the inspector as a side pane. The model glyph per row waits on the CLI's 52a (`chats list` gains `backend`/`model`): until it ships, **one neutral glyph for every row** — the board's own fallback, upgraded when 52a lands, never gated on it.

**Core constraint(s).** ADR 0001/0002; reads through the shipped reads runner, sessions through the shipped core — nothing new below Presentation except what the library genuinely needs in `business/`. **Read back, don't mirror**: after a rename or delete the app runs the read that shows it; no cached copy beyond the screen. **One live chat child** per session in view or mid-turn (the design's decision 2, now the library's law: selection drives the child lifecycle). Destructive actions ask (delete confirms).

**Seam + files.** `source/presentation/sessions/` (the list, the inspector, their view-models), `source/business/session/` (the resume/lifecycle growth), `source/business/reads/` (chats/status reads). Tests: the library's view-model over scripted read documents and a scripted session stream (select/resume lifecycle, rename/delete read-back, the inspector's fields, the neutral-glyph fallback), hermetic.

**Reference.** `ScreenSessionSources.dc.html`, `Inspector.dc.html`, the contract board's SESSIONS rows; [`../cli/backlog/v0.2.0/session-event-fields.md`](../../cli/backlog/v0.2.0/session-event-fields.md) (52a — the glyph upgrade, cross-referenced, not gated); the shipped 1b/1c cores `(consumed decision)`.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call). Fallbacks are the contract board's own, upgraded when the CLI items land — never a gate on the CLI queue.

**Open calls:**
- [default: list ordering and grouping (most recent first) per the prototype; details at build.]

**Guardrail(s).** View-model matrices over scripted documents/streams; child-lifecycle pins (exactly one live child; leaving ends, returning resumes); read-back pins; hermetic throughout.

**Acceptance criteria:**
- [ ] The library lists, opens (resume), renames and deletes sessions through the cores, reading back after every change; the inspector shows the live session's model and roles.
- [ ] Exactly one session child lives at a time; the glyph fallback renders until 52a's fields exist; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on [`app-shell.md`](app-shell.md) (1e). Out of scope: the session states and input extras ([`session-states.md`](session-states.md)), private sessions (there), search.
