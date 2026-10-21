# Wan converted to MLX through mlx-video

**What / why.** M20 refuses the user's Wan set because no converter Apogee runs takes a diffusion transformer. The user's run:

`… is a diffusion model's weights (WanModel, a diffusers class): neither converter takes one`

One exists. Blaizzy's mlx-video ports Wan to MLX: Wan2.1 1.3B and 14B text-to-video, and Wan2.2 T2V-14B, **TI2V-5B** (the user's) and I2V-14B (its README, read 2026-10-21). Wall **W7**, its conversion half. Measured, not guessed (2026-10-21, the MLX-for-every-kind spike):

- **The converter:** `python -m mlx_video.wan2.convert --checkpoint-dir <Wan's original layout> --output-dir <out> [--quantize --bits 4 --group-size 64]`, per its Wan README. Its input is the original checkpoint: `diffusion_pytorch_model*.safetensors`, `models_t5_umt5-xxl-enc-bf16.pth` and `Wan2.2_VAE.pth`, which is why [53j](wan-pulled-whole.md) pulls the set whole. Its output is `config.json`, `t5_encoder.safetensors`, `vae.safetensors` and the transformer's weights. Its README's figures, for scale: 1.3B at 2.7 GB in bf16 and 799 MB at 4-bit; 14B at about 28 GB and about 8 GB. It states none for TI2V-5B.
- **It needs PyTorch to convert** ("For weight conversion: PyTorch").
- **It is not on PyPI:** the README installs it as `pip install git+https://github.com/Blaizzy/mlx-video.git`, and its Releases are empty. Every set Apogee installs today is a PyPI package with a floor and a reviewed Python range (`source/business/training/python_env.cpp:18–49`). A GitHub install is a new kind of dependency.
- **Its module path differs between its READMEs:** `mlx_video.wan_2.generate` in one, `mlx_video.wan2.convert` in the other. It is to be read at build time.

**Core constraint(s).**
- **Opt-in, in Apogee's own environment** (SPEC, the MLX amendment of 2026-10-03): installed by `train setup --with mlx-video` only, never the system Python, and never at install unasked.
- **Offered only where it works** (M21): `converts_to` gains `mlx` for a Wan original-layout set exactly when the set is whole (53j) and mlx-video is installed. M20's refusal stands otherwise, saying which of the two is missing.
- **A child's stderr is captured** (CLAUDE.md → ⚠): the converter runs over the one script runner, its print kept off both streams, as `mlx_convert.py` is.
- **Recorded as `video`** (53e). Never registered as a chat backend.

**Seam + files.**
- **`source/business/training/python_env.cpp`:** an `mlx-video` set (with `torch` for its converter), its Python range (≥3.11, per its README), and the pinned source: a GitHub commit, read and dated (the user's call).
- **`assets/mlx/mlx_convert.py`:** the `mlx_video.wan2.convert` route.
- **`source/data/modelstore/snapshot.cpp`:** the M20 decision answers `["mlx"]` for a whole Wan original layout with mlx-video installed. **`source/business/models/convert.cpp`:** M20's refusal routed instead.
- **`source/presentation/cli/models_pull.cpp`:** the MLX door's route and its estimate, by the README's figures where it states one and none otherwise.
- **Tests:** a stub `mlx_video` package with a `wan2/convert.py` writing the documented output. The route, the record `video`, the decision's three answers (whole and installed, not whole, not installed), and the refusal's wording each way.

**Reference.** M20 and M21 (the decision and its refusals). [mlx-video](https://github.com/Blaizzy/mlx-video), its [Wan README](https://github.com/Blaizzy/mlx-video/blob/main/mlx_video/models/wan_2/README.md). A community 8-bit conversion of the same model exists: [Anes1032/Wan2.2-TI2V-5B-mlx-q8](https://huggingface.co/Anes1032/Wan2.2-TI2V-5B-mlx-q8). Its author says it runs comfortably at 720p, 24 fps on a 64 GB Mac, which is the author's claim.

**Decisions made** (dated):
- 2026-10-21 — **mlx-video installed from GitHub, pinned to a reviewed commit** (the user's call, at the v0.2.0 run's start: "Pinned GitHub install"). Apogee's environment trusts that one commit, read and recorded with its date, as the Python ranges are. A later commit is a deliberate bump.
- 2026-10-21 — **Split from running it** ([53l](mlx-video-generate.md)): a converted directory is useful to inspect and register before an engine runs it, and the install question lives here.

**Open calls:**
- [default: the conversion quantizes to 4-bit by default (the README's default), `--type` offering the precisions the converter takes, as `--mlx` does today.]

**Guardrail(s).**
- **The user's Wan record, completed per 53j, as the fixture:** routed to the stub, recorded `video`, and offered `mlx` only when whole and installed.
- **The refusal for a set missing its `.pth` files** names them. The one for mlx-video missing names the setup.
- **Not run here:** a real conversion. That is the user's run.

**Acceptance criteria:**
- [ ] On the user's whole Wan set with mlx-video installed, `models convert … --mlx` writes an MLX video directory recorded `video`, said in `models info`.
- [ ] Otherwise the refusal says exactly what is missing, and `converts_to` has no `mlx`.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** v0.2.0, gated on [53e](mlx-records-say-their-kind.md) (the `video` kind) and [53j](wan-pulled-whole.md) (the set whole). Other video models (LTX-2, which mlx-video also ports) are out of scope until the user holds one.
