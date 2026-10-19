# Assembly on the seam: scenes into one video

**What / why.** The ffmpeg seam can probe media, extract frames, decode audio and assemble one frame sequence (26e; 49b) — and nothing more, measured 2026-10-19 (the composer spike): no way to **concatenate clips with transitions**, **mix a narration track over a music bed**, or **burn captions**, which is everything that turns a folder of scene clips into one watchable video. This item grows the seam with the three assembly operations as **pure builders over captured children**: `concat_clips` (an ordered clip list, a crossfade duration — 0 a hard cut — one output, one encode), `mix_audio` (the video's track, a narration track laid per-scene offsets, an optional music bed ducked under the narration), and `burn_captions` (an SRT onto the frames). Beside them, one pure text helper: `write_srt` (timed cues from text + offsets — the composer feeds it script lines and the voice runtime's measured durations). Each operation is a typed-argument filtergraph/argv builder tested **argv-exact** against fakes plus tiny real fixtures, every run a bounded, captured, cancellable child of the one seam. Useful alone (any clips, any SRT), consumed whole by [`video-compose.md`](video-compose.md).

**Core constraint(s).** **One spawn path**: every operation runs through `platform/ffmpeg`'s existing child discipline (timeout, output cap, the `stopped` hook, captured stderr folded into failures) — never a second runner. **Builders are pure**: typed inputs to argv/filtergraph strings, unit-tested without any process; the child runs only in the thin executor. **One encode, said**: concatenation with transitions re-encodes once at the end (the honest cost of crossfades), the operation's result reporting the encode settings; a transitionless concat of same-codec clips may stream-copy, decided by probe facts, never by hope. **Durations and offsets come from `probe_media`** — the seam's own facts, never arithmetic on assumptions. Subtitles are UTF-8 through the house door; caption text is data. Paths in, paths out; nothing here knows scenes or projects (the composer's vocabulary stays above this item).

**Seam + files.**
- `infrastructure/platform/ffmpeg.h/.cpp` — the three operations and their builders (`concat_args`, `mix_args`, `burn_args` pure; `concat_clips`, `mix_audio`, `burn_captions` the executors over the existing child runner), plus `write_srt` (pure text, likely `contracts/`-adjacent — the build's call within the layering).
- Tests: `tests/infrastructure/platform/ffmpeg_assembly_test.cpp` — the builders' argv-exact tables (crossfade math per clip boundary, ducking filter shape, SRT escaping); executor runs over a fake ffmpeg (argv pinned, timeout, cancel, failure tails); a small real-fixture pass (two tiny clips concat-ed, probed for duration; gated on a real `ffmpeg` present, skipped-and-said otherwise — the suite's existing external-binary discipline).

**Reference.** 26e — the seam, its bounded-child rules and the fake-ffmpeg test idiom (extended, not duplicated); 49b — the single-sequence assembly this generalizes; the composer spike (2026-10-19) for the seam's measured op inventory.

**Decisions made** (dated):
- 2026-10-19 — The set lands in v0.1.9 as track 50 (the user's placement call on the spike report).
- 2026-10-19 — Assembly is seam-level and scene-ignorant: clips, tracks and cues in; one video out; the project vocabulary stays the composer's.
- 2026-10-19 — Captions burn into the frames for the MVP (one artifact, plays anywhere); soft subtitle tracks are a later option, recorded.
- 2026-10-19 — **Crossfade and ducking**: the crossfade duration is a named constant with 0 allowed (hard cut); the ducking filter is one fixed simple shape — music lowered under narration — tunable later (the default, confirmed at grooming).
- 2026-10-19 — **Encode profile**: the final render's settings are one named profile (codec, bitrate class), tuned at build against the reference pipeline's output (the default, confirmed at grooming).

**Open calls:** none — groomed 2026-10-19.

**Guardrail(s).** The argv-exact builder tables (every boundary: one clip, two, N; zero and nonzero crossfade; bed present and absent; SRT escaping incl. UTF-8); fake-ffmpeg executor matrix (pin, timeout, cancel, tail); the real-fixture concat probe-verified where ffmpeg exists, skipped-and-said otherwise; no seam behavior outside the one child runner.

**Acceptance criteria:**
- [ ] The three operations assemble fixture clips, tracks and captions into one playable output in the real-fixture pass; every builder's argv is pinned.
- [ ] Cancellation, timeouts and failure tails behave as the seam's existing operations do; stream-copy happens only when probe facts allow.
- [ ] `write_srt` produces valid cues from text + measured durations; the full suite is green.

**Scope note.** Earmarked for v0.1.9; ungated — independently buildable and useful alone. Out of scope: scene/project knowledge (the composer's), soft subtitle tracks, per-clip filters beyond the crossfade, music generation.
