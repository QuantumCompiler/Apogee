# Shared agentic loop: model→tool→model behind a Reporter interface, with ask_user

**What / why.** The I/O-agnostic heart, extracted before surfaces multiply instead of after (Ommi's single most load-bearing lesson): Run(ctx, history, Options) drives model→tool→model cycles using native structured tool calls (Anthropic tool_use first), dispatching through a tool registry with an ask/allow/deny permission gate on filesystem-writing tools, until end_turn. Decision (2026-08-24): the permission gate reads an injected policy in this item — the permissions config-schema keys land with the native fs toolsets in the mcp/tools area, which is the gate's first real consumer. A Reporter observer interface (Thinking/ThinkingToken/ToolStatus/ClearStatus/AnswerStart/AnswerToken/AnswerEnd) keeps the loop free of terminal or HTTP knowledge — surfaces are thin adapters. Includes: built-in fetch_url tool and provider web_search server-tool wiring, history compaction (summarize-into-system-message) with token estimation, the transient-splicing seam for future RAG (spliceTransient), half-turn rollback on aborted mid-turn interactions, and the ask_user question tool (AskFn injection intercepted pre-dispatch; TTY-gated availability so nil AskFn ⇔ never advertised; 1–4 multiple-choice questions with free text always accepted; abort rolls the half-turn out of history) — which, unlike Ommi, works uniformly on all cloud providers because Apogee owns the loop everywhere. `apogee complete --tools` is the first consumer. The TOOL_CALL prose protocol, streaming lookahead/openers machinery, and think/markup filters are deliberately NOT built here — they are local-only and arrive with model-profiles-and-management; the loop reserves their seams.

**Core constraint(s).**
- One shared loop for all surfaces (complete/chat/serve later); surfaces are thin adapters over Reporter — a capability present in only one surface is a bug (Ommi's parity product-invariant)
- Loop operates on the IR only; no provider includes
- ask_user advertised iff a prompt exists to answer it (non-nil AskFn ⇔ advertised); never on pipes; never on serve
- Reasoning content is filtered/handled before tool-call parsing — a tool call inside a thinking block never dispatches
- Injected context rides the outgoing request only — persisted history stays clean

**Seam + files.** lib/src/cli/source/agentloop/loop.h/.cpp (Run, StreamTurn, Options with injected AskFn/EmbedFn/Reporter seams), lib/src/cli/source/agentloop/reporter.h, lib/src/cli/source/agentloop/content.cpp (compaction, EstimateTokens ~len/4 flagged estimated, spliceTransient), lib/src/cli/source/agentloop/question.cpp (ask_user schema, validation, EncodeAnswers, rollback), lib/src/cli/source/agent/dispatch.cpp (tool registry, DispatchTool with handled=false fallthrough, permission gate; non-interactive ask resolves to deny), lib/src/cli/source/commands/complete.cpp (--tools + --search/--mcp/--native tool-mode flags, mutually exclusive), lib/src/cli/tests/agentloop/ (scripted-provider conformance suite).

**Reference (Ommi).** src/agentloop (Run/StreamTurn/Reporter, content helpers, spliceTransient, question.go/OMMI-12) and src/agent (DispatchTool, MakeConfigPermChecker). Divergences: cloud tool calls arrive structured, so the TOOL_CALL prose protocol, streaming lookahead, and openers machinery are deferred to the local-model item; the native-claude special cases (StreamClaudeTurn, delegated loop, ask_user's never-on-native-claude exclusion) have no analog because Apogee owns the loop for every provider.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Ommi's sequencing lesson #2 is explicit: extracting the loop behind Reporter BEFORE growing surfaces is what kept chat/complete/analyze/serve consistent and made the TUI deletable.

**Open calls:**
- [user] Web search strategy: rely purely on provider server-side tools (Anthropic web_search / OpenAI web search / Gemini grounding) vs also shipping a local web-search tool for local models (Ommi's DuckDuckGo scraper is fragile)
- [default: record the intent here, decide in model-profiles-and-management] Whether local tool calls should later use llama.cpp GBNF grammar-constrained sampling

**Guardrail(s).** Scripted-provider loop tests covering multi-tool turns, tool error results, rollback, unknown tool, and ask_user paths — these become the conformance suite every later provider must pass.

**Acceptance criteria:**
- [ ] A scripted mock provider emitting tool calls drives a multi-iteration loop to a final answer, fully offline
- [ ] Anthropic native tool_use round-trips through the loop end-to-end (fixture-automated + live manual)
- [ ] ask_user: nil AskFn means the tool is never advertised; a hallucinated call to an unadvertised tool falls through as an unknown-tool error, never a crash or silent drop; an aborted prompt rolls the half-turn out of history (tests)
- [ ] Compaction produces a shorter history preserving a system-message summary; token estimates flag themselves as estimated
- [ ] Transient-spliced content demonstrably never appears in persisted history
- [ ] A registered mock write-tool triggers the ask path on a TTY; allow and deny outcomes are honored; non-interactive ask resolves to deny (scripted test)
- [ ] fetch_url executes offline through an injected HTTP seam and its result round-trips as a ToolResult

**Scope note.** earmarked for v0.1.0. Gate satisfied: `apogee complete` shipped 2026-08-26 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone E) — this is now the topmost claimable item. The loop consumes `harness::Harness` and the IR; `commands/complete.cpp` is the worked example of assembling one, and `backends/factory.h` builds the providers.
