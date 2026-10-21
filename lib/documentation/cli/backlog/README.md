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

Tracks **24** (chat input completion), **25** (local agent tools) and **26** (small-model depth) have shipped: 24 on 2026-09-25 ([MILESTONES.md](../assistant/MILESTONES.md#milestone-h--apogee-chat) → Milestone H), 25 and 26 complete in [v0.1.3](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.3), released 2026-10-04. So have **27** (MLX inference, suites, tasks and graphs), complete in [v0.1.4](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.4), released 2026-10-07; **28** (provider detection and machine-mode integrations) in [v0.1.5](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.5), released 2026-10-08; **32** (system insight and the full-screen TUI, with **37** and **38**, the full-surface TUI, beside it), **39** (effort and permission modes) and **43** (thinking summaries), complete in [v0.1.6](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.6), released 2026-10-12; **40** (the pre-shipped library), **41** (execute as the coding mode) and **42** (training suites in the box), complete in [v0.1.7](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.7), released 2026-10-14; **44** (the doors made law), **45** (completion presented well) and **46** (names and fields editable), complete in [v0.1.8](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.8), released 2026-10-16; and **47** (desktop-parity capabilities), **48** (the review agent), **49** (video generation), **50** (the composer) and **51** (install bootstrap), complete in **v0.1.9** — in development on its branch, released when it merges into `stable`. Their story is [ROADMAP.md](../assistant/ROADMAP.md), and each item's record is in [MILESTONES.md](../assistant/MILESTONES.md). No version table is open: the next release's opens with its first item.

### Architecture

The structural reshaping queue, created 2026-10-03 (the user's call), claimable by name. Its first track, the **four-layer restructure** — Presentation / Business / Data / Infrastructure, each layer modularized, tests mirrored, the layering build-enforced — shipped whole on 2026-10-03 as **A1–A5** ([MILESTONES.md](../assistant/MILESTONES.md#milestone-aa--the-four-layers) → Milestone AA): the contracts carve, the move, `commands/` in three, build-enforced layering, and the per-layer `CLAUDE.md` cards. **The queue is empty** until the next item is specced into it.

| # | Item | Version | File | Status |
|---|---|---|---|---|

### Maintenance

Release-agnostic upkeep — polish, performance and ergonomics — claimable at any time by name (created 2026-10-03, the user's call). Numbers are ever-assigned: **M5** and **M6** were vacated the day they were given, as items moved to the v0.1.3 tail (26r, 26s) and the queue renumbered, and stay spent. Shipped: **M1** the busy line ([Milestone G](../assistant/MILESTONES.md#milestone-g--the-terminal-ux-layer)); **M2** fast model listings, **M3** pull to runnable in one command, **M4** model lineage and **M7** shell completion with store-aware arguments ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)), all 2026-10-03; **M8** one CI pipeline, **M9** `apogee reset --keep` and **M10** install channels ([Milestone K](../assistant/MILESTONES.md#milestone-k--the-install-contract)), 2026-10-04; **M11** installs end green (the same milestone), 2026-10-08 — `check --fix` seeds the starter config where none exists, the opt-in rows (training venv, zero backends) are skips, and `cli.install_parity` holds fresh installs to zero warnings; **M12** `config scan --register` ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)), 2026-10-08 — the store registered in one pass, byte for byte the hand-typed add-backends; **M13** provider model rosters ([Milestone AF](../assistant/MILESTONES.md#milestone-af--provider-detection)), 2026-10-08 — registration fetches the vendor's live catalogue through the `CatalogListing` capability into a disposable cache: listed, completable, and a sole-owned bare id runs, nothing hardwired, fetches on the user's word only. **M14** opened 2026-10-19, from the user's bulk GGUF removal: deleting stored weights strands the backends that ran from them — the delete's warning names no cleanup door, and `check`'s hint names a command the binary refuses on an existing name (`add-backend`, "already exists") — so the delete gains a consented cleanup offer through the one editor, and the hint learns the two real doors (repoint or remove). **Shipped 2026-10-19** ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)): `models delete --and-backends` and the default-no ask, each entry removed as `config delete-backend` removes it, and the doctor's doors held runnable by test. **M15** opened 2026-10-19, from the user's MLX conversion of a vendor Assistant model: mlx-lm resolves a converter by `model_type`, its remap table knows the base (`gemma4_unified`) and not the vendor's variant (`gemma4_unified_assistant`, the snapshot's own config.json, pulled from Hugging Face), and the driver's honest relay names no way out — so `convert --mlx` gains an explicit `--model-type` override (an overlay view, the snapshot untouched, the override recorded in provenance) and the failure folds in the door with candidates read from the installed mlx-lm's own lists. **Shipped 2026-10-19** ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)): `--model-type` over an overlay view, the record telling, and the refusal's door with the nearest types. **M16** opened 2026-10-19, from the user's next ask the same hour (`convert --mlx … --register-with`, refused): M3's one-command chain is GGUF-only by a 27b design decision now superseded on the user's word — with `--mlx`, `--register` executes the very `add-backend --type mlx` line it prints today, and `--register-with` takes the MLX precisions, one conversion per level (MLX quantizes at conversion), each its own backend, M3's step-and-resume honesty whole. **Shipped 2026-10-19** ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)): `--register` and `--register-with` with `--mlx`, the hint's line executed byte for byte, each engine's levels its own. **The queue is empty** until the next item is specced into it.

| # | Item | Version | File | Status |
|---|---|---|---|---|

### v0.2.0

**52, the desktop client's CLI growths**, opened 2026-10-20 (the user's call, from that day's GUI design-completion spike; 52 by the ever-assigned rule). The design's contract board (`lib/design/spec/html/BuildContract.dc.html`) maps all 61 design parts to the machine-mode contract — 29 ready, 13 the app's own, 15 gaps, 4 assumed — and the spike verified it against the installed 0.1.9: every relied-on flag and verb exists, the gaps are real, and two assumed rows resolved into new gaps (no forget-recall command; the model-set JSON an untyped `{name, fields[]}` with no window/cache fields on a safetensors set). Every gap is an **additive** growth (ADR 0006: new fields, events, lines and flags older drivers ignore), each already carrying the app's fallback until it lands, so nothing here gates the GUI queue. The split, by seam, the design's before-v1 recommendation (G1, G8, G9, G11, G12) at the head: **52a** the session's events say more (the permission diff, recall's names, the sources event, each chat's model); **52b** long jobs as event streams (`models pull` and the media verbs under `--output-format stream-json`, a relay of the progress that already exists); **52c** the composer's plan documents (a new-brief `--dry-run`, the edit diff as per-scene `from`/`changes`); **52d** the driver's inbound lines (`detach`, `set`, `compact` + `chats compact`, and `chat --private` — each an existing core's door, ADR 0010 classification where a verb is new); **52e** the doctor and config growths (ffmpeg's encode/filter rows, the allow-listed `config set`, the recall-forget verb, the typed model-set document). No **[user]** call blocks any item. **Groomed 2026-10-20**: every default confirmed; nothing in the track waits on a question. Shipped: **52a**, **52b**, **52c**, **52d** and **52e** on 2026-10-20 — the whole track ([MILESTONES.md](../assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths) → Milestone AO).

| # | Item | Version | File | Status |
|---|---|---|---|---|
