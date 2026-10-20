# Tasks: the list, a run, a new task

**What / why.** The design's task screens (`ScreenTasks`, `ScreenNewTask`): running and past tasks, a live run's plan, rounds, checks and activity, stop-after-this-round / stop-now / resume, and the new-task sheet with grants, checks and the round budget. The contract board marks the whole section **ready**: `task run … --output-format stream-json` streams `task_started`, `task_plan`, `task_round`, `task_grant`, `task_finished`; `task list` and `task status --output-format json` cover tasks started elsewhere (read again on focus); `task halt`, `task cancel`, `task resume` are the controls; grants are explicit flags (`--allow`, `--require`, `--require-file`, `--rounds` 1–32); time-so-far is the app's clock. One design law binds the sheet: **tasks never ask** — a task in machine mode reads nothing, so grants are given up front (the design's decision 6), and the sheet says so plainly.

**Core constraint(s).** ADR 0001/0002; the run's view is the ledger's events, never inference (a task's truth is its transitions — the CLI's 27j law read from the client side); controls are the verbs; tasks started elsewhere are reads, refreshed on focus, never watched by polling loops. The client core's event union already tolerates the task events (shipped 1b) — the screen consumes, the core grows only if a task event shape is missing.

**Seam + files.** `source/presentation/tasks/` (list, run view, the new-task sheet; view-models), `source/business/` (a task-run child over the jobs/session machinery; the task reads). Tests: the run view-model over scripted task-event streams (plan→rounds→finish, halt/cancel/resume); the sheet's flag assembly pinned argv-exact; the list's read-on-focus; hermetic.

**Reference.** `ScreenTasks.dc.html`, `ScreenNewTask.dc.html`; the contract board's TASKS rows (all ready); the CLI's machine-mode task section (27j) — the events rendered.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call); grants-up-front said in the sheet (the design's decision 6, standing).

**Open calls:**
- [default: list columns and the activity feed's density per the prototype; details at build.]

**Guardrail(s).** Event-stream matrices; argv pins for the sheet; read-on-focus pin; hermetic.

**Acceptance criteria:**
- [ ] A task launched from the sheet streams its plan, rounds and finish; halt/cancel/resume work; tasks from elsewhere list and refresh on focus.
- [ ] The sheet assembles exactly the flags a hand-typed run takes and says tasks never ask; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on [`app-shell.md`](app-shell.md) (1e). Out of scope: recipes UI (a later item if wanted), any CLI change.
