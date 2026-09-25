# Hybrid-model prompt checkpoints

**What / why.** A hybrid model re-reads only what is new, on each chat turn and each tool step, rather than its whole conversation. Qwen3.5 and 3.8 (`qwen35`) mix attention layers with linear-attention layers whose running state llama.cpp cannot rewind. When a new prompt diverges from what the cache holds, `trim_to` is refused and the cache is cleared. A thinking model's template re-renders the last answer without its reasoning, so the prompt diverges on every turn, and the whole conversation decodes again. That has been correct since 2026-09-23 and slow ever since.

Measured on Qwen3.8-27B Q4_K_M:
- **Chat turns today:** 1–4 s before the first token on a four-turn chat (2026-09-25).
- **Tool loops:** at the ~100 tokens/s prompt speed seen on this machine, a 10,000-token tool session would spend about 100 s re-reading itself before each step.
- **llama-server in the spike, same model:** each tool step processed only the 95–213 new tokens, keeping 58–93% of its cache.

It does that with **context checkpoints**:
- **Saving:** while it processes a prompt, it saves the state that cannot be rewound. `llama_state_seq_get_data_ext` with `LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY` captures the recurrent (or sliding-window) part only. It saves at the start of a user message and near the prompt's end, keeps up to 32 per sequence, and spaces them apart.
- **Restoring:** when a new prompt diverges, it loads the newest checkpoint at or before the divergence and decodes from there.

**Core constraint(s).**
- **Correctness first.** A restore must leave the context exactly as if the prefix had been decoded. Apogee's position invariant (the KV holds `[0, prompt.size())` after the prompt, `llamacpp.cpp`) holds after a restore as it does after a trim; the M-RoPE crash of 2026-09-23 was this invariant broken.
- **Pure-attention models are untouched.** Their trim already works; a checkpoint is taken only when the context's memory is not fully rewindable.
- **Bounded memory.** Checkpoints are host memory the user never asked for. Their count is capped and their total size is reported by `--verbose`.
- **Side requests never touch the session's checkpoints**, as they never touch its KV (Ommi's SideRequest lesson).
- **The no-llama build still builds**, and the scripted runtime models checkpoints so the logic is tested there.

**Seam + files.**
- `backends/llama_runtime.h`: `LlamaContext` gains `checkpoint()` (save the partial state at the current end of the cache) and a `trim_to` that, when refused, restores the newest checkpoint at or before the position and reports where the cache now really ends. `FakeLlamaContext` (`rewindable = false`) models both.
- `backends/llama_real.cpp`: the llama.h calls (`llama_state_seq_get_size_ext`, `…get_data_ext`, `…set_data_ext`, `llama_memory_seq_pos_min/max`), the checkpoint list per context, and eviction.
- `backends/llamacpp.cpp`: `decode_in_batches` takes a checkpoint at the prompt's end, before generation, and at the start of the last user message. `run` needs no other change, because it already decodes from wherever `trim_to` says the cache ends.
- Reference: `tools/server/server-context.cpp` in the pinned tree (`create_checkpoint`, and the search-and-restore before prompt processing).

**Reference (Ommi).** No analog. Ommi spawned llama.cpp per turn and kept no cache across turns at all.

**Decisions made:**
- 2026-09-25 — **Checkpoints, as llama-server does them**, over two alternatives. Re-rendering each answer with its reasoning, so the prompt never diverges, would need reasoning in the IR, against "thinking is never persisted". It also would not help a template that changes earlier text for other reasons.
- 2026-09-25 — Its own item, after [local tool calling](local-tool-calling.md): tool loops are where the cost multiplies, so that is where it is measured.

**Open calls:**
- [default: at most 8 checkpoints per context, keeping the newest] A chat needs the one before its latest answer. llama-server's 32 is for a server's slots, and each checkpoint is a copy of the recurrent state. Measure the size on Qwen3.8-27B and record it.
- [default: checkpoint at the prompt's end and at the last user message's start] Those are the two divergence points a chat and a tool loop produce; llama-server's mid-prompt spacing is for long pasted prompts and can follow if measured useful.
- [default: no checkpoints on image turns] They already run on a throwaway context (`run_multimodal`); llama-server makes the same exclusion.

**Guardrail(s).**
- Scripted runtime, `rewindable = false`:
  - turn two and a tool step decode only the new suffix;
  - a checkpoint past the divergence is never restored;
  - the cap evicts the oldest;
  - a side request leaves the session's checkpoints untouched.
- Mutation-tested: restoring a checkpoint past the divergence, and skipping the restore.
- **On real weights:** a four-turn chat and a tool loop on Qwen3.8-27B with prompt tokens decoded per step recorded, answers checked against a run without checkpoints for identical output under greedy sampling.

**Acceptance criteria:**
- [ ] On Qwen3.8-27B, each turn after the first and each tool step decodes only the new tokens (the rendered answer plus the next message or tool result, or ~100–300 tokens), not the conversation.
- [ ] Output with checkpoints is identical to output without them, under greedy sampling, on the same prompts.
- [ ] A pure-attention model takes no checkpoints.
- [ ] Checkpoint memory is capped, and `--verbose` reports it.

**Scope note.** Item **25c**; build after 25b. Out of scope: persisting checkpoints to disk across processes (a resumed chat re-reads once), and speculative-decoding checkpoints.
