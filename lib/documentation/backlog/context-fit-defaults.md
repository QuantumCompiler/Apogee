# A context window sized to the machine

**What / why.** A local backend with no `context_size` gets a window that fits the machine it runs on, and an attention cache half the size it is today. At present `context_size` unset means "whatever the model was trained for" (`llama_real.cpp`, `make_context`), and llama.cpp allocates the whole cache up front. For Qwen3.8-27B that is 262,144 positions: 16 full-attention layers of its 65 blocks, times 4 KV heads × 256 dimensions for keys and values at 2 bytes, is 64 KiB a token, or **16 GiB of cache** on top of 16.8 GB of weights, before the first word. That memory is exactly what [helper models](helper-model-roles.md) and [attachments](attachments-documents.md) need, so an embedding model, a vision model and the chat model can be resident together. An 8-bit cache (`q8_0` keys and values, which needs flash attention) roughly halves what remains.

**Core constraint(s).**
- **An explicit `context_size` always wins**, exactly as written; only the unset case changes.
- **The window the chat measures against is the window allocated.** Context monitoring warns at 80% and compacts at 90% of the real window (`agentloop/content.h`), so a smaller default compacts sooner rather than failing at the wall.
- **Side contexts are unaffected.** They are already sized to their request (`side_context_size`, 2026-09-25).
- **The `n_batch` lesson stands.** Batch size stays at llama.cpp's default, below the window (`llama_real.cpp`'s record of the turn-two failure).
- **The no-llama build is unaffected**; the decision is made where the context is created.

**Seam + files.**
- `backends/llama_real.cpp` (`make_context`): the default window, `type_k`/`type_v` for the cache, and flash attention set explicitly rather than left to auto-detection.
- `harness/config.h/.cpp` and the template: `cache_type` on a llamacpp backend (`f16`, `q8_0`, `q4_0`), documented beside `context_size`.
- `commands/models.cpp` (`models info`) and `commands/check.cpp`: the window a backend will get and the cache memory it costs, from the GGUF header (layers, KV heads, head size, the full-attention interval) without loading the weights.
- `backends/llamacpp.cpp`: the effective window reported to context monitoring.

**Reference (Ommi).** Ommi passed a fixed `-c` to `llama-completion` per turn (CHAT.md) and never sized the window to memory. llama.cpp's `common/fit.h` (`common_fit_params`, in the pinned tree) fits a context to free device memory, and is linkable once [local tool calling](local-tool-calling.md) links `llama-common`.

**Decisions made:**
- 2026-09-25 — From the small-models review, asked for by the user ("Speed and memory"). The cache arithmetic above is from the Q4_K_M's own header.
- 2026-09-25 — First in its track: it frees the memory several later items spend.

**Open calls:**
- [default: 32,768, or the trained window when smaller, lowered further only when free memory cannot hold it] Predictable beats clever; `common_fit_params` is the lowering step, used once `llama-common` is linked.
- [default: `q8_0` keys and values with flash attention on] Measured on the acceptance models (Qwen3-VL-8B, Qwen3.8-27B) for speed and answer drift against `f16` before it becomes the default, and recorded.
- [default: `models info` and `check` show the cache size] Memory a user never asked for should be visible where they look.

**Guardrail(s).**
- The unset window is 32K on a large-window model and the trained window on a small one; an explicit `context_size` is honoured byte for byte.
- `cache_type` reaches the context; a bad value is refused at load.
- The reported cache size matches the arithmetic on a fixture header.
- A chat that outgrows the window compacts rather than failing.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] Qwen3.8-27B with no `context_size` allocates about 1 GiB of cache (32K positions at `q8_0`) instead of 16 GiB, as llama.cpp's own load log reports.
- [ ] Generation speed and the spike's six-task results are unchanged within noise on both acceptance models.
- [ ] `apogee models info` shows the window and the cache size for a local backend.

**Scope note.** Phase 4, item **26a**; gated on nothing, and the `common_fit_params` step follows 25b. Out of scope: offloading the cache to system memory, and per-conversation windows.
