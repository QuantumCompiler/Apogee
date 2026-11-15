# The Models place in the React prototype

**What / why.** Port the Models place from the old prototype into the React app: its four tabs (Backends, Stored weights, Jobs, Roles), the store sheets, the download sheet, and the confirms that ask before anything is deleted or registered. It runs on the CLI's recorded documents instead of invented data. Measured in the 2026-11-14 recon:
- **The source in `main.html`, 812 lines in all:**
  - `renderVals()` sections, 521 lines: stored weights 5104–5241 (138), store sheets 5242–5373 (132), models screen 5374–5585 (212), download sheet 5586–5624 (39).
  - Markup, 291 lines: `ui.isModels` 860–1083 (224), with `isList` 873–957, `isStore` 959–1029, `isJobs` 1031–1064 and `isRoles` 1066–1082; `ui.downloadOpen` 2297–2316 (20); `ui.storeSheetOpen` 2383–2429 (47).
  - Store sheet kinds: `convert`, `quantize`, `register`, `rename`.
  - Shared pieces it also uses: 5 of the confirm section's 15 kinds (5625–5781): `model`, `storeDelete`, `storeModelDelete`, `unregister`, `registerAll`. And 2 of the generic sheet's 12 kinds (7300–7563): `adapters`, `pairDrafter`.
  - Its canned data: `baseModels()` (3362, 15 models) and the "the model store" method group from 3400 (`storeBase`, `storeSets` with 19 sets, `deletePlan`, `storeJob`, `jobView`).
- **The line numbers move.** They are the recon's (the 7,604-line working tree). Another session's design work was still landing that day: the file reached 7,780 lines by the afternoon (the four Models sections moved down 140 lines, same sizes; the confirm section gained a 16th kind, `taskRefused`), and HEAD `48f46831` has 7,300. Each section is found by its `/* ---------- name ---------- */` marker, and 1c's frozen baseline fixes the final numbers.
- **9 start presets:** `models`, `models:downloads`, `models:store`, `models:convert`, `models:register`, `models:roles`, `models:delete`, `models:notran`, `download`. The first 8 set `modelsTab` (`applyStart`). `download` only opens the download sheet, filled in, with nothing running. Eight have a Screen board (`ScreenModels`, `…Downloads`, `…Store`, `…Convert`, `…Register`, `…Roles`, `…Delete`, `ScreenDownload`). `models:notran` has none.
- **The old data is invented.** `jobs` holds 2 camelCase objects that tick off a clock. The CLI's pull is a stream of `job`, `stage`, `bytes` and `document` events (`lib/documentation/reference/cli/machine-mode.md:441–450`). No `object:` key appears anywhere in the script (critic V3).
- **The Mac app's `models` module** has 14 files, 6,861 lines:
  - ViewModels: `ModelsViewModel` (840), `StoreViewModel` (805), `DownloadsViewModel` (575), `StoreForms` (326), `DeletePlan` (108), `BackendAdapters` (94).
  - Views: `ModelsScreen` (972), `StoreView` (1049), `DownloadsView` (289), `AdaptersSheet` (92), `DeleteNarrationCard` (78). They hold 96 `#Preview` blocks.
  - Its tabs `backends`, `store`, `jobs`, `roles` (`ModelsViewModel.swift:15–19`) are the prototype's `modelsTab` values.
- **The data already exists.** The Mac's models previews cite 139 distinct recorded fixture files (`models/Previews/PreviewDocuments.swift`, counted 2026-11-14), and `tests/presentation/models/Fixtures` holds 5 more. Two gaps:
  - The only `models pull` stream is hand-made: `tests/business/jobs/Fixtures/pull-chain-done-handmade.jsonl`.
  - No conversion run is recorded, only `models-convert-help.txt`. The Mac's store jobs tick on what their own run prints as it stores each set (`DownloadsViewModel.swift` header); a quantization's output is recorded, for example `tests/business/reads/Fixtures/quantize-llama.txt`.

This is the largest feature port after the session, and the place other places lean on most: jobs, loaded models and roles are read across the app.

