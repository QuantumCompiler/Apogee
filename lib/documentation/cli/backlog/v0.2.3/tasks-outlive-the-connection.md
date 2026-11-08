# Autonomous tasks on the server outlive the connection

**What / why.** The user's vision, 2026-11-07: Apogee as a private, self-hosted harness that feels like Claude on every device the person owns. A strong machine serves the person's desktops and phones. A phone is the device most likely to start a long goal and then go away: it is locked, it loses signal, the app is suspended. Apogee already has the unattended run for this, the task (27h–27j: a goal, its checks, a round budget, a ledger, authority handed over before it runs). But a task lives only in the process that started it, and today that process is a local child.

**W5 — a task's run and its events live in the process that started it.** Re-verified at HEAD (`4f92316d`) on 2026-11-07, against the branch binary in a scratch `APOGEE_HOME` over a scripted mock backend:

1. **The events go to the starting process's stdout and nowhere else.** The reference says it outright: a task's events "arrive on the stdout of the `task run` it invoked, never from anywhere else" (`reference/machine-mode.md:304-306`). `drive` writes them to the `events` stream it is handed (`presentation/cli/task_cmd.cpp:493-497`). The ledger keeps the transitions and each round's summary (`business/tasks/ledger.h:14-23`), but no turn's `thinking`, answer or tool narration. The probe's resumed run streamed 10 events (`session`, `task_started`, `task_plan`, `thinking`, `answer_start`, `answer_delta`, `answer_end`, `result`, `task_plan`, `task_finished`), and afterwards the task's folder held `task.json` alone. Its `transitions` were `created, started, plan_started, resumed, plan_started, plan_recorded, finished`. Nothing a late reader can attach to exists.
2. **When the driver goes away, the task dies with it.** In the probe the driver read three lines of `task run "say mock" --require mock --rounds 3 --output-format stream-json` and closed its pipe. The run exited at once with `-13` (SIGPIPE; 141 from a shell, re-run the same day), and `task list` then read `"status":"planning","interrupted":true`. The CLI guards only its children's pipes from SIGPIPE (`infrastructure/platform/child_process.cpp:84-108`): per descriptor on macOS, and on Linux by ignoring the signal process-wide once a child pipe is made, so a write to a closed stdout fails there instead of killing the run. Either way the stream has no reader left. Its own stdout has no guard of its own, and in machine mode that stdout is the stream (`task_cmd.cpp:495`). The Mac app is that driver. It follows a task as its own child and, after a quit, offers to resume a killed one by spawning `task resume` (`lib/src/darwin/source/presentation/tasks/ViewModels/TasksViewModel.swift:231-234`). That is a restart from the ledger, not a reattach.
3. **A served task does not exist.** `serve` has two task routes, `GET /v1/admin/tasks` and `GET /v1/admin/tasks/{id}`, and both only read (`presentation/httpserver/admin_tasks.cpp:17-47`). "Task control is CLI-only": `run`, `resume`, `halt` and `cancel` have no route, by a recorded parity carve-out (`reference/http-api.md:1341-1345`). The server's own long-work substrate does not carry over either. Admin jobs "live for the process; nothing here is persisted" (`presentation/httpserver/jobs.h:21-27`). The events stream is advisory: a client 256 events behind loses the next one (`http-api.md:1487-1488`), and it has no cursor to catch up from.
4. **Control works across processes on one host, and only there.** `task halt` and `task cancel` leave a `request` file that the running task's watcher polls every 200 ms (`ledger.h:71-80`; the watcher `business/tasks/runner.cpp:63-74`, its 200 ms `task_cmd.cpp:966`; the request written by `request_task_stop`, `task_cmd.cpp:1417-1433`). In the probe, a `task halt` from a second process stopped a running resume after its plan, recorded as `halted by 'apogee task halt' after the plan`. A second `task run` was refused by the lock: `task task-20261108-000042 is running (process 7220) -- one task runs at a time` (`ledger.h:99-118`). All of it is on the host's filesystem, so a device reaches none of it.

This item makes a task started from a paired device run **on the server**, whether or not the device stays connected. Every task, local or served, keeps a numbered log of its event stream beside its ledger. Any of the person's paired devices can **attach later** and follow the task from a cursor: what it missed is replayed, then the stream continues live. Any of them can **halt, cancel and resume** it, in the CLI's own words. The authority is the task's own, **decided at start** (grants and the question policy), so a device never answers anything mid-run. There are no push notifications in v0.3.0. The app catches up when it opens.

