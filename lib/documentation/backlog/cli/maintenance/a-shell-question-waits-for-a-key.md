# CLI-M62 · A [y/N] question in the shell waits for a key, never a redraw

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | The full-screen shell › `tui/list_view`'s two confirm branches, behind every view action that asks `[y/N]` |
| **Labels** | `tui` `confirm-prompt` `event-loop` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | CLI-M51's build (2026-11-27), which saw it on a live PTY drive; then a sandboxed investigation the same day |
| **Created** | 2026-11-27 |

---

## Summary

In the full-screen shell, the next background redraw answers every `[y/N]` question a list view asks with "no". With no key pressed, the question closes as `not done` within two seconds: in five runs it closed after 0.10 to 1.98 s. So a delete, fix, run, cancel or rollback can be confirmed only by a `y` typed before the monitor's next tick. This item makes the question wait for a key, as the session view's permission prompt already does. `y` or `Y` means yes and any other key means no. A test pins the redraw.

## Description

### User story

**As** someone working in the full-screen shell, **I want** a `[y/N]` question to stay open until I press a key, **so that** reading a question before I answer it never quietly cancels what I asked for.

### Steps to reproduce

1. Make a scratch home: `config.yaml` holding `backends:\n  local:\n    type: mock\n`, and two datasets, `training/datasets/alpha.jsonl` and `beta.jsonl`, each holding one chat row.
2. Run bare `apogee` on a 140×45 pseudo-terminal, with `APOGEE_HOME` set to that home.
3. Press Ctrl-G, `d`, then Enter. The Datasets view opens with `alpha` selected.
4. Press `x`. The question `Delete dataset alpha ('apogee datasets delete alpha')? [y/N]` is drawn.
5. Press nothing for 6 s.

### Actual result

Within two seconds the question line gives way to `not done`, and `alpha.jsonl` stays. A `y` pressed 2.5 s after `x` finds no question open, and nothing is deleted. A `Y` pressed at once also says `not done`.

### Expected result

The question stays on screen until a key is pressed. Then `y` (or `Y`) deletes `alpha` and draws `removed`, and any other key draws `not done`. A redraw, a tick or a posted notice never answers the question.

### Background

The shell's pump (32b, 2026-10-10) posts `ftxui::Event::Custom` after every closure it hands the UI thread, so that the screen redraws. That event reaches the active view's handler the way a key does. The list view's confirm (32d, the same day) takes any event other than `y` as a no.

Closures are posted all the time:
- the bottom bar's monitor samples the machine every 2 s (32e);
- a view with a running job ticks every 2 s;
- background reads post their rows, and stderr lines post as notices.

So an open question lives only until the next post.

The in-process tests drive `ManualPump`, whose `post` sends no event, so no test has ever seen a redraw arrive while a question was open. The session view's permission prompt shows the right shape: it answers only on a key, swallows a key that answers nothing, and holds the keys while it is open. A redraw passes through it unanswered.

### Evidence

1. **Seen first** (2026-11-27, CLI-M51's build, on a live PTY drive of the build tree's binary against a scratch home with the mock backend). `x` on a plain dataset, left open about 3 s, drew `not done` and kept the file.
2. **Reproduced** (2026-11-27). The method:
   - **Binary:** a Python `pty` probe ran the binary `apogee 0.2.2 (d5481fee, 2026-11-28, macos-arm64)`, copied out of the build tree.
   - **Environment:** `APOGEE_HOME` and `HOME` in the scratchpad, `PATH=/usr/bin:/bin`, `TERM=xterm-256color`, a 140×45 window.
   - **Steps:** the steps above, eight runs in one shell session.
   - **Measure:** each time is when `[y/N]` and then `not done` or `removed` first appeared in the ANSI-stripped output. Each run's files were read afterwards.

   | Run | Key after `x` | The question closed | Datasets left |
   |---|---|---|---|
   | 1 | none | `not done` after 0.189 s | alpha, beta |
   | 2 | none | `not done` after 1.149 s | alpha, beta |
   | 3 | none | `not done` after 0.097 s | alpha, beta |
   | 4 | none | `not done` after 1.024 s | alpha, beta |
   | 5 | none | `not done` after 1.978 s | alpha, beta |
   | 6 | `Y` at 0.1 s | `not done` after 0.163 s | alpha, beta |
   | 7 | `y` at 2.5 s | `not done` after 0.766 s, before the `y` | alpha, beta |
   | 8 | `y` at 0.1 s (control) | `removed` after 0.18 s | beta |

   The untouched closes fell at 6.02, 14.04, 20.06, 28.06, 36.07 and 46.10 s after the process started. Each came just after an even second, the phase of a 2 s tick. The one close off that phase, at 41.32 s, is run 6's `Y` being read as no.
