# Training screens in the React app

**What / why.** Port the old prototype's Training place into the React app: its five tabs, the promote and pull sheets, the training confirms, and the live pipeline and cycle runs. The data becomes the CLI's own recorded documents and streams. Measured 2026-11-14:
- **The prototype's training code** (`lib/design/spec/html/main.html`):
  - `renderVals()` training section, 6428–6564 (137 lines), builds `trn`. It holds three inline tables: `trRuns` (2 runs), `trRegimes` (3 regimes) and an 8-row kits list.
  - `ui.isTraining` markup, 1279–1396 (118 lines): the tab row 1284–1290, Runs 1296–1321, Pipelines 1322–1348, Cycle 1349–1369, Regimes 1370–1382, Datasets 1383–1395.
  - The replay helpers, `trainRecorded()` through `trainGo()`, 3083–3293. Its comment says it replays darwin's `train-pipeline-*` and `train-cycle-*` fixtures, but the recon found the keys re-shaped (`{id, spec, status, student, started, completed, stages}` where the CLI writes `{id, kind, passed, spec_name, stages, started_at, status}`).
  - `tick()`: one replay event per tick (2939–2948) and the promote, pull and prepare side jobs (2964–2971).
  - The sheets section: `promote` 7573–7629 and `pull` 7630–7644. The confirm section: kinds `promoteRefused` (5839–5844) and `train` (5923–5926).
  - The sidebar's Training place, ⌘9, with a running badge (5192).
- **Line numbers move.** The ones above are the working tree's on the afternoon of 2026-11-14 (7,780 lines).
  - The recon's copy (7,604 lines) had the training section at 6252–6388 and the sheets at 7300–7563. Another session's uncommitted work has since moved both down 176 lines, same sizes. The Training markup has not moved.
  - HEAD `48f46831` (7,300 lines) has the training section at 5957–6093, the sheets at 6998–7259 and the markup at 1253–1370.
  - 1c's frozen baseline fixes the final numbers.
- **Presets:** 4 today, each with a board: `training` (Runs, a run training; "Training · a run, gated"), `training:datasets`, `training:pipelines` (the `skills` pipeline mid-run, stage 2, iteration 2) and `training:cycle` (a skipped pass, then a passing one). No preset opens Regimes.
- **The recorded data exists:**
  - 81 `train-*` reads in `lib/src/darwin/tests/business/reads/Fixtures` (status 17 files, cycle status 14, pipeline status 15, promote 10, among others);
  - 16 `train-*` streams in `tests/business/jobs/Fixtures` (pipeline run, resume and 9 cycle-pass outcomes);
  - 10 `datasets-*` files in the two folders.
  - 3 of them are hand-made already: `datasets-pull-handmade.jsonl`, `datasets-pull-handmade.txt` and `train-cycle-status-halted-by-hand.json`.
- **The Mac module** `presentation/training`: 7 files, 4,503 lines. One view-model, `TrainingViewModel` (1,435 lines), whose 5 tabs match the prototype's (TrainingViewModel.swift:48–53). Three view files (`TrainingScreen`, `PipelinesView`, `CycleView`) holding 75 `#Preview`s, and three preview support files.
- **Where the prototype and the Mac app differ.** The port follows the prototype; these are named, not changed:
  - New dataset, Synthesize, Prepare (outside the pull banner), Gate, Run regime and Delete dataset are toasts ending "(prototype)". The Mac has `RunSheet`, `SynthSheet` and `CreateSheet` (TrainingScreen.swift:453, 578, 604) and runs them.
  - The prototype rolls a backend back at once. The Mac asks first (`Pending.rollback`, TrainingViewModel.swift:231).
  - The Mac draws states the prototype never did: a dead pass's lock, an unreadable history, an older binary's documents.

