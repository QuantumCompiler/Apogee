# The MLX backend: a persistent Python child over pipes

**What / why.** A second local runtime beside in-process llama.cpp: an `mlx` backend type that runs models through Apple's MLX on Apple silicon — day-one support for architectures the GGUF ecosystem lags on (mlx-community publishes most new families at release), strong unified-memory behavior, and the training synergy [27c](mlx-depth.md) collects (the fused SafeTensors `train` produces could run directly, skipping GGUF conversion). MLX's core is C++, but everything that makes it an LLM runtime — the model zoo, quant formats, tokenizers, chat templates — lives in Python (`mlx-lm`), so the realistic shape is **not** a second in-process link: it is a **persistent Python child speaking JSONL over pipes**, assembled from parts the repo has already shipped and hardened. The persistent-child-with-token-streaming shape is the claude-cli backend's; the driver-script discipline (compiled in, seeded like an asset, JSONL framed by the one framer, tested under stub modules on bare `python3`) is the training track's; the interpreter environment Apogee owns — never the system Python — already exists under the `training/` row with MLX in it for `train_mlx.py`. One child per session holds the model and its KV cache across turns; close stdin and it exits.

**Core constraint(s).**
- **The recorded constraint this reopens is amended** *(the user's call, 2026-10-03 — [SPEC.md](../../assistant/SPEC.md) → Background carries the dated revision)*: llama.cpp remains the zero-dependency default local runtime on every platform and the binary still ships and runs alone; the `mlx` type is permitted as an explicitly opt-in, interpreter-backed backend beside that default, refusing loudly at construction when its runtime is absent. This item is built *under* that wording — any drift from it (an MLX requirement leaking into a default path, a quiet fallback instead of a loud refusal) violates the amendment, not just taste.
- **A child process is not a listening socket** — the standing rule, and the driver is held to it: no server mode, no port, ever; the lsof suite and the symbol-check conventions cover it like any other child. Its stderr is captured, never inherited (the bounded-tail discipline vendor CLIs already follow).
- **Fail loud, degrade honestly:** on any platform but macos-arm64, or with the venv or `mlx-lm` absent, the backend refuses construction with the exact fix named (`apogee train setup` / the mlx setup path) — the vendor-CLI-on-Windows precedent. `check` gains an MLX row that reports availability and never lies a pass.
- **One loop, one IR:** the backend implements `LLMProvider` like every other; tools render through the tokenizer's own chat template in the driver (`apply_chat_template` with tools), calls come back as text the existing per-family profiles and filters parse — the 25b lesson (each model's own format) applies unchanged, and nothing in `agentloop/` learns the word MLX.
- **The driver is an asset:** compiled in, seeded skip-if-present, byte-identical to its compiled copy by test (the training drivers' rule); it never reads config, never touches the store layout on its own, and speaks only what the JSONL protocol names.
- **Code style carries:** `.h`/`.cpp` pairs; the child handle through the existing `platform/child_process` seam; no raw owning pointers.

**Seam + files.**
- `assets/mlx/mlx_generate.py` (new, compiled in like the training drivers): load via `mlx-lm`, hold the model and KV cache, read request lines (messages, tools, sampling, stop), stream token deltas, report usage and stop reason, exit on closed stdin; stub-module tests on bare `python3` prove the protocol without MLX installed.
- `backends/mlx_local.h/.cpp` (new): the `LLMProvider` over `platform/child_process` + the JSONL framer; spawn-on-first-turn, per-session persistence, the refusal ladder; registered in the factory under type `mlx`.
- `harness/` config surface: the `mlx` entry type (`model_path` pointing at an MLX model directory, sampling fields as llamacpp's); `cli/check.cpp`: the availability row.
- Tests: `tests/data/backends/mlx_local_test.cpp` over a scripted fake driver (the vendor-CLI fixture pattern — golden JSONL both directions, dead-child-fails-fast, stderr tail folded into failures); the driver's stub-module suite; the no-listen check extended over a live driver run.

**Reference (Ommi).** The analog is Ommi's own design: **process-routed local inference** was how Ommi ran every local model (a per-turn `ommi-completion` spawn with an on-disk prompt cache, CHAT.md) — Apogee diverged to in-process llama.cpp, and this item brings the process-routed pattern back for a *second* runtime beside the first, with one deliberate divergence from Ommi's shape: a **persistent** child holding the cache in memory rather than per-turn spawns with cache files (the claude-cli backend proved the persistent shape against a real peer). The driver discipline ports from Apogee's own training track (Milestone Z), not from Ommi.

**Decisions made** (dated):
- 2026-10-03 — Asked for by the user, targeted **v0.1.8**, specced as track 30 (the renumber freed the number; nothing shipped under it).
- 2026-10-03 — **Child over pipes, not mlx-c in-process:** the C API gives arrays, not a model zoo; reimplementing per-architecture inference in C++ means chasing `mlx-lm`'s zoo forever. The pattern that keeps the invariants is already proven twice in-repo.
- 2026-10-03 — **llama.cpp stays the default local runtime on every platform.** MLX is additive, opt-in, macos-arm64 only; no existing config changes meaning.
- 2026-10-03 — **The SPEC amendment is made** (the user's call, same day): the skeleton's "no runtime interpreter dependency on core inference paths" rule revised in [SPEC.md](../../assistant/SPEC.md) → Background with the recommended wording. The track is unblocked; this item builds to that wording.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): `mlx-lm` joins the training venv by the existing setup path — one Python environment, one owner.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): one driver child per chat session, spawned on first use; `complete` spawns per run.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): sampling fields mirror llamacpp's; 26h's resolved semantics apply to both local types when it lands.

**Guardrail(s).**
- Golden JSONL both directions against the scripted fake driver, chunk-boundary cases through the one framer.
- The refusal ladder: wrong platform, missing venv, missing `mlx-lm`, bad model path — each a distinct, named message; mutation-tested.
- No listening socket across a live turn (the existing continuous-sample check, run against the real driver); stderr captured with the bounded tail.
- The driver byte-identical to its compiled-in copy; the stub-module suite green on bare `python3`.
- On real weights: a streamed, tool-using chat completes against an MLX build of [the model families](../../assistant/DEVELOPER.md#on-real-weights-the-model-families) — the capability is token streaming plus a dispatched tool call per family, recorded on ship.

**Acceptance criteria:**
- [ ] With the venv ready, `apogee chat -m <mlx entry>` streams token deltas from an mlx-community model, holds its KV cache across turns in one child, and exits the child cleanly on `/exit`.
- [ ] A tool-using turn renders tools through the model's own template and dispatches a parsed call through the one loop.
- [ ] On Linux/Windows, or without the venv, the backend refuses at construction naming the fix; `check` shows the MLX row honestly; every other backend is untouched.
- [ ] No listening socket exists at any point of a session; the driver's stderr never reaches the terminal.
- [ ] `apogee complete -m <mlx entry> "…"` works one-shot, byte-clean on a pipe.

**Scope note.** Item **27a**, earmarked for **v0.1.4** — the track's foundation; gated on nothing pending (its SPEC call was answered and the amendment made, 2026-10-03). Out of scope: model acquisition and the store format ([27b](mlx-model-operations.md)); vision, serve routing and the training synergy ([27c](mlx-depth.md)); embeddings through MLX; any in-process MLX linkage.
