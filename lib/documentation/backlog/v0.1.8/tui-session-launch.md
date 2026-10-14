# Launching the shell into a session

**What / why.** The shell cannot be opened *into* a configured session (the disparity audit, 2026-10-14): `apogee tui --help` shows **zero options** against `execute`'s **40 flags**, and `apogee execute` at a terminal opens the REPL, never the shell — so "open the shell in execute mode under suite X with folder Y attached, this model, this effort" has no spelling, even though nearly every knob has a mid-session slash twin once a session exists (the one command table measured: `/model`, `/suite`, `/attach`, `/system`, `/effort`, `/permission-mode`, `/rag`, `/warm`, …). This item gives `apogee tui` (and the bare `apogee` door it spells) **the session flag table**: the same flags `chat` and `execute` parse, handed whole to the shell's first session — `apogee tui --suite dev --attach . --effort high` opens the shell already in that execute session, `apogee tui -m mocky` in that chat — through the one opening [ADR 0011](../../adrs/cli/one-session-opening.md) requires and 44b built (shipped 2026-10-14). With session flags given, the first session opens directly (the picker one key away); with none, today's picker-first behavior stands. Closes the audit's W4.

**Core constraint(s).** **One flag table** (ADR 0011): `tui` re-parses nothing — it takes the session table the two mode commands already share and passes the parsed flags to the one open function; a flag the modes grow lands on `tui` with zero wiring, and a flag combination the modes refuse is refused in the same words. Mode selection follows the flags' own semantics (`--suite`/suite-implying flags → execute's opening; otherwise chat's — the same decision the commands' own parsers make today, never a shell variant). The bare `apogee` door keeps its flagless behavior byte-identical (`cli.bare_help` and the PTY checks stand); only the spelled `apogee tui` grows the table. Completion comes from the binary as always (ADR 0007 — the table's flags complete on `tui` the day they exist). Non-TTY `apogee tui` keeps its measured refusal.

**Seam + files.**
- `cli/tui_cmd.h/.cpp` — the session flag table attached (the shared table `chat`/`execute` register, reused by reference, never copied), the parsed result carried into `ShellEntry` and handed to the driver's first session through the one opening.
- `cli/tui_session.h/.cpp` — the driver accepting an initial session spec (flags-given → open directly; none → the picker).
- Tests: `cli/tui_cmd_test.cpp` — the flag matrix (suite, bare `-m`, attach, effort/permission-mode carried; a refused combination refused in the command's words; flagless byte-stable); a `tui`-launched session byte-equal to the same flags on `apogee execute`/`chat` (the ADR 0011 golden again, from the other direction); completion offering the table on `tui`; `cli.tui_shell` grows a flagged-launch case.

**Reference.** 27s — the shared session core and flag tables this reuses; [ADR 0011](../../adrs/cli/one-session-opening.md) (44a, shipped 2026-10-14) `(consumed decision)`; 44b, shipped 2026-10-14 — the one opening this rides: `open_execute_session`/`open_chat_session` and `execute_doors` in `cli/execute`, `cli/chat`; ADR 0007 — completion from the binary; the disparity audit (2026-10-14) for W4's 0-vs-40 measurement.

**Decisions made** (dated):
- 2026-10-14 — The set lands in v0.1.8 as track 44 (the user's call on the spike report).
- 2026-10-14 — **`apogee tui` gains the session table; no `execute --tui` second door** — one entry point, one flag table (the spike's default, standing).
- 2026-10-14 — Flags open the first session directly; no flags keeps the picker-first shell — the flagless door is byte-stable.
- 2026-10-15 — `tui`-only knobs stay at zero: everything session-shaped belongs to the shared table, and a genuinely shell-shaped flag is a later item's evidence (the default, confirmed 2026-10-15).


**Guardrail(s).** The flag matrix and the launch golden; the flagless byte-stability pins (`cli.bare_help`, the PTY checks, the non-TTY refusal); completion equality on `tui`'s flags; the refusal-wording parity for bad combinations.

**Acceptance criteria:**
- [ ] `apogee tui --suite <s> --attach . --effort <e>` opens the shell inside that execute session; `apogee tui -m <b>` inside that chat; refusals are the commands' own.
- [ ] A flag added to the session table appears on `tui` with no `tui`-side change, completion included.
- [ ] Flagless `apogee` and `apogee tui` are byte-stable against today's goldens.

**Scope note.** Earmarked for v0.1.8; gated on 44b, shipped 2026-10-14. Out of scope: a second entry point, shell-only session knobs, any change to the modes' own flag semantics.
