# The task runner and its ledger

**What / why.** Today a goal that takes several model turns takes a person at the keyboard between them. `apogee task run "<goal>"` removes the person: the application plans first, drives successive turns of the one shared agent loop, checks each round's result against acceptance the user stated up front, composes a corrective follow-up turn when a check fails, and stops on done or on budget — the user states what they want, the application does the rest. The shape was proven in a context-only spike (2026-09-25, at the user's direction — its findings live in these documents, not in a repo record): an outer loop over the shipped binary completed exactly this cycle — one goal in, application-composed turns, a deterministic acceptance check catching an incomplete round, one persisted conversation out. The spike also found the two reasons this belongs *in* the binary with its own state: an external driver sees only result text and status prose, so it cannot verify tool work it did not do itself; and the task's own state — plan, round count, which checks have passed — lived only in the driving process, so a restart lost the task even though the chat survived. Hence the runner in the core, over a **task ledger** that makes a task as resumable as the session it drives.

**Core constraint(s).**
- **An outer loop; the agent loop is untouched.** The per-turn bound (`max_iterations = 12`, the final-pass flag) and the half-turn rollback discipline stand exactly as they are; the runner decides *whether to open another turn*, never how a turn runs. One source of truth per concern.
- **Bounded by construction.** A round budget is not optional and has no off switch — an unbounded autonomous loop must be unrepresentable, not discouraged. The stop set: done, budget exhausted, the breaker (no progress), an unrecoverable error, or an explicit halt.
- **Deny-by-default survives autonomy.** An unattended run has nobody to ask, so `ask`-level destructive tools deny and the denial is a tool result the model reads — the existing rule, byte-for-byte. *Widening it is [28b](task-autonomy-policy.md)'s business and must never happen as a side effect here.* Likewise a model question with nobody present fails the task cleanly in this item; answering policies are 28b.
- **A task rides an ordinary chat session** — persisted per turn, compacted when long, resumable — and the ledger references the session, never copies the transcript. `chats delete` on a task's session is refused while its task is live.
- **The ledger is rewritten on every transition** (the pipeline-manifest pattern, Milestone Z), lives under a `tasks/` layout row declared in `harness/layout.h` in the same commit (the parity rule), and one task runs at a time under a `cycle`-style PID lock.
- **Never a daemon, never a socket.** A task is a user- or scheduler-invoked process that exits when it stops — the training cycle's rule, adopted whole.

**Seam + files.**
- `tasks/` (new guarded package — may include `agentloop/`, `agent/`, `harness/`, `logger/`, `platform/` and itself; never a backend or `commands/`; joins `tests/layering.cmake`): `tasks/task.h/.cpp` (the record: goal, acceptance checks, budgets, plan, per-round outcomes, status, session id — and its transitions), `tasks/runner.h/.cpp` (the outer loop: plan turn → execute turn → check → corrective turn; generation arrives through the loop's existing seams), `tasks/ledger.h/.cpp` (manifest-per-transition, the lock).
- `commands/task_cmd.h/.cpp`: `apogee task run|status|list|resume|halt|cancel`; `run` takes the goal plus repeatable acceptance flags; progress on the status line through the existing Reporter.
- `harness/layout.h`: the `tasks/` row. `logger/`: the session linkage.
- Tests: `tests/tasks/` mirroring the package (transitions table-tested against a scripted mock); an e2e over the real binary — goal → plan → incomplete round → corrective round → done — plus a kill-and-resume check.

**Reference (Ommi).** No analog for a goal-driven task runner. The nearest shapes are in-house and Ommi-derived: the **training cycle** (Milestone Z) — scheduler-invoked autonomous work under a PID lock, a circuit breaker, `halt`/`resume` replacing hand-edited state — and the **pipeline** — staged work, the manifest rewritten on every transition, resume from the first unpassed stage. Precedents to reuse, not ports.

**Decisions made** (dated):
- 2026-09-25 — Asked for by the user, targeted **v0.1.5**: the user states the goal; the application composes and drives every turn after it.
- 2026-09-25 — In-binary, with a ledger (the spike's two findings above); the CLI is the contract, and [28c](task-surfaces.md) carries it to the other surfaces.
- 2026-09-25 — **Plan-first as an ordinary turn**, not a mode: the plan is the first application-composed turn's answer, recorded in the ledger and quoted into later turns — so the whole task is readable in its session transcript afterwards.

**Open calls:**
- [default: first-cut acceptance = checks stated on the command line — `--require "<text>"` (answer must contain) and `--require-file <path>` (must exist), repeatable — plus the model's own done/not-done self-report; a judge-model check joins later under the never-fail contract] What "done" means mechanically.
- [default: 8 rounds; token spend recorded per round but not enforced in the first cut; no wall-clock cap] Budget defaults.
- [default: the breaker trips after two consecutive rounds with no check newly passing and no new tool activity — the "spinning" signature] When no-progress ends a task.
- [default: `task cancel` on a live task = the lock holder finishes its in-flight turn via the loop's cancellation and records `cancelled`; `resume` on a cancelled task is allowed] Cancel semantics.

**Guardrail(s).**
- The scripted-mock e2e asserts the full cycle and the ledger's transition sequence, including the corrective round.
- Kill the process mid-round; `task resume` completes the task from the ledger, and the conversation is one session with no duplicated turns.
- Budget exhaustion and the breaker each end with an honest status naming the unpassed checks — never a claimed success.
- An unattended destructive tool call denies, is recorded in the ledger, and the turn continues (denial-as-tool-result, asserted).
- `harness.layering` covers `tasks/`; the lock refuses a second concurrent `task run`.

**Acceptance criteria:**
- [ ] `apogee task run "<goal>" --require …` completes a multi-round task against a scripted mock with no input after the command returns are pressed — plan recorded, an incomplete round corrected, checks passing, exit 0.
- [ ] `task status` shows the plan, rounds used, per-check state, and the session id; `task list` shows past tasks with outcomes.
- [ ] A task killed mid-round resumes from the ledger and finishes; a task that cannot pass its checks stops at budget saying exactly which checks failed.
- [ ] A destructive tool call inside an unattended task denies and the task records it; nothing prompts.
- [ ] A second `task run` while one is live is refused by the lock, naming the running task.

**Scope note.** Item **28a**, earmarked for **v0.1.5**; gated on nothing pending. Out of scope: per-task permission grants and question policies ([28b](task-autonomy-policy.md)); machine-mode events, JSON reads and admin views ([28c](task-surfaces.md)); schedules and queues (a task is invoked, like the cycle — a queue waits for demand); sub-tasks spawning tasks.
