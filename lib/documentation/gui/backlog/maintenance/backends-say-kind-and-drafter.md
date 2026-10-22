# The Backends table says each entry's kind and drafter

**What / why.** 3a says an MLX set's kind in Stored weights. 3b pairs a drafter from the drafter's side (**Pair with its base…**), and a backend's details say its drafter, with **Unpair**. Three gaps are left on the Backends side, recorded in 3b's MILESTONES entry under "Not done":
- **No kind or drafter in the table.** The Backends table shows neither an `mlx` entry's kind nor which bases have a drafter paired, so a person scanning their backends can't see which ones embed or draft.
- **No pairing from the base.** A base can't be given a drafter from its own details. Someone looking at the model they want to speed up has to go and find the drafter in Stored weights.
- **No "cannot draft" state.** It has no preview, because no recording of it exists. A pairing whose drafter was deleted, or whose base was converted again as mlx-lm's `chat` build, would show only as the CLI's refusal.

The CLI states all of it already:
- an `mlx` backend's row carries `kind` (53e);
- the backend's `models info` has its `draft:` line (53i, read by 3b's `BackendDraft`);
- `__complete config set-backend <base> --draft ""` offers exactly the drafters that pair with that base (53i).

**Core constraint(s).**
- **The binary's word decides.** The pairing is the CLI's (family, width, the drafter's recorded `bases`). The app offers only what the binary's completion offers, as 3b does from the other side.
- **Every command is one the binary takes,** held to recorded verdicts, as M2's store commands and 3b's pairing are.
- **An older binary keeps today's table:** no kind and no draft line mean nothing new is drawn.
- **ADR 0004:** each new state has a preview over recorded documents.

**Seam + files.**
- **`lib/src/darwin/source/presentation/models/`:**
  - `Views/ModelsScreen.swift`: the Backends table's kind beside the format (`MLX · vision`, as Stored weights says it), and a mark on a base with a drafter paired.
  - `ViewModels/ModelsViewModel.swift`: **Pair a drafter…** on a `vision` `mlx` entry, offering the drafters the binary's completion offers for it, then `config set-backend <base> --draft <drafter path>` read back. 3b's `BackendDraft` and its Unpair are reused.
- **Fixtures:**
  - **Completion:** the base-side completion, recorded the way 3b recorded its own: sandbox homes, the home written `/Users/someone/.apogee` (Milestone D's 3b entry).
  - **A pairing that can no longer draft:** a paired base whose drafter's directory was then deleted, its `models info` and the backend's refusal recorded.
  - **Verdicts:** the base-side command, appended to `drafter-pairing-verdicts.txt`.

**Decisions made** (dated):
- 2026-10-22 — **Opened from the MLX-for-every-kind run's record** (the user's call: "Queue the MLX track's follow-ups as backlog items").

**Open calls:**
- [default: the table marks a paired base with a small drafter glyph and the drafter's name in its tooltip, rather than a column of its own.]

**Guardrail(s).**
- **Over recorded documents:**
  - the table's kinds;
  - the paired mark;
  - the base-side pairing's offered drafters and its command, held to the binary's verdict;
  - the cannot-draft state.
- **Against a binary before the CLI's 53e and 53i,** the table and details read as today.

**Acceptance criteria:**
- [ ] The Backends table says each `mlx` entry's kind and marks a base with a drafter paired.
- [ ] A drafter can be paired from its base's details, offered only what the binary offers.
- [ ] A pairing that can no longer draft is drawn with the CLI's words.
- [ ] Previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate. Repairing a pairing a rename broke is the CLI's [M25](../../../cli/backlog/maintenance/drafters-follow-renames.md). This item only draws what the CLI says.
