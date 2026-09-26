# Apogee — Backlog

**The work queue: one document per work item.** This directory holds every feature and fix that has been discussed and specced but **not yet built** — the layer between the loose ideas in [ROADMAP.md](../assistant/ROADMAP.md) and the shipped record in [MILESTONES.md](../assistant/MILESTONES.md). There is no separate TODO file: an agent takes an item from the index below and **implements it straight from its document**.

## Lifecycle

1. A feature is **discussed in chat** and gets a line on [ROADMAP.md](../assistant/ROADMAP.md) (and a [SPEC.md](../assistant/SPEC.md) update if it changes product shape).
2. Once specced — seam, files, decisions, open calls — it gets **its own document here** (kebab-case filename, listed in the index below in priority order).
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

**Reference (Ommi).** <Optional — the Ommi packages/features this item ports, and any deliberate divergence from them. Apogee is a re-implementation of Ommi (see SPEC.md → Background), so most items carry this; the implementing agent should read the named Ommi source/docs before building.>

**Decisions made** (dated):
- <YYYY-MM-DD> — <a design decision and its rationale>

**Open calls:**
- <a question to resolve before building — tagged [user] (a genuinely user-owned decision: BLOCKS the build until answered) or [default: …] (the agent takes the stated default, records it as a dated decision, and the user may veto)>

**Guardrail(s).** <What must be tested or checked so the change can't regress silently.>

**Acceptance criteria:**
- [ ] <observable outcome that means "done">

