# Local tool calling through the model's own chat template

**What / why.** Local models get the tools every other backend already has: files, the shell, git, notes, document search, `fetch_url`, MCP servers, and later [web search](web-search-searxng.md). The llama.cpp backend renders the tool definitions into the prompt through the model's own chat template, parses the calls the model emits back into the IR, and constrains a call's JSON with a grammar. The loop, the gate and the tools themselves do not change. Every surface that runs the shared loop (`chat`, `complete`, `serve --tools`, `analyze` and agents, machine mode) gains local tools at once.

**The gap, measured (spike 2026-09-25).** `LlamaCppProvider` builds its prompt with `llama_chat_apply_template`, llama.cpp's legacy template function, which has no tools input. So `request.tools` is dropped, and no local model has ever been shown a tool. Asked to use `read_file` with `--tools` on, Qwen3.8-27B replied: "I don't have a `read_file` tool available." The only local tool-call path is the gpt-oss control-token parser (`native_tool_calls`), and even gpt-oss is never shown the tool list. Training already recorded the gap: its two tool kits wait for this item (ROADMAP → Outer ring).

**What llama.cpp's own layer achieves on these models.** The pinned `b11151` ships `common/chat.h`, which llama-server uses. It renders the model's embedded Jinja template with tools, generates a parser for that template's tool-call format, and returns a lazy grammar with its trigger. The spike ran six tasks per model through it (read, write, a counting command, list-then-read, search-then-fetch, and arithmetic that needs no tool):

| | Qwen3.8-27B Q4_K_M | Qwen3-VL-8B Q4_K_M | Llama-3.2-3B Q4_K_M |
|---|---|---|---|
| Tasks passed | 6 / 6 | 6 / 6 | 3 / 6 |
| Time per task | 16–192 s (thinking) | 0.3–5 s | 1–6 s |

