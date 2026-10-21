# MLX records say their kind

**What / why.** The first step of MLX for every kind (the 2026-10-21 spike, wall **W9**). Today every directory under a model's `mlx/` is taken for a language model:
- the MLX reader (`source/data/modelstore/mlx_info.h:81–84`) reads `model_type`, a window and a quantization, as a chat model's;
- `models list` lists each as a chat backend's weights (`source/presentation/cli/models.cpp:626`);
- `config scan --register` (M12) registers each as an `mlx` chat backend (`source/presentation/cli/config_cmd.cpp:626`).

That holds while mlx-lm is the only converter (`assets/mlx/mlx_convert.py:88`). It stops holding the day anything else writes there: an embedding model through mlx-embeddings (53h), a drafter through mlx-vlm (53i), a Wan transformer through mlx-video (53k), a Kokoro voice through mlx-audio (53m). Each would be registered as a chat backend it cannot be.

Even a vision model needs it said (53f). An mlx-vlm-made directory stores its weights the way mlx-vlm loads them, and the backend answers text turns through mlx-lm (`assets/mlx/mlx_generate.py:849`). So the backend must know which converter made a directory before it can run one.

Measured, not guessed (2026-10-21). Method: read the source, the store's records, and the MLX projects' main branches. No conversion was run.

**The fix.** An MLX directory's own record (`apogee-snapshot.json`, the `Snapshot` every conversion already writes, whose `transform` already names the converter: `mlx_lm.convert 4bit`) says what kind it is. Every reader follows the record:
- **kinds:** `chat` (mlx-lm), `vision` (mlx-vlm, its towers kept), `embedding`, `drafter`, `video`, `voice`;
- **what follows the record:** the listing, `models info`'s lines and typed `data`, `config scan`'s rows and `--register`, and the roles a backend over it can take.

A directory with no kind recorded (every MLX set made before this, or pulled ready-made) is a `chat` model unless its own files say otherwise, as today.

**Core constraint(s).**
- **A set is known by its own files and its own record, never its name** (the draft-heads and M20 rule).
- **Nothing changes for what exists:** every directory converted or pulled before this item reads exactly as it does now. Its kind is inferred where unrecorded, and written only by the conversion that makes a directory.
- **One reader** (`modelstore/`) decides the kind. Every surface asks it (ADR 0002's one implementation).
- **The JSON grows additively** (ADR 0006): `models info`'s `data` and `models list`'s rows gain a `kind`.

**Seam + files.**
- **`source/data/modelstore/snapshot.h/.cpp`:** the `Snapshot` record gains `kind`, written by a conversion beside its `transform`.
- **`source/data/modelstore/mlx_info.h/.cpp`:** the kind read, from the record or inferred from the files. A `vision_config` with an image processor is `vision`, as `source/data/backends/mlx_local.cpp:332–335` reads it today.
- **`source/business/models/convert.cpp`:** the MLX conversion records `chat` (mlx-lm). Its siblings record their own kinds.
- **`source/presentation/cli/models.cpp`:** the listing's rows, `render_stored_mlx`'s `kind:` line, and `stored_set_data`'s `kind`.
- **`source/presentation/cli/config_cmd.cpp`:** `scan_store_rows` registers a `chat` or `vision` directory as an `mlx` backend and says the others apart, as draft heads are said today.
- **`lib/documentation/cli/reference/machine-mode.md`:** `kind`.
- **Tests:** `tests/data/modelstore/` (the reader: recorded, inferred, absent), and `tests/presentation/cli/models_mlx_test.cpp` (a directory of each kind listed, informed and scanned as its kind).

**Reference.** The draft-head (2026-10-21) and M20/M21 readers: one fact from the files, one wording, every surface reading it. The 2026-10-21 spike's sources: [mlx-lm](https://github.com/ml-explore/mlx-lm/tree/main/mlx_lm/models), [mlx-vlm](https://github.com/Blaizzy/mlx-vlm), [mlx-embeddings](https://github.com/Blaizzy/mlx-embeddings), [mlx-audio](https://github.com/Blaizzy/mlx-audio), [mlx-video](https://github.com/Blaizzy/mlx-video).

**Decisions made** (dated):
- 2026-10-21 — **Opened from the MLX-for-every-kind spike** (the user's call: "Run the spike into converting every type to MLX", then "create the documents for v0.2.0"). This item comes first because every other kind writes into `mlx/` (W9).
- 2026-10-21 — **The record carries the kind**, since a converter's output does not always say it in its own files. Inference covers only what came before.

**Open calls:**
- [default: the kind words are `chat`, `vision`, `embedding`, `drafter`, `video` and `voice`. A kind a later build does not know is shown as written and registered as nothing.]
- [default: `config scan --register` registers `chat` and `vision` directories as `mlx` backends, and says the other kinds apart with what uses them: the embedding role (53h), a drafter beside its base (53i), `video generate` (53l), `voice say` (53m).]

**Guardrail(s).** A directory of each kind, from fixtures written by this item's own tests, lists, informs and scans as that kind. An unrecorded directory reads as today: the existing `models_mlx_test` cases pass unchanged. The record's `kind` survives `models repair` and `models rename`.

**Acceptance criteria:**
- [ ] Every existing MLX directory lists, informs and registers exactly as before.
- [ ] A directory recorded as each kind is listed and informed as that kind, and `config scan --register` registers only `chat` and `vision` ones as backends.
- [ ] `models info --output-format json`'s `data` and `models list`'s rows carry `kind`, documented in `machine-mode.md`.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** v0.2.0, track 53's letters (the README's rule for a release whose rows share a prefix), first of the MLX-for-every-kind set (53e–53m), no gate. Its GUI sibling is the GUI's [3a](../../../gui/backlog/v0.2.0/mlx-kinds-in-stored-weights.md). Out of scope: making any new kind, which is its siblings' work.
