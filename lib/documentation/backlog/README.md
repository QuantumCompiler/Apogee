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

The **training track** (22 — **split at grooming 2026-09-19** into 22a datasets/kits/the Python boundary → 22b runs → 22c pipelines/regime/cycle). Training is a confirmed direction (2026-08-24), **scheduled for v0.1.0 by the user on 2026-09-19**. **22a and 22b shipped 2026-09-19** ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone Z), so 22c is claimable.

| # | Document | Build after | What |
|---|---|---|---|
| 22c | [training-pipelines.md](training-pipelines.md) | ✅ 22b (shipped) | Multi-stage pipelines with cumulative gates and fused-checkpoint chaining, `train regime` distilling a teacher across kits, and the scheduler-invoked cycle with the anchor-baseline dual gate, consented session sources, and the circuit breaker |
