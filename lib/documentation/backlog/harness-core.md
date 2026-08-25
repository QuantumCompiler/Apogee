# Harness core: LLMProvider interface, canonical message IR, router

**What / why.** The interface layer everything else builds on: an abstract LLMProvider (Chat, StreamChat via token-sink callback with a cancellation token, Complete, ListModels) with optional capability interfaces discovered rather than required (can_embed, status reporting, in-text tool calling), a Harness registry with SimpleRouter carrying Ommi's three-rung precedence exactly (exact backend-key match → a backend entry's `model:` field match → default backend), and the canonical IR all providers translate to/from — ChatMessage with dual-mode content (plain string or typed parts for images), Tool/ToolCall/ToolResult, ChatRequest with a transient region (TransientStart/TransientLen/SideRequest, never serialized) for per-turn RAG injection, RAGMeta/StatusEvent, typed errors, and a data-only ModelBehavior seam so the loop can ask about model quirks without importing backends. Ships with a MockProvider so everything downstream is testable offline. Also owns the compiled model→context-window fallback table (lib/src/harness/context_windows.cpp) — provider-neutral data like ModelBehavior; config-engine, the cloud backends, and chat-cli consume it rather than growing their own. Getting streaming, cancellation, multimodal content, and tool structures into the interface on day one is what let Ommi keep four backends and multiple surfaces consistent.

**Core constraint(s).**
- harness never includes backends — ModelBehavior crosses the boundary as plain data (Ommi's import-cycle seam, enforced by a compile-time/review check)
- The loop/surfaces operate on the IR only — provider dialects never leak past the backends layer; the IR never grows provider-specific fields
- Zero-value/unknown ModelBehavior means permissive: failing to recognize a tool call is the expensive direction
- Content is multi-part from day one; streaming is callback-based with a cancellation token
- Every effectful edge injectable (std::function seams) — Ommi's testability discipline

**Seam + files.** lib/src/harness/provider.h (LLMProvider ABC + capability interfaces), lib/src/harness/types.h/.cpp (ChatMessage, MessageContent string-or-parts, Tool, ToolCall, ChatRequest/Response with TransientStart/TransientLen/SideRequest, RAGMeta, errors), lib/src/harness/behavior.h (ModelBehavior plain data + ModelBehaviorFor), lib/src/harness/harness.h/.cpp (registry + SimpleRouter), lib/src/harness/context_windows.cpp (compiled model→window fallback table — single owner), lib/src/backends/mock.cpp (MockProvider), lib/test/harness/.

**Reference (Ommi).** src/harness (LLMProvider 5-method interface, SimpleRouter, ChatRequest transient region json:"-", optional type-asserted capability interfaces StatusReporter/InTextToolCaller, harness.ModelBehavior data seam avoiding the backends import cycle). Divergences: Go channels become callback sinks; Ommi's five-method interface collapses to four — StreamTokens' streaming one-shot role is served by StreamChat (deliberate simplification); Ommi's EmbeddingCapableType hard allowlist becomes a per-backend can_embed capability flag because OpenAI/Google DO embed over the API — the Anthropic-only rationale doesn't transfer.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Layer-1 package imported by everything; both backends and the agent loop gate on it, and Ommi's 'new LLM backend = implement LLMProvider' recipe proves the seam works.

**Open calls:**
- [default: std::function token-sink callbacks — Ommi-equivalent and simpler] Streaming shape: callbacks vs C++20 coroutine generators; decide once

**Guardrail(s).** MockProvider contract tests pin the interface; IR round-trip serialization tests; a test asserting transient messages never appear in persisted/serialized history; a check that lib/src/harness has no include of lib/src/backends.

**Acceptance criteria:**
- [ ] MockProvider round-trips a streamed chat through the Harness with correct router precedence (explicit key > model: field match > default, table-tested across all three rungs) and honors cancellation mid-stream, covered by tests
- [ ] IR serializes to/from JSON with the dual string-or-parts content shape (custom to_json/from_json)
- [ ] Transient-region fields are excluded from any serialization by construction (unit-tested contract)
- [ ] A capability probe (can_embed) is queryable without dynamic_cast leaking into callers; unknown backend is a typed error
- [ ] Zero-value ModelBehavior is defined and documented as 'unknown = permissive'

**Scope note.** earmarked for v0.1.0 (build after [config-engine.md](config-engine.md)).
