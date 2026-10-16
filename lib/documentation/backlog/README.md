# Apogee — Backlog

**The work queue: one document per work item.** This directory holds every feature and fix that has been discussed and specced but **not yet built** — the layer between the loose ideas in [ROADMAP.md](../assistant/ROADMAP.md) and the shipped record in [MILESTONES.md](../assistant/MILESTONES.md). There is no separate TODO file: an agent takes an item from the index below and **implements it straight from its document**.

## Lifecycle

1. A feature is **discussed in chat** and gets a line on [ROADMAP.md](../assistant/ROADMAP.md) (and a [SPEC.md](../assistant/SPEC.md) update if it changes product shape).
2. Once specced — seam, files, decisions, open calls — it gets **its own document in its table's subdirectory** (the directory tree mirrors the index: `architecture/`, `maintenance/`, `v0.1.<x>/` — kebab-case filename, listed in the index below in priority order; the subdirectory is created with a table's first item).
3. When an agent **takes** it (told explicitly, or via the bare-"Continue" convention: the topmost item whose gate is satisfied), the document **is** the working spec — confirm scope with the user first (the document's **Open calls** are the natural questions), mark its index row **in progress**, and build. One item at a time.
4. When it **ships**, the work is recorded in [MILESTONES.md](../assistant/MILESTONES.md) per the docs flow in [CLAUDE.md](../assistant/CLAUDE.md), and the document is **deleted, along with its index row** — **a completed item must never remain in the backlog**. On the same pass, update any other document whose gate the shipped work just satisfied (e.g. a "gated on X shipping" status line).
5. When a **version's table empties** — every item it held shipped or moved — the table is retired at the pre-merge docs pass (`/apogee-cli-maintenance-update-documents`): its heading, track preambles and empty table leave the index, and the shipped-tracks paragraph above the tables says where its tracks went. The merge is the release, so a release's table never outlives its pull request (the user's call, 2026-10-16). The standing Architecture and Maintenance tables are never retired.

**The standing invariant:** every document in this directory is pending work. If it's shipped (in MILESTONES.md), it's not here.

## Document format

Every item document follows this shape (sections in order; omit one only when it's genuinely empty):

```markdown
# <Item title>

**What / why.** <The feature or fix in a paragraph: what it does for the user, and why it's worth building.>

**Core constraint(s).** <The rules the design must not violate — invariants it touches, compatibility it must keep.>

**Seam + files.** <Where the change plugs into the codebase: the interfaces/functions it extends and the files it touches.>

**Reference.** <Optional — the in-house precedents this item builds on and any external prior art, with each deliberate divergence from it.>

**Decisions made** (dated):
- <YYYY-MM-DD> — <a design decision and its rationale>

**Open calls:**
- <a question to resolve before building — tagged [user] (a genuinely user-owned decision: BLOCKS the build until answered) or [default: …] (the agent takes the stated default, records it as a dated decision, and the user may veto)>

**Guardrail(s).** <What must be tested or checked so the change can't regress silently.>

**Acceptance criteria:**
- [ ] <observable outcome that means "done">

**Scope note.** <Status: unscheduled / earmarked for <version> / gated on <prerequisite> — and anything explicitly out of scope.>
```

**On real weights.** A guardrail or acceptance criterion that runs on real models names the capability and the measure, not the models: it runs on [the model families](../assistant/DEVELOPER.md#on-real-weights-the-model-families) — Meta, Qwen, Google and OpenAI, one installed model each (its Q4_K_M build when there is one), never the excluded ones, one family at a time with its model loaded once (the user's calls, 2026-10-03). It is optional: a family with no local model is skipped and said. Where an older document names its acceptance models, the families replace them.

## Index

**One table per version target, every table in one shape.** Each release the queue is prescribed against — `v0.1.2`, `v0.1.3`, and every `vx.x.x` after them — gets its own table, and the tables appear in release order, nearest first — with two standing tables above them all: **Architecture** — the structural reshaping queue, numbered `A1, A2, …`, carrying `arch.` in the Version cell — and below it **Maintenance**, the release-agnostic upkeep queue (polish, performance, developer ergonomics), numbered `M1, M2, …` with `maint.` in the Version cell. The two standing tables are **permanent fixtures**: each keeps its heading, preamble and table even when every row has shipped or moved — an empty Maintenance table means nothing is queued, never that the queue is gone (version tables, by contrast, come and go with their releases). This is the format **all** index tables follow, now and as new versions open:

| Column | Contents |
|---|---|
| **#** | The item's number — its identity. Numbers ascend down the rows and across the tables; other documents cite them, so a renumber is a deliberate act, never a side effect of moving rows. Numbers are never reused: shipped items keep theirs in the [MILESTONES.md](../assistant/MILESTONES.md) record (1–23 are spent), so a new item continues from the highest number ever assigned — **unless it joins a release whose pending items already share a track prefix: then it takes that track's next letter** (26m, 26n, …), so a release's rows keep one prefix and the version tables stay ascending end to end. A **Maintenance** item takes the next `M<n>` and an **Architecture** item the next `A<n>` — each standing sequence ascends on its own, outside the release ascent. |
| **Item** | Plain title — em dash — one-line description. No link here; the link lives in **File**. |
| **Version** | The release the item is prescribed for — or `arch.`/`maint.` for the standing queues, which are prescribed to no release. The column stays even inside a per-version table, so a row keeps its meaning when it moves between tables and every table keeps the same shape. |
| **File** | The item's document, linked into **its table's subdirectory** (`architecture/…`, `maintenance/…`, `v0.1.<x>/…`) — the complete context for the item. The display name stays the bare filename. |
| **Status** | One emoji, plus the gate when blocked: 🟢 workable now (its gate is satisfied) · 🔒 blocked on the items named · 🚧 in progress · ❌ cancelled. |

Within a table, rows are in the **suggested build order**; the topmost 🟢 row of the earliest **version** table is the next item to take — the bare-"Continue" convention skips the standing queues (Architecture, Maintenance), whose items are claimable at any time *by name*. Moving an item to another version (or into a standing queue) means moving its row to that table, updating its Version cell, **and `git mv`-ing its document into that table's directory** (fixing the handful of relative links that cross directories) — creating the table and the directory if it is the first item there. Same-table links are bare filenames; cross-table links are `../<table-dir>/<file>`; links out of the backlog climb two levels (`../../assistant/…`).

**Gate convention:** every pending item builds on top of everything already shipped; a gate — the "build after" in a scope note, the 🔒 cell in a table — names only the ordering among the pending items here, and an item is buildable from its transitive gate chain alone. Items marked **split first** must be groomed into their listed sub-documents before an agent takes them; do not build from the guard document directly.

Tracks **24** (chat input completion), **25** (local agent tools) and **26** (small-model depth) have shipped: 24 on 2026-09-25 ([MILESTONES.md](../assistant/MILESTONES.md#milestone-h--apogee-chat) → Milestone H), 25 and 26 complete in [v0.1.3](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.3), released 2026-10-04. So have **27** (MLX inference, suites, tasks and graphs), complete in [v0.1.4](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.4), released 2026-10-07; **28** (provider detection and machine-mode integrations) in [v0.1.5](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.5), released 2026-10-08; **32** (system insight and the full-screen TUI, with **37** and **38**, the full-surface TUI, beside it), **39** (effort and permission modes) and **43** (thinking summaries), complete in [v0.1.6](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.6), released 2026-10-12; and **40** (the pre-shipped library), **41** (execute as the coding mode) and **42** (training suites in the box), complete in [v0.1.7](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.7), released 2026-10-14. So have **44** (the doors made law), **45** (completion presented well) and **46** (names and fields editable), complete in v0.1.8 -- in development on its branch, and released when that branch merges into `stable`; its table, empty once 46b shipped, was retired at the pre-merge docs pass (2026-10-16, the user's call). Their story is [ROADMAP.md](../assistant/ROADMAP.md), and each item's record is in [MILESTONES.md](../assistant/MILESTONES.md). The earliest table open is **v0.1.9**.

### Architecture

The structural reshaping queue, created 2026-10-03 (the user's call), claimable by name. Its first track, the **four-layer restructure** — Presentation / Business / Data / Infrastructure, each layer modularized, tests mirrored, the layering build-enforced — shipped whole on 2026-10-03 as **A1–A5** ([MILESTONES.md](../assistant/MILESTONES.md#milestone-aa--the-four-layers) → Milestone AA): the contracts carve, the move, `commands/` in three, build-enforced layering, and the per-layer `CLAUDE.md` cards. **The queue is empty** until the next item is specced into it.

| # | Item | Version | File | Status |
|---|---|---|---|---|

### Maintenance

Release-agnostic upkeep — polish, performance and ergonomics — claimable at any time by name (created 2026-10-03, the user's call). Numbers are ever-assigned: **M5** and **M6** were vacated the day they were given, as items moved to the v0.1.3 tail (26r, 26s) and the queue renumbered, and stay spent. Shipped: **M1** the busy line ([Milestone G](../assistant/MILESTONES.md#milestone-g--the-terminal-ux-layer)); **M2** fast model listings, **M3** pull to runnable in one command, **M4** model lineage and **M7** shell completion with store-aware arguments ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)), all 2026-10-03; **M8** one CI pipeline, **M9** `apogee reset --keep` and **M10** install channels ([Milestone K](../assistant/MILESTONES.md#milestone-k--the-install-contract)), 2026-10-04; **M11** installs end green (the same milestone), 2026-10-08 — `check --fix` seeds the starter config where none exists, the opt-in rows (training venv, zero backends) are skips, and `cli.install_parity` holds fresh installs to zero warnings; **M12** `config scan --register` ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)), 2026-10-08 — the store registered in one pass, byte for byte the hand-typed add-backends; **M13** provider model rosters ([Milestone AF](../assistant/MILESTONES.md#milestone-af--provider-detection)), 2026-10-08 — registration fetches the vendor's live catalogue through the `CatalogListing` capability into a disposable cache: listed, completable, and a sole-owned bare id runs, nothing hardwired, fetches on the user's word only. **The queue is empty** until the next item is specced into it.

| # | Item | Version | File | Status |
|---|---|---|---|---|

### v0.1.9

**47, desktop-parity capabilities**, opened 2026-10-16 (the user's call, from that day's built-in-MCP parity spike; 47 by the ever-assigned rule). The spike probed the shipped binary in a sandbox and reframed the ask with evidence: **MCP is provider-neutral by design** — the host translates tools per vendor, and Apogee's wire translators already do — so a per-vendor MCP server would have nothing vendor-specific to say (Apogee itself answered the MCP handshake with **17 read-only tools** through `__mcp-tools`), while the genuinely proprietary edge of Claude Desktop and ChatGPT is the vendors' **server-side tools**: request blocks executed on the vendor's infrastructure, which the host gate can never see (W4, the SPEC collision). The user settled it the same day: **allowed, default-off, per backend entry**, the dated principle amendment in SPEC.md → Principles. The remaining measurements: a fresh install configures **zero MCP servers** (W1) and nothing in the box executes code as an interpreter (W2 — the largest honest gap to ChatGPT). The split: **47a** the vendor server-side tools — capability facts as plain data, each vendor's blocks in its own wire (Anthropic first), default-off and consented per entry through 46b's field family, every use narrated as a tool line; **47b** the code-execution server — one bundled Python stdio MCP server for every provider alike, a throwaway network-less sandbox (the user's standing default), bounded, `writes`-gated, on the seeded-driver precedent; **47c** the presets — `mcp presets` and `create --from`, compiled-in data over the one create path (`code-exec` and the informational `self`), the empty state naming them, tools.md grown with the SearXNG pointer (search stays user-owned). Connectors and image generation are the recorded outs. Gated 47b → 47c; 47a independent. No **[user]** call blocks any item.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 47a | Vendor server-side tools — each vendor's own web-search/code-exec request blocks in its wire, capability facts as plain data, default-off and consented per backend entry (the dated SPEC principle), every use narrated | v0.1.9 | [`vendor-server-tools.md`](v0.1.9/vendor-server-tools.md) | 🟢 |
| 47b | The code-execution server — one bundled Python stdio MCP server for all providers: throwaway network-less sandbox, bounded, artifacts as paths, `writes`-gated, seeded on the driver precedent | v0.1.9 | [`code-exec-server.md`](v0.1.9/code-exec-server.md) | 🟢 |
| 47c | MCP presets — `mcp presets` (+ JSON document) and `create --from` over the one create path, byte-identical to the hand-typed twin; `code-exec` and the informational `self`; the empty state and tools.md say the rest | v0.1.9 | [`mcp-presets.md`](v0.1.9/mcp-presets.md) | 🔒 47b |
