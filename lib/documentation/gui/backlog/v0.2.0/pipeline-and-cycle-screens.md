# Pipeline and cycle screens: the Training place sees multi-stage runs and the unattended cycle

**What / why.** The Training place is blind to two of `train`'s four run families. The CLI/GUI audit (2026-10-22, three-lens verified) measured it:

- **Pipelines.** No argv for `train pipeline run --pipeline <name>` (the TUI's `p` key, `tui_training.cpp:151-156`), `train pipeline resume <id>` or `train pipeline status` (`train.cpp:2690`, `:2697`, `:2721`, `:2739`). `grep "train pipeline"` over the app finds nothing. `TrainingStatus` decodes only the `pipelines` count (`business/reads/Training.swift:91`), shown nowhere. A pipeline runs only indirectly, through `train regime run` (`TrainingViewModel.swift:259-260`), with no stage view and no resume.
- **The cycle.** No controls for `train cycle run|status|halt|resume` (the TUI's `c` key runs one pass behind a confirm, `tui_training.cpp:157-176`; CLI `train.cpp:3199-3203`, `:3333`, `:3354`, `:3379`). `cycleActive` is decoded (`Training.swift:92`) and shown in no view.

The parity table classes `train` as this screen's (`Parity.swift:58`), and 2d's recorded decision left only gate and regime *flags* to the console, not these verbs (GUI MILESTONES:933). This item adds both: a Pipelines section (named pipeline specs listed; run, resume and status with each stage's state as the manifest records it) and a Cycle section (its state, run-once behind a confirm, halt, resume), every run supervised with its stream.

**Core constraint(s).** **Gated on the CLI's JSON**: a driver can render neither family honestly today, because `train pipeline` and `train cycle` take no `--output-format` (the audit's CLI finding, owned by CLI 55h). This item builds on 55h's documents and streams, never by parsing prose. **The cycle is unattended by design**: the app's run-once mirrors the TUI's confirm, and nothing schedules it from the app (the cycle's scheduler is the user's). **Read back**: status re-read after every act. ADR 0002; scripted documents and streams.

**Seam + files.** `presentation/training/ViewModels/TrainingViewModel.swift` + views (Pipelines and Cycle sections), `business/reads/Training.swift` (the pipeline and cycle documents decoded), the jobs core (run/resume streams), `presentation/shell/ViewModels/Parity.swift` (the `train` row naming pipelines and the cycle). Tests: pipeline list, status and stage rendering over 55h-shaped fixtures; run and resume argv and stream handling; the cycle's run-once confirm, halt and resume argv.

**Reference.** [CLI 55h](../../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings) (the gate, shipped 2026-10-25); the TUI's training keys; CLI Milestone Z (pipelines and the cycle); the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-25 — **What the CLI's 55h gives this item** (recorded when 55h shipped).
  - `train pipeline status [<id>] --output-format json` and `train cycle status --output-format json` print the admin routes' bodies: `{"data":[…],"object":"list"}`, `{"kind":"pipeline","pipeline":{…}}` and the cycle's `{"active","anchor_version","runs",…}`. The cycle with no history exits 1 with the 404 wording, so read `train status`'s `cycle: null` first.
  - `train pipeline run|resume` and `train cycle run` stream under `--output-format stream-json`, then the read's document:
    - `stage` events (`state` `pending`/`training`/`evaluating`/`fusing`/`passed`/`failed`, with `pipeline_run_id`, `run_id`, `cumulative_score`);
    - `pass` events (`collecting` … `passed`);
    - `iteration` events (`iteration`, `iterations`, `loss`, `lr`, `throughput`, `stage`).
  - The schema date is `2026-10-25`.
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). Gated on CLI 55h, the one cross-queue gate the audit needed: without JSON there is nothing honest to draw.

**Open calls:**
- [default: the Cycle section offers run-once, halt and resume only; scheduling stays the user's (`launchd`/cron), said in the section.]

**Guardrail(s).** Fixture-driven views; argv pins; no training in any test.

**Acceptance criteria:**
- [ ] A named pipeline can be run, resumed and watched stage by stage from the Training place.
- [ ] The cycle's state shows, and a pass can be run, halted and resumed with a confirm.
- [ ] The parity row names both; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4). Its gate, the CLI's [55h](../../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings), shipped 2026-10-25.
