# Only backends that chat are offered to chat

**What / why.** Since the CLI's 53h, `config scan --register` registers an `embedding`-kind MLX directory as an `mlx` entry. That entry never chats: its chat, stream and complete refuse before anything starts, naming the embedding role. An `mlx` entry registered by hand over a `drafter`, `video` or `voice` directory is no chat backend either. The CLI lists it as needing attention (53e).

`models list` says so: since 53e an `mlx` backend's row carries its `kind`. The app's 3a reads it in Stored weights, and its Backends tab offers **Use in a new session** only on an entry that chats.

Every other place that picks a backend to chat with or to train still offers all of them, read in the source (2026-10-22):

| Place | What it offers | Where |
|---|---|---|
| Sessions | every backend `models list` names, as the settings sheet's choices | `presentation/sessions/ViewModels/SessionLibraryViewModel.swift:230–239` |
| Settings | the default chat backend's choices; only pipeline and voice formats are left out | `presentation/settings/ViewModels/SettingsViewModel.swift:139` |
| Training | every configured backend, as a target to train | `presentation/training/ViewModels/TrainingViewModel.swift:187` |

3a's record names the case: "Sessions, Suites, Settings and Training still list every backend whatever its kind, so an embedding entry can be offered there as a chat backend." Choosing it ends in the CLI's refusal. (Suites reads its members from the config and offers no picker, so it needs nothing here.)

**Core constraint(s).**
- **The binary's word decides:** a row's `kind`, never its name. A row with no kind (a non-MLX backend, or any row from a binary before the CLI's 53e) is offered exactly as today.
- **Nothing the CLI lists is hidden from the places that show backends.** Only the *choices* for chatting and training narrow. The Backends tab and Roles keep every entry, and the embedding role still offers an embedding entry (3a).
- **ADR 0004:** each changed picker has a preview over recorded documents.

**Seam + files.**
- **`lib/src/darwin/source/business/reads/Models.swift`:** one answer to "does this row chat", from `ModelRow.mlxKind` (3a): `chat`, `vision`, or no MLX kind. Every picker asks it.
- **The pickers:**
  - `presentation/sessions/ViewModels/SessionLibraryViewModel.swift`: the settings sheet's choices;
  - `presentation/settings/ViewModels/SettingsViewModel.swift`: the default chat backend's choices;
  - `presentation/training/ViewModels/TrainingViewModel.swift`: the targets, chat and vision `mlx` entries and the non-MLX backends it took before.
- **Fixtures:** 3a's recorded `store-list-kinds-registered.json`, which holds registered chat, vision and embedding entries, beside the existing lists from a binary before 53e.

**Decisions made** (dated):
- 2026-10-22 — **Opened from the MLX-for-every-kind run's record** (the user's call: "Queue the MLX track's follow-ups as backlog items"). 3a's MILESTONES entry records the gap under "Not done".

**Open calls:**
- [default: an entry the pickers leave out is not explained in the picker. Its place is the Backends tab, where its kind and actions are said.]

**Guardrail(s).**
- **Over 3a's registered-kinds document,** each picker offers the chat and vision entries and leaves out the embedding entry.
- **Over a document from a binary before the CLI's 53e,** each picker offers exactly what it offers today.

**Acceptance criteria:**
- [ ] No place offers an embedding, drafter, video or voice `mlx` entry as a backend to chat with or to train.
- [ ] Roles and the Backends tab still show every entry.
- [ ] Previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate. Every fact it needs the CLI already states.
