# The design draws Training's Pipelines and Cycle tabs

**What / why.** 4j (shipped 2026-10-26, [Milestone E](../../assistant/MILESTONES.md#milestone-e--the-audits-gui-findings)) gave the app's Training place two tabs the design doesn't have. The tabs run Runs · Pipelines · Cycle · Regimes · Datasets; the prototype draws Runs · Regimes · Datasets (`lib/design/spec/html/main.html:1165–1175`, the `trn.showRuns`/`showRegimes`/`showDatasets` segment). Neither tab has a board, and the contract board has no row for the reads and streams they run on.
- **Pipelines:** the named specs (from `__complete train pipeline run --pipeline ""`), each with Run…, the runs newest first, a run's stages live from its stream then read back from `train pipeline status <id>`, and Resume… on an aborted or failed run.
- **Cycle:** the cycle's state and passes from `train cycle status`, Run one pass…, Halt… and Resume…, each asked first, scheduling said to be the person's own, and `cycle: null` drawn as the empty state.
- **The contract board** (`html/BuildContract.dc.html`) maps every part of the design to its event or command. 4l (same day) recorded under "Not done" that it has no rows for `train pipeline status|run|resume` and `train cycle status|run|halt|resume`, the CLI's 55h documents and streams.

The design is the reference the app is built from (`lib/design/spec/CLAUDE.md`). An app screen the design never drew is drift the next app, Linux or Windows, would build without.

**Core constraint(s).**
- **The design's own law** (`lib/design/spec/CLAUDE.md`):
  - **The repo and the canvas move together:** edit here, read the canvas first, publish each changed file, and read it back identical.
  - **A new state worth its own board** gets:
    - its start preset in `applyStart()` and in `data-props`' `start` options;
    - its `html/Screen<Name>.dc.html` copied from a sibling;
    - its `json/canvas.json` entry (page `build`, the next free slot in its row), sent because the layout changed;
    - its README row.
  - **Every part maps to the CLI,** in the CLI's words; a part with no event or command behind it is a gap on the contract board, drawn with its fallback.
  - **Every colour is a token,** keyboard first, accessible as drawn, copy in plain words.
- **Draw what the app does, from the CLI's own documents.** Every stage word, state and refusal on the boards is the binary's, from the 4j recordings (`lib/src/darwin/tests/business/reads/Fixtures/train-pipeline-*`, `train-cycle-*`, `train-status-*`; `tests/business/jobs/Fixtures/`), never invented.
- **History stays history.** The two existing Training boards and their starts stand. The new tabs join beside them.

**Seam + files.**
- **`lib/design/spec/html/main.html`:** the Training header's segment gains Pipelines and Cycle. The `trn` section of `renderVals()` gains their canned state, recorded words and handlers, and `tick()` gains a pipeline stage moving.
- **New boards:**
  - `html/ScreenTrainingPipelines.dc.html`, start `training:pipelines`: a pipeline run stage by stage;
  - `html/ScreenTrainingCycle.dc.html`, start `training:cycle`: the cycle with its passes;
  - their `json/canvas.json` entries beside `ScreenTrainingDatasets.dc.html`, and their README rows.
- **`html/BuildContract.dc.html`:** TRAINING rows for the pipeline and cycle reads and streams, ready where 55h states them, and the named specs listed by `train pipeline list` and `show`, ready since the CLI's [M30](../../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths) (shipped 2026-10-26; 4j's app still names them from completion, its adoption a later GUI item).

**Reference.**
- The design's own precedents for a new tab and board: 2d's Training and Datasets boards (canvas version 39) and 2e's Library boards (version 40), recorded in [Milestone C](../../assistant/MILESTONES.md#milestone-c--cli-parity).
- 4j's views (`lib/src/darwin/source/presentation/training/Views/PipelinesView.swift`, `CycleView.swift`) and their previews: what the app draws today.
- 4l's republish of the contract board: [Milestone E](../../assistant/MILESTONES.md#milestone-e--the-audits-gui-findings), canvas version 46.

**Decisions made** (dated):
- 2026-10-26 — **The pipeline-spec gap closed before this item was built.** The CLI's M30 shipped `train pipeline list` and `show` the same day, so the board maps them as ready.
- 2026-10-26 — **Opened from the GUI v0.2.0 run's record** (the user's call: "spec them into the backlog"; placed in GUI Maintenance, the user's call the same day). 4j and 4l recorded the gap under "Not done".

**Open calls:**
- [default: two boards, a pipeline mid-run stage by stage and the cycle with its passes. The empty states, a failed stage's Resume and the run-once confirm are drawn in `main.html` and reached by the prototype, not boards of their own.]
- [default: the Pipelines board draws the specs as `train pipeline list` states them (ready since the CLI's M30), and the contract board adds no gap for them.]

**Guardrail(s).**
- **The design's health check** (`lib/design/spec/CLAUDE.md`, every start screen for 30 ticks): no problems, the two new starts among them.
- **The contract board's counts** match its chips, and its ledger matches the gaps its map names.
- **The canvas law:** each changed file read from the canvas first, published, and read back identical, `canvas.json` included.
- **Words checked against the recordings:** every stage word, state and refusal on the new boards appears in 4j's recorded documents and streams.

**Acceptance criteria:**
- [ ] The prototype's Training place has Runs · Pipelines · Cycle · Regimes · Datasets, each drawn from the CLI's words.
- [ ] Two new boards show a pipeline run stage by stage and the cycle with its passes, on the canvas and in the repo, identical.
- [ ] The contract board maps the pipeline and cycle reads and streams, with the spec-list gap and its fallback.
- [ ] The health check passes.

**Scope note.** GUI Maintenance, claimable by name, no gate; design only, with no app code. Its sibling for track 4's other additions is [M12](design-draws-track-4-additions.md).