**Core constraint(s).**
- **The prototype is the source of truth.** Layout, states, controls and wording match it. Every command string stays word for word (`apogee train pipeline resume <id>`, `apogee train cycle halt`, the promote sheet's built command line).
- **The layers and MVVM shape** are the design's ADR 0001 ([1a](../../../adrs/design/react-design-prototype.md)): the view-model is a plain TypeScript class that never imports React, takes its services by injection, and is driven only through its public methods.
- **Real data only.** Reads and streams come from the stand-in ([1d](cli-stand-in.md)) answering with recorded files read in place. The re-shaped `trainRecorded()` tables are not carried over. A state nobody recorded gets a hand-made file in the CLI's exact shape and words, named `*-handmade*`.
- **Tokens only**, through the styles module ([1e](styles-shell-and-the-first-screen.md)).
- **The old prototype is frozen.** This item reads it; it never edits it.

**Seam + files.** The `training` module in the Presentation layer of `lib/design/prototype/` (folder shape per [1b](../../../assistant/design/DEVELOPER.md)):
- `ViewModels/TrainingViewModel.ts`: the Mac's name. Its tab list (runs, pipelines, cycle, regimes, datasets), a `PromoteForm`, a `Pending` for the five `train` confirm acts (run, resume, one pass, halt, resume the cycle), the pulled banner, the side job bar, and a `running` flag for the sidebar badge. Its reads are the Mac's (TrainingViewModel.swift:452–583): `train status`, `train versions`, `train regime list`, `datasets kits`, `datasets list`, `train pipeline list|status|show`, `train cycle status`.
- `Views/`: `TrainingScreen`, `RunsView`, `PipelinesView` with `StageList`, `CycleView`, `RegimesView`, `DatasetsView` with `PulledBanner`, `PromoteSheet`, `PullSheet`. The Mac's names, rendered through 1e's sheet and confirm hosts.
- `Previews/`: one named entry per state the prototype draws, each built from fixtures through the view-model's public API.
- **Presets** in the stand-in's preset table, each `{fixtures, route, clock}`:
  - `training`: `train-status.json` (run 20261021-051826, failed its gate) and a run training from `train-run.err`'s first lines.
  - `training:datasets`: `datasets-list.json`, `datasets-kits.json`.
  - `training:pipelines`: `train-pipeline-list.txt`, `train-pipeline-status-none.json`, and `train-pipeline-run.jsonl` (33 lines, the `skills` run) replayed to stage 2, iteration 2.
  - `training:cycle`: `train-cycle-status-passed.json` (a skipped pass, then a pass that set anchor v1).
  - `training:regimes`, **new**: `train-regimes.json`, `foundation` selected.
- **Streams** the actions replay: `train-pipeline-run*.jsonl`, `train-pipeline-resume*.jsonl`, `train-cycle-run-*.jsonl`, `datasets-pull-handmade.jsonl`. Promote refusals: `train-promote-names-adapter.err`, `train-promote-holds-merged.err`, `train-promote-adapter-no-suite.err`.
- **One new hand-made file:** `train-promote-gate-failed-handmade.err`. The prototype's first refusal ("the eval gate failed for run …, pass --force to skip the gate") has no recording. Its words come from the CLI's `lib/src/cli/source/business/training/promote.cpp:75` (the failure) and `:45` (", or pass --force to skip the gate").
- Tests beside the module (Guardrails).

**Reference.**
- The Mac module: `lib/src/darwin/source/presentation/training/`; its previews' fakes in `Previews/PreviewSupport.swift`.
- [Darwin ADR 0002: presentation MVVM](../../../adrs/darwin/presentation-mvvm.md), [Darwin ADR 0004: module schemes and previews](../../../adrs/darwin/module-schemes-and-previews.md), [GUI ADR 0001: layer enforcement](../../../adrs/gui/layer-enforcement.md).
- The old guide's 30-tick check: [`lib/design/spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md) "Checking a change".
- The design home: [`assistant/design/CLAUDE.md`](../../../assistant/design/CLAUDE.md).

**Decisions made** (dated):
- 2026-11-14: The user's call: rebuild the prototype in React, TypeScript and Vite, in the production apps' four layers and MVVM modules. The prototype stays the design's source of truth. It builds to one self-contained `lib/design/main.html`. Release v0.2.1, design track 1.
- 2026-11-14: The user's call: parity screenshots are taken by Playwright in [1s](parity-proof.md), not here.
- 2026-11-14: The recorded default: data is the Mac app's recorded fixtures, read in place; an unrecorded state gets a `*-handmade*` file in the CLI's shape. Parity is judged on layout, states, controls and wording, not pixels or data.
- 2026-11-14: The split: 1j is gated on 1e, and it adds the `training:regimes` preset the old prototype never had.

**Open calls:**
- [default: the bare `training` preset reads `train-status.json` only. The prototype's second, passed run (20261026-065637) lives in `train-status-passed.json`, and no recording holds both. It is not merged into a hand-made file, since parity is on structure.]
- [default: the "(prototype)" toasts stay toasts. Porting the Mac's run, synthesize and create sheets would be new design, which is drawn in the React app after this item, if wanted.]
- [default: rollback stays immediate, as the prototype does; the Mac's ask-first is named, not ported.]
- [default: the Mac-only states (dead lock, unreadable history, older binary) are not added. Their fixtures are recorded, so a later design item can draw them cheaply.]
- [default: every preset also runs with the stand-in in its "missing" mode, the prototype's `apogee: missing` prop. Each tab then shows "Apogee isn't installed", as the Mac's "Apogee not installed" previews do.]
- (consumed decision: 1d) the stream replay, the clock and the preset type. (consumed decision: 1e) the sheet and confirm hosts and the ⌘9 badge. (consumed decision: 1s) the old side of the `training:regimes` pair (no old preset opens Regimes, so 1s clicks the tab from `training`) and its gallery board.

**Guardrail(s).**
- **Per-preset test** (the successor of the 30-tick check), for all 5 presets in both "installed" and "missing" modes: build the view-model on the stand-in, step the clock 30 ticks, and walk everything the views read. No throw, no `undefined`, no `NaN`.
- **Replay tests:** a pipeline run aborts at stage 2 and offers "Resume from stage 2, refusals"; the resume completes; a cycle pass ends passed, failed (halting the cycle) and skipped, each from its recorded stream; halt during a pass keeps the halt.
- **Promote tests:** each refusal the sheet can raise shows its recorded `.err` line word for word, and "Change the promotion…" reopens the sheet in the mode the refusal names.
- **Fixture paths:** a test fails, naming the preset, when a fixture it names is missing.
- **The layering check (1b)** stays green; a planted `import React` in `ViewModels/TrainingViewModel.ts` fails, naming the file.

**Acceptance criteria:**
- [ ] The built `lib/design/main.html` opens each of `training`, `training:datasets`, `training:pipelines`, `training:cycle` and `training:regimes`, showing the prototype's tabs, controls and words over the CLI's recorded data.
- [ ] A pipeline run and its resume, a cycle pass, halt and resume, a promote (and a refused one), a pull and a prepare all play through from recorded streams.
- [ ] ⌘9 opens Training, and the sidebar badge shows while a run or pass is live.
- [ ] The local checks pass: typecheck, unit tests, the layering check, staleness.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on [1e](styles-shell-and-the-first-screen.md). Out of scope: the parity screenshots and the `training:regimes` gallery board ([1s](parity-proof.md)); the Mac's run, synthesize and create sheets and its extra states; any edit to the old prototype or the Mac app.
