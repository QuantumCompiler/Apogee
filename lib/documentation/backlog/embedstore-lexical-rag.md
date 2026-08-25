# SQLite chunk store with FTS5/BM25 lexical retrieval + basic --rag injection

**What / why.** The RAG floor, built model-free first exactly as Ommi's history dictates (BM25 rescued RAG during the embedding freeze): vendored SQLite amalgamation compiled with FTS5, per-collection DBs under ~/.apogee/embeddings/, rune-based chunking (512/64 defaults, real UTF-8 codepoint iteration, PDF via pdftotext subprocess — the project's one optional external binary: PDFs are skipped with a named warning when it is absent, and the check doctor gains an optional informational line for it — binary sniffing), an always-maintained external-content FTS5 index with insert/delete/update triggers created in one transaction, a term-quoting query builder so natural language can never inject FTS5 syntax, normalized BM25 scores (s/(1+s)), and the `apogee embed ingest|list|info|query|browse|delete` CLI with auto-registration of embeddings: config entries through the config-engine helpers. Wires --rag (and the auto_rag config key — always-on injection without the flag, read at turn build) into complete/chat via the agentloop transient-splice seam: top-k chunks prepended to the outgoing request only, retriever always reported in every envelope. Lexical works with zero models, zero keys, fully offline — the rationale shifts from Ommi's 'no cloud embeddings exist' to 'works offline/keyless' and is kept deliberately.

**Core constraint(s).**
- Retrieval must never depend on having an embedding model — lexical BM25 is the permanent floor (no model, no backend, no network)
- The FTS index is maintained on every ingest regardless of retriever
- Injected context is transient: outgoing request only, never persisted history
- Every surface reports which retriever produced a score (scales are incomparable)
- Open-time migrations commit in one transaction; triggers self-heal with IF NOT EXISTS
- Mutating embed CLI actions landing before the admin plane exist go into the parity table's documented-skip list; this feature area owns the embeddings-data-plane admin backfill

**Seam + files.** lib/src/cli/source/embedstore/store.h/.cpp (open-time migrations in single transactions, chunk CRUD, ChunkText rune chunker, store_meta), lib/src/cli/source/embedstore/fts.cpp (ensureFTS, ftsMatchQuery term-quoting, BM25 normalization), lib/src/cli/source/embedstore/ingest.cpp (file collection, PDF, binary sniff), lib/src/cli/source/commands/embed.cpp, lib/src/cli/source/agentloop/rag.cpp (BuildRAGPrefix, transient injection), vendored SQLite amalgamation w/ FTS5, lib/src/cli/tests/embedstore/ (score-contract fixtures + FTS injection corpus).

**Reference (Ommi).** src/embedstore (external-content FTS5 index, ftsMatchQuery quoting, s/(1+s) normalization, ChunkText, dim=0 sentinel groundwork, single-transaction Open migrations), src/agentloop rag.go, cmd/ommi embed.go, OMMI-5 lexical-first history. Divergence: none by design — even though Apogee has cloud embedders, the lexical floor is kept as the permanent offline/keyless guarantee.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Ommi milestone G's survival lesson: RAG lived through its embedding freeze only because lexical was model-free — build the no-model path first; the store is also the substrate the knowledge stack later rides with zero new storage services.

**Open calls:**
- [default: vendored amalgamation — FTS5 flag control] Vendored SQLite vs system sqlite3
- [default: utf8proc] UTF-8 handling for rune-based chunking: utf8proc vs hand-rolled decoder
- [default: 512/64, revisit per corpus] Default chunk sizes (ADR-style corpora want 768)

**Guardrail(s).** Cross-surface score-contract tests (CLI query vs library API produce identical rankings, constants pinned); the FTS injection corpus as a permanent regression suite; the migration-interruption test.

**Acceptance criteria:**
- [ ] ingest → query round-trips on a docs directory with zero models and zero backends configured; scores normalized and labeled lexical
- [ ] FTS operators and injection-shaped queries (AND, NEAR, quotes, asterisks, column filters) return results, never syntax errors (fuzz/test corpus)
- [ ] --rag on chat injects chunks into the outgoing request while persisted history stays clean (grep test); the status line reports chunks injected + top score + retriever
- [ ] Interrupted first-open migration rolls back and retries whole (single-transaction test); FTS validation uses the content-comparing `integrity-check 1` form; re-ingesting a source replaces its chunks
- [ ] Auto-registration writes the embeddings: config entry through the comment-preserving helpers (byte-diff verified)

**Scope note.** gated on [agentloop-core.md](agentloop-core.md) shipping.
