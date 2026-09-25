# Structured output by grammar on local models

**What / why.** When a request carries a JSON Schema, a local model's output is constrained to it token by token, so the JSON always parses and always has the shape asked for. Today the llama.cpp backend can only state the schema in the system prompt (`messages_with_schema`: "the pinned subtree carries no schema-to-grammar converter") and hope, with client-side validation and one corrective retry behind it. Small models miss often: a stray sentence, a trailing comma, a missing field. Each miss costs a retry and sometimes the result.

Every structured task Apogee runs locally depends on this:
- the knowledge capture clerk (Milestone Y);
- graph extraction, one call per chunk;
- analyze agents' structured output (Milestone X);
- the rerank judge;
- dataset synthesis with a local teacher;
- the pairwise eval judge.

llama.cpp's `common` library, linked by [local tool calling](local-tool-calling.md), converts a schema to a grammar (`json-schema-to-grammar`, or `common_chat_templates_inputs.json_schema`).

**Core constraint(s).**
- **Validation stays.** The client-side validator and its one corrective retry remain the backstop on every backend (Milestone X's rule); a grammar makes the retry rare, not unnecessary. A schema feature the converter cannot express (it covers most of JSON Schema, not all) falls back to prompt-and-validate with a logged note.
- **Reasoning comes before the constraint.** On a thinking model the grammar must admit the reasoning block and constrain only the answer, or the model is forced to answer without thinking. This is checked on Qwen3.8-27B, the thinking acceptance model.
- **The schema is stated once.** The prompt statement is dropped when a grammar is in force, unless the model's template has nowhere else to show the fields. The "stated once, never twice" guard from Milestone X stands.
- **Tools and schema.** The loop applies the schema on its tools-less final pass (`agentloop/loop.cpp`), and the grammar follows the same rule, so a turn can still call tools first.

**Seam + files.**
- `backends/llama_runtime.h` and `llama_real.cpp`: `render_chat` (24b) takes the schema and returns the full, non-lazy grammar, which enters the sampler chain for that request.
- `backends/llamacpp.cpp`: `messages_with_schema` only as the fallback.
- `tests/backends/llamacpp_test.cpp`: the scripted runtime asserts the grammar reaches the sampler and the prompt statement is absent.

**Reference (Ommi).** Ommi stated schemas in the prompt for local models and validated, which Apogee ported (Milestone X). llama-server's `json_schema` and `response_format` are the upstream precedent for this grammar path.

**Decisions made:**
- 2026-09-25 — Asked for by the user ("Reliability").
- 2026-09-25 — After 24b, which links `llama-common` and adds grammar to the sampler chain.

**Open calls:**
- [default: the grammar applies whenever `response_schema` is set on a local request] There is no reason to leave a local structured request unconstrained.
- [default: an inexpressible schema falls back with a one-line log, never an error] The validator still guards the result.

**Guardrail(s).**
- The grammar reaches the sampler and the schema text leaves the prompt.
- Fallback on an unsupported schema.
- A thinking model still thinks.
- Mutation-tested: grammar dropped; schema stated twice.
- **On real weights:** the knowledge clerk and graph extraction on Qwen3-VL-8B and Qwen3.8-27B over a fixed corpus, with first-try validity counted before and after and recorded.

**Acceptance criteria:**
- [ ] `apogee knowledge capture` with a local clerk produces a valid record on the first try across a 20-transcript corpus on both acceptance models.
- [ ] `apogee graph build` with a local extractor has no invalid-JSON retries on the same corpus.
- [ ] A thinking model's structured answer still shows its thinking, and the JSON follows it.

**Scope note.** Phase 4, item **25f**; build after 24b. Out of scope: grammars for free text (regex constraints), and constrained decoding on cloud backends (they have native JSON modes).
