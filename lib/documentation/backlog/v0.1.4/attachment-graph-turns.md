# Graph-aware attachment turns

**What / why.** A graph built at attach time ([27n](attachment-code-graph.md)) is worthless if turns can't reach it — and today they can't, three ways, all measured in the 2026-10-03 spike: the attachments retrieval path returns before the graph section is packaged (`agentloop/rag.cpp`, the early `package_attachments` return), the one graph-resolution decision is keyed on *config collections* (`graph_context::resolve_turn_graph`) which the chat's attachment store is not, and no surface ever sets `graph_enabled` on an attachments turn. This item closes all three and adds the delivery that actually matters: **the model walks the graph itself.** Concretely: **(a)** an attachment turn whose store holds a graph runs the existing retrieval-time expansion — seeded from the excerpt hits, rendered after the excerpts, under the same budget rules collection turns already obey; **(b)** [27l](graph-navigation.md)'s read-only `graph` toolset is registered in a chat whose attachments carry a graph, **scoped to that store**, so a structural question becomes `graph_explain` / `graph_path` calls instead of invented paths; **(c)** the attachments line gains the graph-entity count collection turns already report. The stress-test transcript — a model attached this repository inventing `lib/src/core/` and cycling — is this item's motivating case and its real-weights check, inverted.

**Core constraint(s).**
- **Passive injection stays bounded; the tools do the walking.** Expansion keeps the existing seeding (top-k before the judge), hop and entity budgets; the correction for the stress test is the toolset, not a bigger transient prefix.
- **Read-only, everywhere** — 27l's rule carries: the scoped toolset never writes, registers ungated, and a future write verb is a different item.
- **One traversal core** (27l's): the scoped tools are 27l's implementations handed the attachment store's path — no attachment-flavored fork of the walk.
- **Transient stays transient:** the graph section rides the same per-turn transient prefix as excerpts, never persisted history (the load-bearing property `agentloop/rag.h` documents).
- **The model-free guarantee holds through use:** entity matching and traversal over an attachment graph work with no embedder (27l's lexical path), so a lexical-only chat walks its graph too.
- **Scoping is explicit, not config:** the attachment store is resolved from the chat's own state, never by teaching `resolve_turn_graph` to find it in `graphs:` config — a chat's attachments are not a named graph, and collection precedence rules must not grow a special case.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `agentloop/rag.cpp` — the attachments branch learns the graph section: build it (the existing labelled-section builder, store = the attachment database) before `package_attachments`, render it after the excerpts under the same fitting rules the collection path uses.
- `agentloop/rag.h` — `RagTurn`'s existing graph fields documented as serving attachment turns too; `cli/helpers.cpp` sets them for an attachment turn when the store holds `kg_*` rows.
- `cli/chat.cpp` / `cli/chat_attachments.cpp/.h` — the chat registers the scoped `graph` toolset when its attachment store holds a graph (and drops it when the last graphed attachment is detached); `ChatAttachments` exposes the store's graph state.
- `tools/` — 27l's toolset constructed over an injected store path (it already is, for named graphs); registration plumbing only.
- Tests: `tests/business/agentloop/` for the attachment-turn expansion (budget, seeding, lexical-only); `tests/presentation/commands/` for toolset registration/deregistration; golden: the same question over the same fixture graph yields identical payloads via CLI verb and scoped tool (27l's one-core assertion extended).
- Consumes: [27n](attachment-code-graph.md) (the graph exists), [27l](graph-navigation.md) (verbs, toolset, caps, addressing), [27k](../../assistant/MILESTONES.md#milestone-ae--the-code-graph) (what the graph contains); 26g (shipped) governs how the added tools rank in selection — no special pleading.

**Reference.** The expansion half builds on Milestone Y's shipped retrieval-time expansion (collection-scoped there).

**Decisions made** (dated):
- 2026-10-03 — Split from the attachment-representation spike as the payoff item: the spike's central design judgment — the stress test failed on *passive injection*, so the graph's value must arrive through tools the model calls — is this item's shape. Expansion rides along because the plumbing is one seam away, but the toolset is the point.
- 2026-10-03 — Scoping is per-chat state, not config, so the one-decision rule (`resolve_turn_graph`) stays about collections and named graphs with no third precedence case.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): attachment turns expand through the attachment graph, `auto_rag` turns through their own — two sections at most, own budgets, never merged.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): scoped tool descriptions carry the attachment root so 26g ranks them against the question.

**Guardrail(s).**
- The attachment-turn expansion tables: seeded from excerpt hits, budget enforcement, lexical-only traversal, empty-graph turns identical to today's.
- Registration lifecycle: toolset present after a graphed attach, absent after detach, never present on a chat with chunk-only attachments.
- 27l's one-core golden extended to the scoped instance.
- On real weights, the capability and the measure (the stress test inverted): with this repository attached in a sandboxed chat, the model families answer a structural question ("where is retrieval implemented?") with at least one graph-tool call and **every cited path existing** under the attachment root.

**Acceptance criteria:**
- [ ] An attachment turn over a graphed store injects excerpts plus a bounded graph section, and the line reports the entity count; a chunk-only store behaves exactly as today.
- [ ] A tool-using chat with a graphed attachment lists the scoped `graph_*` tools; detaching the folder removes them.
- [ ] `graph_explain` through the scoped toolset equals the CLI verb's payload over the same store, golden-compared.
- [ ] A lexical-only chat (no embedder) walks its attachment graph through the tools.

**Scope note.** Item **27o** (30e under the then-v0.1.7, 31o under the then-v0.1.8, until 2026-10-03's merge and migration — the user's calls), earmarked for **v0.1.4**; **gated on [27l](graph-navigation.md), [27n](attachment-code-graph.md)** ([27k](../../assistant/MILESTONES.md#milestone-ae--the-code-graph) shipped 2026-10-04). Out of scope: write tools; growing the transient graph budget (the fix is tools, not more injection); the flags/config surface ([27p](attachment-options.md)); forcing the model through the graph (27l's recorded divergence from Graphify stands).
