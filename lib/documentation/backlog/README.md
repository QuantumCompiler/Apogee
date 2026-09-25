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

Items are numbered in the **suggested order of implementation**. The rule stays the same: the topmost unbuilt item whose gate is satisfied is the next one to build.

**Gate convention:** a v0.1.0 item is buildable from its transitive gate chain alone. A gated-ring or unscheduled item *additionally assumes the complete v0.1.0 set has shipped* — its "build after" names only the ring-internal ordering. Items marked **split first** must be groomed into their listed sub-documents before an agent takes them; do not build from the guard document directly.

### Phase 1 — v0.1.0: complete

**Every v0.1.0 item has shipped.** Items 1–6 built strictly in order (each gating on the one before it — the skeleton proved config → harness → backend → terminal on exactly one provider before anything widened); after 6 the three branches **7, 8, 9** were independent; item 7 was split at grooming (2026-08-26) into 7a/7b/7c; and item 10, the release closer, landed 2026-09-01.

*Items 1 (C++ project skeleton), 2 (config engine), 3 (harness core), 4 (Anthropic backend), 5 (`apogee complete`), 6 (the shared agent loop), 7a (the terminal UX layer), 7b (`apogee chat`), 7c (line editing), 8 (OpenAI + Google backends), 9 (the llama.cpp backend), and 10 (the install contract, `apogee check`, and completions) shipped 2026-08-25 through 2026-09-01 — see [MILESTONES.md](../assistant/MILESTONES.md) → Milestones A–K. Nothing in Phase 1 is pending; the numbering is preserved here because the gate references in the Phase-2 documents still read against it.*


### Phase 2 — the gated ring (after v0.1.0 ships)

Seven semi-independent tracks that can interleave: the **vendor-CLI family** — **complete**: 11 shipped 2026-09-02, and 12's three split items (12a codex, 12b gemini, 12c ollama) all shipped 2026-09-06, the **front-end contract** (13 — shipped 2026-09-06, and the GUI project gates on it), **local-model depth** (14, split at grooming 2026-09-06 into 14a/14b/14c/14d; **all four shipped 2026-09-07 — the track is complete**), **RAG** (15 → 16 → 17; **all three shipped, 2026-09-12 and 2026-09-13 — the track is complete**), **serving** (18 → 19a → 19b — server deployments only; **all shipped 2026-09-13**, 19 having been split at grooming that day into the admin plane and the provider credential store — **the track is complete**), and **tools/agents** (20 — **split at grooming 2026-09-13** into 20a native toolsets → 20b MCP stdio client → 20c analyze/agents; **all three shipped 2026-09-13 — the track is complete**), and the **knowledge track** (21 — **split at grooming 2026-09-13** into 21a records + capture → 21b query/lifecycle/HTTP → 21c graph build + expansion → 21d communities/dedupe/named graphs; **21a shipped 2026-09-13, 21b, 21c and 21d 2026-09-19 — the track is complete**). The numbering is the suggested serial order when working alone.

**Every Phase-2 item has shipped.** The seven tracks are recorded in [MILESTONES.md](../assistant/MILESTONES.md) → Milestones L–Y; nothing in the ring is pending.

### Phase 3 — the outer ring (scheduled for v0.1.0)

The **training track** (22 — **split at grooming 2026-09-19** into 22a datasets/kits/the Python boundary → 22b runs → 22c pipelines/regime/cycle). Training is a confirmed direction (2026-08-24), **scheduled for v0.1.0 by the user on 2026-09-19**. **All three shipped 2026-09-19** ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone Z) — **the track is complete, and nothing in the outer ring is pending.**

### Phase 4 — after v0.1.x

Three tracks. **23** stands alone. **24, local agent tools**, came out of a spike on 2026-09-25 and was split into six items the same day. The spike found that local models were never shown the tools every other backend already has, measured llama.cpp's own tool-calling layer on real weights (6/6 tasks on Qwen3.8-27B and Qwen3-VL-8B), and turned up an outbound-data exposure in today's tools. The user's four calls are recorded in the items: SearXNG for search, ask per new website, the launch folder as the file root, and 8B-class models and up. In build order: 24a first (it closes today's exposure); 24b is the unlock; 24c–24f follow it or 24a, as each names.

