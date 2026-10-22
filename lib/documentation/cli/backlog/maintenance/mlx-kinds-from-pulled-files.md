# A pulled MLX set says its kind from its own files

**What / why.** Since 53e every MLX directory has a kind (`chat`, `vision`, `embedding`, `drafter`, `video`, `voice`), and one reader decides it (`read_mlx_kind`, `source/data/modelstore/mlx_info.cpp:535–557`):
1. the record's `kind`;
2. else a draft head (53i) or a vision model (27c), read from the files;
3. else `chat`.

Every conversion now records its kind (53e–53m). **A pull does not.** `pull_mlx` (`source/presentation/cli/models_pull.cpp:1481`) writes a record with no `kind`, as every pull did. So a ready-made MLX set the MLX community publishes reads as `chat`:
- an embedding build;
- a Kokoro voice;
- a Wan video build.

`config scan --register` then registers it as an `mlx` chat backend, which it cannot be. The listing, `models info` and the GUI's Stored weights (its 3a) offer it chat's actions. 53m's record names the case it found: "A pulled MLX-community Kokoro reads as `chat`, since 53e infers `voice` from a record only."

Measured, not guessed (2026-10-22): the reader and the pull read in the source. No MLX-community repository was pulled. The published file lists of such repositories are read at build time.

**The fix.** The one reader infers `embedding`, `voice` and `video` from a directory's own files where they say so unambiguously, as it already does for `vision` and `drafter`. A directory whose files say nothing certain stays `chat`, as today.

**Core constraint(s).**
- **A set is known by its own files and its record, never its name** (the draft-heads, M20 and 53e rule).
- **One reader** (`modelstore/`) decides. Every surface keeps asking it (ADR 0002).
- **The record keeps its meaning.** A recorded kind always wins, and a kind is written only by the conversion that makes a directory (53e). Inference covers what a pull brought. Existing pulls fix themselves without a re-pull.
- **Nothing that reads as `chat` or `vision` today changes** unless its files unambiguously say another kind. A wrong inference would hide a chat model, which is worse than today's mistake.

**Seam + files.**
- **`source/data/modelstore/mlx_info.h/.cpp`:** `read_mlx_kind` gains three inferences, each with its own wording in `describe` ("inferred from its files: …"):
  - **`embedding`:** M21's sentence-transformers reader (`snapshot_embedding_model`, `modelstore/snapshot`: a `modules.json` with Pooling), or the marker mlx-embeddings' converted config carries, read at build time.
  - **`voice`:** a Kokoro build, meaning a `config.json` naming Kokoro (`model_type: kokoro`, or the shape `voice_info` already reads for the PyTorch checkpoint) beside a `voices/` directory of `.safetensors` packs.
  - **`video`:** 53k's video completeness rule, meaning a `config.json` beside `t5_encoder.safetensors` and `vae.safetensors`, read off the published mlx-video layout.
- **Tests:**
  - `tests/data/modelstore/mlx_info_test.cpp`: each inference, its source said, a recorded word winning, an ambiguous directory left `chat`.
  - `tests/presentation/cli/models_mlx_test.cpp`: each pulled shape listed, informed and scanned as its kind, `--register` adding only what an `mlx` entry runs (chat, vision, embedding).

**Reference.** 53e (the reader, the record's `kind`, `config scan`'s rule), 53i (a draft head inferred from files, the precedent for inferring past `vision`), M21 (`snapshot_embedding_model`), 53k (the video completeness rule), 53m (the MLX Kokoro layout). [The MLX community's models](https://huggingface.co/mlx-community) for the published shapes.

**Decisions made** (dated):
- 2026-10-22 — **Opened from the MLX-for-every-kind run's record** (the user's call: "Queue the MLX track's follow-ups as backlog items"). Each item in that run listed what it left under "Not done" in MILESTONES (Milestone AQ).
- 2026-10-22 — **Inferred by the reader, not recorded by the pull.** A pull that recorded a kind would fix only future pulls. The reader fixes every pulled set already in the store, and keeps 53e's rule that only a conversion writes a kind.

**Open calls:**
- [default: each published shape is read off the repositories' own file lists at build time (read-only), and only a shape whose files are unambiguous is inferred. A shape that cannot be told apart stays `chat` and is named in the item's record.]

**Guardrail(s).**
- **Fixtures written by the tests in each published shape** (embedding, Kokoro, Wan video): each reads as its kind, said "inferred from its files".
- **Every existing `[kind]` case passes unchanged.** A directory recorded as any kind reads as recorded.
- **A chat set with a stray `voices/` or `modules.json` file** but none of the rest stays `chat`.

**Acceptance criteria:**
- [ ] A pulled MLX embedding, Kokoro or Wan video set lists and informs as its kind, and `config scan --register` registers it only where an `mlx` entry runs that kind.
- [ ] Every directory that reads as `chat` or `vision` today, and every recorded directory, reads exactly as before.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** Maintenance, claimable by name, no gate. The GUI follows whatever `kind` the CLI states, with nothing of its own to change. Pulling MLX voice or video sets as their own pull routes (`--voice`-style doors) is out of scope: they arrive through the MLX pull as today.
