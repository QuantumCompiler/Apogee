# Voice on MLX

**What / why.** `voice say` has one engine: Kokoro on PyTorch (`assets/video/voice_say.py:147–148`, `import kokoro` / `from kokoro import KModel, KPipeline`, 50a), its set `torch`, `kokoro` and `misaki[en]`, bounded below Python 3.13 (`source/business/training/python_env.cpp`). A voice model stored by `models pull` (the `voice` format) cannot convert to MLX, and nothing in Apogee would speak with an MLX build if it could. Wall **W8**. Measured, not guessed (2026-10-21, the MLX-for-every-kind spike, mlx-audio's README read on GitHub):

- **mlx-audio speaks Kokoro on MLX:** "Kokoro … 54 voice presets", with quantized variants (8bit, 6bit, 4bit). It converts with `python -m mlx_audio.convert --hf-path … --mlx-path … [--quantize --q-bits N --q-mode affine|mxfp4|mxfp8|nvfp4] [--dtype …]`.
- **It is on PyPI** (unlike mlx-video), and its README names more speech engines (Qwen3-TTS, Dia, OuteTTS …) than this item takes on.
- **What `voice say` is around the engine** (50a, 50b): the record, the timings each segment measures from its own WAV header, the playback clock the GUI lights sentences by, and the install consent. None of that is PyTorch's. Only the driver is.

**Core constraint(s).**
- **One `voice say`, two engines,** as [53l](mlx-video-generate.md) does for video: the record, the timings and the refusals stay one implementation. The engine follows the voice model's directory (`voice` format on PyTorch, a `voice`-kind MLX directory, 53e, on MLX).
- **Opt-in, in Apogee's own environment:** an `mlx-audio` set, installed by `train setup --with mlx-audio` and never unasked. Its Python range is read and dated (M19's rule).
- **The interactive invariants:** the driver over the one script runner, its stderr a tail, its descriptor 1 on stderr before importing (`voice_driver_test`'s proof, mirrored).

**Seam + files.**
- **`source/business/training/python_env.cpp`:** the `mlx-audio` set, its range and its reviewed date.
- **`assets/mlx/mlx_convert.py`:** the `mlx_audio.convert` route for a `voice`-format set. **`source/data/modelstore/`:** a `voice` set's conversions gain `mlx` where mlx-audio is installed (M20's decision, one more answer).
- **`assets/video/voice_say_mlx.py`:** a new seeded driver over mlx-audio's Kokoro, speaking `voice_say.py`'s protocol (the `ready` event, the segments, their WAVs).
- **`source/business/video/voice.h/.cpp`:** the engine chosen by the directory's kind, its ladder, and the record's `engine`.
- **Tests:** a stub `mlx_audio` (a `convert` and a TTS writing a WAV of known length). The route, the engine by kind, the timings from the stub's WAV headers, the record's `engine`, and the stub's prints off the protocol.

**Reference.** 50a and 50b (the speech runtime, its timings, captions). [mlx-audio's README](https://github.com/Blaizzy/mlx-audio).

**Decisions made** (dated):
- 2026-10-21 — **Kokoro first, the one voice model Apogee stores today.** mlx-audio's other engines are later items, on the user's word.

**Open calls:**
- [default: the voices are those the converted directory carries (mlx-audio's Kokoro presets), listed by `models info` as today's voices are, and the default voice is today's default.]

**Guardrail(s).** 50a's voice fixtures through the stub mlx-audio: a speech's segments, their measured timings, the record with `engine: mlx`, and the PyTorch engine unchanged for a `voice`-format set.

**Acceptance criteria:**
- [ ] A stored Kokoro converts to MLX with mlx-audio installed, and `voice say` speaks with it on MLX, its timings measured as today. The real speech is the user's run.
- [ ] Without mlx-audio, everything reads as now, and `converts_to` has no `mlx` for a voice set.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** v0.2.0, gated on [53e](mlx-records-say-their-kind.md) (the `voice` kind). Other speech engines, and speech out of an omni model, are out of scope. The GUI's [3b](../../../gui/backlog/v0.2.0/mlx-engines-and-drafters.md) shows the engine.
