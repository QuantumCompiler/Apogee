# The video store: diffusion pipelines as first-class model directories

**What / why.** A video model is not one weights file — it is a **pipeline**: a repository whose root carries `model_index.json` and whose components (`transformer/`, `vae/`, `text_encoder/`, the scheduler) each hold their own config and safetensors. The store cannot see one today — measured 2026-10-18 (the video-generation spike): `modelstore/snapshot.cpp:85` requires a root `config.json` beside the weights, so `models pull` would refuse a Wan 2.2 or HunyuanVideo repository outright. This item teaches the store the **pipeline snapshot kind**: recognized by its root `model_index.json`, taken as **a directory whole** (the `mlx/` format's 27b rule — every component verified present and intact before the directory is committed, a missing or cut-short shard "cannot load", never a guess), pulled by `models pull` from the same Hugging Face source machinery, stored under the store's one declaration as its own format directory (`<model>/pipeline/<id>/`), and listed by `models list` with its components and size said honestly. This is W3 of the spike; the runtime that loads one is [`video-runtime.md`](video-runtime.md)'s, the verb [`video-generate.md`](video-generate.md)'s.

**Core constraint(s).** The store's one-declaration law: the new format is a row in `modelstore/store.h` beside `gguf/`, `safetensors/` and `mlx/`, and pull, list, delete, repair, check and the rename cores (46a) all learn it from that row — no second list anywhere. **Verified whole before commit** (27b's rule): `model_index.json` parsed, every component directory it names present with its own config and weights, sizes recorded in the provenance sidecar; a partial pull leaves nothing behind. **No bundled models** stands: nothing is downloaded unasked, tests use fixture directories, and the real pull is the user's run (the standing testing rule). `models list` prices it honestly — the pipeline's total on-disk size, its components counted, its kind named `pipeline` — and `models info` shows the component table. UTF-8 and provenance discipline as every format before it.

**Seam + files.**
- `data/modelstore/store.h/.cpp` — the `pipeline/` format row; `data/modelstore/pipeline_info.h/.cpp` — the reader (`model_index.json`'s class and components, each component's config/weights facts; "cannot load" with the missing piece named, never a guess — `mlx_info`'s exact shape).
- `business/models/` — the pull path accepting a pipeline repository (the snapshot fetch already walks HF trees; the acceptance test is the root `model_index.json`), verification before commit, deletion and repair.
- `presentation/cli/models.cpp` — the listing's pipeline rows and `models info`'s component table.
- Tests: fixture pipeline directories (hand-built minimal `model_index.json` + tiny component stubs — never real weights): recognized, verified, a missing component refused naming it, listed and priced; the pull path over the scripted fetch fakes; rename (46a) moving the directory whole; `cli.install_parity` untouched.

**Reference.** 27b — the `mlx/` format (a directory taken whole, verified before commit, `mlx_info`'s honest reader) — this item's template end to end; the store's one-declaration invariant; M4 lineage (the sidecar the pipeline's provenance rides); the video-generation spike (2026-10-18) for `snapshot.cpp:85` and the pipeline-layout evidence.

**Decisions made** (dated):
- 2026-10-18 — The set lands in v0.1.9 as track 49 (the user's placement call); the SPEC scope amendment landed first the same day (SPEC.md → One-liner, dated — the user's yes).
- 2026-10-18 — Pipelines are their own store format (`pipeline/`), never shoehorned into `safetensors/` — the snapshot rule's root-`config.json` requirement (`snapshot.cpp:85`) is a different contract and stays untouched.

**Open calls:**
- [default: the reference pipeline for real-weights verification is **Wan 2.2 TI2V-5B** (pulled by the user per the standing rules); the fixture stubs mirror its component layout.]
- [default: `models pull` infers kind from the repository's root (`model_index.json` → pipeline); no new flag unless ambiguity shows up in practice.]

**Guardrail(s).** The fixture matrix (recognized / missing-component refusal by name / partial-pull leaves nothing / listed-priced-infoed); the one-declaration proof (a planted second list fails the house's existing scans); rename moving the directory whole with references repointed; no network in any test.

**Acceptance criteria:**
- [ ] A pipeline repository pulls, verifies whole, and lands under `<model>/pipeline/<id>/`; a broken one refuses naming the missing piece and leaves nothing.
- [ ] `models list` shows pipelines with kind, components and size; `models info` the component table; delete/repair/rename handle them.
- [ ] No test touches the network or real weights; the full suite is green.

**Scope note.** Earmarked for v0.1.9; first of track 49, ungated. Out of scope: loading or running a pipeline ([`video-runtime.md`](video-runtime.md)), the verb ([`video-generate.md`](video-generate.md)), GGUF-quantized video variants (a later question once the runtime exists).
