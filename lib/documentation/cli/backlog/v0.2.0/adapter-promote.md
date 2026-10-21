# Adapter promote: a version is kilobytes, not gigabytes

**What / why.** Every promote today is a full model: the plan fuses the adapter into the base, converts the fused tree, registers the result (`promote.cpp:113-152` — "a fused tree is model-sized"), and `retain_versions` keeps several complete copies. The spike proved the small path live (2026-10-21): the pinned `convert_lora_to_gguf.py` converted a real-format PEFT adapter against the store's Llama-3.2-1B snapshot into a **1.7 MB** GGUF LoRA (64 tensors, `general.type=adapter`, alpha carried) — against the ~2.5 GB full F16 a 1B promote writes, **~1,500× smaller** — using the already-seeded `gguf-py`; the converter itself is the one piece not vendored (the seeded `training/scripts/convert/` tree holds `convert_hf_to_gguf.py` and `gguf-py` only). This item closes W2 and W4's ledger half: **an adapter promote mode** — the fuse skipped, the PEFT adapter converted by the **vendored-and-seeded** `convert_lora_to_gguf.py` (the existing converter-tree mechanism: `third_party/llama.cpp-convert/`, the generator, the retired digests) into a small versioned adapter file under `training/versions/`, registered onto the entry's `adapters:` (the base untouched); the **mlx target registers the adapter directory as-is** (no conversion — `mlx_lm` consumes its own format directly, the spike's per-trainer asymmetry); the **ledger records `adapter_path` and the base's weight hash**; rollback repoints the small file; retention applies to kilobytes. **Merge-and-promote stays the default** — the adapter mode is the opt-in flag until the user's real pass settles speed and quality, flipping the default a later one-line decision.

**Core constraint(s).** **Promote's law unchanged**: a failing candidate never reaches inference — the gate runs before either mode, and the adapter mode changes what is written, never what is judged. **One converter mechanism**: the lora converter is vendored at the pinned revision beside its sibling, compiled in by the same generator, seeded and digest-tracked identically — never fetched, never a second tree. **The registration is 53a's unit** through the one editor — no second write path. **Closures stay closures** (Milestone Z's shape): `promote` in `training/` still never names a backend; the composition root hands the adapter-convert closure as it hands the full-convert one today.

**Seam + files.**
- `third_party/llama.cpp-convert/` + `scripts/generate_training_assets.py` + `data/contracts/assets_converter.cpp` (the vendored `convert_lora_to_gguf.py`, compiled in, seeded, retired-digest tracked).
- `business/training/promote.h/.cpp` (the adapter plan beside the fused plan), `business/training/convert.cpp` (the lora-convert runner over the one script runner), `business/training/store.cpp/.h` (the ledger's `adapter_path` + base hash; rollback repointing).
- `presentation/cli/train.cpp` (the opt-in flag, completing), machine-mode.md's train documents grown where they say the version's shape (28h).
- Tests: the plan matrix over closures (adapter mode skips the fuse, converts once, registers through the editor; mlx target copies the directory and converts nothing); the ledger round-trip and rollback repoint; retention over small files; byte-identity of the seeded converter; the refusal when the run's trainer was mlx but the target llamacpp (formats don't cross — said, never guessed).

**Reference.** The LoRA spike (2026-10-21) — the live conversion and its numbers; Milestone Z — the promote plan, closures and converter-tree mechanism consumed whole; 27b/27c — the seeded-script discipline and retired digests.

**Decisions made** (dated):
- 2026-10-21 — **Merge stays the default; the adapter promote is opt-in** (the spike's default, standing) until the user's real pass measures the unmerged path.
- 2026-10-21 — Per-trainer formats are routed, never converted across: PEFT → GGUF LoRA for llamacpp targets; the MLX adapter directory as-is for mlx targets.

**Open calls:**
- [default: the flag's name (`--adapter` the working shape) and the version file's naming under `training/versions/` at build; retention semantics unchanged in meaning.]

**Guardrail(s).** The plan and ledger matrices over closures, hermetic; converter byte-identity and digest; the cross-format refusal; the end-to-end real promote (train → adapter promote → chat on the entry) is the user's run, recorded.

**Acceptance criteria:**
- [ ] The opt-in promote writes a small adapter version, registers it on the entry's `adapters:` through the one editor, and leaves the base untouched; rollback repoints it.
- [ ] The mlx target registers the adapter directory with no conversion; a cross-format ask refuses naming the formats.
- [ ] The vendored converter is seeded byte-identically with its digest recorded; the default promote is byte-identical to today; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 53); gated on [`adapters-at-the-runtimes.md`](adapters-at-the-runtimes.md) (53a — the unit it registers into). Out of scope: the doctor's rows and the gate's quantized-base eval (53c), default-flipping (a later decision), BitDelta-style compression (the track's research question).
