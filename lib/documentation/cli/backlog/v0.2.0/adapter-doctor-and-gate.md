# The doctor's adapter rows, and the gate on the base that actually runs

**What / why.** Two honesty gaps the adapter path opens (the LoRA spike, 2026-10-21). **The doctor**: `check` has no adapter row — a deleted adapter file or a base whose weights changed under a registered adapter would surface as a confusing load failure instead of a named fact with its fix. **The gate**: adapters are trained against full-precision weights, and running one on a quantized base costs some unmeasured quality — while the eval gate today scores the candidate where the trainer ran it, not where the promoted entry will actually serve. This item closes both: `check` gains per-adapter rows (the file present; the base's weight hash matching the recorded pin; the scale sane — each FAIL with a runnable remedy, M14's bar), and **the eval gate runs against the base that will actually serve**: when the promote's target entry runs a quantized base, the gate's scoring inference runs the adapter on that quantized base — so the Q4-quality cost is measured where it bites, and a candidate that degrades below the gate's floor on the real runtime never promotes (the promote law's spirit extended to precision).

**Core constraint(s).** **The doctor reports, never edits** (the standing `check` law); every row's hint is a runnable command; a check that cannot run reports skipped, never passing. **The gate's never-fail contract stands** (`run_eval`): a judge failure degrades honestly, and the quantized-base run is the same closure shape — the composition root hands the right inference, `training/` never names a backend. **No double gating**: one eval per candidate, on the serving base; the full-precision score is not also computed unless asked (spend honesty).

**Seam + files.**
- `presentation/cli/check.cpp` (the adapter rows over the entry's `adapters:` and the recorded base hashes).
- `business/training/` (the gate's inference closure fed by the promote target's runtime facts; `promote.cpp` passing them), `presentation/cli/train.cpp` (the composition root wiring the quantized-base closure).
- Tests: the doctor matrix (present/missing/hash-mismatch/skipped, remedies runnable against the fixture state); the gate over scripted closures (a quantized-target candidate scored on the quantized closure, pinned; the floor refusing a degraded candidate with the promote stopped and said).

**Reference.** The LoRA spike (2026-10-21) — the Q4 risk named and deliberately made a gate fact rather than an assumption; M14 — runnable doctor remedies; Milestone Z — `run_eval`'s never-fail contract and the closure shape consumed whole.

**Decisions made** (dated):
- 2026-10-21 — The gate evals on the serving base (the spike's recommendation, standing): quality on quantization is measured per candidate, never assumed either way.

**Open calls:**
- [default: the doctor rows' wording and the gate's report line ("scored on <base> (Q4_K_M)") at build.]

**Guardrail(s).** The doctor matrix with skipped-never-pass; the gate's closure pins and the floor refusal; hermetic throughout — the real adapter-on-Q4 eval numbers are the user's run, recorded.

**Acceptance criteria:**
- [ ] `check` names every registered adapter's state with runnable remedies; a hash mismatch is a FAIL naming the base and the re-promote door.
- [ ] A promote toward a quantized entry gates its candidate on the quantized base, the report saying so; a candidate under the floor never registers.
- [ ] The full suite is green over fixtures and closures.

**Scope note.** Earmarked for v0.2.0 (track 53); gated on 53b, shipped 2026-10-21 ([Milestone AP](../../assistant/MILESTONES.md#milestone-ap--runtime-adapters)) (53b — the ledger and target facts it reads). Out of scope: new eval kits, the runtimes (53a), sharing (53d).
