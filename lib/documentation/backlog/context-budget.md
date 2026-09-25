# A per-turn context budget

**What / why.** Each turn's request is assembled against the model's real window, by priority, instead of from fixed caps. Today every source that adds to a request picks its own size:
- RAG injects `--rag-limit` chunks (4);
- `read_file` returns up to 64 KiB, and `fetch_url` 8 KB;
- tool results stay in history at full size for the rest of the chat;
- the history itself is measured only to warn at 80% and compact at 90% (`agentloop/content.h`).

On a cloud model with a large window that is waste. On a local model it is time, since every token is read at ~100 tokens/s on a 27B model, and it is also crowding, because a small model's attention degrades as unrelated text piles up. The sources this track adds (attachments, recalled chats, larger tool output) make fixed caps untenable.

The budget gives each source a share, in order:
1. the answer's reserve (`max_tokens`);
2. the system prompt, tools and the question;
3. attachments;
4. retrieved chunks and recalled memory;
5. this turn's tool results;
6. history, newest first.

What does not fit is trimmed or elided in reverse order. **Tool results from finished turns** are elided in history to a one-line stub naming the tool, its target and its size, since the answer that used them already carries what mattered. Compaction summarises tool output before conversation.

**Core constraint(s).**
- **One shared loop.** The budget is computed in `agentloop/` for every surface; a surface that assembles its own is a parity bug.
- **Exact where possible, estimated where not, and never mistaken.** A local model counts exactly (the `TokenCounting` capability); a cloud one uses the existing estimate. An unknown window must not read as roomy, the same rule `ContextUsage::fraction()` already states.
- **Transient stays transient.** Injected context never reaches history, and elision changes only what is **sent**, never the saved transcript.
- **Reported honestly.** When the budget trims a source, the status line says what was dropped (`[rag] 2 of 6 chunks fit`), like the retriever's honesty rule.

**Seam + files.**
- `agentloop/budget.h/.cpp` (new): the shares, the counting, and the trimming order.
- `agentloop/loop.cpp`: the request built through the budget; tool results from earlier turns sent as stubs.
- `agentloop/rag.cpp`, `agentloop/content.h`: RAG asks the budget how much it may inject instead of taking a fixed count; `measure_context` becomes the budget's reading.
- `agentloop/` compaction: tool results summarised or elided first.
- Consumers that arrive later: [document attachments](attachments-documents.md) (whether to inline or retrieve), [recall across chats](recall-across-chats.md), and the utility model's tool-output summaries ([helper roles](helper-model-roles.md)).

**Reference (Ommi).** Ommi measured context and compacted near the limit (CHAT.md), which Apogee ported; it had no per-source budget and kept tool results in history whole.

**Decisions made:**
- 2026-09-25 — Asked for by the user ("Memory across chats", which named a context budget manager).
- 2026-09-25 — Before [document attachments](attachments-documents.md), which decides inline-or-retrieve by asking it.

**Open calls:**
- [default: shares as fractions of the window after the reserve: attachments 30%, retrieval and recall 20%, tool results 25%, history the rest] Tuned on the acceptance models and recorded; each is a field in the budget, not a constant scattered through the loop.
- [default: tool results from finished turns become stubs] If a model needs one again, it can call the tool again.
- [default: the budget applies to every backend] A cloud model gains the same discipline; its larger window just means less is ever trimmed.

**Guardrail(s).**
- Trimming order and shares table-tested against scripted token counts.
- A stub replaces a finished turn's tool result in the request and never in the saved transcript.
- Every trim is reported.
- An unknown window never reads as room to spare.
- The loop's existing conformance suite passes unchanged on a large window.

**Acceptance criteria:**
- [ ] A local chat that reads a 60 KB file on turn one sends a stub for it on turn five, and turn five's prompt is correspondingly smaller (measured in tokens).
- [ ] A question with `--rag-limit 12` against a small window injects only what fits, and says how many chunks it dropped.
- [ ] Behaviour on a large cloud window is unchanged for a short conversation.

**Scope note.** Item **26c**; gated on nothing. Out of scope: learning shares per model, and summarising history turn by turn.
