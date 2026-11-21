# CLI-63e · Tasks per person: a queue, not a refusal

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | `task` commands · `business/tasks` · `serve`'s served task |
| **Labels** | `tasks` `people-and-access` `task-queue` `serve` |
| **Epic** | Track 63 · people and access |
| **Blocked by** | [63a](people-on-one-server.md) |
| **Blocks** | — |
| **Reporter** | The admin-mode and people spike, 2026-11-14 (wall P6) |
| **Created** | 2026-11-14 |

---

## Summary

Today an install runs **one task at a time**, for everyone, so once members can start tasks from their devices one person's long autonomous goal shuts every other person out, and the owner at their own terminal is refused because of a member's task. This item replaces the install-wide lock with **task slots**: the owner keeps a slot no member can take, members share a pool the owner sizes, a task with no free slot is **queued** and told its place in line, `serve` starts waiting tasks fairly between people, and every ledger records **the person** who started it.

## Description

### User story

**As** a person using Apogee on a server shared by a household or a small team (the owner or a member),
**I want** my task to wait for a free slot and tell me its place in line, rather than be refused because someone else's task is running,
**so that** one person's long task never shuts the others out, and the owner's own terminal task never waits behind a member's.

### Background

The user's calls, 2026-11-14 (the admin-mode and people spike): one server serves **people**, the owner and members in a household or a small team, and each member has their own tasks ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4, *Revised again 2026-11-14*). Today an install runs **one task at a time**, for everyone. Once members can start tasks from their devices, one person's long autonomous goal shuts every other person out until it ends, and the owner at their own terminal is refused because of a member's task. The user's default: **a second person's task waits; it is not refused.**

This item replaces the install-wide lock with **task slots**. The owner always has a slot of their own that no member can take. Members share a pool whose size the owner sets, each member capped at a few running tasks at once. A task that finds no free slot is **queued** and told its place in line, and `serve` starts it when a slot frees, choosing the next fairly between people. Every ledger records **the person** who started it.

### Evidence

**P6 — one task at a time per install.** Re-verified at HEAD (`4acf4a2b`; no CLI source change since the probe binary `b37b0a9f`) on 2026-11-14, with the branch binary in a scratch `APOGEE_HOME` and the `mock` backend:

1. **One lock, one running task.** `tasks/task.lock` is a PID lock that one running task holds (`business/tasks/ledger.h:14-24`, `:29`; "One task runs at a time", `:120-121`). The layout row says so too: "task ledgers … and the one-task lock" (`data/contracts/layout.cpp:56`). A second start is refused by name (`business/tasks/ledger.cpp:298-300`). Probed: with the lock held by a live process naming task A, `task run "carol's goal"` and `task resume <B>` were both refused with `task task-20261114-200426 is running (process 79485) -- one task runs at a time. 'apogee task status …' shows it; 'apogee task halt' or 'apogee task cancel' stops it`.
2. **The lock does a second job: it is the ledger's only writer.** `task run` takes it "first, so nothing else writes the ledger this run is about to read" (`presentation/cli/task_cmd.cpp:586-592`). The stop path writes a stopped task's ledger under the same lock (`:1501-1505`). 59e's event log is written "only by the process holding the task lock" ([`tasks-outlive-the-connection.md`](../v0.2.3/tasks-outlive-the-connection.md) (Technical approach, `event_log.h/.cpp`: one writer, the process holding the task lock)). Several running tasks therefore need a writer lock per task, not one per install.
3. **Everything that reads "running" assumes one runner.** `lock_holder` and `running_task` return one holder (`ledger.h:115`, `:118`), read at 14 call sites in 5 files (`task_cmd.cpp` 8, `tui_task.cpp` 2, `admin_tasks.cpp` 2, `complete_sources.cpp` 1, `ledger.cpp` 1). A task's liveness is "the one holder names it" (`business/tasks/view.cpp:34-44`). Probed: with two ledgers marked `running` and the lock naming A, `task list --output-format json` showed B as `interrupted: true`, so a second runner would look abandoned and invite a second `task resume` of a task that is already running. `task halt` with no id picked A, "the running one" (`task_cmd.cpp:351-352`).
4. **No ledger names who started it.** The probe ledger's 17 keys (`checks, created_at, goal, id, no_progress, plan, policy, reason, rounds, rounds_budget, schema_version, session_id, status, tools, transitions, updated_at, working_directory`) and its policy's 2 (`grants, on_question`) carry no starter. The ledger's only "person" is the anonymous authority word for the person at the terminal (`business/tasks/task.h:155-157`). 59f adds `started_by` on the policy, naming the credential and read as "the person" when absent ([`paired-apps-with-scopes.md`](../v0.2.3/paired-apps-with-scopes.md) (Technical approach, 59e's served task: the policy records `started_by`, read as "the person" when absent)).
5. **The queued design keeps the single lock on purpose.** 59e: "one task at a time stays … A second start, from any device or the host's terminal, is refused … A queue of tasks is a later call" ([`tasks-outlive-the-connection.md`](../v0.2.3/tasks-outlive-the-connection.md) (Open questions: one task at a time stays, a queue of tasks is a later call), and out of scope there (Out of scope: a queue of several tasks at once)). 59c consumes it: "One served task at a time", and fairness is round robin by caller with no quotas ([`many-sessions-at-once.md`](../v0.2.3/many-sessions-at-once.md) (Open questions: fairness is round robin by caller), and (Open questions, consumed decision 59e: one served task at a time)). This item is that later call.

