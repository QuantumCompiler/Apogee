# Knowledge layer + knowledge graph (placeholder — split before build)

**What / why.** Coverage placeholder for Ommi's two deepest product layers (~six shipped phases, milestones H + T), riding entirely on the embed store with zero new storage services. It MUST be split into roughly four backlog docs before any agent builds — (1) records + capture — including chat's in-session /capture slash command and auto_capture, (2) query/lifecycle/HTTP draft-refine, (3) graph build + retrieval-time expansion, (4) communities/dedupe/named graphs — this entry guards the coverage map and the invariants that must survive the split. Knowledge layer: canonical decision records carrying Ommi's FULL schema — intent, decision, status, discipline, downstream_link, provenance{source, attribution}, raw_ref, timestamp, supersedes; the split docs must enumerate it from Ommi's KNOWLEDGE.md, not from this parenthetical (downstream_link in particular is load-bearing: write-time linking, anonymize preserves it). Records are normalized by a binary-embedded clerk prompt via one provider-native structured-output call (an upgrade over Ommi's prompt-embedded schema), stored as chunk metadata in a standard collection with the raw conversation archived separately; index built only from immutable intent+decision so metadata edits never stale it; query defaulting to shipped status; capture --dry-run on the CLI; the stateless draft→refine→store contract on the admin plane (server never holds a draft; refine is one bounded ≤2000-char revision pass that never stores); anonymized export strips attribution and raw_ref while preserving the provenance chain. Graph: per-chunk extraction clerk with a closed entity whitelist and host-side validation (caps 12/16), kg_* tables with resumable incremental builds whose staleness fingerprint includes the max chunk id, retrieval-time budgeted transient expansion (~1500 runes, seeded pre-rerank) on every RAG surface, label-propagation communities stored as retrievable pseudo-chunks, vector dedupe (decision nodes exempt), named multi-collection graphs, and deterministic materialization of records as reserved `decision` nodes the extractor can never emit.

**Core constraint(s).**
- Archive rich, surface thin: full raw record on disk, thin queryable index from immutable fields only; attribution never enters the searchable index; raw conversations never exported
- Extraction output validated in host code (closed type set, per-chunk caps); the extractor can never emit the reserved decision type
- Deterministic edges are facts (idempotent, weight 1); extracted relations gain corroboration weight; dedupe never merges decision nodes
- The refine loop is stateless server-side — drafts live with the client
- Graph names must never collide with collection names (resolution is graphs-first, check-enforced)

**Seam + files.** lib/src/knowledge/ (record.h/.cpp schema/validate/anonymize, embedded capture+refine prompts and schema via #embed/codegen, store over embedstore, raw archive), lib/src/graph/ (extraction contract, BuildMulti over injected extract/embed closures, materializeRecords, DetectCommunities), lib/src/embedstore/graph*.cpp (kg_* tables, GraphExpand, communities, dedupe), lib/src/cli/knowledge.cpp + graph.cpp, lib/src/httpserver/knowledge.cpp + graph.cpp slices; all clerks injected closures so both layers test model-free; lib/test/knowledge/ + lib/test/graph/.

**Reference (Ommi).** src/knowledge (Record, CapturePrompt/RefinePrompt/CaptureSchema, Store, archive-rich/surface-thin), src/graph (BuildMulti, materializeRecords, DetectCommunities), src/embedstore graph files (kg_* tables, GraphExpand, communities, dedupe), httpserver knowledge.go/graph.go/admin_graphs.go — milestones H + T, OMMI-10, C3. Divergences: all clerks become provider-native structured-output calls (Anthropic tool-forcing / OpenAI json_schema / Gemini responseSchema); cloud embedders make entity vectors and dedupe usable in the default config; batch APIs are the natural fit for full graph builds' per-chunk cost.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Ommi built the entire graph layer in ~2 days only because the embedstore/agentloop seams were mature (sequencing lesson #7); gating on the retrieval turn integration reproduces that readiness.

**Open calls:**
- [user] Extraction/clerk cost policy on cloud: batch APIs for full builds vs a local extractor role by default (metered spend)
- [default: Ommi's order — knowledge layer first] Whether the knowledge layer ships before the graph or the split re-sequences
- [default: decided at grooming] Exact four-way split boundaries

**Guardrail(s).** The split documents must each carry the invariant list above; the staleness-fingerprint and reserved-type rules get dedicated regression tests in whichever split item owns them; round-trip tests (one-shot capture vs draft→refine(noop)→store field equivalence) with injected-clerk fixtures so no test needs a model.

**Acceptance criteria:**
- [ ] Placeholder-level: this document is split into build-sized items before implementation begins
- [ ] Design-level acceptance the split must preserve: search index built only from immutable fields; the reserved decision node type creatable only by the deterministic pass; the staleness fingerprint includes max chunk id (same-count re-ingests re-extract); graph expansion transient/budgeted/best-effort and seeded pre-rerank — its failure is never the reason a turn loses its chunks; draft→refine→store over HTTP field-equivalent to one-shot capture with refine never storing; anonymized export keeps the provenance chain; editing link/status never triggers re-embedding

**Scope note.** gated on [vector-hybrid-rerank.md](vector-hybrid-rerank.md) shipping.
