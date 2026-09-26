# A prompt cache that survives the process

**What / why.** A resumed chat, or a new one with the same system prompt and tools, starts without re-reading what was already read. The KV cache lives only as long as the process today, so `chat --resume` on a long conversation reads the whole transcript before its first token: 10,000 tokens is about 100 s on the reference machine's 27B. Every new chat with `--tools` also reads the same system prompt and tool definitions (1–2K tokens once [local tool calling](../assistant/MILESTONES.md#milestone-j--local-inference) renders them), every time. llama.cpp saves and restores a sequence's state to a file (`llama_state_seq_save_file`, `…load_file`), including a hybrid model's recurrent state.

Two caches:
- **The prefix cache.** Per model, the state after the system prompt and tool definitions, keyed by a hash of those tokens. Computed once, restored into every new chat whose prompt starts the same way.
- **The chat cache.** Optional, per chat. The state at the end of the last turn, saved at exit and restored on `--resume`, keyed by the chat id, the model file's hash and the token prefix it covers.

**Core constraint(s).**
- **A cache that cannot be used is discarded, never trusted.** A key mismatch, a model file changed, a llama.cpp that refuses the state: each means the cache is removed and the prompt is read as today, with one line saying so. Ommi's "self-healing cache" lesson (CHAT.md) came from M-RoPE Qwen models refusing replay, the same family this targets.
- **Correctness first.** A restore must leave the context exactly as decoding the prefix would; output after a restore equals output without it under greedy sampling.
- **Bounded, and in `cache/`.** State files go under the `cache/` layout row ("safe to delete"), with a total size cap and least-recently-used eviction. Sizes are large: 64 KiB a token for Qwen3.8 at `f16`, half at `q8_0` ([26a](context-fit-defaults.md)). `check` reports the total.
- **Private.** A chat's cache holds its conversation, so its files are `0600`, like sessions.
- **Side requests never read or write the session's cache.**

**Seam + files.**
- `backends/llama_runtime.h`: `LlamaContext` gains `save_state(path, tokens)` and `load_state(path) → tokens`.
- `backends/llama_real.cpp`: the llama.h calls.
- `backends/llamacpp.cpp`: the prefix cache consulted when a session context is created; the chat cache restored when a resumed session's first prompt matches; `session_tokens_` set from what was restored, so prefix matching continues as today.
- `harness/layout.h`: `cache/` is already declared, and gains a documented subdirectory.
- `commands/chat.cpp`: save at exit (the per-turn save stays the transcript only).
- `commands/check.cpp`: the cache's size and its cap.

**Reference (Ommi).** Ommi's `ommi-completion` kept an on-disk prompt cache per session with longest-common-prefix reuse, discarded itself when unusable, and was opened read-only on RAG turns so injected chunks never poisoned it (CHAT.md). Apogee's in-process cache made that file unnecessary until a process ends; this brings the persistence back without the per-turn spawn.

**Decisions made:**
- 2026-09-25 — Asked for by the user ("Speed and memory").
- 2026-09-25 — After [25c](hybrid-prompt-checkpoints.md), whose hybrid-state handling a restore must respect, and [26a](context-fit-defaults.md), whose cache type sets the file sizes.

**Open calls:**
- [default: the prefix cache on, the chat cache on for chats over 2,000 tokens] Short chats re-read in a second or two and are not worth the disk.
- [default: a 4 GiB cap for all state files] Covers a handful of long chats; evicted oldest-first.
- [default: saved at a clean exit and after compaction, not after every turn] A state file is hundreds of megabytes, and the per-turn save guarantees the transcript, which is what a crash must not lose.

**Guardrail(s).**
- A restore yields identical greedy output.
- A mismatched key, a changed model hash, or a corrupt file is each discarded with its line, and the turn succeeds.
- The size cap evicts oldest first.
- Files are private.
- Side requests leave the cache alone.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] `apogee chat --resume` on a 10,000-token conversation with Qwen3.8-27B produces its first token within a few seconds instead of ~100 s (both measured and recorded).
- [ ] A new `--tools` chat's first prompt reads only the question, not the tool definitions, on its second launch.
- [ ] Replacing the model file invalidates both caches with one line each.

**Scope note.** Item **26j**; build after 25c and 26a. Out of scope: sharing caches between machines, and caching for `serve`'s sessions (possible later on the same seam).