Llama 3.2 3B's calls were all well-formed; it re-read files, looped, and called a shell for 17 × 3. A prototype linked against Apogee's own build proved the fit:
- **Rendering:** Qwen3.8's template rendered with a tool in its native `<tool_call><function=…>` format, with a 1 KB grammar triggered lazily on `<tool_call>`.
- **Parsing:** a model-shaped reply parsed to content, reasoning (separated) and `read_file({"path":…})` as JSON arguments.
- **Linking:** the link map pulled only the chat, parser, Jinja, grammar and sampling objects, with no downloader, no llama.cpp copy of httplib (which would have clashed with Apogee's 0.56.0), and no listening symbols.

**Core constraint(s).**
- **Interactive turns never open a listening socket.** Tool calling stays in-process; llama-server was the spike's measuring instrument, never a runtime. `cli.no_listen_symbols` walks the link graph, so it scans `llama-common` automatically once it is linked, and must pass.
- **The no-llama build still builds and tests.** All `common` code sits behind `APOGEE_ENABLE_LLAMA` in `llama_real.cpp`, and the scripted runtime models the new seam, so the provider's tool logic is tested in both builds.
- **The harness never includes backends.** Tools and calls cross as the IR already defines them: `harness::Tool` {name, description, parameters_schema} and `harness::ToolCall` {id, name, arguments} map field for field onto `common_chat_tool` and `common_chat_tool_call`.
- **Thinking is displayed live and never persisted** (SPEC success criteria). Reasoning parsed out of a reply goes to the thinking sink; the IR gains no reasoning field.
- **Unknown is permissive, and nothing is dropped silently.** A template that Jinja cannot render falls back to today's path. With `--tools` on, that fallback says in one line that this model gets no tools, rather than quietly answering without them.
- **Smart pointers for C handles.** `common_chat_templates_ptr` is already a `unique_ptr`; any sampler or grammar handle gets the same.
- **KV reuse is unchanged.** Token-prefix reuse keeps working on the rendered prompt; the hybrid-model cost is its own item ([hybrid prompt checkpoints](hybrid-prompt-checkpoints.md)).

**Seam + files.**
- `third_party/CMakeLists.txt`, `source/CMakeLists.txt`: link `llama-common` (PRIVATE, beside `llama` and `mtmd`).
- `backends/llama_runtime.h`: `LlamaModel` gains a chat-format seam. `render_chat(messages, tools, enable_thinking)` returns the prompt, an opaque parser, the grammar with its triggers, extra stops, and whether the template supports thinking. `parse_chat(text, partial)` returns content, reasoning and tool calls. The scripted runtime (`tests/support/fake_llama.h`) implements a word-level version of both.
- `backends/llama_real.cpp`: `common_chat_templates_init` once per model load; `common_chat_templates_apply` per request; `common_chat_parse` with the parser loaded from the params (`reasoning_format` set on the inputs as well as the parser, as the spike found necessary); the lazy grammar added to the sampler chain (`llama_sampler_init_grammar_lazy_patterns`) on a request that carries tools.
- `backends/llamacpp.cpp`:
  - `run`, `run_multimodal` and `generate` pass `request.tools` and `enable_thinking` (from `transient.skip_reasoning`) into the render.
  - Streaming re-parses the partial output per token and emits the difference: reasoning to `on_thinking`, content to `on_token`, tool-call spans held back.
  - Parsed calls become `response.message.tool_calls` with ids assigned, and `FinishReason::ToolCalls`.
- `backends/model_profile.*`, `think_filter.*`, `markup_filter.*`, `native_tool_calls.*`: kept for the legacy fallback path only. `ModelProfile::skip_reasoning` (2026-09-25) is superseded by the template's own `enable_thinking` and stays only for that fallback.
- `agentloop/loop.cpp`: a repeated-call guard. The same tool with the same arguments, a third time in one turn, gets a tool result saying it already has that answer. `max_iterations` (12) is unchanged.
- `tests/backends/llamacpp_test.cpp` and the scripted runtime: the tool path end to end, in both builds.

**Reference (Ommi).** Ommi's `llamacpp.go` taught local models tools through an **injected prose protocol**. `toolSystemPrompt` described each tool and asked for `TOOL_CALL: {json}` lines, `rewriteMessagesForTools` flattened tool results into user turns, and a regex parsed the reply. **Deliberately not ported:**
- A model trained on tool tokens ignores a prose instruction; Ommi's own `native_tool_calls.go` exists because gpt-oss did.
- The prose protocol has no grammar, and it discards the format each model was trained on.

`common/chat.h` renders each model's trained format instead and is maintained upstream with the pin. Ommi's gpt-oss parser, ported as `ToolCallGate` in Milestone P, is superseded by `common`'s own gpt-oss parser wherever Jinja renders the template.

**Decisions made:**
- 2026-09-25 — **Each model's own format, through llama.cpp's `common` chat layer**, not an injected protocol (spike evidence above).
- 2026-09-25 — **Link `llama-common`, never copy it.** It is part of the pinned tree, and the link map shows only the chat objects are pulled in.
- 2026-09-25 — **Built and tested against 8B-class models and up** (the user's call). Qwen3-VL-8B and Qwen3.8-27B are the acceptance models. Smaller models get tools but no special effort; the repeated-call guard is the only concession.
- 2026-09-25 — **Gated on [tool safety defaults](tool-safety-defaults.md)**, so that when local models first reach `fetch_url`, the per-website prompt already exists.

**Open calls:**
- [default: `common`'s parser replaces the profile filters for any template Jinja renders] The profiles stay as the fallback. One parser per template, maintained upstream, beats two that must agree.
- [default: streaming re-parses the partial output per token and emits the difference, as llama-server does] Quadratic in the reply's length, which is fine at chat lengths; revisit only if a measurement says otherwise.
- [default: sampling stays greedy, with the lazy grammar added] Per-family sampling (Qwen recommends against greedy decoding when thinking) is its own concern, already noted in `llama_real.cpp`.
- [default: tool choice `auto`, parallel calls off] Parallel calls are harmless to allow later; one call per step is easier to gate and to show.
- [default: the repeated-call guard trips on the third identical call] The spike's 3B read the same file three times and ran the shell eight times.
- [default: image turns carry tools too] `run_multimodal` renders through the same path, so a model asked about a picture can act on it (the Milestone O rule).

**Guardrail(s).**
- Scripted runtime:
  - `request.tools` reaches the render inputs.
  - An assistant turn with calls and its tool results render back.
  - A parsed call becomes the IR's `tool_calls` with ids.
  - The display never shows a call's markup.
  - Thinking reaches only the thinking sink.
  - A template that cannot render falls back with its one-line notice.
  - The repeated-call guard trips at three.
- `cli.no_listen_symbols` passes with `llama-common` linked; the no-llama preset builds and passes.
- Mutation-tested: tools dropped from the render; a call printed as text; reasoning in the answer.
- **On real weights** (recorded in MILESTONES, as every local change is): the spike's six tasks through `apogee complete --tools`, on both acceptance models.

**Acceptance criteria:**
- [ ] `apogee complete --tools -m <Qwen3-VL-8B>` asked what a file says calls `read_file` and answers from its contents.
- [ ] The six spike tasks pass through Apogee on Qwen3-VL-8B and Qwen3.8-27B, 6/6 each, with writes and the shell prompting through the gate.
- [ ] No tool-call markup reaches the terminal or the saved transcript; the transcript holds the calls and results as IR and no reasoning.
- [ ] `chat`, `complete`, `serve --tools` and an `analyze` agent all complete a tool task on a local backend.
- [ ] The suite passes in both builds, and `cli.no_listen_symbols` passes.

**Scope note.** Phase 4, item **24b**; build after 24a. It unblocks the two training tool kits (a separate item), [hybrid prompt checkpoints](hybrid-prompt-checkpoints.md) (24c), [local tool ergonomics](local-tool-ergonomics.md) (24d) and local use of [web search](web-search-searxng.md) (24e). Out of scope:
- per-family sampling settings;
- speculative decoding with a model's MTP layers;
- the Ollama CLI backend, which has no tool interface.
