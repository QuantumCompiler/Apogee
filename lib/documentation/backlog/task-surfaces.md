# Task surfaces: machine-mode events, JSON status, served reads

**What / why.** [28a](task-runner-core.md) is a CLI command narrating to a terminal. Parity is the product, so a task must be *watchable and readable* from every surface that exists: a front-end driving the binary over machine mode needs task lifecycle events to render live progress (the GUI direction depends on this — a task is exactly the long-running thing a GUI wants a progress panel for); a host or script needs `task status`/`task list` as JSON documents (joining [27e](machine-readable-reads.md)'s read contract); and a server deployment needs its tasks visible over the admin plane — **reads served, control CLI-only**, the rule the training track already settled. One runner, one ledger, three views of the same facts.

**Core constraint(s).**
- **One source of truth:** every surface renders the ledger; nothing computes task state a second way. The machine-mode events, the JSON documents and the admin responses carry the same fields the human `task status` prints, or the parity tests fail naming the drift.
- **Additive on the wire.** New machine-mode event types (task lifecycle and round transitions) arrive under rule 1 — old drivers ignore them — and are documented in [machine-mode.md](../reference/machine-mode.md) and pinned by `cli.machine_schema_conformance` like every other event. If [27d](machine-schema-artifact.md) has shipped, they join the schema artifact in the same change; additive either order, so this is interplay, not a gate.
- **Control stays local.** `run`, `resume`, `halt`, `cancel` are CLI verbs; the admin plane serves reads and nothing else (the training precedent, verbatim — the CLI↔HTTP parity test's carve-out list grows by exactly these). No task control over HTTP.
- **Secrets and privacy:** served task views are built from view types — the goal, checks, rounds, statuses; never a declared answer's text ([28b](task-autonomy-policy.md) stores those in the ledger; the served view says one existed, not what it said), and never a path into the private layout.
- **Never a push channel by the back door:** task events are emitted by the task's own process on its stdout during `task run --output-format stream-json`; a *separate* chat child does not start narrating tasks unasked. The parked push-channel question stays parked and stays the user's.

**Seam + files.**
- `commands/json_reporter.cpp` / `render/json_report.h/.cpp`: the task event types (started, plan recorded, round started/ended with check states, grant exercised, finished with outcome), emitted when `task run` is invoked with `--output-format stream-json`; the `task status`/`task list` JSON documents per 27e's flag and framing conventions.
- `httpserver/`: `GET /v1/admin/tasks` and `GET /v1/admin/tasks/{id}` over the same view builder; the parity test's classification updated (reads served, controls carved out by name).
- [machine-mode.md](../reference/machine-mode.md): the task events section; [http-api.md](../reference/http-api.md): the two routes — both under their existing conformance pins.
- Tests: golden JSON for status/list; an e2e that runs a scripted-mock task under `--output-format stream-json` and asserts the event sequence matches the ledger's transition sequence one for one; the admin-route tests and the leak sweep over served views.

**Reference (Ommi).** No analog (no machine mode, no task runner). In-house precedents: the third Reporter adapter pattern (Milestone T — loop events as SSE frames), 27e's read-contract conventions, and the training track's reads-served/control-local split (Milestone Z).

**Decisions made** (dated):
- 2026-09-25 — Split from the v0.1.5 task work: surfaces after the runner, so the event vocabulary describes a ledger that exists rather than one being designed underneath it.
- 2026-09-25 — Task events ride `task run`'s own stdout, not the chat protocol: a front-end that wants live task progress spawns the task in machine mode, which keeps "events arrive in response to what you invoked" true without touching the parked push question.

**Open calls:**
- [default: event types named `task_started`, `task_plan`, `task_round`, `task_grant`, `task_finished`, each carrying the ledger's fields for that transition] The vocabulary — settled against the ledger's actual shape at build time.
- [default: `task list` JSON is bounded to the most recent 50 with a `--all` escape; the admin list mirrors it] Listing bounds.
- [default: a resumed task's stream re-emits `task_started` with `resumed: true` and the current ledger state first, so a reconnecting front-end needs no other source] Resume over the stream.

**Guardrail(s).**
- The event sequence ↔ ledger transition sequence equality, asserted on the e2e's real run.
- Row-parity: human `task status`, its JSON document, and the served view enumerate the same facts (the 27e pattern extended).
- Conformance: an undocumented task event, or a documented-but-unemitted one, fails the build in both directions.
- The leak sweep over served task views (declared answers and ledger paths never appear).

**Acceptance criteria:**
- [ ] `apogee task run … --output-format stream-json` emits the documented task events interleaved with the ordinary turn events, and a driver reconstructs the task's exact state from the stream alone.
- [ ] `task status --output-format json` and `task list --output-format json` print documents carrying the same facts as the human views.
- [ ] A server deployment lists and inspects its tasks over `/v1/admin/tasks*`; every control verb over HTTP is refused, and the CLI↔HTTP parity test classifies all of them.
- [ ] machine-mode.md and http-api.md document the additions under their conformance pins; a `protocol_version: 1` chat driver is untouched by all of it.

**Scope note.** Item **28c**, earmarked for **v0.1.5**; build after [28a](task-runner-core.md) — and after [28b](task-autonomy-policy.md) only for the grant-exercised event (buildable without it by omitting that event; the default is to build in letter order). Interplay, not gates: [27d](machine-schema-artifact.md)/[27e](machine-readable-reads.md) conventions are adopted whether or not they have shipped first. Out of scope: task control over HTTP; a push channel; GUI work itself.
