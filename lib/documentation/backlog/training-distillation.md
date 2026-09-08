# Training and distillation stack (placeholder — Python boundary, unscheduled)

**What / why.** Coverage placeholder for Ommi's fine-tuning subsystem, and the project's stated 'parts that plausibly stay Python' case: datasets prepare/create/synth over local files and chat logs, training kits (YAML: teacher synth spec + inline eval suite), LoRA/QLoRA runs via Python trainer subprocesses (MLX on Apple Silicon, PEFT on CUDA) speaking a JSONL progress protocol to the C++ orchestrator, eval gates (substring + LLM judge), promote/rollback with versioned GGUF retention and a version ledger, multi-stage pipelines with cumulative gates and fused-checkpoint chaining, teacher→student regimes, and the scheduler-invoked continuous cycle with the anchor-baseline dual gate and circuit breaker. The orchestration contracts port to C++; training execution does not. Teachers and judges become direct cloud API calls with real rate-limit handling — a genuine upgrade over claude-CLI throughput. HONEST SCOPE: unscheduled outer ring, but a CONFIRMED direction (2026-08-24, user decision): Apogee will tune local models on their full-weight SafeTensors files — timing, not existence, is the open question. When scheduled it likely splits into datasets/synth, train/eval/promote, and pipeline/regime/cycle items. SafeTensors snapshots and datasets were to arrive through the Hugging Face source in the models area, which shipped 2026-09-07 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone N) — but **GGUF only**: a SafeTensors ref is refused with the `convert_hf_to_gguf.py` invocation named, and dataset downloads are unimplemented. Apogee deliberately does not run that converter (it needs Python with torch and transformers, which a C++ harness cannot assume), so this item owns whatever it needs on that side.

**Core constraint(s).**
- Training control (start/promote/rollback/cycle/regime) is CLI-only forever — HTTP exposes reads only, EXCEPT datasets synth, which is teacher inference, not training: Ommi deliberately exposed it as an async admin route, and the split must preserve that distinction or record dropping it as a decision (Ommi's parity carve-out, enforced via the parity test's documented-skip list)
- Only full-precision (SafeTensors) backends are trainable; GGUF is an inference-only promotion artifact
- Failing candidates never reach inference; log sources gated on explicit log_consent
- Python env guards (platform hard-exits, env vars set before ML imports) carried verbatim if the same drivers ship

**Seam + files.** lib/src/cli/source/training/ (orchestrator: subprocess driving, JSONL progress parsing, manifests, version ledger, pipeline/cycle state machines), lib/src/cli/source/commands/train.cpp + datasets.cpp, tools/scripts/train_mlx.py + train_peft.py (Python, shipped assets with env guards), training-kit/ YAML assets re-authored for Apogee's local tool-call convention, read-only /v1/training/* routes on serve.

**Reference (Ommi).** src/training (Synthesize, RunPipeline, TrainingStore, cycle.go), cmd/ommi train.go/datasets.go/synth.go/train_regime.go, tools/scripts train_mlx.py/train_peft.py, training-kit/ (Milestone K). Divergences: teachers/judges are direct cloud API calls; everything else keeps the same C++-orchestrator-over-Python-subprocess shape Ommi already proved; the tool-use/web-search kits must be re-authored for Apogee's tool-call convention.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: The most self-contained subsystem, schedulable last (Ommi's own notes say so); gated on the local-model item because promotion writes versioned GGUFs and role pointers.
- 2026-08-24 — Training confirmed as a committed direction (user decision): full-weight SafeTensors tuning of local models ships eventually; only the release that schedules it is open.

**Open calls:**
- [user] Which release schedules it (training itself is confirmed — see Decisions)
- [default: a dedicated venv under the data dir, created on first `train` use — never the system Python] Python environment management for the trainer scripts
- [default: small Python helper first — Arrow C++ only if datasets prepare goes native] Parquet parsing for datasets prepare
- [default: done during the split grooming] Re-authoring the tool-use/web-search kits for Apogee's local tool-call convention

**Guardrail(s).** When scheduled, the JSONL progress protocol between C++ and Python gets a contract test; the CLI-only-mutation rule is enforced by the admin parity test's documented-skip list; the split docs each carry the gate/consent invariants above.

**Acceptance criteria:**
- [ ] Placeholder-level: split into build-sized items with their own docs before implementation
- [ ] Contract-level the split must preserve: eval-gated promotion with rollback and versioned GGUF retention; cumulative eval suites across pipeline stages; anchor + last-passing dual gate with circuit breaker on the cycle; chat-log training sources require explicit log_consent; the C++↔Python JSONL progress protocol is a tested contract

**Scope note.** unscheduled. The models area it would have gated on shipped 2026-09-07 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone N); what this item still needs from it — SafeTensors snapshots and dataset downloads — was deliberately left unbuilt there and is this item's to carry.
