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

**On real weights.** A guardrail or acceptance criterion that runs on real models names the capability and the measure, not the models: it runs on [the model families](../assistant/DEVELOPER.md#on-real-weights-the-model-families) — Meta, Qwen, Google and OpenAI, one installed model each (its Q4_K_M build when there is one), never the excluded ones, one family at a time with its model loaded once (the user's calls, 2026-10-03). It is optional: a family with no local model is skipped and said. Where an older document names its acceptance models, the families replace them.

## Index

**One table per version target, every table in one shape.** Each release the queue is prescribed against — `v0.1.2`, `v0.1.3`, and every `vx.x.x` after them — gets its own table, and the tables appear in release order, nearest first — with two standing tables above them all: **Architecture** — the structural reshaping queue, numbered `A1, A2, …`, carrying `arch.` in the Version cell — and below it **Maintenance**, the release-agnostic upkeep queue (polish, performance, developer ergonomics), numbered `M1, M2, …` with `maint.` in the Version cell. This is the format **all** index tables follow, now and as new versions open:

| Column | Contents |
|---|---|
| **#** | The item's number — its identity. Numbers ascend down the rows and across the tables; other documents cite them, so a renumber is a deliberate act, never a side effect of moving rows. Numbers are never reused: shipped items keep theirs in the [MILESTONES.md](../assistant/MILESTONES.md) record (1–23 are spent), so a new item continues from the highest number ever assigned — **unless it joins a release whose pending items already share a track prefix: then it takes that track's next letter** (26m, 26n, …), so a release's rows keep one prefix and the version tables stay ascending end to end. A **Maintenance** item takes the next `M<n>` and an **Architecture** item the next `A<n>` — each standing sequence ascends on its own, outside the release ascent. |
| **Item** | Plain title — em dash — one-line description. No link here; the link lives in **File**. |
| **Version** | The release the item is prescribed for — or `arch.`/`maint.` for the standing queues, which are prescribed to no release. The column stays even inside a per-version table, so a row keeps its meaning when it moves between tables and every table keeps the same shape. |
| **File** | The item's document, linked into **its table's subdirectory** (`architecture/…`, `maintenance/…`, `v0.1.<x>/…`) — the complete context for the item. The display name stays the bare filename. |
| **Status** | One emoji, plus the gate when blocked: 🟢 workable now (its gate is satisfied) · 🔒 blocked on the items named · 🚧 in progress · ❌ cancelled. |

Within a table, rows are in the **suggested build order**; the topmost 🟢 row of the earliest **version** table is the next item to take — the bare-"Continue" convention skips the standing queues (Architecture, Maintenance), whose items are claimable at any time *by name* — though Architecture's A2 names its own scheduling constraint: a quiet repo at a release boundary. Moving an item to another version (or into a standing queue) means moving its row to that table, updating its Version cell, **and `git mv`-ing its document into that table's directory** (fixing the handful of relative links that cross directories) — creating the table and the directory if it is the first item there. Same-table links are bare filenames; cross-table links are `../<table-dir>/<file>`; links out of the backlog climb two levels (`../../assistant/…`).

**Gate convention:** every pending item builds on top of everything already shipped; a gate — the "build after" in a scope note, the 🔒 cell in a table — names only the ordering among the pending items here, and an item is buildable from its transitive gate chain alone. Items marked **split first** must be groomed into their listed sub-documents before an agent takes them; do not build from the guard document directly.

Six tracks, since **24** (chat input completion) shipped on 2026-09-25 — see [MILESTONES.md](../assistant/MILESTONES.md) → Milestone H. Its handoff to the attachments item — wiring a sent `@` mention into the attach path — shipped with [26d](../assistant/MILESTONES.md#milestone-h--apogee-chat).

**25, local agent tools**, came out of a spike on 2026-09-25 and was split into six items the same day. The spike found that local models were never shown the tools every other backend already has, measured llama.cpp's own tool-calling layer on real weights (6/6 tasks on Qwen3.8-27B and Qwen3-VL-8B), and turned up an outbound-data exposure in today's tools. The user's four calls are recorded in the items: SearXNG for search, ask per new website, the launch folder as the file root, and 8B-class models and up. In build order: 25a first (it closed today's exposure, and shipped 2026-09-25 — [MILESTONES.md](../assistant/MILESTONES.md#milestone-v--the-native-toolsets) → Milestone V); 25b, the unlock, shipped the same day and 25c on 2026-09-28 (Milestone J); 25d, tool ergonomics, 25e, web search through the user's own SearXNG, and 25f, `fetch_url` as a reader, shipped 2026-09-28 too (Milestone V), completing the track.

**26, small-model depth**, came out of a review on 2026-09-25 of what else would let small local models work at their best. It was split into twelve items the same day, with every group the review proposed taken by the user:
- **automatic attachments**: documents, code and folders indexed by the embedding model and handed to the model per turn; images, audio and video read natively or through helper models;
- **helper-model roles** (vision, transcription, utility);
- **reliability**: grammar-constrained JSON, tool selection by relevance, sampling that reaches the model, and thinking control;
- **speed and memory**: a context sized to the machine, a persistent prompt cache, and speculative decoding measured before it is built;
- **memory across chats**: a per-turn context budget and automatic recall.

The user's calls are recorded in the items: attachments kept with their chat and cached by hash, helper models used automatically, and external converters (`pdftotext`, `ffmpeg`). **26a**, the context window sized to the machine, shipped 2026-09-28 ([MILESTONES.md](../assistant/MILESTONES.md#milestone-j--local-inference) → Milestone J). Shipping it found **26m**, a sliding-window model's cache kept at its window, added the same day at the user's request and shipped the same day too ([MILESTONES.md](../assistant/MILESTONES.md#milestone-j--local-inference) → Milestone J): Gemma 4 31B's cache at 32K went from 14.6 GiB to 2.0. **26b**, the helper-model roles, shipped 2026-09-28 too ([MILESTONES.md](../assistant/MILESTONES.md#milestone-n--model-operations) → Milestone N): `vision`, `transcription` and `utility` in the one resolver, with titles, compaction, follow-up search queries and large tool results moved to the utility model. **26c**, the per-turn context budget, shipped the same day ([MILESTONES.md](../assistant/MILESTONES.md#milestone-f--the-shared-agent-loop) → Milestone F): a finished turn's tool results sent as stubs, retrieval fitted to its share of the window, and an overflowing request trimmed in reverse priority and said. **26d**, document attachments, shipped 2026-09-29 ([MILESTONES.md](../assistant/MILESTONES.md#milestone-h--apogee-chat) → Milestone H): files, folders and PDFs attached to a chat, indexed with the embedding model, inlined when they fit and retrieved per turn with page and line citations, kept with the chat. **26e**, images, audio and video, shipped 2026-09-30 (the same milestone): the same `/attach` takes a picture, a recording or a video. The chat model reads it as it is on the turn it is attached when it can. From then on, and for a model that cannot read it, it reaches the model as text: a description from the vision role, a transcript from the transcription role, or a video's timeline of what was on screen and what was said. **26f**, structured output by grammar, shipped 2026-10-03 ([MILESTONES.md](../assistant/MILESTONES.md#milestone-x--agents-as-data) → Milestone X): a local model's answer to a schema is held by its own template's grammar, after any reasoning, with the schema stated in the prompt only where no grammar can hold it. Llama 3.2 3B's captures went from 17 of 20 valid to 20 of 20 on the first try, and its graph extraction from 8 failed calls to none. Real-weights tests leave out Qwen3.8-27B until thinking control (26i) can set its level (the user's call, 2026-10-03). **26g**, tool selection by relevance, shipped 2026-10-03 too ([MILESTONES.md](../assistant/MILESTONES.md#milestone-f--the-shared-agent-loop) → Milestone F): past 16 tools a turn offers a small core, the eight tools its question ranks highest, each MCP server whole, and `find_tools` for the rest — about half the prompt per step on every model family, with the battery passing as before.

Three tail additions joined the track on 2026-09-30 and 2026-10-03, lettered per the release-prefix rule *(two more from those days — the busy line and the GGUF header cache — moved to the **Maintenance** table as **M1** and **M2** on 2026-10-03, re-lettering this tail)*: **26n**, side model calls narrated in the thinking block — the embedder, the utility rewrites and summaries, the rerank judge, in-turn vision/transcription and the `/capture` clerk each a dim labeled line inside the block, collapsing with it, never persisted, the wire untouched; and **26o**, session permission presets — `--allow`/`--deny`/`--allow-host` at launch and `/allow`/`/deny`/`/revoke`/`/permissions` mid-chat, completable through the one command table, every preset exactly the prompt's `session` answer given early (config `deny` never loosened; grants die with the process); and **26p**, the tool-use policy — the environment note gains a registry-composed paragraph telling the model to reach for the matching tool on anything it cannot know instead of refusing, and `web_search`/`fetch_url` descriptions lead with their trigger cases; the motivating transcript (a weather question refused, then searched fine once instructed) is the live acceptance case. None opens a **[user]** call.

**27, machine-mode integrations**, asked for 2026-09-25 for **v0.1.4**: the CLI pluggable into other people's harnesses and applications — the native machine-mode protocol as the floor, a common protocol integrators extend from. **The spike ran the same day** ([MILESTONES.md](../assistant/MILESTONES.md#milestone-m--the-front-end-contract) → Milestone M): a naive external host, knowing only the protocol doc, completed a tool-using, permission-prompted conversation against the shipped binary; a host-run MCP server's tool round-tripped through the loop with zero prompts (host tools already work — the gap is wiring); seven walls were recorded. The recommendation: **grow the JSONL contract additively** — the tolerance rules make it retrofittable in both directions, verified live — with MCP as the host-tools sidecar, never a reframe that breaks `protocol_version: 1` drivers. Split into five: 27a the handshake and stability promise, 27b per-run integration wiring (walls W6/W8 — **its document is still to be written**; the Milestone M record carries its substance), 27c turn ids and cancel, 27d the schema artifact, 27e machine-readable reads. Parked with evidence, the user's call: the push channel.

**28, autonomous tasks**, asked for 2026-09-25 for **v0.1.5**: the user states a goal and the application does the rest — plans, drives successive turns of the one shared agent loop, checks acceptance stated up front, composes corrective rounds, and stops on done or budget. The shape was proven the same day in a context-only spike (at the user's direction, so its findings are baked into the documents rather than a repo record): an outer loop over the shipped binary completed the full cycle with one user input, and the two gaps it exposed — task state living only in the driving process, and external drivers blind to tool work — set the design: in-binary, over a resumable ledger, with the training cycle's safety kit reused. Split into three: 28a the runner and its ledger (safe and narrow: deny-by-default, fail-on-question), 28b the declared autonomy policy (its two **[user]** calls are the feature's safety ceiling), 28c the surfaces.

**29, code knowledge graphs**, asked for 2026-09-30 for **v0.1.6**: the graph layer in [Graphify](https://github.com/Graphify-Labs/graphify)'s shape — a codebase parsed deterministically into the graph (tree-sitter, every edge tagged extracted-or-inferred, cross-file resolution by qualified names — and **model-free end to end, the user's call**: a code-only graph is built, resolved, clustered, walked and exported with no LLM, no key, no embedder, no network), navigation people and models can walk (`path`, `explain`, scoped query — and the same verbs as a read-only toolset, reaching external MCP clients through `__mcp-tools` for free), and the artifacts (a Markdown architecture report, a self-contained interactive HTML file, GraphML/Mermaid exports). Specced as a delta onto Milestone Y's shipped layer — same store, same communities, same expansion — never a second graph system. One **[user]** call blocks 29a: the first-cut language set, since each grammar is a vendored dependency.

**30, MLX inference**, asked for 2026-10-03 for **v0.1.7** (the number freed by the M1/M2 renumber; nothing shipped under it): a second local runtime beside in-process llama.cpp — Apple's MLX on Apple silicon, for day-one model support and the training shortcut. The realistic shape is Ommi's own pattern returning: a **persistent Python child over pipes** (`mlx-lm` in the venv Apogee already owns), built from the claude-cli backend's persistent-child shape and the training track's driver discipline; llama.cpp stays the zero-dependency default on every platform. Split into three: 30a the backend core, 30b model operations (`mlx/` in the store, pull, `convert --mlx`, honest windows), 30c depth (vision via `mlx-vlm`, serve routing allowed — the exclusion list is about credentials — and `train promote --target mlx`, skipping GGUF conversion). The track's one **[user]** call — a dated SPEC amendment, since the skeleton pinned "no runtime interpreter dependency on core inference paths" — **was answered the same day**: the revision is in [SPEC.md](../assistant/SPEC.md) → Background (llama.cpp stays the zero-dependency default everywhere; an explicitly opt-in interpreter-backed type is permitted beside it, refusing loudly when its runtime is absent). The track is unblocked.

### Architecture

The structural reshaping queue, created 2026-10-03 (the user's call) from the four-layer spike: the source restructured into **Presentation / Business / Data / Infrastructure**, each layer modularized, tests mirrored, and the layering made build-enforced. The spike measured the real graph — 22 packages, 81 edges, exactly **six edge-types fighting the model**, all sharing one root cause (contracts living in a Business package) plus one small cycle — so the track is one judgment item, one pure-rename move, one modularization, one enforcement closer. The user's six calls are recorded in the items: this standing category; contracts **in Data, named `contracts`**; `commands/` into three modules; short include paths held by per-layer include dirs; dual enforcement (link graph + the scalpel grep rules); layers per application. A2 requires a quiet window at a release boundary, chosen by its taker.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| A1 | Carve the contracts — the shared interfaces, IR, errors, config engine, layout and `Tool` type down to a Data-floor `contracts` module; `modelstore` and `transport` split out; the six measured violations hit zero while directories stay flat | arch. | [`arch-contracts-carve.md`](architecture/arch-contracts-carve.md) | 🟢 |
| A2 | The move — every package `git mv`'d into `source/<layer>/<module>/`, tests mirrored, short includes byte-stable via per-layer include dirs, the full doc sweep in-change; 100% renames, quiet window required | arch. | [`arch-layer-move.md`](architecture/arch-layer-move.md) | 🔒 A1 |
| A3 | `commands/` into three presentation modules — `cli` (the composition root), `views`, `machine`; the judgment diffs deliberately isolated from A2's renames | arch. | [`arch-commands-modules.md`](architecture/arch-commands-modules.md) | 🔒 A2 |
| A4 | Build-enforced layering — one static library per module, the link graph as the compile-time law, the grep test rescoped to its named scalpel rules; incremental-build timings recorded | arch. | [`arch-build-enforcement.md`](architecture/arch-build-enforcement.md) | 🔒 A2, A3 |

### Maintenance

Release-agnostic upkeep — polish, performance and ergonomics claimable at any time by name, created 2026-10-03 (the user's call) with M1/M2 moved in from the v0.1.3 tail and M3 specced into it. **M1**, the busy line, shipped 2026-10-03 ([MILESTONES.md](../assistant/MILESTONES.md#milestone-g--the-terminal-ux-layer) → Milestone G): chat's status line extended into a scoped line any slow command opens -- `models list/info/status`, `check`, `graph build`, `graph communities` and `embed ingest` say what they are reading, counted, and clear it before their results -- silent on pipes, under the new `--quiet` and with JSON output. Sampling `models list` on the way found where its time goes: each vocabulary string of a GGUF header skipped with its own seek.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| M2 | The GGUF header cache — fingerprinted (path/size/mtime) parsed headers for the sweeps; warm `models list` reads zero headers; loading/convert/pull stay uncached by construction | maint. | [`gguf-header-cache.md`](maintenance/gguf-header-cache.md) | 🟢 |
| M3 | Pull to runnable in one command — `models pull --safetensors --register[-with Q4_K_M,…]` chains pull → convert → quantize → add-backend with per-stage reporting and resume; folds in the quantize log-leak fix | maint. | [`pull-register-chain.md`](maintenance/pull-register-chain.md) | 🟢 |
| M4 | Base-model sessions — turn-marker fragments never reach the screen (generic, censorship-safe), tools honestly off with one notice, the base state visible all session; framing, never a gate | maint. | [`base-model-sessions.md`](maintenance/base-model-sessions.md) | 🟢 |
| M5 | Model lineage — convert/quantize stamp their source into the sidecar; consumed snapshots fold out of `models list` (`--all` restores), derived GGUFs say `converted from …` with recorded/inferred told apart | maint. | [`models-list-lineage.md`](maintenance/models-list-lineage.md) | 🟢 |

### v0.1.3

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 26h | Sampling local models are meant to be run with — `-t` reaches the model (it is silently ignored today), GGUF and family defaults | v0.1.3 | [`sampling-profiles.md`](v0.1.3/sampling-profiles.md) | 🟢 |
| 26i | Thinking control — `/think on\|off\|auto`, a thinking budget, mapped to every vendor | v0.1.3 | [`thinking-control.md`](v0.1.3/thinking-control.md) | 🔒 26h |
| 26j | A persistent prompt cache — resumed chats and repeated tool prompts restored from disk | v0.1.3 | [`persistent-prompt-cache.md`](v0.1.3/persistent-prompt-cache.md) | 🟢 |
| 26k | Speculative decoding, measured first — MTP, a draft model or n-grams, built only on a clean ≥1.3× win | v0.1.3 | [`speculative-decoding.md`](v0.1.3/speculative-decoding.md) | 🟢 |
| 26l | Recall across chats — past chats summarised and retrieved per turn; never on `serve` | v0.1.3 | [`recall-across-chats.md`](v0.1.3/recall-across-chats.md) | 🟢 |
| 26n | Side calls in the thinking block — embedder, utility rewrites/summaries, rerank, in-turn vision/transcription and the `/capture` clerk narrated as dim labeled lines that collapse with the block; transcript and wire untouched | v0.1.3 | [`thinking-side-calls.md`](v0.1.3/thinking-side-calls.md) | 🟢 |
| 26o | Session permission presets — `--allow`/`--deny`/`--allow-host` at launch, `/allow`/`/deny`/`/revoke`/`/permissions` mid-chat with completion; a preset is the `session` answer given early, config `deny` never loosened | v0.1.3 | [`session-permission-presets.md`](v0.1.3/session-permission-presets.md) | 🟢 |
| 26p | The tool-use policy — a registry-composed paragraph in the environment note (reach for the matching tool instead of refusing; never claim an absent capability) plus trigger-led `web_search`/`fetch_url` descriptions | v0.1.3 | [`tool-use-policy.md`](v0.1.3/tool-use-policy.md) | 🟢 |

### v0.1.4

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 27a | The handshake and the stability promise — an optional `hello` line, `capabilities` on the `session` event, and the additivity guarantees in writing | v0.1.4 | [`machine-handshake.md`](v0.1.4/machine-handshake.md) | 🟢 |
| 27c | Turn ids and cancel — a `turn` field on every turn-scoped event, and `{"type":"cancel"}` aborting an in-flight turn the way Ctrl-C does | v0.1.4 | [`machine-turn-control.md`](v0.1.4/machine-turn-control.md) | 🔒 27a |
| 27d | The schema artifact — `apogee __machine-schema` prints the protocol as JSON Schema, conformance-pinned to the code, shipped in the release archives | v0.1.4 | [`machine-schema-artifact.md`](v0.1.4/machine-schema-artifact.md) | 🔒 27a, 27c |
| 27e | Machine-readable reads — `--output-format json` on `models`/`chats`/`agents`/`mcp`/`check`, the same facts as the human view | v0.1.4 | [`machine-readable-reads.md`](v0.1.4/machine-readable-reads.md) | 🟢 |

### v0.1.5

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 28a | The task runner and its ledger — goal in; the application plans, drives successive agent-loop turns, checks stated acceptance, corrects, stops on done or budget; resumable from a manifest-per-transition ledger under a lock | v0.1.5 | [`task-runner-core.md`](v0.1.5/task-runner-core.md) | 🟢 |
| 28b | Task autonomy policy — pre-declared per-task grants and question answers, composed no wider than config × agent policy, every use recorded | v0.1.5 | [`task-autonomy-policy.md`](v0.1.5/task-autonomy-policy.md) | 🔒 28a |
| 28c | Task surfaces — machine-mode task events, `task status`/`list` as JSON, admin-plane reads; control stays CLI-only | v0.1.5 | [`task-surfaces.md`](v0.1.5/task-surfaces.md) | 🔒 28a |

### v0.1.6

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 29a | The code graph — tree-sitter parses a source tree into the existing graph store, zero model calls; edges tagged extracted/inferred; `graph update` rescans changed files only | v0.1.6 | [`code-graph-extraction.md`](v0.1.6/code-graph-extraction.md) | 🟢 |
| 29b | Graph navigation — `graph path`/`explain`/`query` over one traversal core, plus the read-only `graph` toolset served to MCP clients via `__mcp-tools` | v0.1.6 | [`graph-navigation.md`](v0.1.6/graph-navigation.md) | 🟢 |
| 29c | Graph artifacts — the Markdown architecture report, a self-contained interactive HTML file, GraphML and Mermaid exports; a file, never a server | v0.1.6 | [`graph-artifacts.md`](v0.1.6/graph-artifacts.md) | 🔒 29a, 29b |

### v0.1.7

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 30a | The MLX backend — a persistent Python child over pipes (`mlx-lm` in the owned venv), token streaming, KV held across turns, tools via the model's own template; macos-arm64 only, refusing loudly elsewhere | v0.1.7 | [`mlx-backend-core.md`](v0.1.7/mlx-backend-core.md) | 🟢 |
| 30b | MLX model operations — the store's `mlx/` format row, `models pull` of mlx-community refs through the ladder, `convert --mlx`, real windows from `config.json` | v0.1.7 | [`mlx-model-operations.md`](v0.1.7/mlx-model-operations.md) | 🔒 30a |
| 30c | MLX depth — vision via `mlx-vlm`, `serve` routing allowed (the exclusion list is about credentials), and `train promote --target mlx` skipping GGUF conversion | v0.1.7 | [`mlx-depth.md`](v0.1.7/mlx-depth.md) | 🔒 30a, 30b |
