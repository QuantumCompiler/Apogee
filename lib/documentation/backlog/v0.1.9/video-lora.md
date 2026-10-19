# LoRAs at generation: community fine-tunes applied to a pipeline

**What / why.** The quality ceiling for a niche genre or style on an open video model is set by community fine-tunes, not by the base weights — the Wan ecosystem's LoRAs are the largest of any open video family, and a base model plus a genre LoRA beats any base model alone (the user's ask, 2026-10-19, from a concrete genre need the base models serve poorly). Apogee cannot use them: the 49b runtime loads a pipeline exactly as its repository ships it — **zero LoRA handling anywhere in `business/video/` or the seeded driver** (measured 2026-10-19, a grep over the shipped module and `video_generate.py`) — while diffusers itself carries the capability natively (`load_lora_weights` / `set_adapters`). This item adds the knob: **`apogee video generate … --lora <path>[:<scale>]`** (repeatable) — each LoRA a **user-supplied safetensors file on disk** (the `--music <bed.wav>` precedent: Apogee never pulls one), handed to the driver in the request and applied through **diffusers' own loader** before denoising, each with its strength. Every applied LoRA is **recorded in the run document** — path, content hash, scale — so the 49d laws extend unchanged: a `--from` rerun holds the LoRAs as it holds the seed, documents chain, and a clip remains reproducible from its record alone.

**Core constraint(s).** **No second weights path**: the LoRA is applied inside the driver by diffusers' own loader — never parsed, merged or interpreted by Apogee's C++ side, which handles only file facts and the record. **The refusal ladder grows file facts, not guesses** (49b's law): a missing path, or a path that is neither a safetensors file nor a directory holding one, refuses before any spawn, naming the file; a pipeline that cannot take a LoRA is the driver's structured error, said with the pipeline's class. **The record is the reproducibility contract**: each entry carries path, sha256 (`contracts/sha256.h`, the run-manifest precedent) and scale; a rerun that finds a changed file under a recorded path refuses naming it rather than silently rendering something else. **The seeded driver stays governed**: byte-identity to the compiled-in copy, the retired-digest upgrade (27c's mechanism) carrying older unedited drivers forward, the stdout guard untouched. The LoRA count is a named constant quoted in refusals. Every mode: the flags on the one verb (completion free per ADR 0007, the path completed), the run document's `loras` field in machine-mode.md (28h), the exec line already carrying `video generate` (ADR 0010, no new classification).

**Seam + files.**
- `lib/src/cli/assets/video/video_generate.py` — the driver grown: `loras` in the request JSON, applied via `load_lora_weights`/`set_adapters` with scales before denoising, a pipeline without LoRA support a structured error naming its class; the generator regenerates the compiled-in copy, the old digest recorded in `assets/retired-scripts.txt`.
- `business/video/generation.h/.cpp` — the request and record grown: the `loras` list (path, sha256, scale) on both, the hash computed once at request build, the rerun verification against the record.
- `business/video/runtime.h/.cpp` — the ladder's new file facts (path exists, safetensors by name or directory content), refused before a spawn with the fix named.
- `presentation/cli/video_cmd.cpp` — `--lora` parsed (repeatable, the `:<scale>` suffix), the cap's refusal, `--from` interplay; machine-mode.md's run-document section amended.
- Tests (fake driver, no weights): the request JSON carries the list in argv order with scales; the record's fields exact; the rerun matrix (bare `--from` keeps and verifies, a hash mismatch refuses naming the file, any `--lora` on a rerun replaces the whole set); the refusal matrix; the cap; byte-identity.

**Reference.** 49b/49c/49d (shipped, [Milestone AM](../../assistant/MILESTONES.md#milestone-am--video-generation)) — the driver discipline, the record-and-sidecar law, the seed-hold rerun semantics this extends; 27c — the retired-digest driver upgrade; the training run manifests — hashing an input into a record so a rerun is honest; diffusers' LoRA API as the external prior art, used as-is.

**Decisions made** (dated):
- 2026-10-19 — The item lands in v0.1.9 as **49e** (the user's placement call; track 49 grown again by the ever-assigned rule, letters a–d spent).
- 2026-10-19 — LoRAs are **user-supplied files on disk**, never pulled by Apogee; store integration (pulling a LoRA into the model store) is the recorded later item.
- 2026-10-19 — Applied LoRAs ride the run document and obey the 49d laws whole: held on `--from` like the seed, chained, verified by hash on a rerun.
- 2026-10-19 — Application happens inside the driver through diffusers' own loader — Apogee's side never reads the weights.

**Open calls:**
- [default: the flag grammar — repeatable `--lora <path>[:<scale>]`, scale 1.0 when absent; the count cap a small named constant quoted in the refusal.]
- [default: a bare `--from` keeps the record's LoRAs verifying each hash; any `--lora` on the rerun replaces the whole set; `--no-lora` clears it — each said in the rerun's printed overrides.]
- [default: the unsupported-pipeline refusal is the driver's structured error naming the pipeline class and that the base model cannot take a LoRA; file facts stay pre-spawn.]

**Guardrail(s).** The fake-driver matrix (request shape, order, scales); the record fields and rerun matrix (keep/verify/replace/clear, the mismatch refusal by name); the refusal ladder's new rungs exhaustively; the cap; byte-identity of the regenerated driver and the retired digest recorded; no network, no weights in any test — the real pass (one community LoRA on the reference Wan 2.2 pipeline) is the user's run, recorded per 49's standing rule.

**Acceptance criteria:**
- [ ] `video generate --lora <path>` applies the LoRA through the driver in tests (fake driver), the request carrying the list and the record carrying path, hash and scale per entry.
- [ ] `--from` holds recorded LoRAs with hashes verified; a changed file refuses naming it; `--lora`/`--no-lora` on a rerun replace or clear, said.
- [ ] Every refusal rung names its fact and fix; the cap refuses quoting its constant; the document conforms and machine-mode.md agrees.
- [ ] Driver byte-identity green with the retired digest recorded; the full suite green.

**Scope note.** Earmarked for v0.1.9; ungated — track 49 is shipped, this grows it in place. Out of scope: pulling or storing LoRAs (`models pull` for LoRAs, the recorded later), training a LoRA (video training stays a recorded out of 49), a compose-level pass-through (one small follow-up once wanted — the composer consumes 49c's core, so the plumbing is already shared), image-generation LoRAs (ride the image sibling when it lands).
