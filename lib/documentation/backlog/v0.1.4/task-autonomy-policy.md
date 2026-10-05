# Task autonomy policy: questions and permissions with nobody present

**What / why.** [27h](task-runner-core.md) ships safe and therefore narrow: an unattended task fails cleanly when the model asks a question, and every `ask`-level destructive tool denies. That makes whole classes of useful tasks impossible on purpose — a task that must write files, run a build, or make a judgment call needs the user's authority granted **before** the run, since nobody is present during it. This item is that grant, made explicit and auditable: a **per-task question policy** (`--on-question fail` — the 27h default — or `--on-question answer:"<declared answer>"`), and **per-task permission grants** (`--allow <tool>`, repeatable) that widen the gate for this task only. The hazard this item exists to avoid is the rubber stamp: an orchestrator that auto-answers "yes" to every prompt re-creates exactly the hole the permission gate closes. Policy is therefore *declared up front, scoped to the task, and recorded per use* — never answered live by the machine on the user's behalf.

**Core constraint(s).**
- **The gate's semantics do not change.** A denial is still a tool result the model reads; a grant merely resolves `ask` to `allow` for the named tool, inside this task's lifetime. No new answer kind, no bypass path, and `ask`-resolves-to-deny remains the unattended default everywhere else.
- **Grants are per-tool and explicit** — there is no `--allow-all`, and its absence is deliberate and permanent in this item. A grant names one tool; broad authority requires typing each tool's name.
- **Every exercised grant and every auto-answered question is recorded** in the task's ledger and visible in `task status`: which tool, which target, which declared answer — the audit trail is the price of autonomy.
- **The per-agent tool policy (Milestone X) is the shape to reuse, not duplicate:** agents already carry a tool policy enforced as a filter over the registry. A task that runs as an agent inherits that policy; task grants compose with it and can only be *narrower* than what the agent's policy and the config's `permissions:` allow together — a task must not be a way to exceed either.
- **Secrets hygiene:** a declared answer is stored in the ledger like any other task field — so the docs and `--help` say plainly that credentials do not belong in one, and the leak-test sweep covers task records.

**Seam + files.**
- `tasks/policy.h/.cpp` (new, in the guarded package): the parsed policy — question behaviour, the grant set — and its composition rule against the config's `permissions:` and an agent's tool policy.
- `agent/` (the gate): the dispatch context already carries the permission callback; the task runner supplies one backed by the policy instead of a prompt, so the gate's code path is identical attended and unattended.
- `commands/task_cmd.cpp`: `--allow <tool>` (repeatable), `--on-question …`, `--agent <name>` (running the task under an agent's policy); refusals name the composition rule when a grant exceeds what config allows.
- `tasks/ledger.*`, `commands/`: the per-use records and their rendering in `task status`.
- Tests: `tests/tasks/policy_test.cpp` — the composition table (config × agent policy × task grants), exhaustively; e2e: a granted tool runs unprompted and is recorded, an ungranted one denies, a declared answer is consumed and recorded.

**Reference.** Apogee's gate (Milestone V) and its "a surface with nobody to ask denies" rule are the floor this item builds on. In-house precedents: the `permissions:` schema and `[y]es/[n]o/[a]lways/[s]ession` ladder (Milestone V), and the per-agent tool policy filter (Milestone X).

**Decisions made** (dated):
- 2026-09-25 — Split from the v0.1.6 task work: 27h stays deny-by-default with fail-on-question so autonomy widening is a deliberate, reviewable step — this document — never an accident of the runner shipping.
- 2026-09-25 — Grants scoped to the task, composed to be no wider than config × agent policy, recorded per use (the design that survived the spike's rubber-stamp concern).
- 2026-10-03 — **The grant ceiling (the user's call, answered this day): per-tool `--allow <tool>` at launch**, exactly as specced — each tool named explicitly on the command line, every use recorded in the ledger, composed no wider than config × agent policy. Command-line grants are legitimate; the explicitness of the typed command is the audit trail's first line.
- 2026-10-03 — **The question-policy vocabulary (the user's call, answered this day): one declared answer** — `--on-question answer:"…"` serves any question the run asks; no per-question routing. A task whose questions need routing is a task that should run attended.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): `--on-question fail` stays the default with grants present — granting tools says nothing about answering questions.

**Guardrail(s).**
- The composition table is exhaustive and mutation-tested: no combination lets a task exceed config × agent policy.
- E2e: granted tool runs unprompted and lands in the ledger with its target; ungranted denies; the declared answer is consumed, recorded, and appears in `task status`.
- The leak sweep covers ledgers (a distinctive string planted as a declared answer is found only where it belongs, never in logs or served output).
- An attended `task run` (a TTY present) still prompts interactively for anything ungranted — policy adds, never replaces, the human path.

**Acceptance criteria:**
- [ ] A task with `--allow write_file` writes its file unprompted, and `task status` shows the grant and each exercised use with its target.
- [ ] The same task without the grant records a denial and still completes or fails honestly per its checks.
- [ ] `--on-question answer:"blue"` lets a question-asking task finish unattended, the answer and the question both recorded; without it, the task fails naming the question.
- [ ] A grant wider than the config's `permissions:` or the named agent's policy is refused at `task run`, naming the rule.

**Scope note.** Item **27i** (29b under the then-v0.1.6, 31i under the then-v0.1.8, until 2026-10-03's merge and migration — the user's calls), earmarked for **v0.1.4**; build after [27h](task-runner-core.md). Out of scope: any blanket grant; policy for interactive chat (the existing ladder owns it); org-level or shared policy files.
