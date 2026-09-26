# Task autonomy policy: questions and permissions with nobody present

**What / why.** [28a](task-runner-core.md) ships safe and therefore narrow: an unattended task fails cleanly when the model asks a question, and every `ask`-level destructive tool denies. That makes whole classes of useful tasks impossible on purpose — a task that must write files, run a build, or make a judgment call needs the user's authority granted **before** the run, since nobody is present during it. This item is that grant, made explicit and auditable: a **per-task question policy** (`--on-question fail` — the 28a default — or `--on-question answer:"<declared answer>"`), and **per-task permission grants** (`--allow <tool>`, repeatable) that widen the gate for this task only. The hazard this item exists to avoid is the rubber stamp: an orchestrator that auto-answers "yes" to every prompt re-creates exactly the hole the permission gate closes. Policy is therefore *declared up front, scoped to the task, and recorded per use* — never answered live by the machine on the user's behalf.

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

**Reference (Ommi).** No analog (Ommi's tools ran ungated; Apogee's gate — Milestone V — deliberately diverged, and its "a surface with nobody to ask denies" rule is the floor this item builds on). In-house precedents: the `permissions:` schema and `[y]es/[n]o/[a]lways/[s]ession` ladder (Milestone V), and the per-agent tool policy filter (Milestone X).

**Decisions made** (dated):
- 2026-09-25 — Split from the v0.1.5 task work: 28a stays deny-by-default with fail-on-question so autonomy widening is a deliberate, reviewable step — this document — never an accident of the runner shipping.
- 2026-09-25 — Grants scoped to the task, composed to be no wider than config × agent policy, recorded per use (the design that survived the spike's rubber-stamp concern).

**Open calls:**
- **[user]** May a task pre-grant destructive tools from the command line at all, and with what ceiling? (a) `--allow <tool>` per tool as specced, each use recorded — *recommendation*; (b) command-line grants refused: only the config's `permissions:` and an agent's policy may widen a task; (c) named task profiles in config carrying grant sets. This is the safety ceiling of the whole feature and blocks the build.
- **[user]** The question-policy vocabulary: is a single declared answer enough (`--on-question answer:"…"`), or should a task carry per-question defaults (match on the question's header)? *Recommendation: the single answer — a task whose questions need routing is a task that should run attended.*
- [default: `--on-question fail` remains the default even when grants are present — granting tools says nothing about answering questions] Orthogonality of the two policies.

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

**Scope note.** Item **28b**, earmarked for **v0.1.5**; build after [28a](task-runner-core.md). Out of scope: any blanket grant; policy for interactive chat (the existing ladder owns it); org-level or shared policy files.
