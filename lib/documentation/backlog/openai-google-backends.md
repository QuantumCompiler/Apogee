# OpenAI and Google Gemini backends over the shared client/IR

**What / why.** Widen cloud coverage to the full stated backend set: an OpenAI chat client and a Gemini generateContent client, each a per-provider translator to/from the canonical IR — tool_calls vs functionCall shapes, system-prompt placement, streaming frame dialects — reusing the shared HTTP/SSE components from the Anthropic item. Reasoning summaries (OpenAI) and thought parts (Gemini) feed the same OnThinking seam so the thinking display works identically; provider web-search/grounding equivalents are wired as server-side tools. The embeddings-endpoint clients are deliberately NOT here: nothing consumes vectors until the RAG ring, so they land in the embedding-clients ring item (the can_embed capability *interface* is declared in harness-core; these providers implement it there). If grooming judges the two dialects too large for one session, split per provider — each must pass the conformance suite independently. Both providers register as ordinary config backend types and must pass the identical loop-conformance suite — the moment the LLMProvider seam's central bet is validated. These are the **API billing plan** path for their vendors; the **subscription plan** path (codex and gemini CLIs) arrives separately via [vendor-cli-backends.md](vendor-cli-backends.md), and both paths must be selectable per backend entry (SPEC divergence 2, 2026-08-24).

**Core constraint(s).**
- Providers are ordinary LLMProvider implementations — no special cases in the loop or surfaces (the collapse of Ommi's native_tools bifurcation is the payoff; don't reintroduce it)
- All three dialect translators live in backends/; the IR never grows provider-specific fields
- Embedding capability is a per-backend flag, not a type allowlist — implemented later in embedding-clients; nothing in this item may hard-code who can embed
- Same key-hygiene and no-listening-socket rules as the Anthropic backend

**Seam + files.** lib/src/cli/source/backends/openai.h/.cpp, lib/src/cli/source/backends/google.h/.cpp, per-provider IR translators, extensions to lib/src/cli/source/backends/sse_parser.cpp for each streaming dialect, lib/src/cli/tests/backends/openai_test.cpp + google_test.cpp (recorded-trace fixtures), conformance-suite registration in lib/src/cli/tests/agentloop/.

**Reference (Ommi).** No direct Ommi analog — this is Apogee's stated divergence. Ommi's cloud set was Anthropic API + claude CLI; the backends it deliberately removed for network-surface reduction were the Ollama and HuggingFace-Hub INFERENCE backends (local-model serving channels) — Ommi never carried other cloud LLM vendors. Slots into the exact seam Ommi's DEVELOPER.md 'new LLM backend' recipe defines; capability flags replace Ommi's agentloop.EmbeddingCapableType allowlist, whose Anthropic-only rationale doesn't transfer.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Reuses the SSE/HTTP/retry infra from anthropic-backend and must exist before RAG so the capability-driven embedder gate is designed against real cloud embedders.

**Open calls:**
- [default: the Responses API — reasoning summaries and server-side web search live there; confirm at build time] OpenAI API surface: chat/completions vs Responses
- [default: API key only for v0.1.0] Gemini auth mode: API key vs also Vertex-style credentials
- [default: one config knob mapped per provider] Effort/thinking-budget mapping (Anthropic budget_tokens / OpenAI reasoning_effort / Gemini thinkingBudget)

**Guardrail(s).** The shared cross-provider conformance suite over recorded fixtures is the regression net: any IR or translator change must keep all three providers green.

**Acceptance criteria:**
- [ ] complete and chat run against all three cloud providers by -m alone; /model switches between them mid-session carrying history
- [ ] Native tool calling round-trips through the shared agent loop on both new providers (fixture-tested against recorded traces)
- [ ] Both providers pass the identical loop-conformance suite the mock and Anthropic backends pass; one IR-level behavioral table runs identically across all three
- [ ] Streaming, exact usage counts, and thinking/reasoning display work on both; retry/backoff and key hygiene match the Anthropic backend

**Scope note.** earmarked for v0.1.0. Gate satisfied: the shared agent loop shipped 2026-08-26 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone F). Reuse `backends/http_client.h` and `backends/sse_parser.h`; put each dialect in its own `*_wire.h/.cpp` as `anthropic_wire` does. The loop's conformance suite (`tests/agentloop/loop_test.cpp`) is what these backends must pass.
