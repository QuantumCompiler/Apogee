# `video generate` on MLX

**What / why.** `video generate` has one engine: its seeded driver loads a diffusers pipeline on PyTorch (`assets/video/video_generate.py:185`, 49b). An MLX video directory (53k) needs mlx-video's generator to run. Wall **W7**, its running half. Measured, not guessed (2026-10-21, the MLX-for-every-kind spike, mlx-video's README read on GitHub):

- **The generator:** `python -m mlx_video.wan2.generate --model-dir <dir> --prompt "…"`. Its flags:
  - `--negative-prompt`, and `--image` for the image-to-video modes;
  - `--width` (default 1280) and `--height` (720);
  - `--num-frames` (default 81, and it must be 4n+1), and `--steps` (the model's config by default);
  - `--output-path` (`output.mp4`), `--guide-scale`, `--shift` and `--seed`;
  - `--lora-high` and `--lora-low`.

  It reads `config.json` to pick the model's version and mode. Its README spells the module `mlx_video.wan_2.generate` in one place, so the path is read at build time.
- **What `video generate` already is around the engine** (49b–49e): the request as a JSON file, the throwaway working directory under `cache/video`, the JSONL progress protocol, the record beside the clip (`video.generation`, `contracts/video_run.h`), LoRAs hashed into it, and the clip moved into place only on success. None of that is PyTorch's. Only the driver is.

**Core constraint(s).**
- **One generation, two engines:** the record, the progress protocol, the working directory and the refusals stay the one implementation (ADR 0002). The engine is a field of the request and of the record.
- **Known by the directory:** a `video`-kind MLX directory (53e) runs on MLX, and a diffusers pipeline on PyTorch. An explicit `--engine` that cannot run is refused before a spawn, naming the setup.
- **The interactive invariants:** the driver over the one script runner, its stderr a bounded tail, its descriptor 1 on stderr before importing, its whole process tree holding no socket (`cli.video_lifecycle`'s sampling, extended to the MLX driver).
- **A `--from` rerun replays its engine:** the record says which.

**Seam + files.**
- **`assets/video/video_generate_mlx.py`:** a new seeded driver over mlx-video's generator, speaking the same JSONL progress events (its steps reported as the diffusers driver's are). It is held byte-identical to its compiled-in copy.
- **`source/business/video/generation.h/.cpp`, `runtime.h/.cpp`:** the engine chosen by the model's kind, the ladder per engine (mlx-video installed, the directory whole), and the request's flags mapped. The 4n+1 frame rule is refused up front.
- **`source/data/contracts/video_run.h/.cpp`:** `engine` in the record, additively.
- **`source/presentation/cli/video_generate.cpp`:** `--engine auto|torch|mlx` and its completion (ADR 0007). The `--model` door accepts an MLX video directory.
- **Tests:** `video_driver_test`'s pattern over a stub `mlx_video` printing on both streams, the engine chosen by kind, the record's `engine`, the frame rule, and `cli.video_lifecycle`'s socket sampling over the MLX driver.

**Reference.** 49b–49e (the video runtime, the record, LoRAs) and 50c (the composer drives clips through this same core). [mlx-video's README](https://github.com/Blaizzy/mlx-video).

**Decisions made** (dated):
- 2026-10-21 — **The engine follows the model's directory**, with `--engine` there only to refuse or to name it. A person picks the model, and the model says its engine.

**Open calls:**
- [default: LoRAs pass to the MLX engine where mlx-video takes them (`--lora-high`, `--lora-low`, by the mode's two halves) and are refused by name where it does not, recorded as today.]
- [default: the composer (`video compose`) uses whichever engine its clip model's directory says. It gains no flag of its own.]

**Guardrail(s).**
- **The diffusers driver's tests, mirrored over the stub mlx-video:** progress, the record with `engine: mlx`, the clip moved only on success, a cancel leaving nothing behind, and the stub's prints off the protocol.
- **A diffusers pipeline still runs on PyTorch,** its record unchanged but for `engine`.

**Acceptance criteria:**
- [ ] `video generate -m <the converted Wan> "…"` makes a clip on MLX, recorded with its engine. The real clip is the user's run.
- [ ] A pipeline generates as before. A `--from` rerun replays the engine its record names.
- [ ] `lib/scripts/cicd.sh --test` is green, `cli.video_lifecycle` included.

**Scope note.** v0.2.0, gated on [53k](mlx-video-convert.md). The GUI's [3b](../../../gui/backlog/v0.2.0/mlx-engines-and-drafters.md) shows the engine.
