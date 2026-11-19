# Compose screens: compositions in the React app

**What / why.** Port the prototype's Compositions tab, Video's second tab, into the React app as its own `compose` module. Its four start presets then open from the built `lib/design/main.html` showing the same regions, states, controls and wording as the frozen old prototype. Measured 2026-11-14 at HEAD (`48f46831`, `lib/design/spec/html/main.html`):
- **The largest section.** `renderVals()`'s `video: compositions` is 6524–6885 (362 lines). It builds `comp` (6869) from three parts: `ncv` (the new composition), `pv` (a project's page) and `edv` (the edit pane).
- **Its markup.** The `vid.isCompose` block, 1737–1957 (221 lines), sits inside `ui.isVideo`. It holds the list, the new-composition pane, the project page (status, Stop and Resume, phase rows, the stop note, the player, the timeline, the scenes), and the edit pane.
- **Its helpers.** `defaultBrief`, `planFor`, `freshCompose`, `freshEdit`, `taskDur`, `renderTasks`, `editPlan` and `renderEdit` (4165–4281). `tick()` steps three of its states (2829–2860):
  - a new plan goes from planning to planned after 5 ticks;
  - an edit preview shows after 4 ticks;
  - a render advances scene by scene.
- **The line numbers are HEAD's.** The recon measured the working tree: 6819–7180 and 1763–1983. Uncommitted design work has since moved it to 6995–7356 and 1770–1990, same sizes. Find each block by its section comment; 1c's baseline fixes the numbers this item ports from.
- **Four presets** (`applyStart`, 2714–2726). Each has a board:

  | Preset | Board | What it shows |
  |---|---|---|
  | `compose` | `ScreenCompose` | "How a heat pump works", done, the player paused at 9.6 s |
  | `compose:edit` | `ScreenComposeEdit` | the same project with an edit typed ("make the second scene show the indoor unit on the wall"), scene 4 re-rolled, the preview shown |
  | `compose:new` | `ScreenComposeNew` | a new brief, planned, nothing rendered |
  | `compose:tide` | `ScreenComposeStopped` | "Tide pools for kids", stopped partway |

  The preset is `compose:tide`, not `compose:stopped`. Only its board is named Stopped.
- **The data is invented.** `projects` (2596–2626) holds 4 hand-written projects with 6, 4, 4 and 7 scenes, in no CLI shape (critic V3).
- **The Mac module.** `compose` is 5 files, 1,796 lines: `ComposeViewModel` 611, `ComposeView` 637, and `Previews/` 548. It depends on `styles` alone. The Mac app draws it as Video's second tab through a slot: `VideoScreen` takes a `compositions` view (`video/Views/VideoScreen.swift:12`), and the composition root fills it (`app/Views/ApogeeApp.swift:72-75`).
- **The recorded data.** There are 24 `compose-*` files in the Mac app's tests: 22 in `tests/business/media/Fixtures`, 2 in `tests/business/reads/Fixtures`. The recon counted 21.
  - The 8 recorded `video.project` documents are all `planned`, with 2 scenes each: a plan, an edit's parent, and 6 edit previews.
  - The 6 `.jsonl` files are job streams: two plans and four refusals.
  - **Nothing records a done, rendering or cancelled project.** The Mac app's previews make those states by hand, as Swift literals (`compose/Previews/ComposePreviews.swift:23`, `heatPumps`; `:34`, `tides(_:status:made:)`).
- **The CLI's words.** The project document's `status` is `rendering`, `done`, `failed`, `cancelled` or `planned` ([machine-mode.md](../../../reference/cli/machine-mode.md), the `video compose` section). The prototype's `rendered` is the CLI's `done`, and its `stopped` is the CLI's `cancelled`.

