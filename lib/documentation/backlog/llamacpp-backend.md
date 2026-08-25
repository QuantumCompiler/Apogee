# Local inference: llama.cpp linked in-process (generation, KV sessions)

**What / why.** The local backend and Apogee's biggest architectural simplification over Ommi: link llama.cpp as a library (pinned third_party target, Metal on macOS) and implement LlamaCppProvider — model load from a config GGUF path, one llama_context (or sequence) per chat session giving an in-memory KV cache that erases Ommi's entire on-disk prompt-cache apparatus (fingerprinting, M-RoPE replay self-heal, cache-poisoning rules) while keeping its guarantees: warm multi-turn cost, transient RAG never entering the reusable prefix, side requests (titling) never clobbering session KV (Ommi's SideRequest lesson — separate context/sequence). Streamed token callbacks, exact token counts from the real tokenizer feeding context monitoring, idle unload policy (the in-process embedding path is deferred to the embedding-clients ring item alongside the cloud embedders), and chat-template application via llama_chat_apply_template for models that ship templates (the full per-family profile/filter/tool-dialect layer is deliberately deferred to its own item). The interactive-never-listens invariant holds by construction but stays test-locked, and crash isolation is re-decided consciously rather than inherited.

**Core constraint(s).**
- Interactive local inference never opens a listening socket (test-locked with lsof even though it holds by construction — Ommi OMMI-11)
- No models ever bundled — user-supplied GGUF paths; the config template ships a commented example so a fresh install passes check with zero models (Ommi's permanent policy)
- Side requests never touch a session's KV state (SideRequest-exempt by type)
- Transient (RAG) content must never contaminate the reusable KV prefix
- third_party/llama.cpp is never edited; pin updates are deliberate events caught by CI

**Seam + files.** CMake integration for llama.cpp (third_party/, GGML_METAL, static-link decision), lib/src/backends/llamacpp.h/.cpp (LlamaCppProvider: model/context lifecycle, per-session sequence ownership, sampling, streaming, side-request isolation, idle unload), lib/src/backends/llamacpp_tokens.cpp (exact counting), lib/src/backends/chat_template.cpp (llama_chat_apply_template + fallback registry, extended later by profiles), lib/test/backends/llamacpp_test.cpp (tiny GGUF fixture or mocked llama shim).

**Reference (Ommi).** src/backends direct_llamacpp.go / lazy_llamacpp.go / direct_embed.go and OMMI-11 process routing. Deliberate divergence: in-process linking (a C++ advantage Go lacked) deletes the spawn-per-turn machinery, embedded-tool staging, rpath/dylib relocatability apparatus, on-disk prompt caches, and the __embed exec handle — while keeping every invariant those mechanisms enforced. Crash isolation is consciously re-decided (an in-process llama.cpp crash now kills the binary).

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Ommi's sequencing lesson #8: the local process model was re-decided late at the cost of rewriting the routing seam — Apogee decides it inside v0.1.0, right after the skeleton proves the spine.

**Open calls:**
- [user] In-process (recommended) vs an optional spawn-isolation mode for crash containment — decide explicitly and record it; Ommi re-decided this late at real cost (OMMI-11)
- [default: static link, Metal-only for v0.1.0 — +CUDA rides the platform-matrix decision] Static vs shared linking of llama/ggml; binary-size budget; GPU backends
- [default: accept re-ingest on resume initially; llama_state_save_file as a recorded later upgrade] KV persistence across process restarts
- [default: manual, deliberate pin bumps — never auto-tracked] llama.cpp pin/update cadence policy
- [user] Local multimodal/vision (llama.cpp mtmd, mmproj models — requires an mmproj_path backend config field when scheduled): in this item, in model-profiles-and-management, or explicitly deferred — Ommi ships local vision; Apogee must give it a recorded home

**Guardrail(s).** lsof no-LISTEN test; a KV-reuse regression test (eval token count on turn 2); a KV-isolation test for side requests; CI builds the pinned llama.cpp integration on the primary platform so pin drift is caught.

**Acceptance criteria:**
- [ ] Two successive StreamChat calls against one session object process only new prompt tokens — KV reuse asserted via llama eval-count (provider-level test; the chat surface arrives in a parallel branch)
- [ ] A request flagged SideRequest runs on a separate context/sequence, and a subsequent session turn's eval-count proves the session KV was untouched
- [ ] Exact token counts are exposed through the provider counting API, flagged exact (the 80/90 threshold policy itself is tested in chat-cli)
- [ ] lsof test: zero listening sockets during local interactive turns; model unloads on idle when configured; a load failure yields a clear error naming the file, never a crash

**Scope note.** earmarked for v0.1.0 (build after [complete-cli.md](complete-cli.md)).
