# The Orchestrator: symphonies as tools

**What / why.** The vision's keystone (the user, 2026-10-04): in an execute session, a model "has access to the available symphonies, and can orchestrate what symphonies to play based upon the context." The architecture that makes this buildable with zero new cognition machinery: **each symphony's definition projects a tool** — name, description, input schema, straight from [27q](symphonies.md)'s declarative shape — and with orchestration on, the session root's toolset gains them. The ordinary agent loop *is* the orchestration engine: model-chooses-among-tools is shipped, 26g ranks and trims the offer per model, the loop's constrained tool-call shape holds the choice to real names and valid arguments, and a chosen play runs through 27q's runner with its stages narrated. One correction from the feasibility discussion, recorded: "runs in the background" lands as **the root model of the execute session is the Orchestrator** — under the one-call-at-a-time discipline there is no concurrent helper, and none is needed; the role is a framing in the session's environment note, not a second process. The honest risk is not plumbing but **small-model judgment** — whether a local root reliably picks the right symphony — so this ships **opt-in** (`--orchestrate` / a suite's `orchestrate: true`) with a measured pick-rate check on real weights as the acceptance bar, per the house measure-before-default rule; the evidence that constrained choice suits small models is 26f's own datum (grammar alone took Llama-3.2-3B's captures from 17/20 to 20/20).

**Core constraint(s).**
- **Registration and policy only — no new machinery.** The symphony tools are ordinary registry entries built from definitions; selection (26g), the loop, permissions and machine mode treat them as any tool. No planner, no second loop, no new wire shape.
- **The model's initiative never spends** (27f's principle, carried): an orchestrator-initiated play runs only on local members; a suite with a metered member reachable from any symphony's stages is refused `orchestrate: true` at config time with the reason — the 27f config-time refusal pattern, verbatim.
- **Bounded like everything else:** plays join the per-turn budget family (27f's consult caps, 27g's validation spend — one family, shared accounting); a turn that exhausts it degrades to answering without plays, said, never silent.
- **Narrated, always:** a model-initiated play is a labeled line plus its stage lines (26n), so the user watching an execute session always knows the model chose it and what it cost.
- **Pure prompt processes stay pure:** symphony stages call no tools (27q's rule), so an orchestrated play cannot smuggle tool use past the loop's gates; the permission system's scope is unchanged by construction.
- **Off means absent:** without orchestration, zero symphony tools exist in the registry — not hidden, not denied: absent (the 27f registration precedent).
- Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `business/symphony/tools.h/.cpp` (new): the definition→tool projection (name, description from the definition's own, the input contract as the schema) and the registration rule (orchestration on + suite + symphonies available).
- `business/agentloop/`: nothing structural — the registry entries arrive through the existing registration seam; the environment note gains the Orchestrator framing paragraph when orchestration is on (the 26p precedent for registry-composed guidance).
- `contracts/config` (27d's `suites:` block): `orchestrate:` per suite, through the one editor; `cli/execute.cpp`: `--orchestrate` for the session-scoped override.
- Tests: `tests/business/symphony/` — projection goldens (definition → tool schema), registration lifecycle (on/off × suite × definitions present), the metered-reachability refusal, budget interplay with 27f/27g over scripted providers.
- Consumes: [27q](symphonies.md) (definitions, the runner), [27s](execute-mode.md) (the session), [27r](symphony-chaining.md)'s aggregate budgets where a chosen symphony chains; 26g, 26n, 26p (all shipped).

**Reference (Ommi).** No analog — Ommi's models chose among tools, never among model-processes. The in-house chain of precedent is complete, which is the feasibility argument: tools-as-registry (shipped), selection by relevance (26g), constrained calls (26f/the loop), bounded model-initiated model calls (27f).

**Decisions made** (dated):
- 2026-10-04 — Asked for by the user (the vision's keystone); placed in **v0.1.4** with the set at their direction, last in the build order — it composes everything before it.
- 2026-10-04 — **Symphonies-as-tools** is the architecture (the feasibility discussion's mapping): the agent loop orchestrates because orchestration *is* tool choice; building a separate planner would duplicate the loop.
- 2026-10-04 — **The root is the Orchestrator**: no background process, no concurrency — the serial discipline stands, and the role is framing, not infrastructure.
- 2026-10-04 — **Opt-in until measured** *(recorded as the default, vetoable)*: the plumbing is low-risk; the judgment is not. The pick-rate check below is the evidence a future default-on decision cites.

**Guardrail(s).**
- Registration lifecycle tables: orchestration off → the registry contains zero symphony tools (asserted by enumeration, the 27f shape); on with no symphonies → likewise absent, said in the banner.
- The projection golden: a definition's tool schema round-trips its input contract exactly; a definition edit is reflected on next session start.
- The refusal: a suite with a metered member reachable from any registered symphony is refused `orchestrate: true` at config time, naming member and symphony.
- Budget interplay: plays, consults and validation draw one per-turn budget; exhaustion degrades honestly (scripted-provider test, call counts).
- On real weights, the capability and the measure: on [the model families](../../assistant/DEVELOPER.md#on-real-weights-the-model-families), the **planted-task pick-rate check** — a set of requests each answerable only by one specific starter symphony; the root plays the right one unprompted, pick rate recorded per family, one family at a time. The measured rate is the dated evidence any default-on decision must cite.

**Acceptance criteria:**
- [ ] In `apogee execute --suite research --orchestrate`, a request matching a starter symphony's purpose triggers that play unprompted — the choice, the stages and the cost all narrated — and the answer uses the play's output.
- [ ] Without `--orchestrate` (and without the suite's `orchestrate: true`), the session registers zero symphony tools and behaves exactly as [27s](execute-mode.md) shipped it.
- [ ] A suite whose symphonies could reach a metered member refuses orchestration at config time with the reason; the budget family enforces across plays, consults and validation in one turn.
- [ ] The pick-rate check runs on the model families with its rates recorded; machine mode shows an orchestrated play as ordinary tool-call narration with no new event types.

**Scope note.** Item **27t**, earmarked for **v0.1.4** (the set's closer); **gated on [27s](execute-mode.md)** (and through it 27q; consumes 27r's budgets where chains are chosen). Out of scope: default-on orchestration (a later decision citing the measured rates); concurrent plays; the Orchestrator proposing *new* symphonies (authoring stays human); tool-using stages (27q's exclusion, inherited).
