# Execute in the shell, and the Symphonies view

**What / why.** The shell's conversation is chat's alone: 32c shipped the session view with execute mode explicitly out of scope — "an execute session is not offered there yet" ([Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui), the 32c notes) — so `/play` and `/symphonies`, execute's rows in the one command table, never light up on the stage, and `symphonies` has no view although `list`, `show` and `play` already print JSON documents (measured 2026-10-10: 3 of its 6 verbs). The machinery is standing: the session core is mode-parameterized (27s — `chat` and `execute` are thin faces over one core), the completions follow the mode's own command table by construction, and the view already draws **side calls** — which is exactly how a play narrates its stages (27q). Deliver: the picker gains the **execute door** (choose a suite, the session opens under it, the suite's `orchestrate: true` honored exactly as the command honors it); `/play` and `/symphonies` work on the stage because the mode's rows exist, not because the shell learned them; and a **Symphonies** view — the definitions listed (name, description, stages), Enter showing `symphonies show`'s card, `p` playing one **through the open session's own `/play`** (the 32d rule: a chosen thing reaches the session through its own commands, never a second path). Closes wall W4.

**Core constraint(s).** One session core, no second path: the door opens execute's session exactly as `apogee execute` does (the suite insisted on, 27s), and a play from the view is `/play` entered (`SessionView::enter`, the 32d `use_suite` precedent). The walk, its caps and its refusals are the runner's (27q/27r) — the shell renders positions (`outer → inner, stage 2/3`) and never re-validates; an orchestrating session's tools are 27t's offer untouched. The play's narration is the Reporter's side calls, already drawn — no new renderer. Non-TTY `apogee execute` and machine mode's execute are byte-identical before and after (the TUI is additive).

**Seam + files.**
- `lib/src/cli/source/presentation/cli/tui_session.h/.cpp` — the picker's execute door (`open_execute`: the saved-suites list, the default suite preselected; the running chat ended as `/exit` ends it, the 32d `open_chat` idiom) and the driver running execute's flag table (`cli/execute.h`'s, as `chat --tools` is parsed today).
- `cli/tui_workbench.h/.cpp` (or a sibling `cli/tui_symphonies`) — the Symphonies view over `symphony/view`'s documents; `p` through `WorkbenchHooks` into the session.
- `cli/chat_play.h` — untouched; the stage narration arrives as side calls.
- `tests/presentation/cli/tui_session_test.cpp` — new cases; leak-test rows for the view; classification flips in 37a's table (`execute`, `symphonies`).

**Reference.** 27q–27t — the symphony walk, chains, the Orchestrator's offer, and `symphony/view`'s one view of a definition; 27s — the mode-parameterized session core and machine mode's execute events; 32c/32d — the picker, `open_chat`/`use_suite`, and the "through its own commands" rule.

**Decisions made** (dated):
- 2026-10-10 — A play is never offered outside a session: the Symphonies view's `p` needs an open execute session (or opens one under the default suite first, the `use_suite` precedent) — the command's own rule, kept (the user's placement of the set in v0.1.6 on the spike report; the design the spike proposed, accepted).
- 2026-10-10 — The picker offers the execute door only when at least one suite is configured, the default suite preselected; with none, the door is absent rather than refusing (the default, confirmed 2026-10-10).
- 2026-10-10 — `symphonies create|edit|delete` get no curated key: `create` and `delete` run through [37h](tui-command-runner.md)'s exec line, and `edit` is the `$EDITOR` wall recorded on [37g](tui-agents-mcp-views.md) — the runner refuses it naming the prompt (the default, confirmed 2026-10-10, after the runner-spike revision of its original "stay commands").

**Guardrail(s).** A `cli/tui_session_test` case on the real core over a scripted mock: an execute session opened from the picker under a suite, a `/play` of a mock-membered symphony — its stages narrated as side calls, its output the session's answer — and the saved session byte-equal to the same play driven through machine mode's execute on the same starting state (the 32c parity anchor). The Symphonies view's rows against `symphonies list --output-format json`; a play refused (an unconfigured member) surfacing the command's own refusal on the notice row.

**Acceptance criteria:**
- [ ] The picker offers execute when a suite exists; the session opens under the chosen suite with execute's banner, rows and completions.
- [ ] `/play` and `/symphonies` work on the stage; a play's stages narrate as side calls and its output lands as the answer, saved as execute saves it.
- [ ] The Symphonies view lists and shows from the existing documents; `p` plays through the session's `/play`.
- [ ] `apogee execute` off a terminal and machine mode's execute are byte-identical to before.
- [ ] 37a's classification rows for `execute` and `symphonies` flip; the full suite is green.

**Scope note.** Earmarked for v0.1.6; gated on [37a](tui-parity-law.md). Out of scope: any wire or machine-mode change, symphony create/edit surfaces, any change to the walk or its caps.
