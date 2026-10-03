# MLX depth: vision, serve, and the training shortcut

**What / why.** The closers that make the MLX track feel native rather than bolted on. **Vision:** the backend reads images through `mlx-vlm` in the same driver discipline, so an MLX vision model joins the `--image`/attachment flows every other image-capable backend already serves — and the 26b helper-role machinery can point the `vision` role at an MLX model like any other entry. **Serve:** the policy decision made explicit — an `mlx` backend may be routed by `apogee serve`, because unlike the vendor CLIs there are no subscription credentials in play; it is local compute behind the same loop, and the refusal list stays what it was about (credentials, not runtimes). **The training shortcut — the reason this track pays for itself:** today `train promote` must fuse the adapter and convert to GGUF before the tuned model can be chatted with; with MLX inference in the house, promotion gains an **MLX target** that registers the fused SafeTensors (or runs the adapter over its base directly) as an `mlx` backend — the convert step becomes optional, and the train → try loop on Apple silicon shortens from minutes to moments.

**Core constraint(s).**
- **One attachment and image pipeline** (consumed decisions — 26d/26e): an MLX vision model is just another backend that answers "yes" to reading images natively; the media machinery routes to it exactly as it routes to mtmd-equipped llamacpp entries. No MLX-specific path in `agentloop/`.
- **Serve's exclusions keep their meaning:** vendor-CLI types stay refused by type (credentials); `mlx` routes like `llamacpp` does. The driver child under `serve` still opens no socket of its own — only `serve.cpp` listens, per the allow-listed object rule, and the no-listen checks run over a served MLX turn.
- **Promotion stays one plan** (consumed decision — Milestone Z): gate → (fuse) → register → version ledger → retention → rollback, with the MLX target one branch of the existing closure-driven plan in `commands/train.cpp` — `training/` still never names a backend or edits config itself.
- **No silent capability claims:** `check` and `models status` say whether the vision path is actually available (venv has `mlx-vlm`); a `vision` role pointed at a non-vision MLX entry warns exactly as it does elsewhere.
- **Parity:** everything here reaches every surface through the shared cores; nothing is CLI-only except training control, which stays CLI-only by the standing rule.

**Seam + files.**
- `assets/mlx/mlx_generate.py`: the vision extension (image parts in the request protocol, `mlx-vlm` loading when the model carries it); the stub-module suite grows the image cases.
- `backends/mlx_local.cpp`: image-capability answer from the model directory's own markers; the capability probe stays a harness question.
- `httpserver/`: nothing structural — a conformance case proving an `mlx` entry serves `/v1/chat/completions` streamed, and the type-refusal test updated to assert it is *not* refused.
- `training/promote` plan + `commands/train.cpp`: the MLX registration target (`--target mlx`), fused-SafeTensors path through [31b](mlx-model-operations.md)'s store row, the version ledger unchanged.
- Tests: fake-driver image round-trip; promote-plan table gains the MLX branch (closure-tested like the GGUF one); the served-stream conformance case; `check` rows.

**Reference (Ommi).** No analog for any of the three: Ommi had no second runtime, no vision on local models, and its training promoted into its one format. The in-house precedents consumed: local multimodal (Milestone O), the serve plane's refusal semantics (Milestone T), and the promote plan (Milestone Z).

**Decisions made** (dated):
- 2026-10-03 — Split from the MLX track as its closer: each piece needs 31a's child protocol and 31b's store row to exist first.
- 2026-10-03 — **Serve may route MLX** (the policy half of this item, decided at spec time): the serve exclusion list is about credentials, and extending it to a local runtime would misstate the rule it encodes. The conformance test makes the decision structural.

**Open calls:**
- [default: the promote MLX target registers the *fused* model; running an unfused adapter over its base directly is a later convenience if `mlx-lm` keeps it stable] Fused-first.
- [default: `mlx-vlm` joins the venv on demand (the setup path installs it with `mlx-lm`); absent, vision answers "no" and `check` says why] The vision dependency.
- [default: videos and audio stay on the 26e text path (timeline/transcript) for MLX models in this cut — only still images go native] Media scope.

**Guardrail(s).**
- The image round-trip against the fake driver, plus the capability answer flipping with the model directory's markers.
- The promote table: the MLX branch gated, versioned, retained and rolled back identically to GGUF's — mutation-tested with the rest of the plan.
- The served-turn no-listen sample over a live MLX child; the type-refusal test asserting vendor CLIs refused and `mlx` served.
- On real weights: an image question answered natively by an MLX vision build from [the model families](../../assistant/DEVELOPER.md#on-real-weights-the-model-families) where one exists, and a `train` run promoted with `--target mlx` then chatted with — both recorded on ship.

**Acceptance criteria:**
- [ ] `apogee chat -m <mlx vision entry> --image photo.jpg` answers about the image natively; a non-vision MLX entry routes the image through the 26e helper path instead, with the honest notice.
- [ ] A stock OpenAI client against `apogee serve` streams from an `mlx` backend; vendor-CLI types remain refused by type.
- [ ] `apogee train promote --target mlx` registers the tuned model as a runnable `mlx` entry through the one config editor, versioned in the ledger, with rollback intact — and no GGUF conversion ran.
- [ ] `check` reports the vision dependency honestly on installs with and without it.

**Scope note.** Item **31c**, earmarked for **v0.1.8**; build after [31a](mlx-backend-core.md) and [31b](mlx-model-operations.md). Out of scope: MLX embeddings; audio/video native paths; distillation changes beyond the promote target; any second serve plane.
