# `train pipeline` and `train cycle` speak JSON

**What / why.** 52b gave long jobs machine-readable streams, but training's multi-stage verbs never got their JSON reads. `train pipeline run|resume|status` and `train cycle run|status|halt|resume` register no `--output-format`. Only `train versions`, `train status` and `train regime list|show` call `add_read_format()` (`train.cpp:2607`, `:2678`, `:2904`, `:2940`). Pipeline status binds at `:2739` and cycle at `:3199-3203` with none (measured by the CLI/GUI audit, 2026-10-22: `train pipeline status --help` and `train cycle run --help` contain zero `output-format`, while `train status --help` contains one). A driver therefore cannot render a pipeline's stages or the cycle's state, and the macOS app's Training place is blind to both (GUI 4j). The data already exists as JSON over HTTP (`GET /v1/admin/training/runs?kind=pipeline`, `/runs/{id}`, `/cycle`, `mux.cpp:364-376`), the way `train status`/`versions` reuse their bodies (`operations/training_reads.h`). The parity table still classes these reads as read-only prose (CLI MILESTONES:6879).

This item gives `train pipeline status` and `train cycle status` `--output-format json` documents built from the same bodies the admin routes serve, byte-for-byte. `train pipeline run|resume` and `train cycle run` get `--output-format stream-json`, relaying the pipeline's existing stage transitions and the cycle pass's existing narration as events, then the document. That is 52b's relay law: no second accounting, default output byte-identical.

**Core constraint(s).** **One body per document**: the CLI's JSON is the admin route's body (the `training_reads` precedent), never a second rendering. **A relay, not a narrator** (52b): the streamed events come from the pipeline manifest's transitions and the cycle's existing progress, and the final line is the read's own document. **Default output unchanged.** machine-mode.md and the schema grow (28h, `cli.machine_schema_conformance`); the flags complete (ADR 0007); the parity table's rows for these verbs move from prose to JSON reads (ADR 0010).

**Seam + files.** `presentation/cli/train.cpp` (`add_read_format` on pipeline/cycle status; stream-json on the run verbs), `presentation/operations/training_reads.*` (the pipeline and cycle bodies shared with `httpserver/admin_training`), `presentation/machine/` (the events declared), `lib/documentation/cli/reference/machine-mode.md`, `cli/tui_parity.cpp` (the classification). Tests: document equality with the admin bodies; a scripted pipeline's stage events then its document; the cycle pass's events over a scripted run; default-output byte-identity; schema conformance.

**Reference.** 52b (shipped): the relay law; Milestone Z: the pipeline manifest and the cycle consumed whole; the admin training routes, the bodies reused; the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). The GUI's pipeline and cycle screens gate on this item ([`pipeline-and-cycle-screens.md`](../../../gui/backlog/v0.2.0/pipeline-and-cycle-screens.md), GUI 4j), the one cross-queue gate the audit found necessary: without JSON there is nothing honest to draw.

**Open calls:**
- [default: event names in the 52b family (`stage`, `pass`, …), declared as data in the protocol.]

**Guardrail(s).** Body equality, stream goldens, byte-identity and conformance, all over scripted runs. No training in any test.

**Acceptance criteria:**
- [ ] `train pipeline status` and `train cycle status` print JSON documents equal to the admin bodies.
- [ ] `train pipeline run|resume` and `train cycle run` stream their transitions under `--output-format stream-json`; default output is byte-identical.
- [ ] machine-mode.md, the schema and the parity table agree; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 55); ungated. GUI 4j gates on it.