## Acceptance criteria

- [ ] **Given** a sandbox home with no members, **when** `apogee task run` is given while another task runs, **then** it is refused in today's words, and every task command's output is unchanged.
- [ ] **Given** 63a's owner and two members, `tasks.member_slots: 1` and the mock backend, **when** both members start a task over the harness plane, **then** the second is `queued` with `ahead: 0`, gets `task_queued`, and starts on its own when the first ends, with `task_started` following.
- [ ] **Given** both members' tasks hold or wait for the member slot, **when** the owner gives `apogee task run` at the terminal, **then** it starts at once.
- [ ] **Given** one member queues three tasks and the other one, **when** a member slot frees, **then** the other's starts next.
- [ ] **Given** the server is running with a task waiting, **when** `apogee config set tasks.member_slots 2` is given, **then** the waiting task starts without a restart (62b); **and when** the limit is lowered, **then** nothing that runs is stopped.
- [ ] **Given** any new task, **when** its ledger is written, **then** it records `started_by` (the person) and, when served, `started_via` (the credential); `task status` shows the person, and an older ledger reads as the owner's.
- [ ] **Given** two running tasks, **when** `task list` is given, **then** both show as running, neither `interrupted`; **and when** `task halt` is given with no id, **then** it lists them instead of picking one.
- [ ] **Given** a task is queued, **when** `serve` is stopped and started again, **then** it runs; **and given** a queued task is cancelled, **then** it never runs.
- [ ] The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: `apogee-dev serve` on the dev channel with two people's paired test devices, each starting a mock task with `member_slots: 1`, the second shown waiting and then running. Never the release `apogee`.

## Constraints

