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

### Phase 1 — v0.1.0: walking skeleton, then breadth

Items 1–6 build strictly in order (each gates on the one before it — the skeleton proves config → harness → backend → terminal on exactly one provider before anything widens). After 6, the three branches **7, 8, 9 are independent** — take them in any order (or in parallel sessions). Item 7 was split at grooming (2026-08-26) into 7a and 7b; both have since shipped. Item 10 is the release closer and assumes 1–9 are complete.

*Items 1 (C++ project skeleton), 2 (config engine), 3 (harness core), 4 (Anthropic backend), 5 (`apogee complete`), 6 (the shared agent loop), 7a (the terminal UX layer), 7b (`apogee chat`), and 7c (line editing) shipped 2026-08-25/26 — see [MILESTONES.md](../assistant/MILESTONES.md) → Milestones A–H. **The walking skeleton is closed**, the loop is extracted, and both surfaces are complete; only the two remaining backends and the release closer are left. The numbering below is unchanged so the gate references in each document still read true.*

| # | Document | Build after | What |
|---|---|---|---|
| 9 | [llamacpp-backend.md](llamacpp-backend.md) | ✅ 5 (shipped) | Local inference: llama.cpp linked in-process (generation, KV sessions) |
| 10 | [install-check-lifecycle.md](install-check-lifecycle.md) | 1–9 (all of v0.1.0) | Install contract, `apogee check` doctor, completions (release closer) |

### Phase 2 — the gated ring (after v0.1.0 ships)

Six semi-independent tracks that can interleave: the **vendor-CLI family** (11 → 12), the **front-end contract** (13 — the GUI project gates on it), **local-model depth** (14), **RAG** (15 → 16 → 17), **serving** (18 → 19 — server deployments only), and **tools/agents** (20); item 21 needs the RAG track complete. The numbering is the suggested serial order when working alone.

| # | Document | Build after | What |
|---|---|---|---|
| 11 | [claude-cli-backend.md](claude-cli-backend.md) | — | Claude CLI backend: persistent child process with token-level streaming (subscription-plan path; family template) |
| 12 | [vendor-cli-backends.md](vendor-cli-backends.md) | 11 · **split first** (one doc per CLI) | Vendor CLI backends: codex (OpenAI), gemini (Google), ollama (Ollama cloud) |
| 13 | [stdio-machine-mode.md](stdio-machine-mode.md) | — | Stdio machine mode: structured JSONL event stream for front-end drivers (GUI ↔ CLI over pipes, never localhost) |
| 14 | [model-profiles-and-management.md](model-profiles-and-management.md) | — · **split first** (profiles / management+sources) | Local-model depth: per-family profiles, filters, tool dialects + roles, models suite, open model sources (HF + Ollama) |
| 15 | [embedstore-lexical-rag.md](embedstore-lexical-rag.md) | — | SQLite chunk store with FTS5/BM25 lexical retrieval + basic --rag injection |
| 16 | [embedding-clients.md](embedding-clients.md) | 15 | Embedding clients: OpenAI/Google endpoints + in-process llama.cpp, behind can_embed |
| 17 | [vector-hybrid-rerank.md](vector-hybrid-rerank.md) | 16 | Vector + hybrid retrieval, per-turn resolver, and LLM rerank with a capability-driven gate |
| 18 | [serve-public-plane.md](serve-public-plane.md) | — | `apogee serve` — OpenAI-compatible HTTP server with server-side sessions (server deployments only: remote REST clients) |
| 19 | [admin-plane-foundation.md](admin-plane-foundation.md) | 18 · **split first** (plane / credstore) | /v1/admin foundation: bearer auth, events bus, jobs, first CRUD, parity test + provider credential store |
| 20 | [mcp-client-tools-agents.md](mcp-client-tools-agents.md) | — · **split first** (native tools / MCP client / analyze+agents) | MCP client, in-process native tools + analyze/agents runner |
| 21 | [knowledge-graph-stack.md](knowledge-graph-stack.md) | 17 · **split first** (four docs) | Knowledge layer + knowledge graph (placeholder) |

### Phase 3 — unscheduled

| # | Document | Build after | What |
|---|---|---|---|
| 22 | [training-distillation.md](training-distillation.md) | 14, if scheduled at all · **split first** | Training and distillation stack (placeholder — Python boundary; direction confirmed 2026-08-24) |
