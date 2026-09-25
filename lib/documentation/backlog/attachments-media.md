# Automatic attachments: images, audio and video

**What / why.** The same `/attach` that takes a document ([document attachments](attachments-documents.md)) takes a picture, a recording or a video. Apogee gives each to whichever configured model can read it:
- **Natively**, when the chat model can: an image to a model with a vision projector, audio to one with an audio projector, and a short video to a video-capable one such as Qwen3-VL through mtmd.
- **Through a helper**, when it cannot: the [`vision` or `transcription` role](helper-model-roles.md) turns it into text.

That text is indexed with the chat's documents, so every later question can retrieve it. A 12-minute screen recording becomes a timeline of what was on screen and what was said, searchable by moment ("what did they type at 4:30?"), readable by a text-only 8B model.

Today:
- **Images:** `--image` works on the first message only, for vision models only.
- **Video:** Apogee refuses it, although its llama.cpp build has video support (`MTMD_VIDEO` on, through the `ffmpeg` program) and `ffmpeg` is installed on the reference machine.
- **Audio:** no path exists at all.

**How it works.**
- **Images.** Natively on the turn they are attached, when the chat model accepts images. Every image also gets a description, including any text in it, from the `vision` role, or from the chat model itself when no helper is set. On later turns the conversation carries that description instead of the pixels. Today every turn after an image re-runs the multimodal path: a fresh context that encodes the image and re-reads the whole conversation again (`run_multimodal`).
- **Audio.** Decoded by `ffmpeg` to mono PCM at the model's rate (`mtmd_get_audio_sample_rate`), then either given natively to an audio-capable chat model, or transcribed in 30-second windows by the `transcription` role into timestamped text.
- **Video.**
  - Clips up to a minute go natively to a video-capable chat model.
  - Anything longer, or any video on a model without video, becomes a timeline:
    - frames sampled by `ffmpeg` (one every five seconds, plus scene changes), each described by the `vision` role with its timestamp;
    - the audio track transcribed as above;
    - both merged in time order into text the chat's index holds.

**Core constraint(s).**
- **Capabilities are asked, never cast.** `accepts_images` exists; `accepts_audio` comes with [helper roles](helper-model-roles.md); `accepts_video` (from `mtmd_helper_support_video`) is added the same way.
- **A helper is used automatically** (the user's call), and the status line says which model is reading what, and for how long.
- **A child's stderr is captured, never inherited.** Apogee runs `ffmpeg` itself through `platform::ChildProcess` with a bounded stderr tail, and feeds mtmd frames and samples as bitmaps. mtmd's own video helper spawns `ffmpeg` from llama.cpp code Apogee does not control, so it is used only if a PTY check proves it keeps `ffmpeg`'s output off the terminal.
- **External converters, optional:** `ffmpeg` on `PATH` (the user's call). Without it, audio and video are refused by name and `check` says what to install. Images need nothing external.
- **Transient and private as in [document attachments](attachments-documents.md):** descriptions, transcripts and timelines live in the chat's private index, and the session records the attachment by reference.

**Seam + files.**
- `agentloop/attachments.*` (from 26d): the media branch that picks native versus helper, and the timeline builder.
- `backends/llamacpp.cpp`, `backends/llama_real.cpp`:
  - audio bitmaps (`mtmd_bitmap_init_from_audio`);
  - video frames as consecutive image bitmaps, marked mergeable for models that merge them;
  - `accepts_audio` and `accepts_video`.
- `harness/provider.h`: the `VideoCapable` probe; `harness/harness.h`: `can_read(model, medium)` as one typed question.
- `platform/`: an `ffmpeg` runner that extracts frames at timestamps and decodes PCM, with a timeout and a size cap.
- `commands/helpers.cpp`: `attachment_refusal` extended to name the helper role that would read the medium.
- `commands/check.cpp`: `ffmpeg` present or not, and which roles can read which media.

**Reference (Ommi).** Ommi's vision path spawned `ommi-mtmd-cli` per image turn and flattened the conversation into that turn's prompt (CHAT.md), which is the cost this item removes. It had no audio or video.

**Decisions made:**
- 2026-09-25 — Asked for by the user ("same sort of thing with other types of media, like images / video").
- 2026-09-25 — **Helper models automatically** when the chat model cannot read a medium, and **external converters** (`ffmpeg`), both the user's calls.
- 2026-09-25 — An image turns into a description after its first turn, so a vision chat stops re-reading every image on every turn.

**Open calls:**
- [default: native video up to 60 s; longer videos become timelines] A minute of frames at mtmd's default rate is already thousands of tokens for an 8B model.
- [default: a frame every 5 s plus scene changes, capped at 240 frames] Enough for a screen recording; the cap bounds the vision helper's time. The status line reports progress.
- [default: `/attach` on an already-attached image looks at its pixels again] The escape hatch for a follow-up question the description missed.
- [default: the chat model describes its own images when no `vision` role is set] One model's view is better than none, and it is already loaded.

**Guardrail(s).**
- Native versus helper chosen by capability and table-tested with scripted providers.
- The description replaces pixels after the first turn; a re-attach looks again.
- `ffmpeg` is absent → refused with its name; a PTY check shows no `ffmpeg` output on the terminal.
- A timeline orders frames and transcript by time and is retrievable by timestamp.
- The audio sample rate is the model's own.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] A text-only 8B chat model answers a question about a screenshot's contents through the `vision` helper, automatically.
- [ ] A 10-minute screen recording attached to a chat on Qwen3.8-27B is turned into a timeline in the background, and "what was on screen when they mentioned the deadline?" is answered with a timestamp.
- [ ] A short clip is read natively by Qwen3-VL-8B.
- [ ] An audio note is transcribed and questions about it are answered from the transcript.
- [ ] On a vision chat model, the second turn after an image does not re-encode the image (visible in `--verbose` timings).

**Scope note.** Item **26e**; build after [26b](helper-model-roles.md) and [26d](attachments-documents.md). Out of scope: live capture (microphone, camera, screen), generating audio or images, and uploads over `serve`.