**Scope note.** <Status: unscheduled / earmarked for <version> / gated on <prerequisite> — and anything explicitly out of scope.>
```

## Index

**One table per version target, every table in one shape.** Each release the queue is prescribed against — `v0.1.2`, `v0.1.3`, and every `vx.x.x` after them — gets its own table, and the tables appear in release order, nearest first. This is the format **all** index tables follow, now and as new versions open:

| Column | Contents |
|---|---|
| **#** | The item's number — its identity. Numbers ascend down the rows and across the tables; other documents cite them, so a renumber is a deliberate act, never a side effect of moving rows. Numbers are never reused: shipped items keep theirs in the [MILESTONES.md](../assistant/MILESTONES.md) record (1–23 are spent), so a new item continues from the highest number ever assigned. |
| **Item** | Plain title — em dash — one-line description. No link here; the link lives in **File**. |
| **Version** | The release the item is prescribed for. The column stays even inside a per-version table, so a row keeps its meaning when it moves between tables and every table keeps the same shape. |
| **File** | The item's document, linked — the complete context for the item. |
| **Status** | One emoji, plus the gate when blocked: 🟢 workable now (its gate is satisfied) · 🔒 blocked on the items named · 🚧 in progress · ❌ cancelled. |

Within a table, rows are in the **suggested build order**; the topmost 🟢 row of the earliest version's table is the next item to take. Moving an item to another version means moving its row to that version's table (and updating its Version cell) — creating the table if it is the version's first item.

**Gate convention:** every pending item builds on top of everything already shipped; a gate — the "build after" in a scope note, the 🔒 cell in a table — names only the ordering among the pending items here, and an item is buildable from its transitive gate chain alone. Items marked **split first** must be groomed into their listed sub-documents before an agent takes them; do not build from the guard document directly.

Four tracks, since **24** (chat input completion) shipped on 2026-09-25 — see [MILESTONES.md](../assistant/MILESTONES.md) → Milestone H. Its handoff to the attachments item — wiring a sent `@` mention into the attach path — is recorded on [26d](attachments-documents.md).

**25, local agent tools**, came out of a spike on 2026-09-25 and was split into six items the same day. The spike found that local models were never shown the tools every other backend already has, measured llama.cpp's own tool-calling layer on real weights (6/6 tasks on Qwen3.8-27B and Qwen3-VL-8B), and turned up an outbound-data exposure in today's tools. The user's four calls are recorded in the items: SearXNG for search, ask per new website, the launch folder as the file root, and 8B-class models and up. In build order: 25a first (it closed today's exposure, and shipped 2026-09-25 — [MILESTONES.md](../assistant/MILESTONES.md#milestone-v--the-native-toolsets) → Milestone V); 25b, the unlock, shipped the same day (Milestone J); 25c–25f follow, as each names.

**26, small-model depth**, came out of a review on 2026-09-25 of what else would let small local models work at their best. It was split into twelve items the same day, with every group the review proposed taken by the user:
- **automatic attachments**: documents, code and folders indexed by the embedding model and handed to the model per turn; images, audio and video read natively or through helper models;
- **helper-model roles** (vision, transcription, utility);
- **reliability**: grammar-constrained JSON, tool selection by relevance, sampling that reaches the model, and thinking control;
- **speed and memory**: a context sized to the machine, a persistent prompt cache, and speculative decoding measured before it is built;
- **memory across chats**: a per-turn context budget and automatic recall.

The user's calls are recorded in the items: attachments kept with their chat and cached by hash, helper models used automatically, and external converters (`pdftotext`, `ffmpeg`).

**27, machine-mode integrations**, asked for 2026-09-25 for **v0.1.4**: the CLI pluggable into other people's harnesses and applications — the native machine-mode protocol as the floor, a common protocol integrators extend from. **The spike ran the same day** ([MILESTONES.md](../assistant/MILESTONES.md#milestone-m--the-front-end-contract) → Milestone M): a naive external host, knowing only the protocol doc, completed a tool-using, permission-prompted conversation against the shipped binary; a host-run MCP server's tool round-tripped through the loop with zero prompts (host tools already work — the gap is wiring); seven walls were recorded. The recommendation: **grow the JSONL contract additively** — the tolerance rules make it retrofittable in both directions, verified live — with MCP as the host-tools sidecar, never a reframe that breaks `protocol_version: 1` drivers. Split into five: 27a the handshake and stability promise, 27b per-run integration wiring (walls W6/W8 — **its document is still to be written**; the Milestone M record carries its substance), 27c turn ids and cancel, 27d the schema artifact, 27e machine-readable reads. Parked with evidence, the user's call: the push channel.

**28, autonomous tasks**, asked for 2026-09-25 for **v0.1.5**: the user states a goal and the application does the rest — plans, drives successive turns of the one shared agent loop, checks acceptance stated up front, composes corrective rounds, and stops on done or budget. The shape was proven the same day in a context-only spike (at the user's direction, so its findings are baked into the documents rather than a repo record): an outer loop over the shipped binary completed the full cycle with one user input, and the two gaps it exposed — task state living only in the driving process, and external drivers blind to tool work — set the design: in-binary, over a resumable ledger, with the training cycle's safety kit reused. Split into three: 28a the runner and its ledger (safe and narrow: deny-by-default, fail-on-question), 28b the declared autonomy policy (its two **[user]** calls are the feature's safety ceiling), 28c the surfaces.

### v0.1.3

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 25c | Hybrid prompt checkpoints — Qwen3.5/3.8 re-read only what is new each turn and tool step, via state checkpoints as llama-server keeps them | v0.1.3 | [`hybrid-prompt-checkpoints.md`](hybrid-prompt-checkpoints.md) | 🟢 |
| 25d | Tool ergonomics — capped command output, line-range reads, `edit_file`, `grep_files`, and an environment note (date, OS, folder) | v0.1.3 | [`local-tool-ergonomics.md`](local-tool-ergonomics.md) | 🟢 |
| 25e | Web search via SearXNG — `web_search` over the user's own SearXNG, pluggable, never silently empty | v0.1.3 | [`web-search-searxng.md`](web-search-searxng.md) | 🟢 |
| 25f | `fetch_url` as a reader — main content with its links as Markdown, paging, content types, a download cap | v0.1.3 | [`fetch-url-reader.md`](fetch-url-reader.md) | 🟢 |
| 26a | A context window sized to the machine — 32K by default instead of the trained window (16 GiB of cache on Qwen3.8), an 8-bit cache, and the cost shown | v0.1.3 | [`context-fit-defaults.md`](context-fit-defaults.md) | 🟢 |
| 26b | Helper-model roles — `vision`, `transcription` and `utility` in the one resolver; titles, compaction, query rewriting and big tool results move to the utility model | v0.1.3 | [`helper-model-roles.md`](helper-model-roles.md) | 🟢 |
| 26c | A per-turn context budget — each source gets a share of the real window; finished turns' tool results sent as stubs | v0.1.3 | [`context-budget.md`](context-budget.md) | 🟢 |
| 26d | Attachments: documents, code and folders — `/attach`, indexed with the embedding model, inlined when they fit, retrieved per turn with citations, kept with the chat | v0.1.3 | [`attachments-documents.md`](attachments-documents.md) | 🔒 26c |
| 26e | Attachments: images, audio and video — native when the model can, a helper model when not; videos become searchable timelines | v0.1.3 | [`attachments-media.md`](attachments-media.md) | 🔒 26b, 26d |
| 26f | Structured output by grammar — JSON Schema constrained token by token on local models | v0.1.3 | [`local-structured-output.md`](local-structured-output.md) | 🟢 |
| 26g | Tool selection by relevance — the relevant few tools per step, and `find_tools` for the rest | v0.1.3 | [`tool-selection.md`](tool-selection.md) | 🟢 |
| 26h | Sampling local models are meant to be run with — `-t` reaches the model (it is silently ignored today), GGUF and family defaults | v0.1.3 | [`sampling-profiles.md`](sampling-profiles.md) | 🟢 |
| 26i | Thinking control — `/think on\|off\|auto`, a thinking budget, mapped to every vendor | v0.1.3 | [`thinking-control.md`](thinking-control.md) | 🔒 26h |
| 26j | A persistent prompt cache — resumed chats and repeated tool prompts restored from disk | v0.1.3 | [`persistent-prompt-cache.md`](persistent-prompt-cache.md) | 🔒 25c, 26a |
| 26k | Speculative decoding, measured first — MTP, a draft model or n-grams, built only on a clean ≥1.3× win | v0.1.3 | [`speculative-decoding.md`](speculative-decoding.md) | 🔒 25c |
| 26l | Recall across chats — past chats summarised and retrieved per turn; never on `serve` | v0.1.3 | [`recall-across-chats.md`](recall-across-chats.md) | 🔒 26b, 26c |

### v0.1.4

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 27a | The handshake and the stability promise — an optional `hello` line, `capabilities` on the `session` event, and the additivity guarantees in writing | v0.1.4 | [`machine-handshake.md`](machine-handshake.md) | 🟢 |
| 27c | Turn ids and cancel — a `turn` field on every turn-scoped event, and `{"type":"cancel"}` aborting an in-flight turn the way Ctrl-C does | v0.1.4 | [`machine-turn-control.md`](machine-turn-control.md) | 🔒 27a |
| 27d | The schema artifact — `apogee __machine-schema` prints the protocol as JSON Schema, conformance-pinned to the code, shipped in the release archives | v0.1.4 | [`machine-schema-artifact.md`](machine-schema-artifact.md) | 🔒 27a, 27c |
| 27e | Machine-readable reads — `--output-format json` on `models`/`chats`/`agents`/`mcp`/`check`, the same facts as the human view | v0.1.4 | [`machine-readable-reads.md`](machine-readable-reads.md) | 🟢 |

### v0.1.5

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 28a | The task runner and its ledger — goal in; the application plans, drives successive agent-loop turns, checks stated acceptance, corrects, stops on done or budget; resumable from a manifest-per-transition ledger under a lock | v0.1.5 | [`task-runner-core.md`](task-runner-core.md) | 🟢 |
| 28b | Task autonomy policy — pre-declared per-task grants and question answers, composed no wider than config × agent policy, every use recorded | v0.1.5 | [`task-autonomy-policy.md`](task-autonomy-policy.md) | 🔒 28a |
| 28c | Task surfaces — machine-mode task events, `task status`/`list` as JSON, admin-plane reads; control stays CLI-only | v0.1.5 | [`task-surfaces.md`](task-surfaces.md) | 🔒 28a |
