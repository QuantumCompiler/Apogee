# Sampling that local models are meant to be run with

**What / why.** A local model is sampled the way its authors recommend, and the sampling settings a user sets actually reach it. Today the llama.cpp backend is greedy and nothing else (`llama_real.cpp`, `make_context`: "the temperature/top-p knobs belong with the per-family sampling profiles"). So `chat -t 0.7`, `/temperature`, and a backend's `temperature:` are **silently ignored** on every local model: the flag is accepted and does nothing. Greedy decoding is also what Qwen advises against for its thinking models, because it tends to loop and repeat. Ommi honoured the temperature (default 0.7) on its local path; this is a regression from it, not only a missing feature.

Proposed order of precedence:
1. The request's own setting (`-t`, `/temperature`).
2. The backend's config.
3. The GGUF's recommended settings, where it carries them (`general.sampling.*`, which the pinned llama.cpp defines).
4. A per-family default in the model profile, taken from the family's published guidance (Qwen3's thinking and non-thinking pairs, for example).
5. Greedy, for an unprofiled model.

**Core constraint(s).**
- **Deterministic when asked.** Temperature 0 means greedy, byte-for-byte reproducible, which the tests and the hybrid [checkpoints](hybrid-prompt-checkpoints.md)' equivalence check rely on. A seed is settable.
- **One sampler chain.** The grammar (25b's lazy tool grammar, [26f](local-structured-output.md)'s schema grammar) and the [reasoning budget](thinking-control.md) share the chain these settings build; `common_sampler` (`common/sampling.h`) is the upstream implementation and is linked with 25b.
- **Profiles are evidence.** A family default carries its source (the model card) in the profile's evidence line, as profiles already do, and `models info` shows the settings in force and where each came from.
- **Parity.** `temperature` means the same on every backend; the new knobs (`top_p`, `top_k`, `min_p`, `repeat_penalty`, `presence_penalty`) are ignored by a cloud backend that lacks them, and `models info` says so.

**Seam + files.**
- `backends/llama_real.cpp`: the chain built per request from resolved settings.
- `backends/llama_runtime.h`: the settings passed with each generation.
- `backends/llamacpp.cpp`: the precedence resolution.
- `backends/model_profile.h/.cpp`: per-family defaults, split by thinking on or off.
- `harness/config.h/.cpp` and the template: the new per-backend knobs beside `temperature`.
- `commands/models.cpp` (`models info`): the settings in force and their source.
- `commands/chat.cpp`: `/temperature` already exists and gains the others as needed.

**Reference (Ommi).** Ommi honoured the temperature on its local path (CHAT.md: `--temperature`, "config or 0.7"). This item restores that and adds the per-family and per-file defaults.

**Decisions made:**
- 2026-09-25 — Asked for by the user ("Reliability"). The ignored temperature was found while specifying it.
- 2026-09-25 — After 25b, which links `common`'s sampler and puts a grammar into the chain.

**Open calls:**
- [default: the GGUF's `general.sampling.*` outranks the profile default] The file's own recommendation is more specific than a family's.
- [default: tests pin temperature 0] So every scripted and recorded-output test stays deterministic.
- [default: the spike's six tasks are re-run with the family defaults before they ship] A default that loses tasks is not a default.

**Guardrail(s).**
- The precedence ladder table-tested rung by rung.
- `-t` reaches the chain (the regression test for today's silent ignore).
- Temperature 0 is byte-reproducible.
- The GGUF keys are read from a fixture header.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] `apogee complete -m <local> -t 0.9` twice gives different answers, and `-t 0` twice gives identical ones.
- [ ] `apogee models info` on Qwen3.8-27B shows its sampling and where each value came from.
- [ ] The six-task battery passes on both acceptance models with the family defaults.

**Scope note.** Phase 4, item **26h**; build after 25b. Out of scope: learning settings per task, and dynamic temperature.
