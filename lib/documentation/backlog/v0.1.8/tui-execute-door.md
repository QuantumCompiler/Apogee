# The execute door, always there, through the one opening

**What / why.** Execute mode is effectively unreachable from the shell (the disparity audit, 2026-10-14, the installed binary on a PTY): a **fresh shell opens straight into a chat — no picker exists until a saved chat does**, so no execute entry appears anywhere; with a picker but no suite there is still no execute row (measured against a home with a configured mock backend); only a picker **plus** a configured suite shows "Execute under suite devsuite". Three walls, one root cause: the door is its own second composition — `TuiSessionDriver::open_execute(const std::string& suite)` (`tui_session.cpp:333`), suite-only, with **zero references** to 41b's project opening or 41c's persona — so 41a's bare door (shipped in the CLI) never reached the shell, and a session opened from the shell in a repo knows nothing of `AGENTS.md`, the attached folder or the coder persona that `apogee execute` gets in the same directory. This item rebuilds the door under ADR 0011 (written by [`door-adrs.md`](door-adrs.md)): **the picker always appears** on a fresh session — New chat first, the execute rows under it: one per configured suite and, with none, the bare row worded like the CLI's banner (`Execute — <backend> (no suite)`) — and **every row opens through `cli/execute.cpp`'s own open function**, so 41a's three rungs, the project opening and the persona arrive in the shell by construction, never by copy. Closes the audit's W1, W2 and W3 together.

**Core constraint(s).** **One opening** (ADR 0011): the door calls the command's open path with the command's semantics — the bare rungs (suite → `-m`-equivalent selection → `models.default`), the project detection from the shell's working directory, the context cap and its said truncation, the persona's project-only rule, `--orchestrate` staying the suite's — nothing re-decided in `tui_session.cpp`. The parity table's `execute` row (`tui_parity.cpp:28`) and the runner's refusal (`:61–63`) become true statements: the door they point at exists on a fresh install (ADR 0012 (written by [`door-adrs.md`](door-adrs.md))'s rule; its test lands with [`tui-reachability-law.md`](tui-reachability-law.md)). The 32c/32d session idioms stand: an open conversation is ended as `/exit` ends it before the chosen one opens; a busy one is never cut off; quitting cancels as an interrupt does.

**Seam + files.**
- `cli/execute.h/.cpp` — the open path exposed as the one function the command and the door share (the composition the CLI already runs, factored callable; no behavior change to `apogee execute` itself).
- `cli/tui_session.h/.cpp` — the picker shown on every fresh session (saved chats or none), its rows (New chat; Execute under `<suite>` per suite; the bare execute row when a default backend exists and no suite does — present alongside suites too, per the CLI's rungs); `open_execute` replaced by the call into the one opening.
- Tests: `cli/tui_session_test.cpp` — the picker's row matrix (no chats/no suite; chats/no suite; suites with and without chats); a door-opened execute session byte-equal to `apogee execute`'s own on the same home and directory (the ADR 0011 golden: banner, context, persona, attach — compared through the session record and a scripted turn); the project ritual reaching the shell (a fixture repo: the context folded, the folder attached, the persona riding — asserted via FakeTransport capture exactly as 41b/41c's own tests do); `cli.tui_shell` grows the door case on a real terminal.

**Reference.** 41a/41b/41c — the opening this door must reach whole (consumed decisions: the rungs, `AGENTS.md` first, the cap, the persona's project-only rule — nothing reopened here); 37d — the door's first cut and the "through its own commands" idiom; [`door-adrs.md`](door-adrs.md) — ADR 0011/0012 `(consumed decision)`; the disparity audit (2026-10-14) for W1–W3's evidence.

**Decisions made** (dated):
- 2026-10-14 — The set lands in v0.1.8 as track 44, the fixes gated on the ADRs (the user's call on the spike report).
- 2026-10-14 — **The picker always appears** on a fresh session — one door in one place, New chat preselected so the fresh-install experience stays one keypress to a conversation (the spike's default, standing).
- 2026-10-14 — The bare execute row appears whenever the CLI's bare door would open (a default backend exists), suites or no suites — the rungs are the command's, not a shell variant.

**Open calls:**
- [default: the picker's row order — New chat, the saved chats, then the execute rows (suites first, bare last) — exact order and wording at build, the CLI's banner vocabulary reused.]

**Guardrail(s).** The ADR 0011 golden (door-opened == command-opened, byte-equal through the record and a scripted turn, in and out of a project); the row matrix pinned; the ritual's FakeTransport captures in the shell; `cli.tui_shell`'s door case; every existing picker and session test green or deliberately retitled.

**Acceptance criteria:**
- [ ] A fresh `apogee` shows the picker with New chat and the execute rows; one keypress still starts a chat.
- [ ] The bare execute row opens suite-less exactly as `apogee execute -m`/default does; suite rows exactly as `--suite` does.
- [ ] A shell-opened execute session in a fixture repo carries the context file, the attached folder and the persona, byte-equal to the command's own opening.
- [ ] `tui_parity.cpp`'s execute row and the runner's refusal point at a door that exists on a fresh install.

**Scope note.** Earmarked for v0.1.8; gated on [44a](door-adrs.md). Out of scope: launch flags on `apogee tui` ([`tui-session-launch.md`](tui-session-launch.md)), the reachability test ([`tui-reachability-law.md`](tui-reachability-law.md)), any change to `apogee execute`'s own behavior.
