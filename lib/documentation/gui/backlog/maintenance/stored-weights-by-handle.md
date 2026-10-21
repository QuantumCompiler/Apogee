# Stored weights by handle — every stored set its own row, named and acted on by its handle

**What / why.** Reported 2026-10-20 by the user, running the built app: the Models library showed none of the SafeTensors sets they know are in `~/.apogee` (16 SafeTensors sets and 2 MLX models are stored there). Measured, not guessed (2026-10-20): the store's metadata was mirrored into a scratch `APOGEE_HOME`, each weights file a sparse stand-in of the same size, and the installed 0.1.9 read it with `models list --output-format json`. Then the Models screen was drawn offscreen over that exact document.

- **What the CLI sends.** All 18 stored rows arrive, each with `"backend": "(not configured)"`, `"backend_type": "-"` and the handle in `model` (`Gemma4-12B-Instruct/safetensors/920344fd2dac`). `(not configured)` is the human table's placeholder for "no backend points at this". The CLI writes it for every stored kind: a GGUF file, a SafeTensors set, an MLX model, a pipeline, a voice model and both legacy layouts (`lib/src/cli/source/presentation/cli/models.cpp:556, 592, 631, 663, 689, 713, 727`). Pipelines and voice models always get it, since no backend is ever over them.
- **What the GUI does with it.** The GUI keys and names a row by `backend` (`ModelRow.id`, `lib/src/darwin/source/business/reads/Models.swift:31`), so the 18 rows share one identity. The library's `LazyVStack`/`ForEach` (`ModelsScreen.swift:100`) draws one "(not configured)" row under SAFETENSORS and blank space for the rest. The MLX group draws empty, while the tabs count "All 18".
- **The verbs get the placeholder too.** `models info`, `models delete` and `models repair` run with `(not configured)` (`ModelsViewModel.swift:128, 252, 268`), and the CLI refuses it: "no backend or stored weights named '(not configured)'". All three take the handle (`<model>/<format>/<id>`, per their `--help`). `renameName` derives from the backend, where `models rename` takes the stored directory.
- **Voice and Settings break the same way.** Voice takes its voice model from the first voice row's backend (`VoiceViewModel.swift:88`), so `models info "(not configured)"` is refused and no voices are listed. Settings' default-pipeline picker offers `(not configured)` (`SettingsViewModel.swift:140`); `config set-default-video` takes a handle. Settings' backends list also shows each stored row as a backend.
- **Folding.** Apart from the identity bug, `models list` folds the sets a conversion consumed (`"folded": 2` here; `models.cpp:1013`). The CLI's table says so in a dim line ("… snapshots consumed by conversions are folded -- --all"); the GUI says nothing and never reads `--all`.
- **Blast radius.** Every stored model no backend points at collapses into one row in the library, and the Voice and Settings readings above fail on real stores. Configured backends have unique names and are unaffected.
- **Why tests missed it.** The 1h, 1o and 1p fixtures held stored rows hand-written with made-up backend names (`wan-1.3b`, `kokoro-82m`). None was recorded from the binary over a store.

**Core constraint(s).**
- **The machine-mode client law** (`../../assistant/CLAUDE.md` → The GUI is a machine-mode client). The library renders what `models list` says. A stored row's name is the CLI's own handle, its `model` field. The GUI never walks the store itself and never invents a name.
- **No two rows can collide.** A configured row is keyed by its backend, unique in the config. A stored row is keyed by its handle, unique in the store. A row with neither is skipped, as today.
- **Each verb gets the name its `--help` states.** `models info`, `delete` and `repair` take the handle. `models rename` takes the stored directory, the handle's first component. `voice say -m` and `config set-default-video` take the handle.
- **The fold is the CLI's.** The GUI shows the `folded` count in the CLI's words, and reads `models list --all` only when the person asks.