**Core constraint(s).**
- **The prototype is the source of truth.** Port its layout, states, controls and wording. Where the Mac app differs, follow the prototype and name the difference in the item's record. Names follow the Mac module where it has one.
- **The architecture is 1a's ADR.**
  - View-models are plain TypeScript classes that never import React. They take their services in the constructor: 1d's runner and jobs.
  - The module imports only `shell` and `styles`, as the Mac's `models` does.
  - Links to other places are callbacks the app root fills in, as the Mac's `say`, `checkFiles` and `useInSession` (`ModelsViewModel.swift:121–126`). Examples: the toast, "use in a session", "check its files".
- **Shared state is not this module's.** `jobs` is read by 6 of the 28 sections, `loaded` by 6 and `roles` by 7 (recon §4). Outside Models the readers are monitor, sidebar, chat, inspector, settings, compositions and the generate form. They stay behind 1d's services, so Monitor (1e), the sidebar (1g) and Settings (1i) read them without importing `models`.
- **Data is the CLI's own.** Fixtures are read in place from `lib/src/darwin/tests/**/Fixtures`. A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`. Never a shape made up for the screen ([ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md), "the binary's own").
- **Destructive steps ask first, with the CLI's own plan.** A delete shows what `models delete` prints without `--yes`. The second button ("…and the backends that use them") appears only when the plan names a backend. The confirm host is 1e's; this item supplies the five kinds' content.
- **The old prototype stays frozen** and is the reference. No new state is drawn here.

**Seam + files.**
- `presentation/models/` under `lib/design/prototype/`, laid out per 1b:
  - `ViewModels/`: `ModelsViewModel` (tabs, Backends, Roles, the pair-drafter and adapters sheets), `StoreViewModel` (Stored weights, selection, delete and register-all), `DownloadsViewModel` (Jobs: pull, convert, quantize, check), `StoreForms` (the four store sheets), `DeletePlan`, `BackendAdapters`.
  - `Views/`: the screen and its tabs, the store sheet, the download sheet, the adapters and pair sheets, the delete narration card.
  - `Previews/`: one named preview per visible state, over in-module fakes.
- The stand-in's preset table (1d): 9 entries in 1d's shape (rules, a route and a clock), each naming the recorded files behind it. Starting points from the Mac's previews: `backends-status.json`, `store-list-all.json`, `store-info-snapshot-converts.json`, `store-info-mlx.json`, `config-scan.txt`, `delete-plan-gemma.txt`, `quantize-list-before.json`, `models-status-kinds-registered.json`.
- The confirm host (1e): the five kinds' title, body, plan lines and buttons.
- Tests, mirrored under the prototype's test tree:
  - view-model tests for each intent: select, open a sheet, validate a form, run, stop, delete, register;
  - the per-preset test.
- Docs: the design's MILESTONES.md on ship.

