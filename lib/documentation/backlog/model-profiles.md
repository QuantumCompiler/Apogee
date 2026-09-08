# Model profiles: the remainder — control-token dialects and channel headers

**What / why.** **The core of this item shipped 2026-09-07** ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone P): a per-family profile registry with an explicit resolution ladder, a streaming reasoning filter that fixed an observed `<think>` leak, and `ModelBehavior` reaching the harness as plain data. What remains are the two mechanisms that **could not be characterized**, because no model available emits their formats.

**Do not re-plan the shipped parts.** The registry, the ladder, `ThinkFilter`, the provider wiring, and the listing columns exist and are test-locked.

## 1. The channel-header markup filter

Some families emit control-token *headers* mid-answer — `<|channel|>analysis<|message|>` and similar. These are framing the model leaked into its own reply: they carry no content, and unlike a reasoning wrapper they must be removed for every model regardless of tuning.

**Deliberately NOT the reasoning filter, and the distinction is the design.** A reasoning pair *wraps* content and routes it to a thinking view; a header is a bare marker with an identifier after it and nothing to route. Shoehorning headers into `ThinkFilter` would make it wrong about both — that is Ommi's recorded reason for two files, and it still holds.

- **Bound the header by its identifier run, not by a closing marker.** Ommi found models emit the close inconsistently, so an unterminated header must cost one word rather than a paragraph.
- **Why it did not land:** none of `gemma3`, `qwen3`, or the Llama files emits a channel header. Ommi's evidence came from Gemma **4**; Gemma 3 emits nothing of the kind. Writing the filter now would mean writing it from Ommi's description of a model Apogee has never run, which is exactly what this item's core constraint forbids.

## 2. Control-token tool calls

A model trained with its own tool-call *tokens* ignores a prose instruction to emit `TOOL_CALL: {json}` — the instruction is prose, the tokens are what its training reinforced. Unparsed, such a call is not a no-op: nothing dispatches **and** the raw markup is printed as though it were the answer.

- **`ToolDialect` already ships** on every profile (`injected` / `native`), defaulting permissive, and `ModelBehavior::native_tool_calls` already carries it to the harness. What is missing is the parser.
- **The opener-parity rule is the guardrail that matters** when it lands: the streaming display's opener list and the parser's accepted formats must come from **one function**, asserted. A marker missing from the display leaks raw markup; one missing from the parser silently drops the call. Two lists that must agree are two lists that will not.
- **Why it did not land:** same reason. No model here emits a control-token call, so there is nothing to characterize a parser against.

**Core constraint(s).** Unchanged, and binding on both:
- **Characterized from real weights, never from a published format.** This is the constraint that kept both of these unbuilt, and it is the one that stopped a hand-written Gemma template from overriding a working one.
- **Unknown is permissive.** An unprofiled model must have every format Apogee knows recognised; being too eager suppresses a line, being too cautious prints markup as the answer.
- **Filter at the source**, so display, returned text, and persisted history cannot disagree.
- **A known profile's empty list is a verified "emits none"**, distinct from "nothing is known".

**Seam + files.** New: `lib/src/cli/source/backends/markup_filter.h/.cpp`, `backends/native_tool_calls.h/.cpp` (with the shared `tool_call_openers()`). Extended: `backends/model_profile.cpp` (per-profile headers and dialects), the `agentloop/` streaming lookahead so suppression and parsing key off one opener list.

**Reference (Ommi).** `lib/cli/src/backends/markup_filter.go` (208 lines — **read its package comment**, which explains at length why a header filter is deliberately not the think filter), `native_tool_calls.go` (291 — `ToolCallOpeners()`, the next-opener bound that stops a truncated call swallowing the valid one after it, and the control-token string delimiter that survives values containing commas and quotes).

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation; the profile bundle designed in from day one rather than retrofitted.
- 2026-09-07 — **Characterization run** on gemma3, qwen3, and llama3 *(user call: those families)*. It **contradicted this item's premise**: Gemma 3 ships a working chat template, where Ommi's Gemma 4 shipped none. Recorded in Milestone P.
- 2026-09-07 — **The core shipped and this document was reduced to the remainder** rather than deleted. Three acceptance criteria were unmet for a principled reason, and marking the item done would have made the queue claim work that does not exist.
- 2026-09-07 — Both remaining mechanisms are **gated on a model that emits their formats**. Candidates worth checking when someone has one: a Gemma 4 release, or any model whose GGUF carries `<|channel|>`-style framing.

**Open calls:**
- [default: fallback behind the prose protocol first; promote per-profile where characterization proves it reliable] GBNF grammar-constrained sampling for local tool calls. A grammar that constrains a model into a format it was not trained on trades one failure mode for a worse one.
- [default: record it in the profile as a hint, never auto-apply] What to do with a chat template an Ollama layer declares when it disagrees with the resolved profile. The sidecar already treats it as advisory.

**Guardrail(s).** Golden-transcript fixtures recorded from **real** output of whatever model finally emits these formats — not transcribed from Ommi. The **display-vs-parser opener-parity assertion** as a unit test over one shared function, mutation-tested by removing a marker from one side. Streaming filters replayed at adversarial chunk sizes with identical results, the discipline `ThinkFilter` already follows.

**Acceptance criteria:**
- [ ] A model that emits channel headers is located, and its headers are stripped from display, returned text, and persisted history alike
- [ ] A response that opens like a tool call but fails to parse **displays its text** — never a blank turn
- [ ] Display lookahead openers and parser formats provably share one source, asserted and mutation-tested
- [ ] Both filters produce identical results at every chunk size

**Scope note.** Gated ring, local-model depth, the residue of the first of four. **The bulk shipped 2026-09-07.** **Gated in practice on a model that emits control tokens or channel headers** — the same convention the `gemini-cli` item used for its login. Out of scope: everything already shipped — the registry, the ladder, the reasoning filter, and the listing columns. Read Milestone P before touching any of it.