**Core constraint(s).**
- **Anti-goal: nothing here makes standalone use need a server.** `task run`, `resume`, `halt`, `cancel`, `status` and `list` on a host with no server, and the Mac app following a task over pipes, behave as they do today. Their stdout is byte for byte today's stream. The log is a file beside the ledger. A local `task attach` reads that file and needs no server, no network and no account. A failure is anything that puts the server between a person and a task they run at their own machine.
- **Interactive = pipes, serving = server** (CLI CLAUDE.md, the never-listens law; SPEC → Principles, clarified 2026-11-07). A served task runs under `serve`, the one process that owns a port, and adds no listener. A local attach reads a file and opens no socket.
- **One task path, opened by its command's own door** ([ADR 0011](../../adrs/one-session-opening.md)). A served task is opened through `task run`'s own open function, the one the shell's Task view already calls in-process (`run_task_events`, `task_cmd.cpp:1384-1407`, 37e). There is no second runner. A halt or cancel from a device goes through `request_task_stop` (`task_cmd.cpp:1417`), the one stop path.
- **Parity** ([ADR 0002](../../adrs/mode-parity.md)). `task attach` is a CLI verb before it is a network door. The carve-out at `http-api.md:1341-1345` is retired for paired devices and recorded in the reference as retired. The admin bearer plane stays read-only for tasks.
- **Grants are decided at start, and the ceiling holds** (`business/tasks/policy.h:14-35`, `:93-100`). A device names its grants and its `--on-question` when it starts the task. They are held to the *server's* `permissions:` and agent policy by `grant_refusal`. There is no `--allow-all` from a device either. Nothing is asked mid-run, even while a device is attached (`machine-mode.md:352-361`).
- **Secrets are unleakable** (CLI CLAUDE.md). The log carries exactly what the stream carries. A declared answer is in no event (`machine-mode.md:348-350`), so it is in no log line and no attach. No line names a path into the private layout, as the task view's rule already holds (`http-api.md:1366-1367`).
- **Additive protocol** ([ADR 0006](../../adrs/backwards-compatibility.md)). `task attach` is a new verb, and its output's `seq` field is new and appears only there. A `task run`'s stream is unchanged, so an older front end never meets the new field. A task run before this item has no log and is attached from its ledger (Open calls).
- **The layers** ([ADR 0001](../../adrs/layer-enforcement.md)). The log is Business beside the ledger (`business/tasks/`). The verb and the door are Presentation. Nothing in `business/` learns there is a network.
- **Install parity** ([ADR 0005](../../adrs/install-mode-stability.md)). The log lives in the existing `tasks/<id>/` row, so no layout row is added.
- **The dev channel** (CLI CLAUDE.md → [Development runs on the dev channel](../../assistant/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests run in a temporary `APOGEE_HOME`. A live pass serves `apogee-dev`, never the release `apogee` or `~/.apogee`.

**Seam + files.**
- `business/tasks/event_log.h/.cpp` (new): `tasks/<id>/events.jsonl`. It appends one stream line per write, numbering each in order, and is written only by the process holding the task lock (`ledger.h:99`), so it has one writer. Reads are `read_after(root, id, seq)`, plus a follow that wakes on growth and ends at `task_finished`. A torn last line, from a `kill -9` mid-write, is never served.
- `presentation/cli/task_cmd.cpp`:
  - `drive` tees its event stream into the log. The stream itself is unchanged.
  - `run_task_events` takes the request's whole policy (grants, `--on-question`, checks, budget, folder) rather than its goal alone, so a served door opens exactly what `task run` would.
  - New verb `task attach <id> [--after N] --output-format stream-json`: it replays, then follows live, and ends at `task_finished`, or at once for a finished task. It also carries a completion row ([ADR 0007](../../adrs/tab-completion.md)) and a shell classification ([ADR 0010](../../adrs/the-shell-is-a-mode.md)).
- `presentation/httpserver/`: the served task. `serve` starts a device's task on a thread of its own through `run_task_events`, with the server as its driver. When the device leaves, the run carries on. Run, resume, halt, cancel, status and list are 59d's command door (consumed). Attach is 59b's stream door (consumed): an `attach` frame carrying `task_id` and `after`. A server's stop cancels its running task through the loop's own cancellation, leaving it resumable.
- `lib/documentation/cli/reference/machine-mode.md` → A task's run: the log, `task attach`, `seq`, and "a task's events arrive from its log too". `reference/http-api.md`: the carve-out retired for paired devices, and the task doors. `machine-schema.json`: `task_attached` and `seq`.
- Tests (hermetic: temporary `APOGEE_HOME`, the mock backend, scripted turns with `delay_ms`):
  - `tests/business/tasks/event_log_test.cpp`: append and number; read after a cursor; a cursor past the end; a torn last line; a reader following a writer; one writer under the lock.
  - `tests/presentation/cli/task_test.cpp`: the log equals the stream, line for line; `attach --after N` gives lines N+1 onward with `seq`; a finished task's attach ends; a pre-log task attached from its ledger; no declared answer appears in any line.
  - `tests/presentation/httpserver/` (beside `admin_tasks_test.cpp`): a device's grant past the ceiling is refused naming the rule; a device disconnecting mid-round leaves the run going; halt and cancel from a second connection; resume of an interrupted task.
  - `cli.task_lifecycle` (`tests/scripts/sh/task_e2e.sh`): grows a driver that closes its pipe mid-run, as the probe did. Locally that still ends the run, as today. Under a served task it does not. A `task attach` from a second process then reconstructs the view `task status` prints, as the 27j driver reconstruction does today.

**Reference.**
- Siblings in this table:
  - [paired-devices.md](paired-devices.md) (59a): who may start and control a task. Only the person's paired devices.
  - [machine-mode-over-the-network.md](machine-mode-over-the-network.md) (59b, the gate): the stream transport an attach rides.
  - [reads-and-commands-over-the-network.md](reads-and-commands-over-the-network.md) (59d): the command door for run, resume, halt, cancel, status and list.
  - [many-sessions-at-once.md](many-sessions-at-once.md) (59c): adjacent. Until it lands, a task's turn waits on the server's one-inference gate as any served request does (`http-api.md:33-34`).
- Later items:
  - [../v0.2.4/device-tools.md](../v0.2.4/device-tools.md) (60b): a connected desktop's own files and shell, which a task does not use (Open calls).
  - The GUI's [7c](../../../gui/backlog/v0.2.4/every-place-against-a-server.md) is gated on this item. It is the Tasks place against a server: start, attach from its last cursor when the app opens, halt, cancel, resume.
- In-house precedents:
  - The task ledger and lock (27h).
  - The autonomy policy (27i).
  - Machine mode's task events, one per ledger transition (27j).
  - The shell's in-process run (37e, `run_task_events`).
  - ACP's sessions, each a door into the binary's own path (56a).
  - The admin jobs registry. It is the counter-example: in-process only, not persisted, cancel-wins. This item keeps cancel-wins and drops the rest.
- External prior art (Claude Code's remote sessions, push-to-resume on phones) was not checked for this document and is not relied on.

**Decisions made** (dated):
- 2026-11-07: Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item:
  - **The vision.** A private, self-hosted harness on the person's own strong machine, serving their own devices. No customer accounts, billing or metering. The server admits the person and their devices only.
  - **Remote is an add-on, never the only way.** The CLI alone, and the Mac over pipes, stay complete and the default.
  - **iPhone and iPad are clients of the server only.** The device most likely to leave mid-task is one that cannot run the task itself.
  - **Where the work happens is C.** Server tools by default, with a connected desktop offering its own files and shell.
  - **The SPEC amendment is approved and written.** `serve` carries "autonomous tasks that outlive a connection" (CLI SPEC → Core surfaces, `serve`, revised 2026-11-07).
  - **Placement.** Successive versions, with v0.3.0 the release where macOS, iPadOS and iOS (the iPhone and the foldable iPhone) fully work. Windows and Android follow v0.3.0.
- 2026-11-07: Track 59, v0.2.3, gated on 59b. An attach is a stream, and 59b is the one network stream. The item sits before v0.2.4 so that the GUI's 7c, which brings every place to a server, has tasks to bring.
- 2026-11-07: The authority is decided at start, as 27i made it, and a device never answers a task's question live, even while attached. A session answers its permission cards on the device (59b). A task is the unattended run, and keeping it that way is what lets it outlive the device.
- 2026-11-07: No push notifications in v0.3.0. When the app opens, it lists the tasks and attaches to each live or unseen one from its last cursor. Apple push needs Apple's infrastructure, and that is a later call behind the Apple Developer Program call ([8c](../../../gui/backlog/v0.2.5/iphone-and-ipad-distribution.md), consumed).

**Open calls:**
- [default: a served task runs in `serve`'s own process, on the server's backends, through `run_task_events`. It is not a `task run` child. A child would load a second copy of a large local model (llama.cpp runs in-process), and the person's server is meant to hold one finely tuned large model once. The lock, the ledger and the request file are unchanged: `serve`'s pid holds the lock while its task runs.]
- [default: one task at a time stays, under the lock (`ledger.h:99`). A second start, from any device or the host's terminal, is refused in today's words, naming the running task. A queue of tasks is a later call. 59c's concurrency is sessions, not tasks.]
- [default: a task runs on the server's tools only, never a device's. 60b's device tools belong to a connected session, and a task outlives its connection by design. A task that needs a Mac's files waits for a later call.]
- [default: locally nothing changes. A `task run` whose driver leaves still ends as today (SIGPIPE, the ledger left interrupted and resumable). Only a served run outlives its driver, because the server is the driver. Making a local run survive its pipe would change today's local behavior, and nothing asks for that.]
- [default: the log keeps every line the stream carried, deltas included, numbered from 1, for as long as the ledger is kept (no task is deleted today). A bound is a later call, made when a measured task needs one.]
- [default: `task attach` opens with `task_attached` (`task_id`, `after`, `status`, `live`, and the task's view), then the replayed lines, each with `seq`, then the live ones. A cursor past the end is refused, naming the last `seq`. A task run before this item has no log. Its attach carries the view and its ledger history and replays nothing.]
- [default: every paired device is the person's, so any of them may attach to, halt, cancel or resume any task. A per-device owner adds nothing when every device belongs to one person (59a admits only them).]
- [default: a stopping `serve` cancels its running task through the loop's cancellation, as SIGTERM does (57g), and the ledger says so. Any device resumes it once the server is back.]
- (consumed decision) A served task's folder, and the server tools it has, take 59b's answer for a served session. The pairing and its credential are 59a's. The command and stream frames are 59d's and 59b's.

No **[user]** call blocks this item.

**Guardrail(s).**
- The log equals the stream, line for line, pinned in the CLI test and in `cli.task_lifecycle`.
- A driver closing its pipe:
  - ends a local run, unchanged;
  - leaves a served run going.
- `attach --after N` gives exactly lines N+1 onward. A reconstruction from attach equals `task status --output-format json` at the end, as 27j's does from the live stream.
- A device's grant is held to the server's ceiling, and a question with no declared answer ends the task with a device attached.
- No declared answer, secret or private-layout path appears in any log line, attach or frame.
- `cli.machine_schema_conformance` pins `task_attached` and `seq`.
- With no server configured or running, the local task suite passes unchanged.

**Acceptance criteria:**
- [ ] A task started over a paired connection keeps running after that connection closes mid-round, and finishes on the server (`task status` there shows its rounds advancing and its end).
- [ ] A second connection, or the first reconnected, attaches with a cursor, receives exactly what it missed and then the live stream, and ends at `task_finished`. Its reconstruction equals the task's final view.
- [ ] Halt, cancel and resume from a paired device each take effect in the CLI's words. A start beyond the server's `permissions:` is refused naming the rule.
- [ ] On a host with no server, `task run`'s stdout is byte for byte today's, `task attach` follows a task from its log, and the full suite is green.
- [ ] Optional live pass, said when skipped: `apogee-dev serve` on the person's machine, a scripted client starting a task, closing, reattaching and halting. Never the release `apogee`.

**Scope note.** Earmarked for v0.2.3 (track 59); gated on 59b. The GUI's [7c](../../../gui/backlog/v0.2.4/every-place-against-a-server.md) consumes it, and it is part of the road to [v0.3.0](../../../gui/backlog/v0.3.0/three-apple-platforms-whole.md). Out of scope:
- push notifications (a later call behind 8c);
- a queue of several tasks at once;
- device tools inside a task;
- a local `task run` that survives its driver;
- a bound on the log;
- anything paid: no accounts, billing or metering.
