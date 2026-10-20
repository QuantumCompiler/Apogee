# Downloads and supervised jobs

**What / why.** The design's download flow (`ScreenDownload`, `ScreenModelsDownloads`): a pull with its steps, stop, and run-the-stopped-step-again. The contract board's map: the chain itself is ready (`models pull … --safetensors --register-with …`), stopping is interrupting the child and resuming is running what is left (`models convert`, `models quantize` — app-side, ready); **progress is stderr text until the CLI's 52b lands** (G9: `--output-format stream-json`), so the board's fallback is a busy bar with the step parsed from stderr — upgraded to real step/bytes rows on landing, gated on nothing. This item builds the supervised-job machinery the design's long runs share (a Business `jobs` package: a child run with captured output, stop, outcome; one place video/compose/voice reuse) and the downloads UI over it: start a pull from a reference, show steps (fallback parse → 52b events), stop, resume the remaining step, and land the result in the library via read-back.

**Core constraint(s).** ADR 0001/0002; one supervised-job core for every long child (no per-screen process code); the stderr parse is **visual only** — nothing decides on parsed text, and an unparsed line degrades to "working" honestly; stop is interrupt, outcome read from the exit and the store (never assumed). Mutations are the person's commands; read-back after.

**Seam + files.** `source/business/jobs/` (the supervised child over `infrastructure/process`: lifecycle, output tail, stop), `source/presentation/models/` grown (the downloads screens), later consumers noted (1m/1n/1o). Tests: the jobs core over scripted children (steps, stop, failure tails); the downloads view-model over scripted runs; the fallback parser's degrade path; hermetic.

**Reference.** `ScreenDownload.dc.html`, `ScreenModelsDownloads.dc.html`; the contract board's MODELS rows (G9's fallback); [52b](../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths) (shipped 2026-10-20) cross-referenced, not gated; M3's chain — the steps and resume commands the UI names.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call); one jobs core for every long run (downloads first, media next).

**Open calls:**
- [default: the fallback parser recognizes the busy line's step names only; anything else is "working" — details at build.]

**Guardrail(s).** Jobs-core matrix over scripted children; downloads view-model matrix; degrade-path pin; no network, no binary in tests.

**Acceptance criteria:**
- [ ] A pull runs supervised with steps shown (fallback today, 52b events when present), stops cleanly, resumes its remaining step, and the library shows the result by read-back.
- [ ] The jobs core is the one supervisor (the layering check sees no process code in Presentation); the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on [`models-library.md`](models-library.md) (1h). Out of scope: media runs' screens (they consume the jobs core), the CLI's stream-json itself.