3. **Root cause** (2026-11-27, read at d5481fee):
   - **The confirm branches.** In `lib/src/cli/source/presentation/tui/list_view.cpp`, `on_key` (`:458`) handles an open question first. A typed ask's question is `:473`–`:481` (`confirming_ask`); an action's question is `:483`–`:492` (`confirming`). Each resets the question on *any* event. Only `ftxui::Event::Character('y')` runs the action; every other event sets `notice = "not done"` (`:479`, `:490`).
   - **The redraw.** `lib/src/cli/source/presentation/tui/pump.cpp:117`–`121`, `TerminalPump::post`, posts the task and then `app.PostEvent(ftxui::Event::Custom)`, commented "A posted closure changes what is drawn: a custom event redraws."
   - **The 2 s tick.** `lib/src/cli/source/presentation/cli/tui_cmd.cpp:80` always puts the `MonitorBar` on the bottom bar. Its `cadence` defaults to 2000 ms (`lib/src/cli/source/presentation/tui/monitor_bar.h:32`), and `monitor_bar.cpp:203` registers it with `pump.every`. A list view with a progress seam ticks too: `kRunTick` is 2000 ms (`list_view.cpp:29`), registered at `:608`.
4. **Why no test saw it** (2026-11-27, read at d5481fee). `ManualPump::post` (`pump.cpp:165`) queues the task and raises no event. `tests/presentation/tui/list_view_test.cpp` drives a `ManualPump` and feeds only key events. `tests/scripts/py/pty_tui_check.py`, the real terminal's checks, never leaves a question open.
5. **Blast radius: 17 questions in six view files** (2026-11-27, read at d5481fee). Every `.confirm` a view hands the list view goes through one of the two branches:
   - **Agents:** `x` delete (`tui_agents.cpp:137`).
   - **Doctor:** `f` fix (`tui_doctor.cpp:100`) and `r` register a found provider (`:182`).
   - **Knowledge:** two `x` deletes (`tui_knowledge.cpp:92`, `:147`).
   - **Tasks:** `r` run (`tui_task.cpp:334`), `r` resume (`:363`), `h` halt (`:379`) and `c` cancel (`:391`).
   - **Train:** the typed `r` run, `p` pipeline and `P` promote (`tui_training.cpp:146`), `c` cycle (`:162`) and `R` rollback (`:186`).
   - **Datasets:** the typed `p` prepare and `s` synth (`tui_training.cpp:258`) and `x` delete (`:274`).
   - **Chats:** `x` delete a chat (`tui_workbench.cpp:129`), `c` compact (`:138`) and `x` remove (`:211`).

   The question always fails safe, so nothing runs that was not asked for. The harm is the window: only a `y` typed within up to 2 s confirms, and a person who reads the question first loses it. That includes a task run's grants and a synth run's cost, which their questions spell out to be read.
6. **What already works** (2026-11-27, read at d5481fee):
   - A `y` inside the window runs the action (run 8).
   - The session view's prompt, `on_prompt_key` (`session_view.cpp:754`), answers only on a character, Escape or Return. It swallows a key that answers nothing (`:766`) and "holds the keys while it is open" (`:780`), so a redraw passes through it unanswered.
   - The typed-ask row, `on_typing` (`list_view.cpp:418`), returns `false` for an event it does not know.
   - The Go-to menu, `menu_key` (`shell.cpp:264`), ignores events it does not know.

   The two confirm branches are the only handlers in the shell that take a non-key event as an answer.
7. **The line-mode convention** (2026-11-27, read at d5481fee). The CLI's line-mode `[y/N]` prompts take `y`, `Y` and `yes` as yes: `agents_cmd.cpp:327`, `session_output.cpp:92`, `task_cmd.cpp:1234` and `models_pull.cpp:442`. The shell's question takes only `y`.
8. **Since v0.1.6** (2026-11-27, `git log -S`). The confirm and its `not done` came with 32d (commit f4ddff8d, 2026-10-10); the redraw post came with 32b (commit 83bc0526, 2026-10-10).

