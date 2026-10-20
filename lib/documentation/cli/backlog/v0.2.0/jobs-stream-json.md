# Long jobs speak JSON: pull and the media runs as event streams

**What / why.** A GUI supervising a long run today must parse stderr written for people — the design's contract board marks both halves **before the app's first release**: **(G9)** `models pull` narrates its chain (download, convert, quantize, register) as busy-line text, so the app's download rows with steps, bytes done and totals have no machine source; **(G11)** `video generate`, `video compose` and `voice say` narrate denoise steps, frames, assembly and scenes the same way, while their drivers already speak structured JSONL internally (49b's protocol: per-step progress events the CLI consumes and re-words). This item gives each verb **`--output-format stream-json`**: one JSON object per line on stdout — for a pull, the chain's steps with per-file bytes done and total, each step's outcome, then the stored entry's document; for the media verbs, `ready` (device, dtype), `step` n of N, `frames`, `assembling`, the composer's per-scene events, then the run's existing document (the 49c sidecar, the 50c project) as the final line. The growth is a **relay, not a second narrator**: the driver events and chain steps that exist today are framed as machine events instead of prose, by the same one producer that feeds the busy line.

**Core constraint(s).** **One narration source**: the events are produced where the progress already exists (the chain's steps, the driver's JSONL, the progress seam) — never a parallel accounting that can drift from what the busy line says. **Additive** (ADR 0006): the default output is byte-identical to today; the flag is opt-in; the final line is the exact document the verb already prints under `--output-format json` where one exists. **Honesty rules carry over**: no invented totals — a step whose size is unknown says so (the store's honesty laws); cancellation mid-stream still exits 130 with the output untouched and the stream ending cleanly. Machine-mode.md gains the job-stream section and the schema the event shapes (`cli.machine_schema_conformance`); the flag completes (ADR 0007); the verbs' ADR 0010 classification is unchanged (a flag on existing verbs); the TUI's progress seam is untouched — it already has its own channel.

**Seam + files.** `presentation/machine/` (the job events declared in `protocol`, framed by the one writer), `presentation/cli/models_pull.cpp` (the chain's steps through the new relay), `business/video/` + the voice path (the driver events relayed instead of re-worded when the flag is on), `machine-mode.md`. Tests: scripted chains and fake drivers → event-sequence goldens; the default-output byte-identity pin; cancellation mid-stream; the final-line document equality with the `--output-format json` twin.

**Reference.** The contract board's G9/G11 rows and fallbacks ("a busy bar with the step parsed from stderr"); 49b's driver protocol — the events that already exist; M3's chain — the steps a pull reports; the progress seam (37e) — the house's one-producer law this honors.

**Decisions made** (dated):
- 2026-10-20 — Specced from the GUI design spike, the user's call; before-v1 per the design's recommendation.
- 2026-10-20 — A relay of existing progress, never a second accounting; the final line is the verb's own document.

- 2026-10-20 — **Event names and shapes** (`step`, `bytes`, `scene`, …) finalized at build in the machine protocol's declared-as-data style, one family across the four verbs (the default, confirmed at grooming).

**Open calls:** none — groomed 2026-10-20.

**Guardrail(s).** Event goldens over scripted chains/fake drivers; default-output byte-identity; the document-equality pin; schema conformance; cancellation; no network, no weights.

**Acceptance criteria:**
- [ ] `models pull --output-format stream-json` over a scripted chain emits the steps with bytes and the stored entry; the three media verbs emit ready/step/frames/assembling/scene events then their document, over fake drivers.
- [ ] Without the flag every verb's output is byte-identical to today; machine-mode.md and the schema agree; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 52); ungated. Out of scope: TUI/terminal narration changes, new progress facts the runs don't already know, `train` runs (their JSONL is already a protocol; a later item if the app wants it relayed).
