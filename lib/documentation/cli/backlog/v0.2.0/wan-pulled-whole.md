# Wan pulled whole, and said for what it is

**What / why.** The user's run, 2026-10-21, after M20 shipped:

`'Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83' is a diffusion model's weights (WanModel, a diffusers class): neither converter takes one -- llama.cpp's finds a model by its architectures, and mlx-lm's builds a language model`

The refusal is true, and it is not the whole story. The MLX-for-every-kind spike, the same day, found more. Measured, not guessed: the set's own record and config were read in the mirror of the user's store, mlx-video's Wan README was read on GitHub, and the source was read. Wall **W6**.

- **The set is Wan's original layout, and it is incomplete.** It was pulled from `Wan-AI/Wan2.2-TI2V-5B`, a repository in Wan's own layout, not the `-Diffusers` pipeline. Its `config.json` names `_class_name: WanModel` with `_diffusers_version: 0.33.0` (Wan's code saves through diffusers' config mixin) and no `architectures`. Its record (`apogee-snapshot.json`) lists 10 files:
  - `config.json` (251 B) and `configuration.json` (43 B);
  - the transformer's three shards (9,825,014,472 + 9,995,661,736 + 178,558,176 bytes) and their index (72,865 B);
  - `google/umt5-xxl`'s four tokenizer files.

  It lacks the two files every runtime needs beside the transformer: the T5 text encoder (`models_t5_umt5-xxl-enc-bf16.pth`) and the VAE (`Wan2.2_VAE.pth`). mlx-video's Wan README names exactly this layout, those `.pth` files included, as its converter's input.
- **The pull dropped them.** The SafeTensors pull keeps `.safetensors .json .model .txt .py .tiktoken .jinja` and nothing else (`source/business/models/source_hf.cpp:117–129`, `snapshot_wanted`). The voice pull, by contrast, takes a root `.pth` (`voice_wanted`, `:168–180`, 50a's precedent).
- **So nothing in Apogee can make video from it.**
  - `video generate` loads a diffusers pipeline (`assets/video/video_generate.py:185`) and refuses this set as "not a pipeline (no model_index.json)" (`source/presentation/cli/video_generate.cpp:407`).
  - An MLX engine (53k, 53l) needs the missing files.
  - The runnable PyTorch form is the `Wan-AI/Wan2.2-TI2V-5B-Diffusers` pipeline, pulled as one. M20's refusal names none of this.

**The fix.**
1. **The pull takes a Wan original layout whole.** When a repository's root `config.json` names a Wan transformer (`WanModel`) and its root holds `.pth` components, those come too, recorded and verified like every file.
2. **A set already pulled without them is completed by pulling again.** The pull fetches only what its record lacks, into the same set.
3. **The refusal and `models info` say what the set is** (Wan's original layout), what it lacks when it lacks it, and what runs it. Today that is the Diffusers pipeline for `video generate`, and after 53k mlx-video.

**Core constraint(s).**
- **Known by its own files:** a Wan transformer by its config's class, never its repository's name.
- **Nothing outside a recognized layout changes:** every other SafeTensors pull keeps exactly today's file list. The voice pull is untouched.
- **A `.pth` is stored, never loaded by Apogee:** only a runtime that needs one reads it (mlx-video's converter, 53k). The integrity record covers it like any file.
- **ADR 0006:** a set pulled before keeps its id. Completing it extends its record and does not move it.

**Seam + files.**
- **`source/business/models/source_hf.cpp`:** `snapshot_wanted` gains the Wan layout's root components, the listing's facts already read (`has_root_checkpoint`, `:295–297`).
- **`source/business/models/` (the pull):** an existing set completed from its record's gaps.
- **`source/data/modelstore/snapshot.h/.cpp`:** M20's diffusion reader learns the Wan layout and whether its components are present. `describe_diffusion_model` says it.
- **`source/presentation/cli/models.cpp`:** `render_snapshot`'s `diffusion:` line, with what is missing and the way.
- **Tests:**
  - the user's record and config as the fixture: a fake Hugging Face listing of the repository, with its `.pth` files, through the pull's seam;
  - the components taken and the set completed in place;
  - a non-Wan repository's file list unchanged;
  - the refusal's new wording.

**Reference.** M20 (the diffusion reader and its refusal). 50a (`voice_wanted`, a root `.pth` taken). [mlx-video's Wan README](https://github.com/Blaizzy/mlx-video/blob/main/mlx_video/models/wan_2/README.md) (the original layout, the `.pth` components).

**Decisions made** (dated):
- 2026-10-21 — **The user's refusal folded in** (the user's call: "Fold this into the previous spike"). The refusal stands for conversion today, and says what the set is and what it lacks.
- 2026-10-21 — **Pulled whole, rather than redirected to the Diffusers pipeline only** (the spike's default, standing). mlx-video converts the original layout and not the pipeline, so the original layout must be whole for MLX video (53k). The Diffusers pipeline remains the PyTorch engine's, and the refusal names it.

**Open calls:**
- [default: the layout is recognized by a root `config.json` naming `WanModel` (or a later Wan transformer class) beside root `.pth` files. Every root `.pth` of such a repository is taken.]
- [default: re-pulling a set that lacks files the pull now wants fetches only those, by the pull's existing resume. The set keeps its id, which is the transformer shards' hash.]

**Guardrail(s).** The user's own record (10 files, those sizes) and config are the fixture.
- **Pulled again over a fake listing that adds the two `.pth` files:** the same set gains exactly those two, verified.
- **`models info`:** says the set is complete. Before the re-pull, it names the two missing files and the way.
- **A Llama repository's pull:** its file list unchanged.

**Acceptance criteria:**
- [ ] `models pull Wan-AI/Wan2.2-TI2V-5B --safetensors` on the user's existing set fetches the T5 encoder and the VAE into it, and a fresh pull takes them from the start. The download is the user's run.
- [ ] `models info` and the conversion's refusal say what the set is, what it lacks, and what runs it.
- [ ] No other pull's file list changes.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** v0.2.0, no gate. [53k](mlx-video-convert.md) converts the whole set to MLX. Running a Diffusers pipeline is already `video generate`'s.
