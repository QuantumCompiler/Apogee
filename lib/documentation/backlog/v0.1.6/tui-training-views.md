# The training views: Train and Datasets

**What / why.** The training track is the shell's deepest uncovered surface and the least machine-readable (measured 2026-10-10, the parity spike): **0 of `train`'s 10 verbs and 0 of `datasets`' 8 carry a `--output-format json` read** — the status, versions and dataset listings print human tables only. Two views over `tui/list_view` and 37e (shipped, [Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui))'s widget: **Train** — the backends with versions (`train versions`' rows carved), the run/pipeline state (`train status`'s), `r` launching a run (and `p` a pipeline, `c` a cycle) after an ask with the run narrated in the progress widget, `P`romote and `R`ollback as confirmed actions in the commands' own words; **Datasets** — the datasets listed (`datasets list` carved; `kits` and `info` as cards), `p` preparing and `s` synthesizing over the widget, `x` deleting after an ask. The reads ship as JSON documents in the same change (the 28h idiom), which the admin plane's read-only training slice already foreshadows. Closes W3's remaining consumers and the training slice of W1/W2.

**Core constraint(s).** The trainer's JSONL progress protocol and the runner's narration are the only sources the widget renders — no second protocol (Milestone Z's shape). Promotion's gates, closures and "a failing candidate never reaches inference" are untouched: the view's `P` calls the command's core and prints its gate words, never re-deciding. The cycle's PID lock and circuit breaker stand (one cycle; the view's `c` refused exactly as the command refuses a held lock). The venv rules hold — never the system Python, and the view probes nothing the command does not. New reads are carved **as** documents ([ADR 0002](../../adrs/cli/mode-parity.md): the view's rows, the JSON document and the human table are one row function), `machine-mode.md` gaining their rows.

**Seam + files.**
- `lib/src/cli/source/presentation/cli/tui_training.h/.cpp` — both views, registered in `cli/tui_cmd.cpp`.
- `cli/train.h/.cpp` — carve `read_train_versions` / `read_train_status` rows (ship `train status|versions --output-format json`); the run/pipeline/cycle/promote/rollback cores carved out of the callbacks where they hold them (the 32d idiom), narration handed to the widget.
- `cli/datasets.h/.cpp` — carve `read_dataset_rows` / info / kits (ship `datasets list|info|kits --output-format json`); prepare/synth over the widget.
- `lib/documentation/reference/machine-mode.md` — the new documents' rows; `tests/presentation/cli/tui_training_test.cpp` (new, over the in-process `mock` trainer); leak-test rows; classification flips in 37a's table (`train`, `datasets`).

**Reference.** Milestone Z — the trainer contract, the JSONL progress protocol, the mock trainer (the hermetic test vehicle), promote's closures and gates; 27b/27c — the MLX promote target and converter (untouched, reached only through the cores); 37e — the widget and its laws (consumed, not re-owned); 28h — reads as documents.

**Decisions made** (dated):
- 2026-10-10 — Launches ask first, in the command's own words with the spend named (a training run holds the machine for hours; a single key is not a typed command) — the 32d ask idiom at training's stakes (the user's placement of the set in v0.1.6 on the spike report; the design the spike proposed, accepted).
- 2026-10-10 — `train setup` and `datasets create|pull` get no curated key — setup is an install-ish venv act, create and pull take arguments a form would badly mimic; each runs in the shell through [37h](tui-command-runner.md)'s exec line (the default, confirmed 2026-10-10, after the runner-spike revision of its original "stay commands").
- 2026-10-10 — `train eval` rides Enter on a version row, a bounded act reported in the command's words, rather than its own key (the default, confirmed 2026-10-10).

**Guardrail(s).** Rows golden against the new JSON documents; a mock-trainer run launched from the view leaving a manifest byte-equal to `apogee train run`'s for the same scripted progress, its lines in the widget exactly the protocol's; promote and rollback from the view leaving the command's own store state and refusing the command's own refusals (the gate words compared); the cycle's held lock refused identically; the leak test's planted key searched in both views.

**Acceptance criteria:**
- [ ] Train and Datasets are views on the shell; every list re-reads on show.
- [ ] `train status|versions` and `datasets list|info|kits` print documents, documented in machine-mode.md; the views draw exactly those rows.
- [ ] A mock run launched from the view narrates in the widget and lands byte-identically to the command's; promote/rollback/cycle hold their parity and refusals.
- [ ] 37a's classification rows for `train` and `datasets` flip; the full suite is green.

**Scope note.** Earmarked for v0.1.6; gated on 37e (shipped, [Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui)) (and through it 37a (shipped, [Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui))). Out of scope: any trainer, promote or cycle behavior change, real-weights runs (the mock trainer is the test vehicle; the families stay optional per the standing rules).