- **Anti-goal: nothing here makes standalone use need a server or an account.** On an install with no members, which is every install until 63a's `people add`, `task run`, `resume`, `halt`, `cancel`, `status` and `list` behave as they do today, byte for byte: one task at a time, the owner's slot is today's `tasks/task.lock`, and a second start is refused in today's words. A queue exists only where `serve` runs, because only a long-lived process can start a waiting task later. The CLI stays "never a daemon" (SPEC → Autonomous tasks). The owner's terminal task never waits behind a member's: it uses the owner's own slot.
- **One task path, opened by its command's own door** ([ADR 0011](../../../adrs/cli/one-session-opening.md)). A queued task starts later through the same `run_task_events` 59e's served task uses (`task_cmd.cpp:1427`). There is no second runner. A stop still goes through `request_task_stop` (`:1460`), the one stop path.
- **One config mutation path** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One config mutation path). The slot limits are config keys, written only through the one editor (`apogee config set`, the admin plane's config routes and the server window, all through it). 62b's reload applies them to a running server.
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). Additive only: a new status `queued`, new transitions, new keys `started_via` and `queued_at`, a new event `task_queued`, new config keys with today's behavior as their defaults. A ledger with none of them reads as today's and as the owner's. The one repurposed key (`started_by`, credential to person) is a deliberate, dated revision with its migration stated (Decision log).
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). The queue is visible the same way from the CLI (`task list`, `task status`, `--output-format json`), machine mode (`task_queued`), the admin plane's read-only task routes, the shell's Task view and 59d's command door. The owner's server-wide view of slots and queue is 62a's status record, read over pipes at the server Mac, or from a paired device the owner marked to administer (62g).
- **Privacy between people** (the user's call: the owner sees metadata, never contents). A member's place in line says how many tasks are ahead, never whose or what. The owner's view of slots and the queue shows person, task id, status and times, never the goal, plan or checks (a member's own words).
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The slot locks and the ledger fields sit in `business/tasks` beside today's lock. The fair order is a pure Business function. The dispatcher that starts a waiting task sits in `httpserver` with 59e's served task. No new module.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). `started_via` names a credential row, never a token or hash. No person id or task goal goes into a path or a query string.
- **Serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). The queue is served by `serve` alone. `task run` at a terminal opens no socket and never talks to a local server.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Any live pass runs `apogee-dev` over `~/.apogee-dev` and never the release `apogee` or `~/.apogee`.

## Technical approach

| File | Change |
|---|---|
| `source/business/tasks/ledger.h/.cpp` | The lock splits in two: a writer lock per task and slot locks (detail below). `TaskSlots::acquire(root, task_id, person, limits)` returns a held slot, `Wait` (with the reason), or `Refuse` (with today's words for the owner at a terminal). `lock_holder`/`running_task` become `running_tasks(root)`: every live slot holder with its task and person. The single-holder forms are kept for the no-members case so the 14 call sites move one at a time. |
| `source/business/tasks/queue.h/.cpp` (new) | `next_to_start(waiting, running, limits)` and `place_in_line(id, waiting, running, limits)`, both pure functions of ledgers and limits. Every case is table-tested with no clock and no files. |
| `source/business/tasks/task.h/.cpp` | Status `queued` (resumable, never interrupted). Transitions `queued` and `dequeued`. Fields `started_by` (the person; absent means the owner), `started_via` (the credential row; absent for a terminal or a pipe) and `queued_at`. `view.cpp`'s `liveness` reads `running_tasks`, so a second runner is never shown `interrupted`. `TaskView` gains `person`, `queued`, `ahead`. |
| `source/data/contracts/config.h/.cpp` + `config_edit` | A `tasks` section, `owner_slots`, `member_slots`, `per_member`, `queue_per_member` (defaults under Open questions), absent-safe. `check` warns on a value that can never run a member's task (`member_slots: 0` with members present) and says that it means members' tasks are refused. |
| `source/data/contracts/layout.cpp:56` | The row's description says "the task slots' locks" in place of "the one-task lock". |
| `source/presentation/cli/task_cmd.cpp` | `run` and `resume` take a slot, not the lock; `status` and `list` show `queued` with the place in line; `halt` and `cancel` take a queued task out of the line (detail below). |
| `source/presentation/httpserver/` (beside 59e's served task) | The **dispatcher** (detail below). |
| `source/presentation/httpserver/admin_tasks.cpp` | Rows carry `person`, `queued` and `ahead` (additive). The owner's slots-and-queue view is added to 62a's runtime record: slots in use by person, and queue length per person, with no goals. |
| `source/presentation/machine/` (`protocol.cpp`, `json_reporter.h/.cpp`) and `machine-schema.json` | The event `task_queued` (`task_id`, `ahead`, `queued_at`), emitted before `task_started` for a task that waited. |
| `source/presentation/cli/tui_task.cpp` | The Task view lists every running task and shows a queued one's place. |

**The two locks in `ledger.h/.cpp`.**
- **A writer lock per task**, `tasks/<id>/run.lock` (a `platform::PidLock`, `infrastructure/platform/pid_lock.h`). It is held by the process running or writing that task, and it takes over the lock's second job (Evidence 2) and 59e's one-writer rule for the event log. A second `task resume` of a running task is refused by it, naming the holder.
- **Slot locks.** The owner's first slot is `tasks/task.lock`, unchanged in path and format, so an install with no members, and an older binary at the same terminal, behave as today. Every other slot is `tasks/slots/<owner|member>-<n>.lock`.

**`task_cmd.cpp`.**
- `run` and `resume` take a slot, not the lock. At a terminal the owner's slot is the only one tried, and a busy slot is refused in today's words.
- `status` and `list` show `queued` with the place in line: `queued -- 2 tasks ahead of it; it starts when a slot frees (the server runs it)`.
- `halt` and `cancel` take a queued task out of the line. With no id and several running they refuse and list them, in place of picking one.

**The dispatcher, in `httpserver/`.**
- A served start or resume that gets `Wait` writes the ledger `queued` and emits `task_queued`.
- When a slot frees, and when `serve` starts, the dispatcher takes `next_to_start`. It takes that task's writer lock, re-reads its ledger, and starts it through `run_task_events` only if it still says `queued`.
- A position change is re-emitted as `task_queued` into the task's event log (59e), so an attached device sees it move.
- 62b's reload re-reads the limits. A raised limit starts the next waiting task. A lowered limit stops nothing that runs and only holds back new starts.

**Reference.**
- In-house precedents:
  - The task ledger and lock (27h), the authority words (27i), the stop kept while a run starts (57u, [machine-mode.md](../../../reference/cli/machine-mode.md):362-370).
  - 59c's round robin by caller ([`many-sessions-at-once.md`](../v0.2.3/many-sessions-at-once.md) (Open questions: fairness is round robin by caller)). The fair order here is the same idea one level up: by person, between tasks, where 59c's is by caller, between model calls. Running tasks remain callers on 59c's lanes.
  - 59f's `AppScope` `tasks` kinds and `task_refusal` ([`paired-apps-with-scopes.md`](../v0.2.3/paired-apps-with-scopes.md) (Technical approach, `devices.h/.cpp`: `AppScope`'s `tasks` kinds), and (Technical approach: `task_refusal(request, scope)`)). Whether a person may start a kind at all is decided there, before a slot is asked for.
