# Apogee GUI — Backlog

**The work queue: one document per work item.** This directory holds every GUI feature and fix that has been discussed and specced but **not yet built** — the layer between the loose ideas in [ROADMAP.md](../assistant/ROADMAP.md) and the shipped record in [MILESTONES.md](../assistant/MILESTONES.md). There is no separate TODO file: an agent takes an item from the index below and **implements it straight from its document**. (The CLI's queue is its own, at [`cli/backlog/`](../../cli/backlog/README.md); an item that needs a CLI change gets a sibling document there, gated both ways.)

## Lifecycle

1. A feature is **discussed in chat** and gets a line on [ROADMAP.md](../assistant/ROADMAP.md) (and a [SPEC.md](../assistant/SPEC.md) update if it changes product shape).
2. Once specced — seam, files, decisions, open calls — it gets **its own document here** (kebab-case filename, listed in the index below in priority order).
3. When an agent **takes** it (told explicitly, or via the bare-"Continue" convention: the topmost item whose gate is satisfied), the document **is** the working spec — confirm scope with the user first (the document's **Open calls** are the natural questions), mark its index row **in progress** (🚧), and build. One item at a time.
4. When it **ships**, the work is recorded in [MILESTONES.md](../assistant/MILESTONES.md) per the docs flow in [CLAUDE.md](../assistant/CLAUDE.md), and the document is **deleted, along with its index row** — a completed item must never remain in the backlog. On the same pass, update any gate the shipped work just satisfied.

**The standing invariant:** every document in this directory is pending work. If it's shipped (in MILESTONES.md), it's not here.

**Tags and gates** follow the CLI backlog's conventions: an open call is `[user]` (genuinely user-owned; blocks the build) or `[default: …]` (agent-decidable, with the recommendation recorded); a decision owned by another item is `(consumed decision)`; a row's Status is 🟢 workable, 🔒 gated on a named real item (no cycles), or 🚧 in progress.

## Document format

Every item document follows this shape (sections in order; omit one only when it's genuinely empty):

```markdown
# <Item title>

**What / why.** <The feature or fix in a paragraph: what it does for the user, and why it's worth building — evidence baked in.>

**Core constraint(s).** <The rules the design must not violate — invariants it touches, compatibility it must keep.>

**Seam + files.** <Where the change plugs into the codebase: the interfaces it extends and the files it touches.>

**Decisions made** (dated):
- <YYYY-MM-DD> — <a design decision and its rationale>

**Open calls:**
- <tagged questions, per the conventions above>

**Guardrail(s).** <What must be tested or checked so the change can't regress silently.>

**Acceptance criteria:**
- [ ] <observable outcome that means "done">

**Scope note.** <Status: unscheduled / earmarked for <version> / gated on <item> — and anything explicitly out of scope.>
```

## Index

Priority-ordered: the topmost item whose gate is satisfied is the next one to build.

**Track 1, the macOS application** (1a–1q) has shipped: opened 2026-10-20 from that day's GUI spike (the user's call: Swift and SwiftUI, macOS first, unsigned ad-hoc) and grown the same day by the design-completion split, it is complete in **v0.2.0** — in development on its branch, released when it merges into `stable`. Its story is [ROADMAP.md](../assistant/ROADMAP.md), and each item's record is in [MILESTONES.md](../assistant/MILESTONES.md): the app, its client core, the session window and its pipeline in [Milestone A](../assistant/MILESTONES.md#milestone-a--the-macos-application) (1a–1d), every screen of the design in [Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens) (1e–1q). The v0.2.0 table below holds track 2; the standing Maintenance table holds the GUI's upkeep.

### Maintenance

Upkeep and fixes, claimable at any time by name: opened 2026-10-20 with its first item, mirroring the CLI's conventions (`M<n>` ever-assigned, the documents in `maintenance/`). **M1** comes from the user's run of the built app the same day, when the Models library showed none of the store's SafeTensors sets: every stored row arrives named by the CLI's `(not configured)` placeholder, and the GUI keyed rows by it. **M1 shipped 2026-10-20** ([MILESTONES → Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens)).

| # | Item | Version | File | Status |
|---|---|---|---|---|

### v0.2.0

**Track 2, CLI parity**, opened 2026-10-20 (the user's call, from their first hands-on run of the built app: "the GUI is meant to be able to do everything and anything that the CLI and TUI does" — recorded the same day as the gui SPEC's **Full parity** principle). The audit measured the gap against the TUI's own classification (`cli/tui_parity.cpp`, ADR 0010's **30 top-level commands**): fifteen are covered by track 1's screens, but **eight whole families have no GUI place at all** — `knowledge`, `embed`, `symphonies`, `train`, `datasets`, `agents`, `mcp`, `review` — plus partials (execute sessions beyond chat, prose graphs and communities, task recipes, `analyze`) and no recorded carve-outs; the root cause is structural: the GUI has **no parity law** (no ADR 0010 analog) and **no catch-all** (no exec-line analog), so coverage could drift silently. The split: **2a** the parity law — ADR 0003 and the classification table held by test against the binary's own word list, the TUI's three carve-outs mirrored with reasons; **2b** the console — any verb run supervised with the binary's own completion, the catch-all that makes the mandate true while screens accrete; **2c** knowledge & collections (plus the graph screen's prose half); **2d** the training studio (Train and Datasets, the trainer's own structured stream); **2e** agents, MCP & symphonies (native in-app editing superseding the `$EDITOR` carve, plays reaching the session); **2f** execute sessions, plays & recipes (the picker's execute door, `analyze` console-covered and said); **2g** the review screen (dry-run-first, click-consented posting, silence a first-class pass). Every screen item lands its design boards per the design's new-screen law. Gated 2a first and 2b–2g on it; 2f also on 2e. No **[user]** call blocks any item.

**2a shipped 2026-10-20** ([MILESTONES → Milestone C](../assistant/MILESTONES.md#milestone-c--cli-parity)): the parity law -- ADR 0003 and `ParityTable`, held to the binary's command words -- which opens 2b–2e and 2g. **2b shipped 2026-10-20** ([MILESTONES → Milestone C](../assistant/MILESTONES.md#milestone-c--cli-parity)): the console, ⇧⌘K -- any command run as typed, completion from the binary, the carve-outs refused.

**2h–2i, the model store**, joined the same evening from the user's next run of the built app, after M1. Their words: the Models place "is only pulling up the configured backends. There's no actual way to look at like the safe tensors and then convert them … I want that as part of this GUI application." Measured, not guessed (2026-10-20, by reading the source and probing the installed `apogee 0.1.9 (73495a5)`):

- **One table holds two things.** The place still mixes backends with the store's sets (`ModelsViewModel.Tab`, `ModelsViewModel.swift:15`).
- **5 store verbs are unreached:** `models convert`, `models quantize`, `config add-backend`, `config scan --register` and `config delete-backend`. Of the store's verbs, only delete, rename and repair are reached (`:281–303`). 2a's top-level classification counts this partial as covered.
- **Repair holds up the place.** It runs as a one-shot command, with no Stop.
- **0 of the 3 long store verbs** (convert, quantize, repair) takes an `--output-format`.

The design grew first: canvas version 33, with the tabs Backends · Stored weights · Jobs · Roles, three new boards, and the contract board's G18–G20 with their fallbacks (G9 and G17 closed, by probe). The split: **2h**, the store screen and its quick verbs (register one or all, remove a backend, delete with or without its backends, rename), ungated, since M1 has shipped and the screen stands alone; **2i**, convert, quantize and check as supervised jobs on the Jobs tab, gated on 2h. No CLI sibling: every verb exists, and G18–G20 are later CLI growth whose fallbacks are drawn. Every call is a recorded default; no **[user]** call blocks either item.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 2c | Knowledge & collections — the record and store screens, ingest and builds supervised, the graph screen's prose-and-communities half | v0.2.0 | [`knowledge-collections.md`](v0.2.0/knowledge-collections.md) | 🟢 |
| 2d | The training studio — Train and Datasets over the documents, runs supervised on the trainer's own structured stream, promote/rollback confirmed | v0.2.0 | [`training-studio.md`](v0.2.0/training-studio.md) | 🟢 |
| 2e | Agents, MCP & symphonies — the resource screens over the one scaffold, native in-app editing validated by the binary, plays into the session | v0.2.0 | [`agents-mcp-symphonies.md`](v0.2.0/agents-mcp-symphonies.md) | 🟢 |
| 2f | Execute sessions, plays & recipes — the picker's execute door (suite/model/project/orchestrate), recipe expansion shown before launch, `analyze` console-covered | v0.2.0 | [`execute-sessions-plays.md`](v0.2.0/execute-sessions-plays.md) | 🔒 2e |
| 2g | The review screen — dry-run-first runs with findings rendered, click-consented posting, targets and sweeps, the skills; silence a first-class pass | v0.2.0 | [`review-screen.md`](v0.2.0/review-screen.md) | 🟢 |
| 2h | The model store — Backends and Stored weights apart: every set by handle, with made-from links and its actions by format; register one or all, remove a backend, delete with or without its backends, rename | v0.2.0 | [`model-store-screen.md`](v0.2.0/model-store-screen.md) | 🟢 |
| 2i | Convert, quantize and check — sheets that build the CLI's own chain, each run supervised on the Jobs tab, its steps ticking from the store's read-back until G18 | v0.2.0 | [`convert-quantize-jobs.md`](v0.2.0/convert-quantize-jobs.md) | 🔒 2h |
