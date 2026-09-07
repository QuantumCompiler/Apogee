# Local multimodal: llama.cpp mtmd and the `accepts_images` promise

**What / why.** Make `VisionCapable` tell the truth on the local backend. Everything above the provider is **already shipped**: `harness::ContentPart::ImageUrl` exists in the IR, `--image` loads and base64-encodes files on both `complete` and `chat` (`commands/helpers.cpp` → `load_image_part`), all three cloud wire translators already emit vendor-correct image blocks, and `Harness::accepts_images()` already asks the provider through the capability seam rather than switching on a type. `LlamaCppProvider::accepts_images()` returns a hardcoded `false`, and `complete.cpp` prints a message that literally says local vision "has not landed".

**So this item is small and specific: wire llama.cpp's mtmd, add `mmproj_path`, and make that `false` become a real answer.** It is the last step of a capability that was designed end-to-end and then left with one unimplemented leaf — which is why it is its own item rather than a line inside a larger one: the work is a provider implementation, not a quirk-layer or a management concern, and it shares no code with either.

**It also closes a parity gap found during grooming (2026-09-06).** `complete.cpp:162` guards attachments on `harness.accepts_images(model)` and fails with a clear message; **`chat.cpp` has no such guard** — `apogee chat --image pic.png` against a llamacpp backend loads the file, builds the part, and hands it to a provider that has just said it cannot accept images. "Parity is the product" makes a capability check present on one surface and absent on the other a bug in its own right, and it is one this item must fix whether or not mtmd lands cleanly.

