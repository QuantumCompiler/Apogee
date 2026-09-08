# Model acquisition: the remainder — quantize, the multimodal transform, and SafeTensors

**What / why.** **The core of this item shipped 2026-09-07** ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone N): `apogee models pull` fetches any user-named ref from Hugging Face or the user's Ollama store through a copy → verify → commit ladder that never leaves a half-downloaded model on disk, writes a provenance/integrity sidecar, and `models delete` / `models repair` manage what it acquired. What is left are the three pieces that were not buildable in the same pass, kept here rather than quietly dropped.

**Do not re-plan the shipped parts.** The ladder, the sidecar, both sources, and the three subcommands exist and are test-locked. This document is only the residue.

## 1. In-process quantization

`apogee models quantize <in.gguf> <out.gguf> <type>` via `llama_model_quantize`, so a user can shrink a model they already have without a second toolchain.

**Why it did not land with the rest:** it needs llama.cpp *linked*, and llama.cpp is off by default (`-DAPOGEE_ENABLE_LLAMA=ON`). That is the same constraint that made the GGUF header reader Apogee's own rather than llama.cpp's — but where a header read could be reimplemented in 200 lines, quantization cannot. So this one genuinely has to sit behind the flag.

- **Seam:** follow `backends/llama_real.cpp`'s pattern exactly — an `#if defined(APOGEE_ENABLE_LLAMA)` implementation and an `#else` branch returning a clear "not built in" answer that names the flag. The command must exist in every build and *refuse* in one without llama.cpp, never be silently absent: a missing subcommand reads as "Apogee cannot do this" rather than "this build cannot".
- **Guardrail:** the refusal path is testable everywhere and must be tested there; the real path is covered on the llama-enabled CI job against a tiny fixture GGUF.

## 2. The Ollama combined text+vision transform

Ollama distributes some models as a single blob holding both a text model and a vision tower. Ommi **stripped** the vision tensors to make such a file loadable, recording `TransformVisionStripped` in its sidecar.

**The blocking question was answered 2026-09-07** ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone O) **— and it changed the shape of the work.** mtmd requires a **separate projector file**: passing a text model as its own `mmproj_path` fails at `mtmd_init_from_file` ("Failed to load CLIP model"). So a combined blob cannot be used for vision as it stands.

But note the asymmetry with Ommi, because it inverts the task. **Ommi stripped vision tensors so the text model would load**; in Apogee's in-process llama.cpp the text model loads fine untouched. What a combined blob actually needs is the projector **extracted into its own file** — the opposite operation from a strip.

- **The detection already ships:** `GgufInfo::is_projector()` and `has_vision_tensors()` distinguish a pure projector from a combined blob, `models list` and `check` report each correctly, and the sidecar's `transform`/`transform_note` fields are written and parsed.
- **What remains** is an *extract*, not a strip — and only if a user turns out to have such a blob. **No combined blob was available to test against**, so whether Ollama still distributes them in that shape is itself unconfirmed. Confirm that before building anything: the cheapest outcome here is discovering the case no longer exists.

## 3. Hugging Face SafeTensors and datasets

[SPEC.md](../assistant/SPEC.md) → Scope lists Hugging Face downloads as covering "models, GGUFs, SafeTensors, datasets". Only the GGUF path exists; a repository with no `.gguf` is refused with a message saying so.

- SafeTensors matters for the confirmed training direction ([training-distillation.md](training-distillation.md)), which needs full-weight snapshots rather than quantised GGUFs.
- **Conversion needs a Python script dependency** (`convert_hf_to_gguf.py`), which the repo already permits — "primarily C++" governs the harness, not every satellite script.
- A dataset download is a different shape again: many files rather than one, and no GGUF header to verify. The ladder's size and digest rungs still apply; the header rung does not, and the verification report must **say** so rather than reporting a check it skipped as passed.

**Core constraint(s).** Unchanged from the shipped half, and binding on these three too:
- **No refusal by policy.** Warnings are advisory and fire before bytes move.
- **Copy → verify → commit.** Any new acquisition path uses `models::acquire`; it does not grow its own.
- **The verification report names which checks ran.** A path that cannot run the header check must report that, never imply it passed.
- **Never mutate the Ollama store.** Its blobs are shared between models; `ollama rm` is the only supported removal.
- **Mutating models actions** stay in the admin plane's documented-skip list until their HTTP twins are backfilled ([admin-plane-foundation.md](admin-plane-foundation.md)).

**Seam + files.** New: `lib/src/cli/source/models/quantize.h/.cpp` (behind the llama flag, mirroring `llama_real.cpp`), and a projector-extraction unit only if a real combined blob turns up. Extended: `models/source_hf.cpp` (non-GGUF refs), `commands/models_pull.cpp` (the `quantize` subcommand).

**Reference (Ommi).** `lib/cli/src/ollama/transform.go` (294 lines — `TransformVisionStripped`, and the sidecar bookkeeping a transform requires), `lib/cli/src/cmd/ommi/models.go` → the `quantize` / `convert` / `convert-ggml` / `convert-lora` subcommands.

**Decisions made** (dated):
- 2026-08-24 — Open local-model policy and dual sources *(user decision)*. Verification is integrity protection only.
- 2026-09-07 — **There is no end-of-install offer** *(user call)*. Consumed: the offer module, the decline marker, and the install-path wiring were never built, and the acceptance criterion for them was deleted rather than left unmet.
- 2026-09-07 — **The core shipped and this document was reduced to the remainder** rather than deleted. Three acceptance criteria were unmet, and marking the item done would have made the queue claim work that does not exist.
- 2026-09-07 — The vision transform's blocking question is **answered** ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone O): mtmd needs a separate projector, so the operation a combined blob would need is an *extract*, not Ommi's strip. Whether any such blob still exists is unconfirmed and is the first thing to check.

**Open calls:**
- [default: refuse a dataset ref until the multi-file shape is designed] Whether dataset downloads ride the same single-file ladder. They are a different shape — many files, no header — and bolting them onto a ladder built for one file is how the `.partial` discipline gets implemented twice.
- [default: a Python satellite script, invoked through the existing child-process seam] How SafeTensors→GGUF conversion runs.

**Guardrail(s).** The refusal path of `quantize` tested in the default build; the real path on the llama-enabled job against a tiny fixture GGUF. Any new source reusing `models::acquire` rather than reimplementing it, asserted by the same failure-injection shape the shipped sources use — a failure must land nothing. Every guardrail mutation-tested before it is trusted.

**Acceptance criteria:**
- [ ] `quantize` runs in-process in a llama-enabled build, and **refuses with a message naming the flag** in one without — never silently absent
- [ ] A real Ollama combined text+vision blob is located (or confirmed not to exist any more); if one exists, the projector is **extracted** from it — mtmd needs a separate file, so Ommi's strip is the wrong operation
- [ ] A SafeTensors ref either downloads or is refused with a message naming the conversion path, rather than today's generic "no .gguf"
- [ ] Any added path lands nothing on failure, verified by the same failure-injection shape as the shipped ladder

**Scope note.** Gated ring, local-model depth, the residue of the third of four. **The bulk shipped 2026-09-07.** Section 2's blocking question was answered by multimodal-vision ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone O). Out of scope: everything already shipped — the ladder, the sidecar, both sources, `pull`/`delete`/`repair`, and the listing. Read Milestone N before touching any of it.
