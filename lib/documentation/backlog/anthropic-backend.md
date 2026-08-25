# Anthropic Messages API backend (direct HTTPS + SSE streaming)

**What / why.** First real provider and the biggest single divergence from Ommi: a direct Anthropic Messages API client replacing both Ommi's anthropic HTTP backend and its claude-CLI shell-out. Streams via a hand-rolled SSE event parser built as a shared component the OpenAI/Gemini backends will reuse, over a shared HTTP-client wrapper with retry/backoff on 429/5xx. Maps system prompts, tool_use/tool_result blocks, and image parts to/from the IR; surfaces extended-thinking deltas through a typed OnThinking seam (no <thinking> text demux); carries exact usage/token counts and count_tokens for context accounting; API key from config/env; server-side web_search tool configurable per backend. Everything Ommi rented from the claude CLI (thinking stream, native tool use, web search, session state) is re-sourced here from native API features. *(Revised 2026-08-24: a claude-CLI sibling backend has rejoined the queue — see [claude-cli-backend.md](claude-cli-backend.md) — as the subscription-auth path with a persistent-child design; this item remains the direct API-key path, and Ommi's per-request `claude -p` special cases — native_tools bifurcation, --allowedTools plumbing — still have no analog in either.)*

**Core constraint(s).**
- Interactive turns are outbound HTTPS only — never a listening socket (Ommi's interactive-never-listens invariant, satisfied by construction for cloud but still test-locked downstream)
- API keys never appear in logs, error messages, or serialized output
- Thinking is display/loop metadata only: never persisted to history, always stripped from returned text
- Exact token usage used when the API provides it; estimates always flagged

**Seam + files.** lib/src/cli/source/backends/anthropic.h/.cpp, lib/src/cli/source/backends/sse_parser.h/.cpp (shared SSE frame parser), lib/src/cli/source/backends/http_client.h/.cpp (thin wrapper over the chosen HTTP lib with retry/backoff, shared by all cloud backends), IR↔Anthropic wire translation unit (system extraction, tool blocks, thinking blocks, image sources), lib/src/cli/tests/backends/anthropic_test.cpp (recorded-fixture SSE streams incl. frames split across read boundaries — no network).

**Reference (Ommi).** Replaces src/backends/anthropic.go (HTTP API) plus the responsibilities of claude.go (`claude -p` shell-out — the CLI path returns separately as [claude-cli-backend.md](claude-cli-backend.md), redesigned as a persistent child rather than per-request spawns). Divergences: web search becomes the Anthropic server-side web_search tool; thinking arrives as typed thinking_delta events instead of <thinking> text demux; StreamClaudeTurn/native_tools/permission_mode/allowed_tools special-casing all collapse; the SSE/HTTP/retry infrastructure built here is the template the other cloud clients reuse.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Proves the hardest new C++ ground (streaming HTTPS, SSE, provider dialect mapping) on exactly one provider before tripling the surface; its infra is the template for openai-google-backends and the gate for the loop's first end-to-end run.

**Open calls:**
- (consumed decision) HTTP client is **libcurl**, standardized project-wide 2026-08-25 — do not reopen the pick. It is deliberately **not wired yet**: this item owns adding `find_package(CURL)` to `lib/src/cli/cmake/ApogeeDependencies.cmake` and the platform TLS backends, then builds the shared streaming wrapper over it
- [default: system certs] TLS/cert strategy per platform (system certs vs bundled)
- [default: compiled-in with config override] Context-window rows for Anthropic models — the table lives in harness-core; this item populates its Anthropic rows

**Guardrail(s).** Recorded-fixture SSE conformance tests (including split-across-chunk events) with a fuzz/split-boundary suite guarding the parser; a grep-style test that no logging or error path can emit the api_key field.

**Acceptance criteria:**
- [ ] A streamed multi-turn chat with a system prompt works against the live API (manual) and against recorded SSE fixtures (automated), including events split across chunk boundaries
- [ ] tool_use blocks arrive as structured IR ToolCalls; tool_result round-trips back correctly including thinking blocks replayed in multi-turn tool use (an Anthropic requirement Ommi never had)
- [ ] Thinking deltas reach the OnThinking callback and never enter returned text or persisted history
- [ ] 429/5xx retry with backoff is fixture-tested; absent API key yields a clear actionable error; exact usage counts surfaced, estimates flagged estimated
- [ ] Server-side web_search tool can be enabled per backend from config

**Scope note.** earmarked for v0.1.0 (build after [harness-core.md](harness-core.md)).
