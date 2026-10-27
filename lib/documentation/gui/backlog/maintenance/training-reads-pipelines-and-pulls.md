# Training reads named pipelines, the cycle's halt and datasets pull as the CLI states them

**What / why.** The CLI's M30 (shipped 2026-10-26, [Milestone AO](../../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths)) fixed what 4j's Training tabs had to work around, and the app still works around it:
- **Named pipelines are read, not completed.** `train pipeline list` and `train pipeline show <name>` (`--output-format json`) state each named spec, its student and its stages (dataset, gate). The Pipelines tab still takes the names from `__complete train pipeline run --pipeline ""` (`presentation/training/ViewModels/TrainingViewModel.swift:417`) and shows no stage plan before a run.
- **A halt taken during a pass is kept.** It ends the pass with a `notice`, `[cycle] halted while this pass ran (<reason>) -- the halt is kept: …`, and the next run is refused. The app disables Halt while a pass holds the cycle and says "A halt taken during a pass is lost when the pass records itself" (`TrainingViewModel.swift:661–670`), which is now wrong. GUI M11 already draws the CLI's behaviour in the design.
- **`datasets pull` streams as a job.**
  - **What it sends:** `--output-format stream-json` gives `job`, `notice`s, `bytes` and a closing `document` (`{object: "dataset.pull", ref, path, files, bytes}`), and Ctrl-C ends it on `error`, leaving nothing.
  - **What the app does today:** 4i's Pull… runs it as text, reads prose for the folder its Prepare it takes, and can't show bytes.
- **A failed pass says so on stderr,** and a resumed stage carries no stale score. 4j hid the stale score by gating; the hiding can stay.

**Core constraint(s).**
- **The binary's word decides:** the specs from `list`/`show`, the pull's folder from its `document`, the halt's outcome from the pass's notice.
- **An older binary keeps today's behaviour:**
  - completion for the specs;
  - Halt disabled during a pass, with today's warning;
  - the pull as text.

  The app tells them apart by trying the read (a refused `train pipeline list` means an older binary) or by the machine schema's date (`2026-10-26` or later), whichever is honest for each. Record which.
- **Every run is supervised** through the jobs core and its `ChildRegistry` (4c), Stop reaching the pull.
- **ADR 0004:** each changed state has a preview over recorded documents.

**Seam + files.**
- **`business/reads/Training.swift`:** the `list`/`show` documents decoded (spec, student, stages).
- **`presentation/training/ViewModels/TrainingViewModel.swift`** and `Views/PipelinesView.swift`, `CycleView.swift`, `TrainingScreen.swift`:
  - the specs and their stage plan;
  - Halt during a pass enabled on a binary that keeps it, the pass's halt notice drawn;
  - Pull… as a `stream-json` job with bytes and Stop, Prepare it from the `document`'s `path`.
- **Fixtures:** recorded from the branch binary over a sandbox with the mock trainer:
  - `train pipeline list` and `show`;
  - a pass halted mid-run, ending with its notice (M30's FIFO hold);
  - a `datasets pull` stream over a scripted or loopback source, nothing reaching the network.

  Hand-made only where a recording needs the network, said.

**Reference.**
- GUI 4j (the tabs) and 4i (datasets Pull…) in [Milestone E](../../assistant/MILESTONES.md#milestone-e--the-audits-gui-findings).
- GUI M11's design, which draws the CLI's halt.
- The CLI's M30 record.

**Decisions made** (dated):
- 2026-10-26 — **Opened from the CLI and GUI Maintenance run's record** (the user's call: "spec them into the backlog and then execute them"). It carries the Halt wording fix M11 found.

**Open calls:**
- [default: the spec list replaces completion when `train pipeline list` answers, and a spec's row shows its stage count and student, with its stages on its page.]
- [default: Halt during a pass is enabled, and says the pass running now finishes and keeps the halt, on a binary whose machine schema is dated `2026-10-26` or later. Older binaries keep today's disabled button and warning.]

**Guardrail(s).**
- **Recorded documents and streams:** the specs listed, a stage plan drawn, a halted pass's notice drawn, and Halt enabled or not by binary.
- **The pull's stream:** bytes, Stop and the document's folder taken by Prepare it.
- **Every older recording** still draws as today.

**Acceptance criteria:**
- [ ] The Pipelines tab lists the named specs and their stages as `train pipeline list`/`show` state them.
- [ ] Halt during a pass is offered, in the CLI's words, where the binary keeps it, and the pass's halt notice is drawn.
- [ ] `datasets pull` shows its bytes, can be stopped, and Prepare it takes the folder its document names.
- [ ] Older binaries work as today; previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate. The app's adoption of the CLI's M30.