## Acceptance criteria

- [ ] **Given** the Datasets view with `alpha` selected and its delete question open, **when** the shell redraws (a monitor tick, a posted notice) and 6 s pass with no key, **then** the question is still drawn, `not done` is never drawn, and `alpha.jsonl` still exists.
- [ ] **Given** that open question, **when** `y` is pressed 2.5 s after `x`, **then** `alpha` is deleted, `removed` is drawn and `beta` is kept. This is runs 7 and 8 of Evidence 2, now passing.
- [ ] **Given** an open question, **when** `Y` is pressed, **then** the action runs as it does for `y`, as the line-mode prompts read it.
- [ ] **Given** an open question, **when** any other key is pressed (`n`, Escape, Return, an arrow, any other character), **then** `not done` is drawn and nothing runs, as today.
- [ ] **Given** a typed ask's question (the Datasets view's `p` or `s`, the Train view's `r`, `p` or `P`), **when** redraws arrive before the answer, **then** it behaves as an action's question does above.
- [ ] **Given** any view action that asks, **when** it is answered by a key, **then** its question text, its `not done`, and the command it runs are byte-identical to today's.

## Constraints

- **A question fails safe.** Only `y` or `Y` runs the action. Every other key is still a no, `[y/N]`'s default, and no event other than a key press answers.
- **One seam.** The fix is in `list_view.cpp`'s two confirm branches. The pump keeps posting its redraw, which every view's live drawing depends on, and no view's actions change.
- **The session prompt's rule.** The question follows `session_view.cpp`'s `on_prompt_key`: a key answers, and an event that is not a key passes it unanswered.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). Only the shell asks this way. The line-mode prompts read a whole line and already wait, and no HTTP or machine-mode surface asks a `[y/N]` question. The skipped modes are named here.
- **FTXUI stays in `tui/`** (the Presentation layer card). The fix and its test use FTXUI's events only inside `tui/` and its tests.
- **Hermetic tests.** The in-process test uses `ManualPump` and feeds its own events. The terminal check runs the build's binary against a scratch `APOGEE_HOME` and the mock backend, never the real home. Like the file's other checks, it is POSIX-only.
- **Code style.** Formatted with clang-format, with zero warnings on every CI compiler.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/presentation/tui/list_view.cpp` | In `on_key`, while `confirming` or `confirming_ask` is set, an event that is not a key press leaves the question open and returns `false` unanswered, as `on_typing` does. That covers the pump's `Event::Custom` and a mouse or terminal report. A key press closes the question: `y` or `Y` runs the action or the typed ask, and any other key draws `not done`. Both branches read one small predicate, so the two can't drift. |
| `tests/presentation/tui/list_view_test.cpp` | The bug's fixture, described under Testing. |
| `tests/scripts/py/pty_tui_check.py` | A `confirm` check on the real terminal, described under Testing, with the module docstring's list of checks gaining its line. |

What counts as a key: FTXUI's `Event` has `is_character()`, `is_mouse()` and `is_cursor_position()`, and `Event::Custom` is a special event. The predicate takes `Event::Custom`, a mouse event and a cursor report as not keys. Every other event a terminal sends for a key press (a character, Escape, Return, the arrows, the function keys) is a key. The build reads FTXUI's `event.hpp` for the exact members at the pinned version.

**Reference.** The session view's `on_prompt_key` (`source/presentation/tui/session_view.cpp:754`) is the precedent: an open prompt answers only on a key and lets a redraw through. One divergence is deliberate. The session prompt swallows a key that answers nothing, while the list view's question takes any key but `y`/`Y` as a no, because `[y/N]` has a default and this question always had one.

**Docs when it ships:** the MILESTONES entry (Milestone AH, the full-screen shell, beside CLI-M51's), and DEVELOPER.md's rows for `tui/list_view`, `list_view_test` and `pty_tui_check.py`.

## Testing

Hermetic: no network, no models, nothing written outside the test's own folder.

- **`tests/presentation/tui/list_view_test.cpp`**, the bug's fixture. A `ListView` over a `ManualPump` gets a confirm action that records its runs.
  - Press `x`. Then, three times, feed `ftxui::Event::Custom` (what `TerminalPump::post` sends after each closure) with a `drain()` of the pump between them.
  - The draw still holds `[y/N]` and never `not done`, and the action has not run. Then `y` runs it once.
  - The same holds for a typed ask's question (`confirming_ask`).
  - `Y` runs the action; `n`, Escape, Return and an arrow each draw `not done` and run nothing.
  - Red first: at d5481fee the first `Event::Custom` closes the question.
- **`tests/scripts/py/pty_tui_check.py`, a new `confirm` check.** The real binary runs on a pseudo-terminal against a scratch home with the mock backend and the two datasets, following the steps to reproduce.
  - `x` on `alpha`, then 3 s with no key, past one monitor tick: the question is still drawn and `alpha.jsonl` still exists.
  - Then `y`: `removed` is drawn, `alpha` is gone and `beta` is kept.
  - Then Ctrl-D: the terminal modes are put back, as the file's other checks require.
- **Gate:** `lib/scripts/cicd.sh --test`.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Which events answer an open question? | `[default]` | A key press. `y` or `Y` runs the action or the typed ask. Any other key (a character, Escape, Return, an arrow, a function key) draws `not done`, as today. An event that is not a key (the pump's `Event::Custom`, a mouse or cursor-position report) leaves the question open and is passed on unanswered (`return false`), as the typed-ask row already does. |
| Does `Y` mean yes? | `[default]` | Yes, as every line-mode `[y/N]` prompt reads it (Evidence 7). A typed-out `yes` is not offered, because the question reads one key. |
| Should an unanswered question time out? | `[default]` | No. It waits, as the session view's prompt waits. Any key closes it, and Ctrl-D still quits the shell. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-27 | **Diagnosed.** The pump turns every posted closure into an `Event::Custom` (32b), the list view's confirm takes any event but `y` as a no (32d), and the monitor posts every 2 s (32e). So any open question closes at the next post. Reproduced on the real binary: 5 of 5 untouched questions closed in 0.10–1.98 s, on the tick's phase (Evidence 2, 3). | the spec |
| 2026-11-27 | **Placed in Maintenance as M62**, at the table's end, by the user's call to investigate it and spec its fix as a CLI maintenance item, the next id after M61. | the user |
| 2026-11-27 | **The fix is the list view's, not the pump's.** Every view's live drawing depends on the pump's redraw: the monitor bar, progress lines, notices. The session view's prompt already shows that a question can hold through it (Evidence 6). | the spec |
| 2026-11-27 | **Not gated.** No pending item touches `list_view.cpp`'s confirm branches. CLI-M61 changes the question texts in `tui_training.cpp` and `tui_agents.cpp`, not how a question is answered, so either can land first. | the spec |

## Out of scope

- The questions' wording and the commands the views run ([CLI-M51](../../../assistant/cli/MILESTONES.md#2026-11-27--the-tuis-row-actions-reach-a-dash-led-row-maintenance-item-m51-the-tuis-row-actions-reach-a-row-whose-name-starts-with-a-dash), shipped, and [CLI-M61](every-printed-command-goes-through-the-renderer.md)).
- The line-mode `[y/N]` prompts, which read a line and already wait.
- The pump's redraw post and `ManualPump`'s behaviour, both unchanged; the test feeds `Event::Custom` itself.
- A timeout on an open question.
- The session view's prompt, which already waits.

## Links

- **Blocked by:** — · **Blocks:** —
- **Follows from:** [CLI-M51](../../../assistant/cli/MILESTONES.md#2026-11-27--the-tuis-row-actions-reach-a-dash-led-row-maintenance-item-m51-the-tuis-row-actions-reach-a-row-whose-name-starts-with-a-dash)'s build, which saw it on a live drive and recorded it as found and not fixed.
- **Relates to:**
  - [32b](../../../assistant/cli/MILESTONES.md#2026-10-10--tui-shell-backlog-item-32b-bare-apogee-opens-a-screen), the pump and its redraw;
  - [32d](../../../assistant/cli/MILESTONES.md#2026-10-10--tui-workbench-views-backlog-item-32d-the-rest-of-the-product-on-screen), the list view's confirm;
  - [32e](../../../assistant/cli/MILESTONES.md#2026-10-10--tui-system-monitor-backlog-item-32e-the-machine-on-the-bottom-bar), the monitor's 2 s tick;
  - [CLI-M61](every-printed-command-goes-through-the-renderer.md), the question texts.
