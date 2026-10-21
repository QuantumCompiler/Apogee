# Conversions say what a set takes

**What / why.** The user's run of the macOS app on 2026-10-21 started an MLX conversion of `Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83`. It ran three seconds and ended in mlx-lm's own words:

`apogee models: FileNotFoundError: No safetensors found in /Users/taylor/.apogee/models/Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83 -- stderr: [INFO] Loading`

Measured, not guessed (2026-10-21). Method: read the source, and probe the installed `apogee 0.1.9 (3e6efaf)` against a throwaway `APOGEE_HOME` holding a mirror of the user's store's layout (their sets' own config files, the weights sparse).

- **The set is no language model.** Its `config.json` is a diffusers transformer's: `"_class_name": "WanModel"`, `"_diffusers_version": "0.33.0"`, `"model_type": "ti2v"`, and no `architectures`. Its weights are `diffusion_pytorch_model-0000N-of-00003.safetensors`, beside a `google/` text encoder.
  - **mlx-lm** builds a language model by `model_type`, and its loader looks for `model*.safetensors`, hence the error.
  - **llama.cpp's converter** resolves a model by its `architectures`, in this CLI's own words (`source/presentation/cli/models_pull.cpp:2966`).
  - Neither takes it.
- **Both ladders let it through to Python.** `conversion_refusal` (`source/business/models/convert.cpp:110`) and `mlx_conversion_refusal` (`:193`) check rung 1 for a `config.json` and any `*.safetensors`, and refuse a damaged set and a draft head (the "Draft Heads" change, the same day). A diffusers model passes every rung, so the converter's child starts and fails in its own words.
- **`models info` advertises both conversions for it.** `render_snapshot` (`source/presentation/cli/models.cpp:1304–1307`) writes, for any set nothing was made from: `made from it: nothing yet -- 'apogee models convert Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83' makes a GGUF of it (--mlx an MLX model)`. Probed, it writes the same line for `Gemma4-12B-Assistant`, in the document whose `format:` line two lines up says "a draft head's weights, not runnable and not convertible".
- **A client has no typed fact to act on.** `models info`'s `data` (52e) states a set's format, bytes, architecture, window, cache and lineage. The draft-heads change left it without a field, "for a client that needs one". The macOS app is that client now. Its Stored weights offers Convert to GGUF and Convert to MLX on every SafeTensors set, deciding nothing itself by its invariant, and so offered both on the Wan set and on the user's four draft heads. Its sibling is the GUI's [M3](../../../gui/backlog/maintenance/convert-offers-what-the-set-takes.md).

**The fix: one decision, read everywhere.** Which conversions a stored SafeTensors set takes (GGUF, MLX, or none) is decided once, from the set's own files.
1. **Both ladders refuse** a set that takes neither, before any Python starts, naming what it is: a draft head (as today), or a diffusers model by the class its `config.json` names.
2. **`models info`'s "made from it" line** offers only the conversions the set takes. For one that takes none, it says so.
3. **`models info`'s `data`** gains `converts_to`, the formats the set converts to, `[]` when none. The field is additive (ADR 0006) and documented in `machine-mode.md`.

**Core constraint(s).**
- **A set is known by its own files, never its name** (the draft-heads rule): its `config.json` as written.
- **Refused before anything runs.** No interpreter starts, nothing is staged, nothing is made.
- **The JSON grows additively** (ADR 0006). An older client reading `data` is unaffected.
- **A language model's snapshot converts exactly as today.** Its `converts_to` is `["gguf","mlx"]`.

**Seam + files.**
- **`source/data/modelstore/snapshot.h/.cpp`:** a reader beside `snapshot_draft_head`, the diffusers class a snapshot's `config.json` names, if any. One wording for it, as the draft heads have.
- **`source/business/models/convert.h/.cpp`:** the one decision (`snapshot_conversions`, or its like), which both ladders' rung 1 consults after the draft-head check.
- **`source/presentation/cli/models.cpp`:** `render_snapshot`'s "made from it" line, and `stored_set_data`'s `converts_to`.
- **`lib/documentation/cli/reference/machine-mode.md`:** the `models info` document's `data` fields, and the conformance that pins it where it does.
- **Tests:**
  - `tests/data/modelstore/store_test.cpp`: the reader over the Wan set's recorded `config.json`, a language model's, and a draft head's;
  - `tests/business/models/convert_test.cpp`: both ladders refuse before the converter seam is called;
  - `tests/presentation/cli/models_mlx_test.cpp`: the user's command refused, nothing staged; `models info`'s line and `data`.

**Decisions made** (dated):
- 2026-10-21 — **The diagnosis**, as above. The failure is the converter's, reached because nothing refused first. The mislabelled hint is the same missing decision seen from `models info`.
- 2026-10-21 — **One decision, three readers.** The ladders, the human line and the JSON field read the same function, so they cannot disagree. Today the `format:` line and the "made from it" line in one document already do.

**Open calls:**
- [default: a diffusers model is known by its `config.json` naming a `_class_name` with a `_diffusers_version` and no `architectures`: the weights' own declaration, as the draft-head reader reads its own. The refusal names the class (`WanModel`) and says that neither llama.cpp's converter nor mlx-lm's takes a diffusion model. It names no other command unless one exists at build time that runs these weights as they are stored.]
- [default: the field is `converts_to`, an array of the store's format words (`gguf`, `mlx`), in that order, `[]` for none; present on a SafeTensors set's `data` only, `null` on the other formats, whose sets are what conversions make.]
- [default: the GGUF conversion is refused too, as the draft-heads change decided for its case: Apogee converts to make backends, and llama.cpp's converter takes no diffusion transformer.]

**Guardrail(s).** The bug becomes a fixture.
- **The fixture:** the Wan set's own `config.json` and its shard names, recorded as `tests/fixtures/` data. The weights are not needed: the decision reads the config.
- **Both ladders** refuse it with the converter seam never called. The user's exact command, `models convert Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83 --mlx`, is refused in its words, with nothing staged.
- **`models info`** writes no conversion hint for it, nor for a draft head. A language model's snapshot keeps today's line.
- **`data.converts_to`** is `[]` for both of those, and `["gguf","mlx"]` for a language model's snapshot.
- **`machine-mode.md`** names the field.

**Acceptance criteria:**
- [ ] The user's Wan command is refused before any interpreter starts, in under a second, naming `WanModel` and why neither converter takes it, with nothing made or staged. The GGUF form is refused alike.
- [ ] `models info` on the Wan set and on `Gemma4-12B-Assistant` offers no conversion. On a language model's snapshot it reads as today.
- [ ] `models info --output-format json` on a SafeTensors set carries `data.converts_to`: `[]` for those two, `["gguf","mlx"]` for a language model's. The field is documented in `machine-mode.md`.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** Maintenance (`maint.`), claimable by name, no gate. Its GUI sibling, [M3](../../../gui/backlog/maintenance/convert-offers-what-the-set-takes.md), waits on it. Out of scope:
- **Converting a diffusion model to MLX at all,** or any type mlx-lm does not build: a capability, not upkeep. It is a spike into what each stored type would need, on the user's word.
- **The GUI's own argv bugs from the same run:** the GUI's M2, shipped 2026-10-21 ([its record](../../../gui/assistant/MILESTONES.md#milestone-c--cli-parity)).