**Reference.**
- The recon (2026-11-14): the prototype map §1 (sections 7–10, 11 and 27; markup blocks), §3 (the invented `jobs`, `baseModels`, `storeSets`), §4 (shared state) and §5 (presets); the darwin report §2 (this module in detail) and §4 (fixtures).
- The Mac module: `lib/src/darwin/source/presentation/models/`, and its tests and fixtures under `lib/src/darwin/tests/presentation/models/`.
- The boards: `lib/design/spec/html/ScreenModels*.dc.html` and `ScreenDownload.dc.html`.
- [ADR 0001 (GUI)](../../../adrs/gui/layer-enforcement.md), [ADR 0002 (MVVM)](../../../adrs/darwin/presentation-mvvm.md), [ADR 0004 (previews)](../../../adrs/darwin/module-schemes-and-previews.md); [the old design guide](../../../../design/spec/CLAUDE.md).
- Siblings: [`design-app-architecture-and-law.md`](design-app-architecture-and-law.md) (1a), [`cli-stand-in.md`](cli-stand-in.md) (1d), [`styles-shell-and-the-first-screen.md`](styles-shell-and-the-first-screen.md) (1e), [`sessions-inspector-execute-and-console.md`](sessions-inspector-execute-and-console.md) (1g, the sidebar's job rows), [`settings-and-appearance.md`](settings-and-appearance.md) (1i, the Models & roles and Backends & keys panes), [`parity-proof.md`](parity-proof.md) (1s).

**Decisions made** (dated):
- 2026-11-14: The user's call: the prototype is rebuilt in React, TypeScript and Vite, with the production GUIs' four layers and MVVM modules, so a new app ports from it. It builds to one self-contained `lib/design/main.html`. This is a design-queue item in v0.2.1.
- 2026-11-14: The user's call: parity screenshots are taken by Playwright in [1s](parity-proof.md), not here.
- 2026-11-14: The recorded default: the data is the Mac app's recorded fixtures, read in place; an unrecorded state gets a `*-handmade*` file in the CLI's shape. View-models are plain TypeScript.
- 2026-11-14: The recorded default: parity with the frozen old prototype is judged on layout, states, controls and wording, not pixels or data.
- 2026-11-14: The split: Models is item 1h, gated on 1e, one module per item.

**Open calls:**
- [default: the six view-model names are the Mac's. Roles stays inside `ModelsViewModel`, as on the Mac, rather than a seventh class.]
- [default: the jobs live in 1d's jobs service, as the Mac's `BusinessJobs`. `DownloadsViewModel` presents them. A finished job's effects (the set added, a backend registered, a toast) come from what the run itself reports (a pull's `document` events, a store job's printed sets), never from the clock.]
- [default: `download` opens the filled-in sheet, as the old prototype does; pressing Download replays `pull-chain-done-handmade.jsonl`, the only `models pull` stream. `models:downloads` (a Llama conversion with `--register-with Q4_K_M`) and `models:notran` (a quantization stopped during its first store read, GUI 6d) use recorded output where it exists. Where it doesn't, a new `*-handmade*` file is made in the CLI's shape and named in the record.]
- [default: the prototype's Models screen draws no "Apogee isn't installed" or "config could not be read" state. Its `apogee` prop (`missing`, `config-typo`) reaches Review, Library and Training, not Models. The Mac's Models does (`connect(binary: nil)` sets `.missing`), and `models-list-bad-config.err` is recorded. The port mirrors the prototype and names the gap. Drawing those states is new design, a follow-up item after this one.]
- (consumed decision: 1d) where new hand-made files live, and the preset type.
- (consumed decision: 1e) the confirm and sheet hosts, the toast, and routing.

**Guardrail(s).**
- **The per-preset test** (the successor of the old guide's 30-tick `node -e` check). For each of the 9 presets it builds the place over the stand-in and advances 30 ticks. It fails on a throw, or on `undefined` or `NaN` anywhere in a view-model's output. Proven on a planted fixture with a missing field.
- **View-model tests.** These run without React: tab switching, store selection, each sheet's validation (a taken backend name refuses), delete with and without the backends, register-all, Stop during the first read ends with nothing started.
- **1b's layering check stays green.** A planted `import … from 'react'` in a `models/ViewModels/` file fails it. So does a planted import of another place's module.
- **A fixture check.** Every file a models preset names exists, and none is a shape made up (each traces to a recorded file or a `*-handmade*` one).

**Acceptance criteria:**
- [ ] `lib/design/main.html?start=<preset>` opens each of the 9 presets on its tab, sheet or confirm, with the old prototype's regions, controls and wording.
- [ ] The four tabs, the four store sheets, the download sheet, the adapters and pair sheets, and the five confirms work live. A pull and a conversion run, finish, and land in Stored weights.
- [ ] Every value on screen comes from a recorded or `*-handmade*` fixture.
- [ ] The per-preset test, the view-model tests, the layering check and the staleness check pass locally.
- [ ] The record names every place the port follows the prototype where the Mac app differs.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on 1e. Out of scope:
- the confirm and sheet hosts and the toast (1e);
- the sidebar's job rows (1g);
- Settings' Models & roles and Backends & keys panes (1i);
- a missing-CLI state for Models (a follow-up);
- any change to the Mac app or its fixtures, beyond a new `*-handmade*` file where 1d puts it;
- parity screenshots (1s).
