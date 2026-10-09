# Apogee — Backlog

**The work queue: one document per work item.** This directory holds every feature and fix that has been discussed and specced but **not yet built** — the layer between the loose ideas in [ROADMAP.md](../assistant/ROADMAP.md) and the shipped record in [MILESTONES.md](../assistant/MILESTONES.md). There is no separate TODO file: an agent takes an item from the index below and **implements it straight from its document**.

## Lifecycle

1. A feature is **discussed in chat** and gets a line on [ROADMAP.md](../assistant/ROADMAP.md) (and a [SPEC.md](../assistant/SPEC.md) update if it changes product shape).
2. Once specced — seam, files, decisions, open calls — it gets **its own document in its table's subdirectory** (the directory tree mirrors the index: `architecture/`, `maintenance/`, `v0.1.<x>/` — kebab-case filename, listed in the index below in priority order; the subdirectory is created with a table's first item).
3. When an agent **takes** it (told explicitly, or via the bare-"Continue" convention: the topmost item whose gate is satisfied), the document **is** the working spec — confirm scope with the user first (the document's **Open calls** are the natural questions), mark its index row **in progress**, and build. One item at a time.
4. When it **ships**, the work is recorded in [MILESTONES.md](../assistant/MILESTONES.md) per the docs flow in [CLAUDE.md](../assistant/CLAUDE.md), and the document is **deleted, along with its index row** — **a completed item must never remain in the backlog**. On the same pass, update any other document whose gate the shipped work just satisfied (e.g. a "gated on X shipping" status line).

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

Tracks **24** (chat input completion), **25** (local agent tools) and **26** (small-model depth) have shipped: 24 on 2026-09-25 ([MILESTONES.md](../assistant/MILESTONES.md#milestone-h--apogee-chat) → Milestone H), 25 and 26 complete in [v0.1.3](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.3), released 2026-10-04. So have **27** (MLX inference, suites, tasks and graphs), complete in [v0.1.4](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.4), released 2026-10-07, and **28** (provider detection and machine-mode integrations) in [v0.1.5](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.5), released 2026-10-08 — all of it but 28e, carried to v0.1.6 below. Their story is [ROADMAP.md](../assistant/ROADMAP.md#shipped-releases) → Shipped releases, and each item's record is in [MILESTONES.md](../assistant/MILESTONES.md).

### Architecture

The structural reshaping queue, created 2026-10-03 (the user's call), claimable by name. Its first track, the **four-layer restructure** — Presentation / Business / Data / Infrastructure, each layer modularized, tests mirrored, the layering build-enforced — shipped whole on 2026-10-03 as **A1–A5** ([MILESTONES.md](../assistant/MILESTONES.md#milestone-aa--the-four-layers) → Milestone AA): the contracts carve, the move, `commands/` in three, build-enforced layering, and the per-layer `CLAUDE.md` cards. **The queue is empty** until the next item is specced into it.

| # | Item | Version | File | Status |
|---|---|---|---|---|

### Maintenance

Release-agnostic upkeep — polish, performance and ergonomics — claimable at any time by name (created 2026-10-03, the user's call). Numbers are ever-assigned: **M5** and **M6** were vacated the day they were given, as items moved to the v0.1.3 tail (26r, 26s) and the queue renumbered, and stay spent. Shipped: **M1** the busy line ([Milestone G](../assistant/MILESTONES.md#milestone-g--the-terminal-ux-layer)); **M2** fast model listings, **M3** pull to runnable in one command, **M4** model lineage and **M7** shell completion with store-aware arguments ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)), all 2026-10-03; **M8** one CI pipeline, **M9** `apogee reset --keep` and **M10** install channels ([Milestone K](../assistant/MILESTONES.md#milestone-k--the-install-contract)), 2026-10-04; **M11** installs end green (the same milestone), 2026-10-08 — `check --fix` seeds the starter config where none exists, the opt-in rows (training venv, zero backends) are skips, and `cli.install_parity` holds fresh installs to zero warnings; **M12** `config scan --register` ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)), 2026-10-08 — the store registered in one pass, byte for byte the hand-typed add-backends; **M13** provider model rosters ([Milestone AF](../assistant/MILESTONES.md#milestone-af--provider-detection)), 2026-10-08 — registration fetches the vendor's live catalogue through the `CatalogListing` capability into a disposable cache: listed, completable, and a sole-owned bare id runs, nothing hardwired, fetches on the user's word only. **The queue is empty** until the next item is specced into it.

| # | Item | Version | File | Status |
|---|---|---|---|---|

### v0.1.6

**32, system insight + the full-screen TUI**, opened 2026-10-04 (the user's call) with one item: the machine, read honestly. The track takes **32** by the ever-assigned rule — 29, 30 and 31 were assigned 2026-10-03 and vacated the same day by the merges and migration, so they stay spent (the M5/M6 precedent at track scale). **32a** gives Apogee a resource surface: `apogee system` — CPU, memory, GPU and the store's disk in one snapshot, human table and one JSON document — built as composition over the one machine read the 26a window sizing already uses (the no-second-estimator rule suite admission set), with every unanswerable field an explicit `unknown`, never a guess; GPU's first cut is what the OS answers cheaply (Apple silicon's unified-memory story; `unknown`, said, elsewhere), vendor tooling deferred. No **[user]** call blocks it. **The track grew three the same day: the full-screen TUI** (the user's call, from the day's TUI spike). The spike probed the shipped binary in a sandbox — bare `apogee` prints the help and exits 0, 18 subcommands, `views/` painting lines and composing no screen — and found the architecture mostly standing: the Reporter seam with three sibling adapters, the machine-mode protocol carrying the whole interactive loop conformance-pinned (permission prompts included), `markdown/` emitting render operations with the painter separate, and the in-process seam richer than the wire (26n's side calls never cross it). The user's three calls, resolved the same day: **the SPEC's no-TUI non-goal removed, unconditioned** (the dated revision is in SPEC.md → Non-goals; a TUI of hand-built views and its per-view parity burden stay cited on the items as the anti-pattern, not a ban); **full-screen on FTXUI**, vendored and pinned; and the raw-CLI re-tiering half **withdrawn** — the TUI is strictly additive, every command and non-TTY invocation byte-identical. Now **32b** the shell (FTXUI, the bare-TTY entry, screen ownership and restore, the Windows story), **32c** the session view (the fourth Reporter adapter, one Markdown renderer, one session format both doors open), **32d** the workbench views (models/chats/suites/config rendering `operations/` cores and 28h reads, CLI↔TUI byte-parity tested — the item that carries the anti-pattern on its face). Gated 32b → 32c → 32d; no **[user]** call blocks the sub-track. **32e** joined the same day (the user's call): the two halves of the track composed — a **system monitor on the shell's bottom bar**, persistent across every view, rendering 32a's one probe on a slow tick that lives in 32b's pump and dies with the shell (`apogee system` keeps its one-shot, no-watch-loop scope); unknowns dimmed never guessed, a failed sample stale-marked never blanked, the cost unmeasurable beside a running model. **32f**, provider model rosters, joined 2026-10-08 (the user's call) and **moved to the Maintenance table as M13 later that day** (the user's call again) — 32f a vacated spent identity living on in this note. **28e**, per-run integration wiring, was carried here from v0.1.5 on 2026-10-08 (the user's call) and holds no row until its document is written — walls W6/W8, whose substance the [Milestone M](../assistant/MILESTONES.md#milestone-m--the-front-end-contract) record carries.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 32a | System resources — `apogee system`: CPU (model, cores, load, sampled utilization), memory (system + Apogee's own + resident models), GPU (honest per platform), store disk footprint; one machine read shared with the 26a sizing; JSON as one document | v0.1.6 | [`system-resources.md`](v0.1.6/system-resources.md) | 🟢 |
| 32b | The TUI shell — FTXUI vendored and pinned; bare `apogee` on a TTY opens the full-screen shell (non-TTY and every subcommand byte-identical); one screen owner with restore-on-exit; the SPEC no-TUI removal rides it | v0.1.6 | [`tui-shell.md`](v0.1.6/tui-shell.md) | 🟢 |
| 32c | The session view — chat on the shell's stage as the fourth Reporter adapter (side calls included — richer than the wire); one Markdown renderer via `markdown/`'s render ops; permission modal = 26o's flow behind a widget; one session format both doors open | v0.1.6 | [`tui-session-view.md`](v0.1.6/tui-session-view.md) | 🔒 32b |
| 32d | The workbench views — models/chats/suites/config as renderers of `operations/` cores and 28h reads, actions calling the same cores, CLI↔TUI byte-parity tested; few views, each earning its place (the anti-pattern carried on its face) | v0.1.6 | [`tui-workbench-views.md`](v0.1.6/tui-workbench-views.md) | 🔒 32b, 32c |
| 32e | The system monitor — 32a's probe on the shell's bottom bar, every view, on a slow pump-owned tick (the command stays one-shot); unknowns dimmed, failed samples stale-marked, cadence held during streaming, cost unmeasurable | v0.1.6 | [`tui-system-monitor.md`](v0.1.6/tui-system-monitor.md) | 🔒 32a, 32b |

