# Video clips: the library and the generate flow

**What / why.** The design's Video screens (`ScreenVideoClip`, `ScreenVideoRun`, `ScreenVideoConsent`, `ComponentsMedia`): a clips library, a generate flow with run-again/extend/draft-final/LoRAs, live run progress, stop, and the one-time install consent. The contract board's map: generation and iteration are **ready** (`video generate` with `--from`, `--extend`, `--draft`, `--final`, `--lora`, `--no-lora` — all shipped through 49c–49e), a clip's record and history are ready (the `video.generation` sidecar beside each clip), **the library is app-side** — the records found in `~/Movies/Apogee` and folders the person adds (the design's decision 5: nothing written over, a taken name gets a number), stopping is interrupting the child (exit 130, output untouched, no record — ready), the **install prompt is the app's** (a child on a pipe never asks: the app asks, then runs `train setup --with video`), and **live steps are stderr text until 52b lands** (G11's fallback). This item builds it: the library as a folder-watch over the records, the clip page (player, the record's settings, its parent chain), the generate/iterate sheet assembling the real flags, runs as supervised jobs, and the consent flow.

**Core constraint(s).** ADR 0001/0002; the library is **records, not a database** — what the folders hold is the truth, re-read on focus and after runs; every run is the command a person would type (argv pinned in tests); the consent flow runs `train setup` only on an explicit yes, the refusal ladder's words shown as the CLI says them; media output paths honor the design's decision 5.

**Seam + files.** `source/presentation/video/` (library, clip page, the sheets; view-models), `source/business/media/` (the records scan over the folders, the run assembly over the jobs core), `source/data/preferences/` (added folders). Tests: the records scan over fixture folders (sidecars parsed, chains linked); argv pins for every flag combination the sheet makes; the consent flow over a scripted refusal; run lifecycle over scripted children; hermetic.

**Reference.** The `ScreenVideo*` boards and `ComponentsMedia.dc.html`; the contract board's VIDEO AND VOICE rows (G11's fallback); the CLI's 49c–49e records and flags `(consumed decision)`; [52b](../../cli/backlog/v0.2.0/jobs-stream-json.md) cross-referenced, not gated.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call); the library is the folders' records (the design's decision 5, standing).

**Open calls:**
- [default: the records scan's shape (shallow walk for `*.json` sidecars naming clips) and the watch cadence (on focus + after runs, no FSEvents yet) at build.]

**Guardrail(s).** Fixture-folder scan matrix (chains, numbering, missing sidecars skipped-and-said); argv pins; consent and refusal renders; hermetic — no real weights, no real runs (the live pass is the user's, per the standing rules).

**Acceptance criteria:**
- [ ] The library lists fixture clips with their records and chains; the sheet assembles every documented flag combination argv-exact; runs supervise with stop; consent asks before `train setup`.
- [ ] Progress renders the stderr fallback today and 52b's events when present; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on [`downloads-jobs.md`](downloads-jobs.md) (1i — the jobs core it consumes). Out of scope: compose ([`compose-screens.md`](compose-screens.md)), voice (1o), any CLI change.
