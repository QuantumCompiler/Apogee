# MLX model operations: the store format, pull, convert, and honest numbers

**What / why.** [27a](mlx-backend-core.md) runs an MLX model it is pointed at; this item makes those models first-class citizens of the model directory. The store's rule — one directory per model, per format, per set of weights named by the weights' own hash — gains an **`mlx/` format directory** beside `gguf/` and `safetensors/`: an MLX model is a directory (sharded SafeTensors + `config.json` + tokenizer files, usually pre-quantized), and it lands, lists, and deletes like everything else. `apogee models pull` already fetches HF snapshots through the copy → verify → commit ladder (the SafeTensors path the training track uses), so pulling `mlx-community/...` refs is mostly recognition, not new machinery. **`models convert --mlx`** quantizes a full-weight snapshot into the format through `mlx_lm.convert` in the owned venv — the same boundary discipline as the GGUF converter, a different engine. And the numbers stay honest: `models info` and the 26a/26c window machinery read the real context window and sizes from MLX's `config.json`, so an MLX chat gets the same warned-at-80%, compacted-at-90% behavior a GGUF chat gets, instead of a window nobody measured.

**Core constraint(s).**
- **One layout declaration, one level down** (consumed decision — the model store, 2026-09-23): where an MLX model lives is declared in `modelstore/store.h` beside the other formats, and pull, convert, list, info, delete, repair, `check` and training promotion all ask it. No second path invents a location.
- **The acquisition ladder is the acquisition ladder:** `.partial` staging, per-shard digest verification where the source publishes one, reporting **which** checks ran, never a half-downloaded model in place — the shipped rules apply to the new format unchanged.
- **Conversion is a child, never a link:** `mlx_lm.convert` runs in the owned venv through the training track's script-runner discipline (stderr captured, progress framed, Ctrl-C cleans staging — the lesson the GGUF convert hang taught on 2026-09-24 applies from day one here).
- **Honest capability reporting:** `models list/info` say what an entry *is* (format, quant, window, size on disk) from files actually read — the GGUF-header-reader lesson (a check that reports "loads" for files that cannot load is worse than none). [M2](../../assistant/MILESTONES.md#milestone-n--model-operations)'s lesson extends to MLX `config.json` reads: measure the sweep before caching it -- M2's slow `models list` was a seek per vocabulary string, fixed in the reader, and no cache was needed (shipped 2026-10-03).
- **No network in tests:** fixture model directories (tiny synthetic config/tokenizer/shards) drive everything hermetic; real pulls are live verification.

**Seam + files.**
- `modelstore/store.h/.cpp`: the `mlx/` format row and its resolution; `models/mlx_info.h/.cpp` (new): the `config.json`/index reader — window, quant bits, shard inventory, total size — pure over injected files.
- `models/acquire.cpp` / `commands/models_pull.cpp`: recognizing an MLX snapshot (directory with `config.json` + MLX quant markers) and landing it under `mlx/<hash>/`.
- `commands/models.cpp`: list/info/status rows for the format; `commands/check.cpp`: the store validation extended.
- `models/convert.cpp` + the venv script path: `models convert --mlx <source>` over the script runner; staging and Ctrl-C per the house rules.
- `harness/` window plumbing: the `mlx` entry type's context size resolved from `mlx_info` when unset (the 26a chain).
- Tests: `tests/models/mlx_info_test.cpp` goldens over fixture directories (well-formed, missing fields, truncated shard); store/migrate cases; a pull e2e against a local fixture "source"; convert staged-cleanup case.

**Reference (Ommi).** No analog — Ommi had no second local format (and no open acquisition at all; its models were allowlisted). The in-house precedents consumed: the model store and its ladder (Milestone N, revised 2026-09-23), the GGUF info reader's honesty rule, and the training track's SafeTensors snapshot fetching.

**Decisions made** (dated):
- 2026-10-03 — Split from the MLX track: model operations after the backend core, so the format serves a runtime that exists.
- 2026-10-03 — **Pre-quantized community models are the primary path** (pull `mlx-community/...` and run); `convert --mlx` is for full-weight snapshots the user already holds — mirroring how GGUF acquisition actually gets used.

**Open calls:**
- [default: an `mlx` entry's `model_path` points at the model *directory*; a bare model name resolves through the store like GGUF paths do] Addressing.
- [default: `models delete`/retention treat the directory atomically — a shard is never removed alone] Deletion granularity.
- [default: no MLX-side embedding support claimed; the entry answers "no" to the embed capability until someone builds it] Capability honesty.

**Guardrail(s).**
- Store goldens: an MLX directory lands under `mlx/<hash>/`, lists with format and quant, deletes atomically; the flat-layout refusal naming `models migrate` covers the new row.
- The ladder cases: interrupted pull leaves only `.partial`; verification reports which checks ran on a source with and without digests.
- `mlx_info` goldens including the dishonesty guards (missing `config.json` → "cannot load", never a guess).
- Convert: Ctrl-C mid-run leaves no staging; the output lands under the store rule; mutation-tested where convention applies.
- On real weights: `models pull` of one mlx-community build per family in [the model families](../../assistant/DEVELOPER.md#on-real-weights-the-model-families) lands, lists with a real window, and loads in 27a's backend — recorded on ship.

**Acceptance criteria:**
- [ ] `apogee models pull mlx-community/<ref>` lands the model under `<model>/mlx/<hash>/`, verified per the ladder, and `models list` shows it with format, quant and window.
- [ ] Pointing an `mlx` entry at it chats with correct window behavior (warning and compaction at the real window, from `config.json`).
- [ ] `models convert --mlx <safetensors snapshot>` produces a runnable MLX directory in the store; Ctrl-C mid-convert leaves the store untouched.
- [ ] `check` validates the new rows; `models delete` removes the directory whole.

**Scope note.** Item **27b**, earmarked for **v0.1.4**; build after [27a](mlx-backend-core.md). Out of scope: vision model assets and projector-equivalents ([27c](mlx-depth.md)); MLX dataset tooling; serving pre-quantization choices beyond what `mlx_lm.convert` exposes.