- Siblings:
  - [people-on-one-server.md](people-on-one-server.md) (63a): who the owner and members are, every credential bound to a person, the presets. This item's gate.
  - [personal-data-has-an-owner.md](personal-data-has-an-owner.md) (63b): who may list, read, attach to, halt, cancel and resume a task, and where a member's ledgers live.
  - [allowances-keys-and-usage.md](allowances-keys-and-usage.md) (63c): what each person may use, and usage per person (a running task's tokens count to its `started_by`).
  - [consent-goes-to-its-person.md](consent-goes-to-its-person.md) (63d): the authority words gain the person. `answered_by` is 63d's.
  - [serve-status-and-runtime-record.md](serve-status-and-runtime-record.md) (62a): the record the owner's slots-and-queue view joins.
  - [serve-reload-and-served-vs-disk.md](serve-reload-and-served-vs-disk.md) (62b): limits applied live.
  - [audit-log.md](audit-log.md) (62f): the owner stopping a member's task is an audited act.
  - [serve-as-a-background-service.md](serve-as-a-background-service.md) (62d): with the server as a service, a queued task does not wait for someone to start `serve`.
  - [../v0.2.3/tasks-outlive-the-connection.md](../v0.2.3/tasks-outlive-the-connection.md) (59e): the served task and its event log, whose "one task at a time" this revises.
  - GUI [7c](../../darwin/v0.2.4/every-place-against-a-server.md) (the app's Tasks screen against a server shows `queued` and the place in line) and darwin [10c](../../darwin/macos/v0.2.4/people-screens.md) (the owner sets the limits in the server window).
- Prior art: CI runners and print queues hold a waiting job and say its place rather than refuse it. Plex and Jellyfin cap concurrent streams per user and in total, the same two numbers as `per_member` and `member_slots`. Neither reserves capacity for the owner. This item does, because the owner's machine is also their own workstation.

**Docs when it ships:** [machine-mode.md](../../../reference/cli/machine-mode.md) → A task's run: `task_queued`, `started_by`/`started_via`, "a task may wait for a slot when served". [http-api.md](../../../reference/cli/http-api.md) → `GET /v1/admin/tasks`: the new row fields, and a dated revision of "decided … from the same lock" (`:1430`). The commands reference: `task` and the `tasks.*` keys. CLI CLAUDE.md's codebase map: the slot and writer locks.

## Testing

Hermetic: a temp `APOGEE_HOME`, the mock backend, a fake clock where order matters, handlers through `Mux::dispatch` with no port wherever a port is not the point.

- **`tests/business/tasks/queue_test.cpp`** — the fair-order table (Open questions), the owner never behind a member, `per_member` and `queue_per_member`, places in line after a cancel, a raised and a lowered limit.
- **`tests/business/tasks/ledger_test.cpp`** — slot acquire and release, a stale slot taken over, the writer lock refusing a second resume, `running_tasks` with three holders, and today's refusal words byte for byte with no members.
- **`tests/business/tasks/task_test.cpp`** — `queued` round trip, a ledger with no `started_by` read as the owner's, a 59f-era ledger (`started_by` with no `started_via`) read through the registry to its person.
- **`tests/presentation/httpserver/`** (beside 59e's) — two members and the owner start tasks over the harness plane with `member_slots: 1`. The second member's task is `queued` with `ahead: 0` and starts when the first ends. The owner's starts at once. A queued task cancelled never starts. `serve` restarted with a task queued starts it.
- **`tests/presentation/cli/task_test.cpp`** — the no-members golden (`run`, a refused second `run`, `list`, `status`) unchanged byte for byte.

Guardrails:

- **The no-members golden.** `task run`, a refused second `run`, `list --output-format json` and `status` in a sandbox home are byte-identical to today's. The probe's refusal line (Evidence 1) is pinned word for word. This is the anti-goal's test.
- **The spike's probe as a regression case** in `ledger_test.cpp`: two ledgers `running`, each held by its own slot, with neither shown `interrupted` and a `task resume` of either refused by its writer lock.
- **Starvation-freedom.** `queue_test.cpp`'s fair-order table pins it. With the owner and two members, one member queuing 10 and the other 1, the second member's task starts within one free slot, and the owner's start is never `Wait` while an owner slot is free.
- **The privacy case.** A member's `task_queued`, `task status` and `GET /v1/admin/tasks` row for their own task carry no other person's id or goal, and the owner's slots-and-queue view carries no goal.
- **`cli.machine_schema_conformance`** pins `task_queued` as additive. A driver that ignores it sees today's stream.
- **The 59f-era ledger fixture** (credential in `started_by`, no `started_via`) reads to the right person, and a ledger with neither reads as the owner's.
- **The leak test** gains a distinctive goal on a member's queued task and finds it in no owner-facing listing, log line or audit row.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What are the task limits and their defaults? | `[default]` | `tasks.owner_slots: 1`, `tasks.member_slots: 1`, `tasks.per_member: 1`, `tasks.queue_per_member: 10`. With no members, only `owner_slots` applies, and at 1 it is today's install. A start past `queue_per_member` is refused naming the key. `member_slots: 0` refuses members' tasks with words, rather than queuing them forever. All are set through the one editor and applied by 62b's reload. |
| What is the fair order: who gets a free slot? | `[default]` | A free member slot goes to the member, among those with a waiting task and below `per_member`, whose last task **started** longest ago, never-started first, ties to the earlier `queued_at`. Within one person, first in first out. One person queuing ten tasks therefore never holds back another's one. The owner's waiting served tasks use the owner's slots only, in their own first-in-first-out order. |
| May the owner's tasks borrow a free member slot, or members the owner's? | `[default]` | No: the owner's tasks never borrow a free member slot, and members never borrow the owner's. Borrowing would let a burst of the owner's tasks fill the members' pool just before a member asks. The owner who wants more raises `owner_slots`. |
| How is the place in line told? | `[default]` | **As a count, never a name**: `ahead` is how many tasks start before this one by `place_in_line` under the current limits. Text says `queued -- 2 tasks ahead of it`, and 0 says `next`. It is re-told as a new `task_queued` whenever it changes (a cancel, a raised limit, a finished task). |
| When is a queued task's ledger written, and what do halt, cancel and resume do to a queued task? | `[default]` | A queued task's ledger is written at once, with status `queued`, so `task list` and the app show it and a restart of `serve` keeps it. The dispatcher re-reads it under the task's writer lock and starts it only if it still says `queued`. A halt or a cancel takes it out of line (`halted` or `cancelled`, zero rounds, resumable), and a resume puts it back at the end of the person's line. |
| What happens to waiting and running tasks when `serve` stops? | `[default]` | A `serve` stopped with tasks waiting leaves them `queued`, and the next `serve` starts them in order. A running served task is cancelled on stop as 59e decided, and is resumable. `task list` at the terminal says a queued task "waits for the server". |
| What may the owner do to a member's task? | `[default]` | The owner may halt or cancel a member's running or queued task (an admin act on capacity, audited by 62f) but never read its goal, plan or rounds. Every other control of a task is 63b's. |
| What do `task halt` and `task cancel` with no id do when several tasks run? | `[default]` | With several tasks running, they refuse and list the running tasks the caller may stop, in place of guessing. With one running, they act on it as today. |
| Can a member's per-person cap differ by person? | `[default]` | No: a member's own cap per person is the config's `per_member` for everyone. A per-person override in the allowance is a later call. The presets (Full, Family, Limited) are 63a's and 63c's, and whether Limited may run tasks at all is the allowance's `tasks` kinds. |
| Who may see and control a task, and do a member's ledgers live under a per-person root or carry an owner column? | `(consumed decision)` | [63b](personal-data-has-an-owner.md)'s. If they move under a per-person root, an older binary at the server's terminal never sees a member's task. If they stay in `tasks/`, the known limitation under Out of scope applies. |
| What is a person id, and is the owner the local terminal and pipe? | `(consumed decision)` | [63a](people-on-one-server.md)'s. |
| Who answers a question raised inside a running task? | `(consumed decision)` | [63d](consent-goes-to-its-person.md)'s. A task's authority is still decided at start (59e). 63d takes halt and cancel from the task's person by `started_by`, which it reads as "a person since 63a". Neither 63a nor 63d writes that change; this item does. Whichever of 63d and this item builds first carries the `started_by` revision and its migration as decided in the Decision log, and the other reads it. |

No `[user]` call blocks this item. The user's one call for it, "a queue, not a refusal", is recorded in the Decision log.

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the admin-mode and people spike (wall P6). The user's calls from it follow. | — |
| 2026-11-14 | The server serves **people**: an owner (the OS account that owns `APOGEE_HOME` on the server Mac) and members, in a household or a small team. The words are **people** and **access**, never "roles". | the user |
| 2026-11-14 | The owner sees members' metadata (devices, apps, activity, which model, when), **never** chat contents. The same holds for a task's goal, plan and checks, which are a member's own words. Whoever controls the server Mac can still read its disk, and the docs say so honestly. | the user |
| 2026-11-14 | **A per-person task queue, not refusal**: a second person's task waits. | the user |
| 2026-11-14 | Each member has an **allowance** in 59f's `AppScope` shape, set from the presets **Full**, **Family** and **Limited** in place of groups. A guest is a member on Limited with nothing kept (63a). Whether a person may start a task at all is the allowance's `tasks` kinds. | the user |
| 2026-11-14 | Members use the server's **local models only**, unless the owner grants a household cloud key with a limit. A running task's model calls draw on its person's allowance and usage (63c). | the user |
| 2026-11-14 | Live changes are applied on the spot (reload), with what still needs a restart said. | the user |
| 2026-11-14 | An audit is kept 90 days: who, what and when, never request contents (62f). The owner stopping a member's task is one of its rows. | the user |
| 2026-11-14 | Administering is **local by default**: the server is administered at its own Mac through the local CLI over pipes, plus paired devices the owner marks as allowed to administer remotely (62g, revising 59d's one admin token for any network caller). The task limits are set there. | the user |
| 2026-11-14 | The admin mode is a separate **Apogee Server window** in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes. The owner sets the task limits in it (darwin 10c). The window installs the server as a **background service** (launchd) and starts, stops and restarts it (62d). | the user |
| 2026-11-14 | Remote use is an add-on. The CLI alone and the Mac app over pipes stay complete and need no server. | the user |
| 2026-11-14 | Placement, both before v0.2.5 and both in v0.2.4: the server console (CLI track 62, darwin 10a-10b), people and access (CLI track 63, darwin 10c). Nothing already queued is renumbered. | the user |
| 2026-11-14 | The SPEC amendments are written: CLI SPEC → Core surfaces 4, *Revised again 2026-11-14*; GUI SPEC → Core surfaces 3, the server window. | the user |
| 2026-11-14 | **The owner's tasks never starve, by a reserved slot, not by priority.** The owner always has `owner_slots` slots that no member can take. Members share `member_slots`. A member's long task can therefore never refuse or delay the owner's terminal `task run`, which is standalone use and must not depend on the server's members. Priority alone would still leave the owner waiting behind a member's task that is already running. | the spec |
| 2026-11-14 | **A queue only where `serve` runs.** A queued task must be started later by a living process, and the CLI is never a daemon. At a terminal the owner's busy slot refuses as today. A member never sits at the server's terminal: the local terminal and pipe are the owner (63a). | the spec |
| 2026-11-14 | **`started_by` is the person.** It is written as the person's id, absent read as the owner, with the credential that carried the start in `started_via`. This repurposes 59f's key, which names the credential. Under ADR 0006 it is a dated revision in [machine-mode.md](../../../reference/cli/machine-mode.md) and the release notes, with its migration stated: a ledger holding `started_by` and no `started_via` is a 59f-era ledger, and its value is a credential name, resolved through the registry to that row's person (a row from before 63a is the owner's). 59f's "an app lists only its own" keys on `started_via` from then on. | the spec |
| 2026-11-14 | Placed in v0.2.4, track 63, after 63d, gated on 63a only: it needs a person on every credential and nothing else from the track. 63b's filters and 63c's usage read the `started_by` it writes, but neither blocks it. | the spec |
| 2026-11-18 | Moved here from v0.2.7, when the releases were merged; the id is unchanged. | the user |

## Out of scope

- Who may see and control a task, and where members' ledgers live (63b).
- Usage and limits on what a task may spend (63c).
- Answers to questions inside a task (63d).
- Priorities, deadlines or scheduled start times.
- A per-person override of `per_member` in the allowance.
- Queuing at a terminal with no server.
- Device tools inside a task (59e's out of scope stays).
- The server window's controls for the limits (darwin 10c).
- **Known limitation**, for the release notes: if 63b leaves members' ledgers in `tasks/`, an older binary run at the server's terminal while `serve` runs members' tasks sees only `tasks/task.lock`, and could resume a member's running task a second time. Downgrading while serving members is unsupported, and the release notes say so, dated.

## Links

- **Blocked by:** [63a](people-on-one-server.md) · **Blocks:** —
- **Revises:** 59e's "one task at a time" ([tasks-outlive-the-connection.md](../v0.2.3/tasks-outlive-the-connection.md)); 59f's `started_by`, credential to person ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md))
- **Relates to:** [63b](personal-data-has-an-owner.md), [63c](allowances-keys-and-usage.md), [63d](consent-goes-to-its-person.md), [62a](serve-status-and-runtime-record.md), [62b](serve-reload-and-served-vs-disk.md), [62d](serve-as-a-background-service.md), [62f](audit-log.md), 62g, 59c ([many-sessions-at-once.md](../v0.2.3/many-sessions-at-once.md)), 59d, 27h, 27i, 57u; GUI [7c](../../darwin/v0.2.4/every-place-against-a-server.md), darwin [10c](../../darwin/macos/v0.2.4/people-screens.md); [SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4; ADRs [0001](../../../adrs/cli/layer-enforcement.md), [0002](../../../adrs/cli/mode-parity.md), [0006](../../../adrs/cli/backwards-compatibility.md), [0008](../../../adrs/cli/module-map.md), [0010](../../../adrs/cli/the-shell-is-a-mode.md), [0011](../../../adrs/cli/one-session-opening.md)
- **Follow-on:** a per-person override of `per_member` in the allowance (a later call)
