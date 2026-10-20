# Voice: speeches, the say flow, the timings

**What / why.** The design's Voice screen (`ScreenVoice`): the speeches library, a say flow with a voice picker, playback with sentence timings, and the install consent. The contract board's map: **all ready** — `voice say … --output-format json` returns the `voice.speech` document with per-sentence timings (50a shipped), the voices for the picker come from `models info kokoro-82m --output-format json` (the model's own set), the library is the records in `~/Music/Apogee` and added folders (app-side, the design's decision 5), the install prompt is the app's (`train setup --with voice` on consent), and live progress is stderr until 52b (G11's fallback — speech runs are short, so the fallback is mild here). This item builds it: the library over the records scan (1m's machinery pointed at Music), the say sheet (text, voice from the picker, output), runs as supervised jobs, playback with the timings highlighted sentence by sentence.

**Core constraint(s).** ADR 0001/0002; the library is the folders' records; the picker's voices are the model's honest set from the read (no hardcoded list); every run the person's command, argv-pinned; timings rendered from the document, never re-measured.

**Seam + files.** `source/presentation/voice/` (library, say sheet, player; view-models), `source/business/media/` (the Music scan, say-run assembly). Tests: the scan over fixture speech records; argv pins; the player's timing cursor over a fixture document (scripted clock); consent; hermetic.

**Reference.** `ScreenVoice.dc.html`, `ComponentsMedia.dc.html`; the contract board's rows; 50a `(consumed decision)` — the speech document and voices; [52b](../../cli/backlog/v0.2.0/jobs-stream-json.md) cross-referenced, not gated.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call).

**Open calls:**
- [default: player affordances (scrub, sentence click-to-seek) per the prototype; details at build.]

**Guardrail(s).** Scan and argv pins; timing-cursor matrix on a scripted clock; hermetic — real synthesis is the user's run.

**Acceptance criteria:**
- [ ] The library lists fixture speeches; the sheet speaks through the real command with the model's own voices; playback highlights sentences by the document's timings.
- [ ] Consent gates the voice set's install; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on [`video-clips.md`](video-clips.md) (1m — the shared media machinery). Out of scope: voice cloning (the design's recorded out), any CLI change.
