# A drafter's pairing survives renames, and its weights count

**What / why.** 53i pairs a Gemma 4 drafter with its MLX base: `draft: <path>` on the base's `mlx` entry, plus the drafter's own record naming its `bases`, the stored model folders it pairs with. Two things around that pairing were left behind, read in the source and 53i's record (2026-10-22).

- **`models rename` breaks the pairing.** The rename core rebases a backend's `model_path` and `mmproj_path` when the folder they point into moves (`source/presentation/operations/renames.cpp:250–254` and `:376–382`). It never touches `draft:`.
  - Renaming the drafter's folder leaves `draft:` pointing at a path that is gone.
  - Renaming the base's folder leaves the drafter's recorded `bases` naming the old folder.
  - Either way the ladder refuses the pairing, naming the stale path or base. The way back today is converting the drafter again (53i "Not done").
- **A suite's footprint leaves the drafter out.** 27e's memory estimate counts a member's weights once per base (53d), but not the drafter its entry loads beside it. So a suite that fits on paper may not fit once drafting (53i "Not done").

**Core constraint(s).**
- **Every config write goes through the one editor** (`contracts/config_edit`, ADR 0002), as the rename's other rewrites do.
- **A record is rewritten in place, never moved or re-hashed.** A set keeps its id (ADR 0006), and the drafter's `bases` field is the only thing that changes.
- **The pairing rule stays 53i's:** family, width and the record's `bases`. A rename preserves a pairing that held, and never creates one that didn't.

**Seam + files.**
- **`source/presentation/operations/renames.cpp`:** `draft:` rebased like `model_path`, both when a backend's own folder is renamed and in the store-wide pass. When a base's folder is renamed, every stored drafter whose record names it has its `bases` rewritten.
- **`source/data/modelstore/snapshot.h/.cpp`:** a record's `bases` rewritten through `write_snapshot`, the rest of the record byte-identical.
- **27e's footprint** (the suite estimate, in `business/harness/` or `presentation/cli/` where 53d left it): a member's drafter counted by its bytes on disk.
- **Tests:**
  - a paired drafter's folder renamed;
  - a paired base's folder renamed;
  - both still pair, with `models info` and the ladder unchanged in their verdict;
  - the footprint of a suite whose member drafts.

  All over 53i's fixtures (`tests/support/draft_head_configs.h`).

**Reference.** 46a (`models rename`, the one core), 53i (the drafter, its record's `bases`, `mlx_drafter_refusal`), 53d and 27e (the footprint counting a base once).

**Decisions made** (dated):
- 2026-10-22 — **Opened from the MLX-for-every-kind run's record** (the user's call: "Queue the MLX track's follow-ups as backlog items").

**Open calls:**
- [default: a rename rewrites the drafter's recorded `bases` rather than letting the pairing fall back to family and width alone. The record stays the evidence of which base a drafter was made for.]

**Guardrail(s).**
- **Each rename direction leaves a working pairing:** the drafter's folder, the base's folder, and a model renamed by its backends' name (46a).
- **A rename that touches no drafter** writes the config and every record exactly as today.
- **The footprint's change shows only on a member that drafts.**

**Acceptance criteria:**
- [ ] After `models rename` of a drafter or of its base, the paired backend still drafts, with no reconversion.
- [ ] A suite member's footprint counts its drafter.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** Maintenance, claimable by name, no gate. Pairing more than one drafter per base is out of scope; 53i's one `draft:` stands.
