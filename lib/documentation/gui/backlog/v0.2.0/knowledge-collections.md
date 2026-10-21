# Knowledge and collections: the record layer's screens

**What / why.** Two whole families with no GUI place (the 2026-10-20 audit; the TUI has had both as views since 37c): **Knowledge** — the organizational records (`knowledge list|show|query|refine|delete|export`, their JSON documents shipped with the TUI's reads) — and **Collections** — the embed store (`embed list|ingest|query|delete`, collection rows, the retriever honesty rules). This item builds both screens plus the graph screen's missing half: **prose graphs and communities** (1l shipped code graphs only, its recorded out). The shapes mirror what the TUI proved: Knowledge as a browsable, queryable record list with refine/delete and export; Collections as the store's rows with ingest (a supervised job over the jobs core — its narration is busy-line text, the stderr-parse fallback until a CLI stream lands, recorded) and query; the graph screen grown with the prose-graph builds, communities, and the decision nodes. Retrieval honesty renders as the CLI says it: the retriever that ran, scores on their scales, demotions said.

**Core constraint(s).** ADR 0001/0002; every read a `--output-format json` document, re-read after mutations (read back, don't mirror); deletes confirm; ingest and builds are supervised jobs, their outputs the commands' own; the retriever/score honesty is the CLI's wording relayed, never recomputed. **Design boards land with the screens** (the design's new-screen law — Knowledge and Collections boards, the graph board grown, contract-board rows).

**Seam + files.** `source/presentation/knowledge/`, `source/presentation/collections/` (screens + view-models), the graph screen grown (`source/presentation/graph/`), `source/business/reads/` (the families' commands), the jobs core for ingest/builds; `lib/design/spec/` — the boards.

**Reference.** The TUI's 37c (Knowledge, Collections, Graph views over the same reads) — the proven surface cut; the contract-board pattern (fallback until a stream exists) from the downloads screen; 26s — the score-honesty wording relayed.

**Decisions made** (dated):
- 2026-10-20 — From the parity audit (the user's mandate); the TUI's view cut mirrored as the screens' scope.
- 2026-10-20 — Ingest/build progress uses the stderr-parse fallback; a CLI stream for them is a recorded later growth, not this item's.

**Open calls:**
- [default: screen layouts per new design boards drawn in the prototype's idioms; details at build.]

**Guardrail(s).** View-model matrices over fixture documents (records, collections, prose graphs, communities, the retriever wordings); job supervision pins for ingest/build; delete confirmations; the boards' health check green; hermetic throughout.

**Acceptance criteria:**
- [ ] Knowledge and Collections screens list, query, mutate-with-read-back and export over the real commands; ingest and graph builds run supervised with stop.
- [ ] The graph screen shows prose graphs, communities and decision nodes beside the code graph; retrieval honesty reads as the CLI says it; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 2); gated on 2a, the parity law (shipped 2026-10-20: [MILESTONES → Milestone C](../../assistant/MILESTONES.md#milestone-c--cli-parity)). Out of scope: new CLI streams for ingest/builds (recorded later), the console (2b), recall settings (Settings owns them).
