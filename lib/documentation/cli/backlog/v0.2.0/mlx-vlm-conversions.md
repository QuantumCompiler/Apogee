# Vision and omni sets convert through mlx-vlm, their towers kept

**What / why.** `models convert --mlx` has one converter. Its driver imports `mlx_lm.convert` and nothing else (`assets/mlx/mlx_convert.py:88`). mlx-lm builds language models, so a set that also sees or hears loses its towers, and a type it has no module for cannot convert at all. Measured, not guessed (2026-10-21, the MLX-for-every-kind spike). Method: the user's store's own configs, read in a mirror of it, and the projects' main branches, read on GitHub. No conversion was run. Walls **W1** and **W2**, and W10's mlx-vlm part.

- **The user's sets this touches.** Of the 18 SafeTensors sets, 7 declare towers:
  - Gemma4 ×5 (`Gemma4ForConditionalGeneration` / `Gemma4UnifiedForConditionalGeneration`, audio and vision configs);
  - Qwen3.8-27B (`Qwen3_5ForConditionalGeneration`, vision);
  - Qwen3-VL-8B (`qwen3_vl`, vision).

  mlx-lm converts all but the omni model as text alone. And Qwen3-Omni-30B (`qwen3_omni_moe`, audio and vision) cannot convert, since mlx-lm's `models/` has no `qwen3_omni_moe.py` (M21's check now leaves MLX unoffered for it).
- **mlx-vlm builds every one of them, towers kept.** Its `models/` holds `qwen3_vl`, `qwen3_vl_moe`, `qwen3_omni_moe`, `qwen3_5`, `qwen3_5_moe`, `gemma4` and `gemma4_unified`. It converts with `mlx_vlm.convert --hf-path … --mlx-path … --quantize --q-bits N --q-group-size N`.
  - Apogee already installs it: `train setup --with mlx-vlm`, `mlx-vlm>=0.3` (`source/business/training/python_env.cpp:49`).
  - Apogee already runs it: the backend's vision driver (27c, `assets/mlx/mlx_generate.py:885`), for a directory with a `vision_config` and an image processor (`source/data/backends/mlx_local.cpp:332–335`).
- **W2, unproven.** The backend answers text turns through mlx-lm (`mlx_generate.py:849`) and swaps to mlx-vlm only for a turn carrying a picture. A directory mlx-vlm wrote stores its weights the way mlx-vlm loads them, so whether mlx-lm can load it for a text turn is not known without real weights. This item makes the backend choose by the directory's recorded kind (53e): a `vision` directory runs through mlx-vlm for every turn.

**Core constraint(s).**
- **The set's own files choose the converter:** a config declaring a `vision_config`, an `audio_config` or a `thinker_config` converts through mlx-vlm. Never its name.
- **Offered only where it works** (the user's call, M21): MLX is offered for a towered set when the installed mlx-vlm has a module for its type, read from its own `models/` as M15 reads mlx-lm's. Otherwise mlx-lm's text-only conversion is offered where mlx-lm builds the type, and it says so.
- **A language model's conversion is unchanged:** mlx-lm, byte for byte as today.
- **Refused before Python starts** where neither converter builds the type, M15's `--model-type` door kept.

**Seam + files.**
- **`assets/mlx/mlx_convert.py`:** a `--with mlx-vlm` route calling `mlx_vlm.convert`'s own entry, its quantization flags mapped from the precision table. Seeded and held byte-identical to its compiled-in copy, as today.
- **`source/business/training/mlx_convert.h/.cpp`:** the converter chosen (`MlxConverterKind`), and `mlx_model_types` over mlx-vlm's `models/` too.
- **`source/presentation/cli/models_pull.cpp`:** `convert_to_mlx_command` routes by the set's towers, and `installed_mlx_builds` asks the converter the route uses.
- **`source/data/modelstore/snapshot.cpp`:** the M20/M21 decision unchanged in its answers (`mlx` stays a format). Which converter is the route's, recorded as the kind (53e: `vision`).
- **`source/data/backends/mlx_local.cpp`, `assets/mlx/mlx_generate.py`:** a `vision` directory runs through mlx-vlm for text turns too.
- **`source/business/training/python_env.cpp`:** mlx-vlm's floor raised to the release whose `models/` holds the user's types, with its reviewed date. An existing environment upgrades only on the person's word (M19).
- **Tests:** the converter route over the stub `mlx_vlm` (a `convert.py` and `models/` stubs beside `tests/data/backends/scripts/stub_mlx/mlx_lm/`), the route by towers, the offer by mlx-vlm's types, and a `vision` directory's text turn through the vision driver.

**Reference.** 27c (the vision driver, its readiness ladder). M15 (`mlx_model_types`, the `--model-type` door). M21 (MLX offered only where it works). [mlx-vlm's README and models](https://github.com/Blaizzy/mlx-vlm). The spike's report, 2026-10-21.

**Decisions made** (dated):
- 2026-10-21 — **Towers are kept by default** (the spike's default, standing). A set that sees or hears converts through mlx-vlm. mlx-lm's text-only build stays reachable by asking.

**Open calls:**
- [default: the text-only build is asked for with `--text-only` on `models convert --mlx`, the CLI's word for the old route, and said in the sheet (the GUI's 3a).]
- [default: the floor is the mlx-vlm release that ships `qwen3_omni_moe`, `gemma4_unified` and `qwen3_5`, read at build time and recorded with its date.]

**Guardrail(s).**
- **The user's configs as fixtures,** written inline: Qwen3-VL, Qwen3-Omni, Gemma4 and a Llama.
  - The towered ones route to mlx-vlm and record `vision`.
  - The Llama routes to mlx-lm, its argv unchanged.
  - The omni model is offered MLX only where the stub mlx-vlm lists `qwen3_omni_moe`.
- **A `vision` directory's text turn** runs through the vision driver (the stub's `mlx_vlm`), never mlx-lm.
- **Not run here:** a real conversion, and W2's real-weights question. Those are the user's run, per the standing rule.

**Acceptance criteria:**
- [ ] `models convert <Qwen3-VL> --mlx` builds through mlx-vlm with vision kept, and the backend over it reads pictures and answers text.
- [ ] `<Qwen3-Omni> --mlx` is offered and converts where the installed mlx-vlm builds `qwen3_omni_moe`, and is refused before Python otherwise.
- [ ] A language model converts exactly as before. `--text-only` gives a towered set mlx-lm's build.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** v0.2.0, gated on [53e](mlx-records-say-their-kind.md), which records `vision` and runs it so. Audio into these models is [53g](mlx-audio-input.md). Drafters, which also go through mlx-vlm, are [53i](mlx-draft-heads.md). The GUI's [3a](../../../gui/backlog/v0.2.0/mlx-kinds-in-stored-weights.md) shows the route.
