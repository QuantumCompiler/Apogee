# Adapters at the runtimes: a fine-tune applied, never merged

**What / why.** A promote today writes a full multi-GB model per version when only the delta changed; the pinned llama.cpp already carries everything needed to apply the delta at load time, unused — measured 2026-10-21 (the LoRA spike): `llama_adapter_lora_init` and the per-context `llama_set_adapters_lora` at `llama.h:683/721` (plus a `FILE*` variant at `:688`), **8 matching symbols exported by the built library**, while `llama_real.cpp` loads the model (`:1298`) and initializes its chat and embedding contexts (`:1137`, `:1173`) with no adapter step, and config entries have no adapter unit. The mlx side is equally ready: `mlx_lm.load(adapter_path=…)` and `load_adapters` exist in the owned venv (`mlx_lm/utils.py:607/652`), and `train_mlx.py` already infers with an unmerged adapter for its own eval scoring (`:133`). This item closes W1 and W3: **`adapters: [{path, scale}]` on llamacpp and mlx entries** — written through the one editor, documented in the starter template — carried through `llama_runtime` into `llama_real` (each adapter loaded once after the model, set on every context at its scale) and into the mlx driver as its `adapter_path`; **each adapter pinned to its exact base by weight hash** (recorded at registration, verified at load, a mismatch refused by name — the 49e `--lora` pattern); and **the prompt-cache fingerprint grows the adapter set** — today it is the base file's path, size and mtime alone (`prompt_cache.h:41-57`), so a cache saved under one adapter would silently restore under another; the set (paths, hashes, scales) joins the fingerprint, and a cache from a different set is a miss, never a lie.

**Core constraint(s).** **The layering holds**: the adapter list crosses from config as plain data; `harness/` and the loop never learn what an adapter is beyond the entry's fields. **llama.cpp stays unmodified** (the APIs are the pin's own). **Refusals are file facts first** (a missing adapter file, a base-hash mismatch — each naming its fix), the runtime's own errors relayed after. **Per-context is the law** on the llamacpp side (the API's own design): the model loads once, every context gets the entry's set — the seam 53d's sharing later stands on. Hermetic tests over the `llama_runtime` seam and the fake mlx driver; the F16/Q4 output-equivalence, speed and RAM comparisons are **the user's real pass, recorded** (no GGUF bases exist on the dev machine since 2026-10-19; the spike could not measure them). New flags complete (ADR 0007); the entry fields ride `set-backend`'s family (46b).

**Seam + files.**
- `data/contracts/config.h/.cpp` + the editor (the `adapters:` unit on llamacpp and mlx entries), `data/contracts/config_template.cpp` (the documented field).
- `data/backends/llamacpp.cpp/.h`, `llama_runtime.h`, `llama_real.cpp` (the load-and-set at the three anchors; the hash check before any load).
- `data/backends/mlx_local.cpp` + the seeded driver (the `adapter_path` argument; byte-identity and retired digest ride any driver change).
- `data/backends/prompt_cache.h/.cpp` (the fingerprint's adapter set).
- Tests: the runtime-seam matrix (adapters loaded once, set per context with scales, order kept); the refusal table (missing file, hash mismatch by name); the fingerprint (same base + different set = miss; byte-equal set = hit); the mlx driver's argv pinned; config round-trips golden.

**Reference.** The LoRA spike (2026-10-21) — every anchor and measurement above; 49e — the hash-pinned user-supplied-weights pattern consumed whole; 26j — the prompt cache this extends; 27a–c — the driver discipline the mlx flag rides.

**Decisions made** (dated):
- 2026-10-21 — Specced from the LoRA spike (the user's call); runtime application lands first, the promote path second (53b), sharing third (53d).
- 2026-10-21 — The adapter set is part of the cache's identity; a cache is never restored under a different set.

**Open calls:**
- [default: the entry field's exact spelling and `set-backend`'s flag for it at build, within the 46b family; scale defaults to 1.0.]

**Guardrail(s).** The seam matrices above, all hermetic; the real pass (one adapter on F16 and Q4_K_M bases, outputs against the merged twin, tok/s and RAM) is the user's run, recorded per the standing rules.

**Acceptance criteria:**
- [ ] An entry with `adapters:` runs with them applied (llamacpp per context at scale; mlx via the driver), proven over the seams; without the field, byte-identical behavior.
- [ ] A missing adapter or a changed base refuses by name with its fix; the prompt cache misses across different adapter sets and hits within one.
- [ ] Config edits round-trip comment-preservingly; flags complete; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 53); ungated — the track's head. Out of scope: producing adapters ([`adapter-promote.md`](adapter-promote.md)), doctor rows and the eval gate ([`adapter-doctor-and-gate.md`](adapter-doctor-and-gate.md)), base sharing ([`one-base-many-roles.md`](one-base-many-roles.md)), BitDelta/TIES/DARE (the track's recorded research question).
