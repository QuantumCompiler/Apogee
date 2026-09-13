# Embedding clients: OpenAI/Google endpoints + in-process llama.cpp, behind can_embed

**What / why.** The embedding supply side, deferred out of the v0.1.0 backend items because nothing consumes vectors until the RAG ring (they would otherwise ship as dead code in the release's two heaviest items): OpenAI (text-embedding-3-*) and Google (gemini-embedding) embeddings-endpoint clients with batch input over the shared HTTP client from anthropic-backend, the in-process llama.cpp embedding path, dimension discovery, and each backend's implementation of the can_embed capability interface declared in harness-core. Anthropic has no embeddings endpoint and correctly reports can_embed=false — the capability flag, not a type allowlist, is what the retriever gate downstream reads. This item is the EmbedFunc seam's supplier; vector-hybrid-rerank is its consumer.

**Core constraint(s).**
- Embedding capability is a per-backend flag, never a hard-coded type list
- Batch-first API shape — per-chunk calls are the graph item's recorded cost trap
- Same key-hygiene and no-listening-socket rules as every backend

**Seam + files.** lib/src/cli/source/backends/openai_embed.cpp, lib/src/cli/source/backends/google_embed.cpp, lib/src/cli/source/backends/llamacpp_embed.cpp, per-backend can_embed/dimension implementations against the harness capability interface, lib/src/cli/tests/backends/ embedding fixtures (recorded traces for cloud; tiny-model or shim for llama.cpp).

**Reference (Ommi).** src/backends direct_embed.go and the embedding half of OMMI-11 (llamacpp path). The cloud embedding clients are Apogee divergence: Ommi's cloud could not embed (Anthropic has no embeddings endpoint), which is exactly why its type-allowlist gate doesn't transfer — OpenAI and Google DO embed over the API.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Slots between the lexical floor and the vector matrix so the v0.1.0 backends ship no dead code and vector-hybrid-rerank's gate chain actually contains its embedders (the missing-dependency fix from verification).

**Open calls:**
- [default: provider-documented maxima] Embedding batch-size defaults per provider (the metered-spend policy for auto retrieval lives on vector-hybrid-rerank, not here)

**Guardrail(s).** Recorded-fixture tests for both cloud providers; a batch-splitting boundary test; the dimension fixtures shared downstream.

**Acceptance criteria:**
- [ ] Batch embeddings round-trip for OpenAI and Google against recorded fixtures, with retry/backoff and key hygiene matching the chat clients
- [ ] In-process llama.cpp embeddings are produced for a batch of texts
- [ ] can_embed and dimensions are discoverable per backend through the harness capability interface; Anthropic reports false
- [ ] Dimension-mismatch fixtures exist for vector-hybrid-rerank's per-store binding tests to consume

**Scope note.** **Gate satisfied 2026-09-07, and the whole of item 15 shipped 2026-09-12.** The floor this waited on — the SQLite/FTS5 chunk store, the chunker, ingest, the `Store::search` seam every retriever will implement, and now the `embeddings:` config section a per-collection `backend:` hangs on — is all recorded in [Milestone Q](../assistant/MILESTONES.md#milestone-q--the-retrieval-floor). Read it for what already exists before designing against it; the template's commented `# backend: embedder` line under `embeddings:` marks where this item's field goes.