**Seam + files.**
- `source/business/reads/Models.swift`: `ModelRow` gains `isStored`, `name` (the backend, or for a stored row its handle; `id` becomes `name`) and `renameName` for a stored row (the handle's directory). The list document's `folded` count is read beside its rows.
- `source/presentation/models/ViewModels/ModelsViewModel.swift`: selection, `models info`, delete, repair and rename go by `row.name` / `renameName`. New: `folded`, and `showAll`, which reads `models list --all`.
- `source/presentation/models/Views/ModelsScreen.swift`: a row shows its `name`, and a stored row's status says it is not configured. The fold gets its line and **Show them**.
- `source/presentation/voice/ViewModels/VoiceViewModel.swift`: the store's voice model by its handle.
- `source/presentation/settings/ViewModels/SettingsViewModel.swift` and `Views/SettingsView.swift`: the pipelines by handle, and the backends list limited to configured rows.
- Tests: `tests/business/reads/`, `tests/presentation/models/`, `tests/presentation/voice/` and `tests/presentation/settings/`.

**Decisions made** (dated):
- 2026-10-20 — **The diagnosis** (measured, method above). One root cause: the GUI takes `backend` as a row's name, and the CLI writes a placeholder there for every stored, unconfigured row. The handle the verbs take is already in the document, in `model`.
- 2026-10-20 — **The fix is the GUI's.** No CLI change is needed: the document carries the handle, and every verb takes it.
- 2026-10-20 — **Placed as the GUI's first maintenance item, M1.** It opens the GUI's Maintenance table, claimable by name. It is best built before `v0.2.0` merges, since it fixes that branch's own Models (1h), Voice (1o) and Settings (1p).

**Open calls:**
- [default: recognize a stored row by the CLI's two markers together, `backend_type` `-` and the backend `(not configured)`, and pin both with a fixture recorded from the real binary, so a CLI rewording fails a test rather than the screen] The alternative is a CLI sibling item that adds an explicit field (`"configured": false`), additively per the CLI's ADR 0006, with this rule kept as its fallback. That sibling is only needed if the contract should say it outright.
- [default: **Show them** reads `models list --all` and holds it until the place is left; the fold line is the CLI's own sentence, its count from `folded`]

**Guardrail(s).** The bug becomes a fixture.
- **The fixtures.** Two `models list --output-format json` documents, recorded from the installed binary over a scratch store (metadata-only sets with sparse weights, the method of this diagnosis; paths made neutral): one default list and its `--all` counterpart. The store holds several SafeTensors sets, an MLX model, a pipeline, a voice model, one configured GGUF backend, and a set consumed by a conversion.
- **Models tests over them:**
  - every row has a distinct id;
  - each stored row is named by its handle;
  - `models info`, `delete`, `repair` and `rename` for a stored row are argv-exact (the handle, and the directory for rename);
  - the fold count is read, and `--all` is read only when asked.
- **Voice:** the voice model is the handle, and `models info <handle>` is read for its voices.
- **Settings:** the pipelines are handles, and the backends list holds configured rows only.

**Acceptance criteria:**
- [ ] Over the recorded 16 SafeTensors sets and 2 MLX models, the library shows 18 rows under their groups. Each is named by its handle (`Gemma4-12B-Instruct/safetensors/920344fd2dac`, …), with not-configured as its status. Today the same document draws one "(not configured)" row and blank space.
- [ ] Selecting a stored set reads `models info <handle>` and shows its details, including 52e's typed data. Delete and Repair run with the handle; Rename runs with the stored directory.
- [ ] The fold shows itself: the CLI's sentence with the count (2 here) and **Show them**, which reads `models list --all` and lists the folded sets.
- [ ] Voice finds the store's voice model by its handle and lists its voices. Settings' default-pipeline picker offers the stored pipelines' handles, and its backends list holds configured backends only.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** Maintenance, claimable by name; no gate. Out of scope:
- the CLI-side explicit field (the open call's alternative, its own CLI item if wanted);
- an ON DISK figure for SafeTensors sets: `models list` states no bytes for them, and `models info` does;
- what an MLX or SafeTensors set's own "cannot load" note says (the CLI's words, shown as they are).