**Core constraint(s).**
- **The prototype is the source of truth.** The port mirrors what the old prototype shows and does. It takes the Mac module's shape and names where they exist. Where the two differ, the record names the difference; it never adds a state the prototype lacks.
- **Its own module.** `compose` never imports `video`. Video's view takes a slot for its second tab, and the app root fills it with the compose view, as the Mac app does.
- **The shared player is 1k's.** The player and the phase rows come from `styles` (1k's `MediaPlayer` and `PhaseRow`, from the prototype's `media: shared` section). Compose never copies them.
- **View-models are plain TypeScript.** They take their services in the constructor and never import React (the four layers, [layer-enforcement.md](../../../adrs/gui/layer-enforcement.md); [presentation-mvvm.md](../../../adrs/darwin/presentation-mvvm.md)).
- **The project document is the progress.** A render's progress is the project document re-read after each step, never the screen's own count (the Mac view-model's rule, `ComposeViewModel.swift:12-16`). In the stand-in, a render is a short series of project documents, one more scene made each step.
- **Every document is in the CLI's shape.** Recorded files are read in place. A state nobody recorded gets a hand-made file in the exact shape, named `*-handmade*`.
- **The old prototype stays frozen.** This item reads it; it changes nothing in `lib/design/spec`.

**Seam + files.** Paths are under `lib/design/prototype/`, in [1b](../../../assistant/design/DEVELOPER.md)'s layout: code in `source/`, tests in `tests/`.
- `source/presentation/compose/ViewModels/ComposeViewModel.ts`: the Mac names where they fit. That means `projects`, `selected`, `sheet` (new or edit), `select`, `newComposition`, `compose` (plan first, or render), `stop`, `resume`, `edit`, `previewEdit`, `renderEdit`. It also keeps the prototype's two refusals word for word: "One composition renders at a time. Stop the other first." and "…Wait for the other, or stop it."
- `source/presentation/compose/Views/`: the list, the new-composition pane (brief, voice, voice model, music, assets, captions note), the project page, the scenes and timeline, and the edit pane. `Components/` only when a piece is used twice.
- `source/presentation/compose/Previews/`: one named preview per preset, plus each state the view-model can reach (planning, rendering, previewing, preview failed, the edit pane locked).
- `source/business/media/` (1k's module): `Projects.ts` reads a `video.project` document into a project record, mirroring the Mac app's `business/media/Projects.swift` (`ProjectRecord`, `ComposeForm`).
- `source/presentation/app/`: the root passes the compose view into Video's second tab, replacing 1k's placeholder.
- The stand-in's preset table (1d): rules for the four presets.
  - `compose:new` uses `compose-planned-project.json` and `compose-planned.jsonl`.
  - `compose:edit` uses `compose-edit-parent.json` and `compose-edit-preview.json`.
  - The voice-model rows use `compose-voices-list.json`.
- Hand-made documents, in the stand-in's folder for them. They re-encode the prototype's projects in the CLI's shape:
  - a done heat-pump project;
  - the two night-market projects, one edited from the other (`parent`, `edits`);
  - the tide project, `cancelled` with its finished scenes' files filled in;
  - the render series.
- Tests: `tests/presentation/compose/`.

**Reference.**
- The prototype: `main.html` sections `video: compositions` and `media: shared`, the `vid.isCompose` markup, `applyStart`'s compose branch, and `tick()`'s plan, preview and render steps. Its guide is [`lib/design/spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md), whose 30-tick check the per-preset test succeeds.
- The Mac module: `lib/src/darwin/source/presentation/compose/` and its tests, `tests/presentation/compose/` (4 test files). Previews follow [module-schemes-and-previews.md](../../../adrs/darwin/module-schemes-and-previews.md); the Mac app's guide is [`assistant/darwin/CLAUDE.md`](../../../assistant/darwin/CLAUDE.md).
- The siblings: [video-and-runtime-consent.md](video-and-runtime-consent.md) (1k, the player and the Video screen), [cli-stand-in.md](cli-stand-in.md) (1d), [styles-shell-and-the-first-screen.md](styles-shell-and-the-first-screen.md) (1e), [voice-screen.md](voice-screen.md) (1m, the other 1k child), [parity-proof.md](parity-proof.md) (1s).

**Decisions made** (dated):
- 2026-11-14: The user's call: the prototype is rebuilt in React, TypeScript and Vite, with the production apps' four layers and screen modules, so a new app ports from it. It ships in v0.2.1, in the design's own queue.
- 2026-11-14: The user's call: the build is one self-contained `main.html` that opens by double-click. That file is the review surface; the canvas becomes view-only and is retired at the end (1t).
- 2026-11-14: The recorded default: one module at a time against the frozen old prototype. Parity is judged on layout, states, controls and wording, not pixels or data. The data is the Mac app's recorded fixtures read in place, with hand-made `*-handmade*` files only where nothing was recorded.
- 2026-11-14: The split: Compose is its own module, gated on 1k because it uses 1k's shared player and lives in 1k's Video screen.

**Open calls:**
- [default: the prototype's four projects are re-encoded as hand-made documents that keep their titles, briefs and scene counts, so the list reads like the old board. Recorded documents back the new and edit presets, so `compose:edit` shows the recorded parent and its real diff (`from`, `changes`, `removed`), not the heat-pump project.]
- [default: a render is a series of hand-made project documents (none made, one made, … all made, then `done`) that the stand-in serves on its clock. The screen re-reads them, as the Mac app does. The prototype's own task timer is not ported.]
- [default: the stopped project keeps the prototype's word "Stopped" on screen. Its document says `cancelled`, the CLI's status. The record names the difference for 1s.]
- [default: the Mac app's extra states are not drawn: a refused run with its install door, the "rendered otherwise" note, and the time limit on a preview. The prototype lacks them. Adding them is new design for a later item in this queue.]
- [default: the "passed over" rows (the prototype's two hard-coded files, 6880) come from a hand-made list the stand-in serves. The Mac app finds them by scanning folders on disk, which no CLI command records.]
- (consumed decision: 1k) the shared player and Video's compositions slot; (consumed decision: 1d) where hand-made files live and the preset table's form; (consumed decision: 1s) the side-by-side judgment.

**Guardrail(s).**
- **The per-preset test:** `compose`, `compose:edit`, `compose:new` and `compose:tide` each build at their preset and run 30 ticks of the stand-in's clock. Each walks the view state with no throw, no `undefined` and no `NaN`.
- **Behavior tests on the view-model:**
  - a plan goes from planning to planned and says "nothing is rendered yet";
  - a second render is refused in the prototype's words;
  - Stop keeps the finished scenes, and Resume renders only the rest;
  - stopping a preview says "Preview stopped: nothing was changed";
  - an edit form changed after its preview drops the preview.
- **A fixture-shape test:** every hand-made compose file reads through `Projects.ts`, carries `-handmade` in its name, and uses only the CLI's statuses. A planted file with `status: "stopped"` fails.
- **The layering check stays green.** Compose declares `styles` alone. A planted `import` of `video` from a compose file fails, naming the edge, and so does a planted `import React` in `ComposeViewModel.ts`.

**Acceptance criteria:**
- [ ] The built `lib/design/main.html` opens at `?start=compose`, `compose:edit`, `compose:new` and `compose:tide` (with `&still=true`), each showing its board's regions, states, controls and wording.
- [ ] Video's Clips and Compositions tabs switch between 1k's module and this one, the count on each tab read from its documents.
- [ ] Live, a new brief plans, a project renders scene by scene, stops and resumes, and an edit previews and renders as a new video.
- [ ] Every document behind the four presets is a recorded fixture or a `*-handmade*` file in the CLI's shape. The local checks are green: typecheck, unit tests, the layering check, and the built file not stale.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on 1k (the shared player and the Video screen), and so on 1e and 1d. Out of scope:
- the Clips tab and the generate form (1k), and the voice screen (1m);
- the Mac app's extra states (refusals with install, rendered otherwise);
- the gallery's compose rows (1q);
- the side-by-side parity report (1s);
- any change to the old prototype or to the Mac app's fixtures.