**25, small-model depth**, came out of a review on 2026-09-25 of what else would let small local models work at their best. It was split into twelve items the same day, with every group the review proposed taken by the user:
- **automatic attachments**: documents, code and folders indexed by the embedding model and handed to the model per turn; images, audio and video read natively or through helper models;
- **helper-model roles** (vision, transcription, utility);
- **reliability**: grammar-constrained JSON, tool selection by relevance, sampling that reaches the model, and thinking control;
- **speed and memory**: a context sized to the machine, a persistent prompt cache, and speculative decoding measured before it is built;
- **memory across chats**: a per-turn context budget and automatic recall.

The user's calls are recorded in the items: attachments kept with their chat and cached by hash, helper models used automatically, and external converters (`pdftotext`, `ffmpeg`).

| # | Item | Status |
|---|---|---|
| 23 | [Terminal markdown rendering](terminal-markdown.md) — an answer's Markdown rendered as it streams on a TTY; pipes, machine mode and history untouched | specced 2026-09-23; two **[user]** open calls (hand-written vs md4c, code highlighting) block the build |
| 24a | [Tool safety defaults](tool-safety-defaults.md) — `fetch_url` asks per new website, redirects hop by hop, and the file tools default to the launch folder | specced 2026-09-25; no **[user]** calls open; gated on nothing |
| 24b | [Local tool calling](local-tool-calling.md) — the llama.cpp backend renders tools through the model's own template, parses its calls, and constrains them by grammar, via llama.cpp's `common` chat layer linked in-process | specced 2026-09-25; build after 24a |
| 24c | [Hybrid prompt checkpoints](hybrid-prompt-checkpoints.md) — Qwen3.5/3.8 re-read only what is new each turn and tool step, via state checkpoints as llama-server keeps them | specced 2026-09-25; build after 24b |
| 24d | [Tool ergonomics](local-tool-ergonomics.md) — capped command output, line-range reads, `edit_file`, `grep_files`, and an environment note (date, OS, folder) | specced 2026-09-25; build after 24b |
| 24e | [Web search via SearXNG](web-search-searxng.md) — `web_search` over the user's own SearXNG, pluggable, never silently empty | specced 2026-09-25; build after 24a and 24b |
| 24f | [`fetch_url` as a reader](fetch-url-reader.md) — main content with its links as Markdown, paging, content types, a download cap | specced 2026-09-25; build after 24a |
| 25a | [A context window sized to the machine](context-fit-defaults.md) — 32K by default instead of the trained window (16 GiB of cache on Qwen3.8), an 8-bit cache, and the cost shown | specced 2026-09-25; gated on nothing |
| 25b | [Helper-model roles](helper-model-roles.md) — `vision`, `transcription` and `utility` in the one resolver; titles, compaction, query rewriting and big tool results move to the utility model | specced 2026-09-25; gated on nothing |
| 25c | [A per-turn context budget](context-budget.md) — each source gets a share of the real window; finished turns' tool results sent as stubs | specced 2026-09-25; gated on nothing |
| 25d | [Attachments: documents, code and folders](attachments-documents.md) — `/attach`, indexed with the embedding model, inlined when they fit, retrieved per turn with citations, kept with the chat | specced 2026-09-25; build after 25c |
| 25e | [Attachments: images, audio and video](attachments-media.md) — native when the model can, a helper model when not; videos become searchable timelines | specced 2026-09-25; build after 25b and 25d |
| 25f | [Structured output by grammar](local-structured-output.md) — JSON Schema constrained token by token on local models | specced 2026-09-25; build after 24b |
| 25g | [Tool selection by relevance](tool-selection.md) — the relevant few tools per step, and `find_tools` for the rest | specced 2026-09-25; build after 24b |
| 25h | [Sampling local models are meant to be run with](sampling-profiles.md) — `-t` reaches the model (it is silently ignored today), GGUF and family defaults | specced 2026-09-25; build after 24b |
| 25i | [Thinking control](thinking-control.md) — `/think on\|off\|auto`, a thinking budget, mapped to every vendor | specced 2026-09-25; build after 24b and 25h |
| 25j | [A persistent prompt cache](persistent-prompt-cache.md) — resumed chats and repeated tool prompts restored from disk | specced 2026-09-25; build after 24c and 25a |
| 25k | [Speculative decoding, measured first](speculative-decoding.md) — MTP, a draft model or n-grams, built only on a clean ≥1.3× win | specced 2026-09-25; build after 24b and 24c; the build half is conditional |
| 25l | [Recall across chats](recall-across-chats.md) — past chats summarised and retrieved per turn; never on `serve` | specced 2026-09-25; build after 25b and 25c |
