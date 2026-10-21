# The console: every verb reachable, today

**What / why.** The TUI's parity became true the day the exec line landed (the CLI's 37h: every verb and flag runnable from the shell, Tab through the one completion core, the law's refusals) — the screens then accreted without ever leaving a capability unreachable. The GUI needs the same catch-all: a **console surface** — a sheet or panel where any `apogee` command runs as typed, supervised by the shipped jobs core (captured output streamed, stop, exit honestly shown), with **Tab completion answered by the binary** (`apogee __complete`, the ADR 0007 discipline from the client side) and the parity table's **carve-outs refused with their recorded reasons** (serve, reset, uninstall, the plumbing). This is what makes the user's mandate true *now*: a family's screen (2c–2g) upgrades the experience; the console guarantees reach while screens accrete, exactly as the exec line did for the shell.

**Core constraint(s).** **The jobs core supervises** — no second process code (the layering check already refuses it in Presentation); output is the command's own, streamed as the supervisor tails it, never re-worded. **Completion is the binary's** (`__complete` asked per keystroke's word boundary, the CLI's own candidates — never a GUI-side list that can drift, ADR 0009's lesson). **Refusals are the table's** (2a): a carved-out command refuses with its recorded reason, byte-aligned with the table. **Honest and plain**: commands that prompt on a TTY run on pipes here — their pipe behavior (deny, refuse naming the flag) shown as the CLI says it; the console never fakes a TTY. **A design board lands with it** (the design's new-screen law: the board, its `canvas.json` slot, the README row, the contract-board row).

**Seam + files.** `source/presentation/console/` (the surface: input with completion, the streamed output view, run controls; MVVM per ADR 0002), `source/business/jobs/` consumed (grown only for the completion ask — one bounded `__complete` run per query); the parity table's refusal reasons read from 2a's declaration; `lib/design/spec/` — the console's board per the design law.

**Reference.** The CLI's 37h (the exec line) — the proven catch-all shape, its scoped/unscoped grammar not mirrored (the console is unscoped; screens are the GUI's scoping); ADR 0007 — completion from the binary; the shipped jobs core (1i) `(consumed decision)`.

**Decisions made** (dated):
- 2026-10-20 — The console is the parity mandate's catch-all (the default, standing — the TUI's exec-line precedent applied); screens remain the destination, the console the guarantee.
- 2026-10-20 — Completion is always the binary's answer; the GUI carries no command list of its own.

**Open calls:**
- [default: the surface's form (a global sheet on a key, matching the design's keyboard-first law) and its board's look at build, per the prototype's idioms.]

**Guardrail(s).** View-model matrices over scripted children (run, stream, stop, exit codes, a refused carve-out's exact reason); the completion ask argv-pinned over a scripted `__complete`; no process code in Presentation (the layering check); the design board's health check green.

**Acceptance criteria:**
- [ ] Any non-carved command runs from the console as typed, output streamed, stoppable; Tab completes from the binary's own candidates.
- [ ] The three carve-outs refuse with the table's reasons; the design board and contract-board row exist; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 2); gated on [`parity-law-and-table.md`](parity-law-and-table.md) (2a). Out of scope: per-family screens (2c–2g), scoped-grammar mirroring, any CLI change.
