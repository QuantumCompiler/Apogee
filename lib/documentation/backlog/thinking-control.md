# Thinking control: on, off, automatic, and a budget

**What / why.** A user, and the harness, decide when a reasoning model thinks, and for how long. On the reference machine, thinking was 26 of the 57 seconds of one ordinary answer (2026-09-25), and it is the same cost whether the question is arithmetic or small talk. The controls:
- **`/think on|off|auto`** in `chat`, `--think` on `chat` and `complete`, and a `thinking:` default per backend.
- **`auto`**: the [utility model](helper-model-roles.md), or a cheap heuristic without one, decides per question whether it needs reasoning.
- **A thinking budget**: the most tokens a model may spend reasoning before it must answer. llama.cpp's reasoning-budget sampler (`common/reasoning-budget.h`) forces the end-of-thinking tag when the budget is spent.
- **The display says what happened**: `✻ Thought for 8s` or `✻ Thought for 20s (budget reached)`.

Switching thinking off goes through the template's own `enable_thinking` once [local tool calling](local-tool-calling.md) renders through Jinja, generalising the Qwen-only `skip_reasoning` added for titles on 2026-09-25.

**Core constraint(s).**
- **Parity across backends.** The same setting maps to each vendor's own control: Anthropic's thinking budget, OpenAI's reasoning effort, Gemini's thinking budget. A backend with no control says so in `models info` rather than pretending.
- **Thinking is displayed live and never persisted**, unchanged.
- **Honest about budgets.** A budget that cut reasoning short is reported on the thinking line and in machine mode's thinking event, so an answer that suffered for it can be understood.
- **One sampler chain** (with [25h](sampling-profiles.md)): the budget is a sampler in the chain, not a second generation loop.

**Seam + files.**
- `harness/types.h`: `ChatRequest` gains a thinking setting (mode and budget); `transient.skip_reasoning` becomes its `off` case.
- `backends/llamacpp.cpp` and `llama_real.cpp`: `enable_thinking` into the render, and the budget sampler.
- `backends/anthropic*.cpp`, `openai*.cpp`, `google*.cpp`: the vendor mappings.
- `agentloop/`: `auto`'s decision, asked of the utility model as a side request when one is set.
- `commands/chat.cpp`, `commands/complete.cpp`: the flag and the slash command, added to `slash_commands()`.
- `commands/thinking_view.cpp`: the budget note on the summary line.
- `harness/config.*`: the per-backend default.

**Reference (Ommi).** Ommi displayed and filtered reasoning (think_filter.go, CHAT.md) but had no control over whether or how long a model thought.

**Decisions made:**
- 2026-09-25 — Asked for by the user ("Reliability").
- 2026-09-25 — After 24b (the template's switch) and [25h](sampling-profiles.md) (the chain the budget joins).

**Open calls:**
- [default: `on` unless configured] Today's behaviour; `auto` is opt-in until measured.
- [default: `auto` without a utility model thinks for questions over 200 characters or containing code, maths or "why/how", and not otherwise] Measured against the six-task battery and a set of chat questions before it ships.
- [default: no budget unless configured; `--think-budget N` and `thinking_budget:` set one] A budget changes answers, so it should be chosen.

**Guardrail(s).**
- `off` renders the template's switch.
- The budget forces the close tag at N tokens and is reported.
- Each vendor mapping, tested with its wire fixture.
- `auto` decides as tabled.
- Thinking never enters history.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] `/think off` on Qwen3.8-27B answers with no thinking block, and the first token arrives within the prompt-reading time.
- [ ] `--think-budget 256` stops thinking at 256 tokens and says so.
- [ ] The same `--think off` on an Anthropic backend sends no thinking parameter, and on OpenAI the lowest effort.

**Scope note.** Phase 4, item **25i**; build after 24b and 25h. Out of scope: per-tool-step thinking policies.
