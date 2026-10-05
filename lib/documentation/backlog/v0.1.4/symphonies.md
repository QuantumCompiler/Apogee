# Symphonies: named, staged multi-model prompt processes

**What / why.** The suites vision's second half (the user, 2026-10-04): a suite ([model-suites.md](model-suites.md)) names *which* models work together; a **symphony** is *how* — an input goes in, each member plays a role in a staged prompt process, an output comes out. A symphony is a **declarative definition**: a name, a description, an input contract, and ordered stages, where each stage names a suite **role** (never a backend — the suite decides placement, the symphony decides process), a prompt template with the input and prior stages' outputs available as variables, optionally a schema its answer is grammar-held to (26f, shipped), and optionally its own caps. The runner walks stages serially, each stage one bounded, brief-only call through the one resolver — [suite-consult.md](suite-consult.md)'s call core with a second, harness-driven consumer. Apogee **pre-ships starter definitions as assets** and users add their own: the full lifecycle (`symphonies list|show|create|edit|delete`) plus a one-shot `symphonies play <name>` for scripting, so the item is useful before the interactive surface ([execute-mode.md](execute-mode.md)) exists. Feasibility was mapped in-conversation against shipped shapes rather than re-spiked — the suites spike's measurements carry: **agents** are the exact lifecycle precedent (named user-addable workflow definitions in config, `AgentConfig`, the shared scaffold making CLI- and HTTP-created ones byte-identical), **training pipelines** the staged-definition precedent (`training.pipelines.<name>.stages[]`, config entries and spec files through one parser), and **consult** the call mechanic. The honest cost, stated up front like 27g states latency: a symphony is **serial generations** — N stages is N sequential model calls; small members at pinned windows keep each one cheap, but a deep process is seconds by construction.

**Core constraint(s).**
- **One model-calling path.** Stages run through 27f's bounded call core — brief-only context, serial, the one-call-at-a-time discipline — harness-driven where consult is model-initiated. Never a second calling path; the core's seam is shared, not copied.
- **Roles, not backends:** a stage resolves its role through the one chain (the suite rung and all the others), so the same symphony plays on any suite; a definition naming a backend directly is invalid at parse.
- **Deterministic scaffolding, models inside:** parsing, validation, template rendering, stage ordering and output threading are deterministic and table-tested; the stage calls are the only nondeterminism.
- **Structure by grammar, where declared:** a stage with a schema is grammar-held (26f, shipped), never prompt-begged; stage handoff is otherwise plain text rendered into the next template.
- **The user's initiative spends:** playing a symphony is a user act; stages still resolve to the suite's members — local today. Model-initiated plays and their spend rules are [orchestrator.md](orchestrator.md)'s business, not this item's.
- **No stage calls tools** in this cut — a symphony is a pure prompt process; the agent loop's tool machinery is not in the stage path.
- **Asset parity:** the shipped starter definitions land identically across every install path in the same change (the no-silent-install-drift non-goal), and `symphonies:` config entries are written only through the comment-preserving editor; spec files are read-only inputs, the training precedent.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test ([ADR](../../adrs/cli/tests-mirror-architecture.md)).

**Seam + files.**
- `business/symphony/` (new guarded package, joining the layering map): `definition.h/.cpp` — the parsed shape, validation (roles known, templates well-formed, schemas loadable); `runner.h/.cpp` — the stage walk, template rendering, output threading, per-stage narration through 26n's conventions, generation through the shared bounded-call core.
- `contracts/config.h/.cpp` + the editor: the `symphonies:` block; the same parser reads spec files (the `training.pipelines` precedent, same file ↔ entry duality).
- `business/scaffold/symphony.h/.cpp`: the shared create core, so CLI- and admin-plane-created symphonies are byte-identical (the `scaffold/agent.h` precedent).
- `presentation/cli/symphonies_cmd.h/.cpp`: the lifecycle verbs and one-shot `play` (input from the argument or stdin, output to stdout, `--output-format json` per the house rule); completion for verbs and names per [ADR tab-completion](../../adrs/cli/tab-completion.md).
- `assets/symphonies/`: the starter set, registered by name beside user definitions.
- Tests: `tests/business/symphony/` — parse/validation tables, runner over scripted mock providers (stage order, output threading, call counts), template goldens; scaffold byte-identity; the asset-parity check in the install suite.
- Consumes: [27d](model-suites.md) (roles and the suite), [27f](suite-consult.md) (the call core), 26f (shipped — grammars), 26n (shipped — narration), M1 (shipped — the busy line for long plays).

