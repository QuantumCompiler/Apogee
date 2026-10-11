# The progress seam, and the Task view

**What / why.** The shell has no surface for work that runs long (measured 2026-10-10, the parity spike): `task run`, `train run|pipeline|regime|cycle`, `datasets prepare|synth`, `embed ingest`, `models pull|convert|quantize` and `graph build|update` all narrate progress on the terminal through `views/` and the status line, while the shell offers only the notice row and `list_view`'s one-line outcomes. This is the split's one root-cause wall (W3): **one progress widget** closes it for every consumer at once, and a second progress path per view would be the drift the one-core rule exists to prevent. Deliver `tui/progress` — a narrated-lines region (the run's own words, scrolled), a cancellation key wired to a token, produced **off the pump's thread** (the monitor bar's sampler idiom: a stalled producer holds back lines, never a frame) — and its first consumer, the **Task view**: the tasks listed and shown from the documents `task list|status --output-format json` already print (measured: 4 of task's 6 verbs carry reads), `r` starting a run with a goal typed in the input row, `h`/`c` halting and cancelling through the commands' own cores, a live run's narration in the widget. Closes W3's root cause and task's slice of W1; `train` and `datasets` consume the seam in [37f](tui-training-views.md).

**Core constraint(s).** The pump never blocks and never bursts (32e's tick law). Cancellation goes through the existing paths — the turn's token, `task cancel`'s request file — never a new channel (28f's semantics, 32c's Ctrl-C precedent). The ledger stays the one record and `tasks/view` the one view of a task (27j): the shell renders that view, derives nothing of its own, and reads it afresh on show and on a slow tick while a run is live. **The unattended rules are untouched**: a task run from the shell still runs under its declared policy (27i) — the view watches and controls, it never becomes an answerer `ask_user` can reach; with no grant, `ask` denies exactly as on any pipe. The widget renders a run's narration; it owns no protocol — the trainer JSONL, the task ledger and the Reporter stay the only sources.

**Seam + files.**
- `lib/src/cli/source/presentation/tui/progress.h/.cpp` — the widget: lines appended from a producer thread through the pump's post, a bounded scrollback, a cancel key surfaced to the owner; `tests/presentation/tui/progress_test.cpp` on the manual pump.
- `lib/src/cli/source/presentation/cli/tui_task.h/.cpp` — the Task view: rows from `task list`'s document, Enter the status document's card, `r` run (the goal from the input row, the config's declared policy, never a widened grant), `h` halt, `c` cancel; registered in `cli/tui_cmd.cpp`.
- `cli/task_cmd.h/.cpp` — the run/halt/cancel cores carved where the callbacks hold them (the 32d carve idiom); the reads exist.
- Leak-test rows; classification flip in 37a's table (`task`).

**Reference.** 32e — the sampler-thread and tick idioms (the structural model for "a stalled producer never delays a frame"); 27h–27j — the ledger, the policy ceiling, `tasks/view` and the task documents; 32d — carved cores and ask-first; the terminal's `views/` progress painting — the sibling this widget parallels, never replaces.

**Decisions made** (dated):
- 2026-10-10 — The seam ships with its first consumer in one item, so the widget's shape is proven against a real run before 37f builds on it (the spike's split, accepted; the user's placement of the set in v0.1.6 on the spike report).
- 2026-10-10 — The view watches a run through the ledger's one view, polled on show and on a slow tick — never a second transition source; a task started outside the shell shows identically.
- 2026-10-10 — `r` asks `[y/N]` naming the goal and the policy before starting (a run spends model turns; a single key is not a typed command); `task resume` rides the same key on a resumable row (the default, confirmed 2026-10-10).
- 2026-10-10 — A run's widget lines are the machine-mode task events' wording — the one vocabulary 27j already pinned — so the shell invents no phrasing (the default, confirmed 2026-10-10).

**Guardrail(s).** `progress_test` on the manual pump: lines arrive in order across ticks; a stalled producer leaves the frame drawn and the lines late, never the pump waiting; cancel reaches the token exactly once. Task view byte-parity: rows against `task list --output-format json`, the card against `task status`'s document; `h`/`c` leaving exactly the commands' ledger states (the request file, the transition); a run started from the view producing a ledger byte-equal to `apogee task run`'s for the same scripted turns. The leak test's planted key searched in the view and the widget.

**Acceptance criteria:**
- [ ] `tui/progress` exists with its manual-pump tests; a stalled producer never delays a frame.
- [ ] The Task view lists, shows, runs, halts and cancels through the commands' own cores, byte-parity held on the ledger.
- [ ] A live run's narration appears in the widget with the task events' wording; cancel lands as `task cancel` does.
- [ ] The unattended gate's behavior is unchanged — a view-started run denies `ask` exactly as a pipe-started one.
- [ ] 37a's classification row for `task` flips; the full suite is green.

**Scope note.** Earmarked for v0.1.6; gated on 37a (shipped, [Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui)). The seam is the deliverable; `train`/`datasets` consume it in [37f](tui-training-views.md). Out of scope: curated `embed ingest`, `graph build` and `models pull` surfaces — each runs in the shell through [37h](tui-command-runner.md)'s exec line, and a curated surface over this widget is a later call — and any ledger or policy change.