**Core constraint(s).**
- **The capability answers for the configured state, not the build.** `accepts_images()` is true when *this backend entry* has a usable mmproj model, false otherwise — a llamacpp entry without one must still answer false, and a build without `APOGEE_ENABLE_LLAMA` must not claim vision.
- **No `dynamic_cast` above the provider** ([⚠ Capability probes never leak a cast](../assistant/CLAUDE.md#-capability-probes-never-leak-a-cast)). The seam already exists and both call sites already use it; this item must not add a type test to make vision work.
- **A probe on an unroutable model answers "no" rather than throwing** — the existing rule, unchanged.
- **Refusal is uniform across surfaces.** One helper, used by `complete`, `chat`, and machine mode, so a fourth surface cannot forget it. The current duplication-by-absence is the bug.
- **In machine mode the refusal is an `error` event, not prose on stdout** ([machine-mode.md](../reference/machine-mode.md)) — stdout carries only JSONL.
- **`mmproj_path` is a backend-entry field**, so one config can hold a vision-capable llamacpp entry and a text-only one; it follows `model_path`'s existing shape and validation.
- **llama.cpp stays behind its existing seam.** mtmd is reached through `backends/llama_runtime.h`, so vision is testable without weights and without `APOGEE_ENABLE_LLAMA`, exactly as the rest of the backend already is.

**Seam + files.** Extended: `lib/src/cli/source/backends/llamacpp.h/.cpp` (mtmd context, image-part → token embedding, a real `accepts_images()`), `backends/llama_runtime.h` (the mtmd calls added to the injectable seam), `harness/config.h/.cpp` + `assets/config.yaml` (the `mmproj_path` field and its documentation comment), `commands/helpers.h/.cpp` (**the one shared attachment guard** the surfaces call), `commands/chat.cpp` (call it — the gap above), `commands/complete.cpp` (call it instead of its private copy), `backends/llamacpp.h:116` and `commands/complete.cpp:164` (the two comments and the one error message that currently say local vision has not landed). Tests: `lib/src/cli/tests/backends/llamacpp_test.cpp` (capability truth-table over the fake runtime), `tests/commands/` (the guard, asserted on every surface).

**Reference (Ommi).** Thin. Ommi carried an `MmProjPath` field on its backend config (`lib/cli/src/harness/models.go:593`) and stripped multimodal tensors out of Ollama's combined blobs (`lib/cli/src/ollama/transform.go` → `TransformVisionStripped`) rather than using them — its llama.cpp was a subprocess and mtmd was not reachable. **That inversion is the interesting divergence:** Ommi discarded the vision tensors to make a file loadable; Apogee links mtmd in-process and should be able to use them. [model-acquisition.md](model-acquisition.md) records the corresponding question on its side — whether the strip transform is still needed once this ships.

**Decisions made** (dated):
- 2026-08-31 — Local vision was assigned to the local-model-depth area by user decision on the llamacpp-backend item, with `VisionCapable` shipped in `harness/provider.h` and `LlamaCppProvider` answering `false` through it, so wiring vision means implementing a capability rather than inventing one.
- 2026-09-06 — **Split out of the `model-profiles-and-management` guard document** as the fourth of four *(user call: four-way split)*, on the finding that the item shares no code with the quirk layer or with model management — it is one provider implementation plus a cross-surface guard.
- 2026-09-06 — **Scope corrected during grooming.** The guard document treated local vision as a large multimodal build-out. It is not: the IR, the `--image` flag, the file loading, the base64 encoding, the three cloud translators, and the capability seam **all already ship**. What is missing is the llama.cpp leaf and one missing guard.
- 2026-09-06 — This item owns fixing the `chat` attachment-guard gap, because it is the item that makes the guard's answer non-constant and therefore the item where a wrong answer starts costing something.

**Open calls:**
- [default: yes — a separate `mmproj_path`, following `model_path`] Whether the projector is its own config field or inferred from the model file. Inference guesses; a field states.
- [default: refuse with the existing message shape] What a vision-capable entry does with an image format mtmd cannot handle. `load_image_part` already refuses unsupported *extensions*; this is the narrower case of a supported extension mtmd rejects.
- [default: defer to characterization] Whether Ollama's combined text+vision blobs load through mtmd as-is. Answering it is what tells [model-acquisition.md](model-acquisition.md) whether to keep the strip transform.

**Guardrail(s).** A **capability truth table** over the injected runtime — no mmproj configured, mmproj configured, llama disabled at build time — asserting `accepts_images()` for each, so the promise is tested without weights. **The attachment guard asserted on every surface that accepts `--image`** (`complete`, `chat`, and machine mode), written as one test over the surface list rather than one test per surface, so a fifth surface fails the test by existing. Mutation-test it: delete the guard from one surface and the test must name that surface. A fixture-based mtmd path so image handling is covered in CI without a multi-gigabyte vision model.

**Acceptance criteria:**
- [ ] `LlamaCppProvider::accepts_images()` answers from configured state: true with a usable `mmproj_path`, false without one, false in a build without `APOGEE_ENABLE_LLAMA`
- [ ] A local vision model answers a question about an attached image through `complete` and `chat`
- [ ] `apogee chat --image` against a backend that cannot accept images **refuses**, with the same message `complete` gives — the parity gap found 2026-09-06 is closed and test-locked across all surfaces
- [ ] In machine mode the refusal arrives as an `error` event; stdout carries only JSONL
- [ ] No `dynamic_cast` is added above the provider layer; both call sites still go through `Harness::accepts_images()`
- [ ] The three stale "vision has not landed" comments and messages are gone
- [ ] Whether Ollama's combined blobs load through mtmd as-is is answered and recorded, so [model-acquisition.md](model-acquisition.md) knows whether it still needs the strip transform

**Scope note.** Gated ring, local-model depth, **fourth of four**. Gate satisfied: llamacpp-backend shipped 2026-08-31 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone J) with the capability seam and the injectable runtime; the `--image` surface and the cloud translators shipped with Milestones I and J. No dependency on the other three local-model items — it can be taken at any point, and the `chat` guard fix inside it is worth having regardless. **Practically gated on a vision-capable local model plus its projector file** for the end-to-end criterion; the capability truth table and the cross-surface guard need neither. Out of scope: cloud vision (already shipped), audio or video parts, and image *generation*.