**Reference.** The nearest shapes are the training track's pipelines and regimes (staged multi-model work: teacher distilling, eval-gated stages); the in-house precedent this item actually follows is **agents** (named, user-addable, scaffold-created workflow definitions).

**Decisions made** (dated):
- 2026-10-04 — Asked for by the user (the suites long-term vision); placed in **v0.1.4** with the suites foundation at their direction, taking 27q–27t per the release-prefix rule. Feasibility grounded by mapping onto shipped shapes (agents, training pipelines, consult) rather than a new spike — the 2026-10-03 suites spike's evidence carries.
- 2026-10-04 — Stages name roles, never backends: the suite owns placement (the 27d contract), so one definition serves every suite and machine.
- 2026-10-04 — Stage handoff is plain text with per-stage opt-in grammars *(recorded as the default, vetoable)*: 26f already holds structure where a stage declares it, and free-text handoff keeps definitions writable by hand.
- 2026-10-04 — Definitions live as `symphonies:` config entries **and** spec files through one parser *(the default, from the training precedent)*; shipped starters are asset spec files registered by name.
- 2026-10-04 — The serial-latency cost accepted and stated: chains of generations are the physics of the feature, priced in the doc, never hidden.

**Open calls:**
- [default: the starter set is small and demonstrative — a summarize-then-verify duo, a single-stage structured extraction, and a describe-then-answer media pair — final roster at build time, each exercising a distinct stage feature]

**Guardrail(s).**
- Runner tables over scripted providers: stage order, output threading and call counts asserted; a failing stage stops the walk with the stage named.
- The wire pin: each stage's recorded request is exactly its rendered template — no history, no creep (27f's isolation discipline, same test shape).
- A schema stage's output is grammar-valid by construction (26f's machinery, asserted on the mock and on real weights).
- Scaffold byte-identity CLI ↔ admin plane; the asset-parity install check covers `assets/symphonies/`.
- On real weights, the capability and the measure: on [the model families](../../assistant/DEVELOPER.md#on-real-weights-the-model-families), a two-stage symphony where stage two can only succeed with stage one's output (a planted fact threaded through) completes correctly — one family at a time, members loaded once.

**Acceptance criteria:**
- [ ] `apogee symphonies list` shows shipped and user definitions distinctly; `show` prints a definition's stages and contracts.
- [ ] `apogee symphonies play <name> --input "…"` with a suite active runs the stages serially (wire-recorded: each request is its rendered template), narrates each stage per 26n, and prints the output; `--output-format json` returns one document.
- [ ] A stage with a declared schema emits grammar-valid output; a definition naming an unknown role or a backend is refused at parse with the reason.
- [ ] `symphonies create` through the scaffold produces a definition byte-identical to the admin plane's; the starter assets are present and identical on every install path.

**Scope note.** Item **27q**, earmarked for **v0.1.4** (the suites vision's first new item); **gated on [27d](model-suites.md), [27f](suite-consult.md)**. Out of scope: chaining symphonies ([symphony-chaining.md](symphony-chaining.md)); the interactive surface ([execute-mode.md](execute-mode.md)); model-chosen plays ([orchestrator.md](orchestrator.md)); tool-using stages (a later deliberate item); parallel stages (serial is the discipline); member-to-member free conversation.
