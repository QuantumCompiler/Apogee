# The session todo: the plan, visible while it runs

**What / why.** A long agentic session is only trustworthy when you can see what it thinks it is doing — Claude Code keeps a live todo for exactly this. Apogee has no session plan surface (the execute-mode spike's W4, 2026-10-11): tasks carry plans in their ledgers, 39b's plan *mode* will hold writes, but a chat or execute session shows nothing a person can watch across a multi-step job. This item gives sessions with tools a **todo**: a small ordered list the model maintains through one native tool — items added, checked off, revised — rendered live by every front end (the terminal below the thinking block, the shell beside the session, machine mode as an event, a served stream in its meta-frames), and saved with the session so a resume shows where things stood. It complements 39b's plan mode (the mode holds writes; the todo is what the plan *says*), and it is deliberately small: a list of short lines with states, not a planner.

**Core constraint(s).** **One surface, four renderers** ([ADR 0002](../../adrs/cli/mode-parity.md)): the todo's state changes cross the Reporter seam once, and the terminal, the shell's session view, `machine/` and the SSE reporter each draw the same change — the machine event joins the conformance-pinned vocabulary (`cli.machine_schema_conformance`, 28g) and `machine-mode.md` in the same change. The tool is **native and ungated** (it writes session state only — the notes precedent): registered like every tool, counted in tool selection's core set so it never drops out past sixteen tools (26g). The todo lives **in the session record** (logger), bounded (a named cap on items and line length), UTF-8-mended at entry, and renders identically after a resume on either door. Nothing nags the model: the tool is offered with the kit and described; whether a turn uses it is the model's (and the persona's) business.

**Seam + files.**
- `lib/src/cli/source/business/tools/todo.h/.cpp` — the tool (`todo_write`: the whole list each call — items with `pending|active|done`, the simplest shape a small model holds), its state on the session.
- `agentloop/` — the Reporter callback (`on_todo`), the loop handing state changes through it.
- `views/` (the terminal's block), `tui/session_view` (the shell's strip or fold), `machine/json_reporter` + `machine/protocol` (the event, the schema), `httpserver/sse_reporter` (the meta-frame) — each drawing the one change.
- `logger/` — the todo saved with the session; `cli/` — `/todo` printing the current list in the one command table.
- `lib/documentation/reference/machine-mode.md` — the event's row. Tests per layer: the tool's state table, each renderer's golden, conformance, resume round-trip.

**Reference.** The notes toolset (an ungated session-state tool — the shape); `Reporter::on_side_call` (26n — adding one callback across four adapters, the exact precedent); 28g (the pinned vocabulary a new event joins); 39b (the plan mode this complements — `(consumed decision)`: its semantics are its own item's, in build now); the execute-mode spike (2026-10-11) for W4.

**Decisions made** (dated):
- 2026-10-11 — The set lands in v0.1.7 as track 41 (the user's placement call on the spike report).
- 2026-10-11 — The todo is a **whole-list-per-call tool** (`todo_write` replaces the list), not incremental item verbs — one call a small model can form, idempotent, nothing to diff (the spike's "deliberately small", standing).
- 2026-10-11 — Scope: sessions with `--tools`, both modes (chat and execute) — the surface is the session core's, not execute's alone.
- 2026-10-13 — The caps are 20 items and 120 characters a line, the overflow refused in the tool result naming the cap (the default, confirmed 2026-10-13).
- 2026-10-13 — The terminal renders the todo as a dim block repainting on change below the status area, the shell as a fold above the input; exact looks the build's, against both painters' goldens (the default, confirmed 2026-10-13).

**Guardrail(s).** The tool's state table (replace, states, caps, UTF-8); one scripted turn driving every Reporter adapter's golden for the same change; schema conformance green with the new event; a resume on either door showing the saved list; the leak test's surfaces gain the todo renders.

**Acceptance criteria:**
- [ ] A tools-on session can maintain a todo; all four front ends render the same state; `/todo` prints it.
- [ ] The machine event is documented and conformance-pinned; the session file carries the list across resume.
- [ ] Caps enforced and said; sessions without tools unchanged; the full suite green.

**Scope note.** Earmarked for v0.1.7; ungated — independently buildable (it lands in the session core both modes share). Out of scope: any planner, auto-generated todos, task-ledger changes, 39b's mode semantics.
