# Vector + hybrid retrieval, per-turn resolver, and LLM rerank with a capability-driven gate

**What / why.** The full retrieval matrix atop the lexical floor: the EmbedFunc seam (func(ctx,texts)→vectors; nil = cannot embed) fed by the embedding-clients item's cloud and in-process embedders (a genuine capability gain over Ommi); per-store binding of embed model + dimensions in store_meta with hard fallback-to-lexical on mismatch and a re-ingest hint; brute-force cosine over float32 blobs with the dim=0 lexical-only sentinel; the centralized one-per-turn retriever resolver shared by every surface (flag > collection pin > auto; auto picks vector only on capability + model match + full coverage + single dim space, never hybrid; vector-pinned collections excluded-with-note from lexical turns, never silently searched the other way); explicit-only hybrid via RRF rank fusion (k=60, halves fetched 50 deep, rank-only — never raw scores) with honest per-collection degradation; and rerank as one judge generation call per turn (~10× widened candidates capped 50×400 runes) with the never-fail (ranked, applied) contract — every failure path returns raw order with reranked:false — using provider-native structured output so 'reply is not a ranking' failures largely vanish. Retriever/rerank choices persist in chat session meta and restore on resume; check rejects unrecognized retriever:/rerank: values so a typo never silently means auto.

**Core constraint(s).**
- Never fuse raw scores across retrievers — ranks only (BM25/cosine/RRF scales are incomparable)
- One retriever per turn, resolved once through one shared resolver, reported honestly everywhere including degradations — nothing claims hybrid/reranked when it degraded
- A vector-pinned collection is excluded from a lexical turn with a note, never silently searched the other way
- Rerank can never cost context; a broken judge degrades to raw order; the judge may legitimately drop everything (the relevance floor)
- check must reject unrecognized retriever:/rerank: config values

**Seam + files.** lib/src/cli/source/agentloop/embed_func.h (the one embedding seam), (embedders themselves live in embedding-clients), lib/src/cli/source/embedstore/vector.cpp (cosine over float32 blobs, dim=0 sentinel), lib/src/cli/source/agentloop/retriever.cpp (ResolveTurnRetriever), lib/src/cli/source/agentloop/hybrid.cpp (RRF), lib/src/cli/source/agentloop/rerank.cpp ((ranked, applied) contract), per-collection retriever:/rerank: pins via config-engine helpers, logger SessionConfig extension, /retriever and /rerank slash commands, lib/src/cli/tests/agentloop/retrieval_test.cpp (exhaustive resolution-rule table).

**Reference (Ommi).** src/agentloop rag.go/rerank.go (ResolveTurnRetriever, EmbedFunc, NewReranker (ranked,applied) contract), OMMI-8 hybrid, RRF k=60 / HybridFetchK=50 / rerank widen-10x-cap-50x400 constants, per-store embed_model/embed_dim binding, logger SetSessionRetrieval. Divergences: the capability gate is rethought rather than ported — Ommi's llamacpp/hf-local-only EmbeddingCapableType becomes per-backend can_embed; Ommi's dual endpoint-vs-capability resolver families collapse into one capability-driven form (Ommi itself noted the drift risk); per-store binding matters MORE with mixed cloud/local vector spaces.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Separated from the store because Ommi's subtle bug surface lived here (resolution rules, honest degradation) and it touches every surface; must exist before serve exposes ?retriever=/?rerank= and before the knowledge stack.

**Open calls:**
- [user] auto policy with paid cloud embedders in play: may auto choose a metered per-query cloud embedding call, or should auto prefer local/lexical unless opted in (auto-local vs auto-any knob)? This is a spend policy — genuinely the user's call
- [default: brute-force cosine first; sqlite-vec if collections outgrow it] Vector search implementation; query-vector caching for cloud embedders

**Guardrail(s).** The resolver rule matrix as an exhaustive table-driven test (Ommi's rules are subtle and its tests encode them); mixed-fleet hybrid degradation fixtures; a dedicated test per rerank failure path; a grep test on persisted sessions for injected content.

**Acceptance criteria:**
- [ ] A collection ingested with one embed model and queried under another falls to lexical with a re-ingest hint — never a mismatched vector-space query (test)
- [ ] auto demotes a partially-vectorized store to lexical wholesale rather than silently returning a fraction (dim=0 test); explicit --retriever vector with no embedder is a hard error naming lexical; auto never resolves to hybrid
- [ ] hybrid fuses by RRF ranks only with pinned constants and degrades per-collection with a note when the vector half is unavailable; every surface reports the retriever that actually ran
- [ ] Every rerank failure path (no judge, timeout, garbage reply, judge drops all) returns raw order with reranked:false; an applied rerank is honestly flagged; a working judge visibly reorders
- [ ] The full resolution matrix (flag/pin/auto × capability/coverage/model-match) passes as a table-driven suite; /retriever and /rerank persist and restore on resume; check rejects a retriever typo

**Scope note.** gated on [embedding-clients.md](embedding-clients.md) shipping.
