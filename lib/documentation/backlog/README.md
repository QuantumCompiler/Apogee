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

Tracks **24** (chat input completion), **25** (local agent tools) and **26** (small-model depth) have shipped: 24 on 2026-09-25 ([MILESTONES.md](../assistant/MILESTONES.md#milestone-h--apogee-chat) → Milestone H), 25 and 26 complete in [v0.1.3](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.3), released 2026-10-04. So have **27** (MLX inference, suites, tasks and graphs), complete in [v0.1.4](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.4), released 2026-10-07; **28** (provider detection and machine-mode integrations) in [v0.1.5](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.5), released 2026-10-08; **32** (system insight and the full-screen TUI, with **37** and **38**, the full-surface TUI, beside it), **39** (effort and permission modes) and **43** (thinking summaries), complete in [v0.1.6](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.6), released 2026-10-12; and **40** (the pre-shipped library), **41** (execute as the coding mode) and **42** (training suites in the box), complete in [v0.1.7](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.7), released 2026-10-14. Their story is [ROADMAP.md](../assistant/ROADMAP.md), and each item's record is in [MILESTONES.md](../assistant/MILESTONES.md). No release table is open: v0.1.8 is in development, and its table arrives with its first item.

### Architecture

The structural reshaping queue, created 2026-10-03 (the user's call), claimable by name. Its first track, the **four-layer restructure** — Presentation / Business / Data / Infrastructure, each layer modularized, tests mirrored, the layering build-enforced — shipped whole on 2026-10-03 as **A1–A5** ([MILESTONES.md](../assistant/MILESTONES.md#milestone-aa--the-four-layers) → Milestone AA): the contracts carve, the move, `commands/` in three, build-enforced layering, and the per-layer `CLAUDE.md` cards. **The queue is empty** until the next item is specced into it.

| # | Item | Version | File | Status |
|---|---|---|---|---|

### Maintenance

Release-agnostic upkeep — polish, performance and ergonomics — claimable at any time by name (created 2026-10-03, the user's call). Numbers are ever-assigned: **M5** and **M6** were vacated the day they were given, as items moved to the v0.1.3 tail (26r, 26s) and the queue renumbered, and stay spent. Shipped: **M1** the busy line ([Milestone G](../assistant/MILESTONES.md#milestone-g--the-terminal-ux-layer)); **M2** fast model listings, **M3** pull to runnable in one command, **M4** model lineage and **M7** shell completion with store-aware arguments ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)), all 2026-10-03; **M8** one CI pipeline, **M9** `apogee reset --keep` and **M10** install channels ([Milestone K](../assistant/MILESTONES.md#milestone-k--the-install-contract)), 2026-10-04; **M11** installs end green (the same milestone), 2026-10-08 — `check --fix` seeds the starter config where none exists, the opt-in rows (training venv, zero backends) are skips, and `cli.install_parity` holds fresh installs to zero warnings; **M12** `config scan --register` ([Milestone N](../assistant/MILESTONES.md#milestone-n--model-operations)), 2026-10-08 — the store registered in one pass, byte for byte the hand-typed add-backends; **M13** provider model rosters ([Milestone AF](../assistant/MILESTONES.md#milestone-af--provider-detection)), 2026-10-08 — registration fetches the vendor's live catalogue through the `CatalogListing` capability into a disposable cache: listed, completable, and a sole-owned bare id runs, nothing hardwired, fetches on the user's word only. **The queue is empty** until the next item is specced into it.

| # | Item | Version | File | Status |
|---|---|---|---|---|

### v0.1.8

**44, the doors made law**, opened 2026-10-14 (the user's call, from that day's CLI↔TUI disparity audit; 44 by the ever-assigned rule). The audit drove the fresh binary on a real PTY against sandboxed homes and measured one drift class behind every wall: **a front end composing its own second opening of a mode**. A fresh shell shows no picker at all (straight into a chat — no execute entry anywhere); a suite-less picker shows no execute row though the CLI opens bare since 41a; the shell's door is its own suite-only path (`tui_session.cpp:333`) with zero references to 41b's project opening or 41c's persona; `apogee tui` takes **0 flags against execute's 40**; and the parity table classified `execute` as a reachable View (`tui_parity.cpp:28`, the runner's refusal pointing at it) while the door was absent — the law green over a missing door. Mid-session parity measured strong (the one command table's ~40 slash twins), so the fixes concentrate at entry — and **the ADRs come first** (the user's call, 2026-10-14, future-proofing before fixing): **44a** writes ADR 0011 (a mode is opened by its command's own path — one open function, one flag table, every front end's door) and ADR 0012 (a classified door is reachable on a fresh install), each carrying the audit's evidence; **44b** rebuilds the execute door under them — the picker on every fresh session, suite rows and the bare row, every row through `cli/execute.cpp`'s own opening, so the rungs, the context ritual and the persona reach the shell by construction; **44c** gives `apogee tui` the session flag table, handed whole to the first session; **44d** lands ADR 0012's teeth — every View row's door proven reachable on a fresh install under the PTY recipe, a planted conditional door failing by name. Gated 44a → 44b → 44c → 44d (44d on both fixes). No **[user]** call blocks any item. **Groomed 2026-10-15**: every default confirmed; nothing in the track waits on a question. **44a shipped 2026-10-14**: [ADR 0011](../adrs/cli/one-session-opening.md) and [ADR 0012](../adrs/cli/doors-are-reachable.md) ([Milestone AH](../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui)), their enforcement pending on 44b–44d.

**45, completion presented well**, opened 2026-10-14 (the user's call, from the same day's completion-presentation spike; 45 by the ever-assigned rule). The spike measured the experience: `--model <Tab>` dumps **~60 candidates** as one flat, case-sensitively ordered wall (every source's union — keys, three rosters, aliases, every local quant — `Llama`/`Qwen` exiled behind lowercase); the wire is `:values` + bare strings, the zsh stub a plain `compadd` (`_apogee:20`), fish's native descriptions unused — and the owned surfaces (the exec line, the session input) draw the same flat strings. Four concepts were mocked for the user — **A** namespace-first shaping, **B** groups/descriptions on the wire with per-shell renderings, **C** a curated first tier, **D** a real menu on the owned surfaces — and **the user chose A + D the same day, B and C out** (recorded on both items, so revisiting either is deliberate). The split: **45a** the namespaces and the one ordering rule in the core — the first Tab offers the map (configured keys, `claude:`/`codex:`/`gemini:`, `local:` — the shipped 34 spelling reused), every value kind ordered configured-first and case-insensitively per a per-kind treatment table (chats most-recent-first), every surface improved with **no wire or stub change**; **45b** the completion menu on the TUI's exec line and session input — grouped, described, arrow-selectable, filter-as-you-type — over in-process candidate metadata the wire never carries. Gated 45a → 45b. No **[user]** call blocks either item. **Groomed 2026-10-15**: every default confirmed; nothing in the track waits on a question.

**46, names and fields editable**, opened 2026-10-14 (the user's call, from trying to rename a model and finding no way: the pasted verb tables — `models`' nine verbs, `config`'s twenty-four — hold no rename and no edit; 46 by the ever-assigned rule). The finding's blast radius, by `file:line`: a backend key is referenced by `models.default`, the five role pointers, every suite's members **and every saved chat** (`logger/session.h:148`, the orphan case already documented at `:58`); a stored model's name is the store directory itself (`modelstore/store.h`'s one declaration) that every `model_path` spells; and the editor holds no rename transform while two field transforms already exist (`config_edit.h:144/149`). Two items, each alone: **46a** the renames — `config rename-backend` and `models rename`, every reference rewritten in one atomic edit, saved sessions swept with the orphan path as the net, the store directory moved with its records, and **the coupling rule (a)**: bidirectional and automatic where 1:1 (M3-derived names follow the model), withheld and said where shared or underived (the spike's recommendation, standing — the user executed without a veto); **46b** `config set-backend` — one flag per editable field over one transform each, in place, comments kept, no `--api-key` ever (keys stay `auth add`'s). Admin twins per ADR 0002, the exec line covering both per ADR 0010. No **[user]** call blocks either item. **Groomed 2026-10-15**: every default confirmed; nothing in the track waits on a question.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 44b | The execute door — the picker on every fresh session (New chat, the suite rows, the bare row), every row opening through execute's own function: the rungs, the project opening and the persona reach the shell | v0.1.8 | [`tui-execute-door.md`](v0.1.8/tui-execute-door.md) | 🟢 |
| 44c | Session launch — `apogee tui` takes the session flag table and opens its first session with it; flagless stays byte-stable, refusals the commands' own | v0.1.8 | [`tui-session-launch.md`](v0.1.8/tui-session-launch.md) | 🔒 44b |
| 44d | The reachability law — every View row's door proven on a fresh install on a real PTY, conditions recorded and said, a planted conditional door failing by name; ADR 0012's enforcement flipped live | v0.1.8 | [`tui-reachability-law.md`](v0.1.8/tui-reachability-law.md) | 🔒 44b, 44c |
| 45a | Completion namespaces — the map before the territory: configured keys + `<vendor>:` + `local:` on the first Tab, every shelf behind its prefix, one case-insensitive configured-first ordering rule over a per-kind treatment table; no wire or stub change | v0.1.8 | [`completion-namespaces.md`](v0.1.8/completion-namespaces.md) | 🟢 |
| 45b | The completion menu — Tab on the exec line and session input opens a grouped, described, filterable picker over in-process candidate metadata; the wire stays bare strings, the terminal REPL stays plain | v0.1.8 | [`completion-menu.md`](v0.1.8/completion-menu.md) | 🔒 45a |
| 46a | Renames that propagate — `config rename-backend` and `models rename`: every reference rewritten in one atomic edit, sessions swept, the store moved with its records, coupling 1:1-automatic and said otherwise | v0.1.8 | [`rename-propagation.md`](v0.1.8/rename-propagation.md) | 🟢 |
| 46b | `config set-backend` — one flag per editable field, one transform each, in place with comments kept; never a key on the line; the admin twin byte-identical | v0.1.8 | [`edit-backend.md`](v0.1.8/edit-backend.md) | 🟢 |
