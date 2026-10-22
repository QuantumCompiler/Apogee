# Audio into MLX omni models

**What / why.** The user's Qwen3-Omni-30B and the five Gemma4 releases carry audio towers (`audio_config` / `thinker_config` in their configs, read in the mirror of their store, 2026-10-21). Once 53f converts them through mlx-vlm with their towers kept, an MLX backend over one could hear. Apogee never hands it sound, though:
- the contract has the capability (`AudioCapable`, `source/data/contracts/provider.h:248`, 26e), and the MLX backend does not implement it;
- the driver passes no audio (`assets/mlx/mlx_generate.py` has no audio path, by grep);
- mlx-vlm takes audio for omni models (`--audio <wav>` on its CLI, `audio=[...]` to `generate()`, its README).

Today an attached recording reaches such a model only as the transcription role's text (26e's fallback). Wall **W3**. Measured, not guessed (2026-10-21, the MLX-for-every-kind spike): the source read, mlx-vlm's README read.

**Core constraint(s).**
- **Capability probes never leak a cast** (CLAUDE.md → ⚠): the MLX provider implements `AudioCapable`, and the Harness discovers it.
- **Audio only where the directory can hear:** a `vision`-kind directory (53e) whose config declares an audio tower, with mlx-vlm installed. Anything else refuses before a spawn, as the vision ladder does (27c).
- **26e's media rules hold:** the audio is read natively on the turn it is attached, and as text for every turn after. `ffmpeg` is still the one decoder (`platform/ffmpeg`).

**Seam + files.**
- **`source/data/backends/mlx_local.h/.cpp`:** `AudioCapable` and its readiness rung, `probe_mlx_audio`.
- **`assets/mlx/mlx_generate.py`:** the vision driver passes `audio=` with the decoded WAV paths.
- **`source/data/backends/mlx_protocol.*`:** the turn's audio parts on the wire.
- **Tests:** the stub `mlx_vlm` records the audio it was handed. A turn with a recording on an omni directory reaches it; on a text-only directory it refuses before a spawn.

**Reference.** 26e (media: the native read on the attaching turn, text after). 27c (the vision ladder, its refusals by rung). [mlx-vlm's README](https://github.com/Blaizzy/mlx-vlm) (omni models, `audio=`).

**Decisions made** (dated):
- 2026-10-21 — **Split from 53f**, so vision lands without waiting on audio. The audio path is smaller, but its own question.

**Open calls:**
- [default: audio is offered to an MLX directory only when its config declares an audio tower and the installed mlx-vlm has its type's module; otherwise 26e's transcription fallback, as today.]

**Guardrail(s).** The 26e media tests' audio fixture through the stub driver. An omni directory gets the audio natively on the attaching turn and as text after. A text-only directory refuses audio with the rung said, and nothing spawns.

**Acceptance criteria:**
- [ ] An attached recording reaches an MLX omni backend as audio on its turn, and its answer reflects it (the user's real run).
- [ ] A backend that cannot hear says so before anything starts, and the transcription fallback stands.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** v0.2.0. Its gate, [53f](../../assistant/MILESTONES.md#milestone-aq--mlx-for-every-kind) (mlx-vlm's route, a `vision` directory run through mlx-vlm for every turn), shipped 2026-10-21. Speech *out* of an omni model (Qwen3-Omni's talker) is not in scope: it is a speech engine's work, nearer [53m](mlx-voice.md), and unasked.
