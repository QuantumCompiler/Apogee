# `apogee video generate`: the clip, honestly priced

**What / why.** The store can hold a pipeline and the runtime can run one ([`video-store.md`](video-store.md), [`video-runtime.md`](video-runtime.md)); this item is the door: **`apogee video generate "<prompt>" [--image <png>] [--model <name>] [--duration <s>] [--resolution <WxH>] -o out.mp4`** — the pipeline resolved from `--model` or the config's `models.default_video` pointer, the run narrated step by step through the progress seam (37e's widget in the shell, the status line at the terminal), cancellable, its record a JSON document (`--output-format json`: the model, the settings, the timings, the output path) whether watched or piped. `--image` ships in the first cut because it is the realism workflow the research settled on (a photoreal still animated beats text-to-video for realism). Caps are named constants quoted in refusals — duration, resolution, steps — and **speed honesty is part of the surface**: the first progress line says the device and the expected scale (minutes per clip on Apple silicon), so nobody watches a silent spinner wondering. Closes the spike's W4.

**Core constraint(s).** **No seventh role**: video generation lives outside the role resolver — a `models.default_video` config pointer (written through the one editor) plus `--model`, nothing in `harness/roles.h` (the standing default). The refusal ladder is the runtime's, surfaced verbatim (`(consumed decision)` — [`video-runtime.md`](video-runtime.md) owns it); a missing pipeline names `models pull`. **Every mode** per the house: the JSON document documented in machine-mode.md (28h), the subcommand classified in the shell (ADR 0010 — the exec line runs it; a curated view is not earned yet, said in the row), completion from live state (ADR 0007 — `--model` completes the store's pipelines). Generation is **local compute the user asked for** — no gate prompt (it writes only the named output and its throwaway frames), but the document and the narration always say the spend in time. Output honesty: the mp4 lands only on success; a cancelled or failed run leaves the output path untouched and says so.

**Seam + files.**
- `presentation/cli/video_cmd.h/.cpp` — the `video` group and `generate` verb: flags, the pointer/`--model` resolution, the runtime invocation, the progress narration (terminal + 37e), the document; registered with the root and the parity table.
- `data/contracts/` — the `models.default_video` pointer (the editor transform + `config set-default-video`, the role-pointer verbs' shape without the resolver rung).
- `lib/documentation/reference/machine-mode.md` — the document's row.
- Tests: the command over the **fake driver** end to end (flags to argv contract, progress narrated, cancellation, the document conformant, the output-path honesty); the pointer's editor goldens; completion of pipelines from a fixture store; the classification row; refusal parity with the runtime's ladder.

**Reference.** [`video-store.md`](video-store.md) and [`video-runtime.md`](video-runtime.md) — consumed whole; the role-pointer verbs (`config set-default-*`) — the pointer's shape, minus the resolver; 37e — the narration; 28h — the document; the video-generation spike (2026-10-18).

**Decisions made** (dated):
- 2026-10-18 — The set lands in v0.1.9 as track 49 (the user's placement call); the SPEC amendment landed first (dated, the user's yes).
- 2026-10-18 — No new role: `models.default_video` + `--model`, outside the six-role resolver (the standing default).
- 2026-10-18 — `--image` (image-to-video) ships in the first cut — the realism workflow (the spike's research, standing).
- 2026-10-18 — The recorded later items: a `generate_video` session tool once this verb exists, audio tracks, video training — each a deliberate pickup, named here.

**Open calls:**
- [default: the caps — duration, resolution, steps — are named constants with conservative first values, quoted in refusals, tuned at build against the reference pipeline's limits.]
- [default: a progress line's wording follows the download-progress idiom (step N of M, elapsed, device), exact text at build.]

**Guardrail(s).** The fake-driver end-to-end matrix (every flag on the argv contract, narration, cancel, document, output honesty); pointer goldens; completion and classification; refusals byte-equal to the runtime's ladder; machine-document conformance. The real-weights pass — one genuine generation on the reference pipeline — is the user's run, its result recorded in the milestone per the standing rules.

**Acceptance criteria:**
- [ ] `video generate` produces an mp4 through the fake driver's protocol in tests, narrated and cancellable, the document conformant either way.
- [ ] `--model` and `models.default_video` resolve pipelines with completion from the store; every refusal names its fix.
- [ ] The caps refuse quoting their constants; a failed or cancelled run leaves the output path untouched and says so.
- [ ] Classified in the shell, documented in machine-mode.md; the full suite is green.

**Scope note.** Earmarked for v0.1.9; gated on [`video-store.md`](video-store.md) and [`video-runtime.md`](video-runtime.md) (49a, 49b). Out of scope: the session tool, audio, image generation (expected to ride this shape later, per the SPEC amendment — its own item when asked), any TUI view beyond the exec line.
