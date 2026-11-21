# The gallery and the foundations

**What / why.** The canvas lays every board side by side for review. Once the canvas is retired (1t), the React app has to do that job itself. This item builds the gallery: a screen inside the app that reproduces the canvas's pages and rows, mounts the real app at each screen board's start preset, and shows the foundations generated from the tokens instead of copied by hand. Measured on 2026-11-14 (the recon, re-checked against the files):
- **The canvas index, `lib/design/spec/json/canvas.json`:**
  - 3 pages: Build, Foundations, Exploration. In the recon's copy, 74 boards: Build 63, Foundations 6, Exploration 5. The Build count moves with another session's work: 60 at HEAD, 65 in today's working tree (76 boards). 1c's baseline fixes it.
  - 8 section headings (`kind: "title1"`), each 300 above its row. Build has 5 ("Build — the contract, the measurements, and the app itself" through "Screens — settings"); Foundations and Exploration have 1 and 2.
  - Every board is 1440 wide. Heights run from 900 (every screen, App, Exploration) to 5600.
  - Rows sit at y 0, 1400, 2720, 4040 and 5360 on Build; the column step is 1520 (a 1440 frame plus an 80 gutter). Build's first row is Contract (x −3040), Layout (−1520), App (0), Processes (1520).
  - `launch` is `{view: "focused", file: "App.dc.html"}`. Only App and Appearance are `is_interactive`.
- **A Screen board is just a preset and a title.** Each of the 59 (61 in today's tree) is 26 lines; line 15 is its only varying line, `<dc-import name="App" start="…" still="{{ true }}">` (for example `ScreenModels.dc.html:15`, `start="models"`).
- **The canvas's titles are the curated ones.** 15 of the 73 board files carry a `<title>` that differs from `canvas.json` (17 differ in today's tree; 2 of those are only `&amp;`). Example: `Chat.dc.html` says "Apogee session · streaming", the canvas "Session · first merged pass".
- **The prop menus the canvas exposes:** ColorTokens and Appearance `chrome` (dark, light); TypeSpace `theme` (dark, light); the 4 Components boards and Chat `theme` (dark, light, contrast); Main and Trace `mode` (dark, light); Inspector `mode` (default light); App `start`, `still` and `apogee`.
- **The foundations are hand-copied and have drifted:**
  - `ColorTokens.dc.html` lists the 25 tokens with hex values and **hand-entered** contrast ratios (`:43`, for example `10.6:1`). Its own theme block defines only 13 of the 25 variables. Its `status.warning` row says "Context at 80% and above" (`:44`); `tokens.json` says "…, waiting for you".
  - `TypeSpace.dc.html:34` says "a 4 px spacing grid". `tokens.json` `spacingPx` starts at 2 (`[2, 4, 6, …, 32]`, 13 steps), as BuildLayout's Spacing row does.
  - The prototype already computes contrast: `lum()` and `ratio()` follow `colorGroups()` in `main.html` (3309–3322 in the recon's copy; 3191–3204 at HEAD).
- **The 4 Components boards are static copies** of prototype pieces (Model 234 lines, Turn 173, Prompts 187, Media 388), kept in step by hand (`spec/CLAUDE.md:31`). Their header counters disagree: Model says "1 OF 3", Media "4 OF 4".
- **Exploration** (Main, Inspector, Trace, Chat, Appearance) has had no content edit since the scaffold. `spec/CLAUDE.md:33`: "Leave them as they are."

**Core constraint(s).**
- **The gallery is the design's review tool, not a product screen.** It never appears inside the app's own frame, the shell's key map or Settings › Keyboard. A port to another framework skips it.
- **The real app, not copies.** A screen frame mounts the app root at its preset. A component specimen renders the feature module's own component. Nothing on a Foundations page is typed by hand that the tokens already hold.
- **The layering law** (1a's ADR). `gallery` is a Presentation module that imports `styles` and nothing else. It never imports `app`, `shell` or a place. `app` hands it what it shows, as it hands the shell each place's screen.
- **Still frames are still.** Every screen frame runs its preset with the clock frozen (1d's `still`); only the App board runs live.
- **The design's rules hold:** tokens only, real controls with labels, the gallery's own copy in plain words.

**Seam + files.** Under `lib/design/prototype/src/presentation/`:
- `gallery/Models/`: the board table, converted once from `canvas.json` and committed. Pages, the headings (text, row, page; 6 of the 8, Exploration's 2 left out), and one entry per board: `{id, start?, title, page, row, height, props?}`. The id is the old board's file name, so 1s pairs each entry with its baseline image. Screen entries name a start preset; App is the live entry. Titles are the canvas's.
- The logo's page (from the Apple apps' 11a, which added `html/Logo.dc.html` on 2026-11-18): a Foundations page with the artwork and its rounded icon form, from `lib/design/assets/Logo-320.png`, embedded by the build so `main.html` stays self-contained. `icon.sh` already regenerates that file, so the design app needs no script step.
- `gallery/ViewModels/`: `GalleryViewModel` (plain TypeScript, no React): the open page, the focused board, each board's prop values, and the address it reads and writes: `?gallery` (Build), `?gallery=foundations`, and `&board=<id>` for a focused board. It never uses `?page=`, which 1r's pages own.
- `gallery/Views/`:
  - the gallery itself: page tabs, headings, rows of 1440-wide frames scaled down to fit, laid out by row in table order;
  - a focused view (one board at full size, as the canvas's `focused` launch), with Esc back to the rows;
  - the per-board prop switcher: theme (dark, light, contrast) on every board, and the `apogee` mode (installed, missing, config-typo) on screen boards and App.
- `gallery/Views/` foundations pages, generated from 1e's tokens module:
  - **Color tokens:** the 25 tokens in their 6 groups (3, 3, 3, 9, 5, 2) × 3 themes, each with its label, what it is used for, the token it is measured against, and the ratio computed by the shared `lum()` / `ratio()` in `styles` (the same function 1i's Appearance badges use).
  - **Type, space, shape:** both typefaces and their alternates, the 14 sizes at S, M, L and XL, the 13 spacing steps, radii, control heights, the layout sizes, and the icon set (TypeSpace draws 21 icons; the page lists whatever `styles` holds).
- **Component specimens:** each feature module's `Previews/` exports its specimens (a name, a section, and a render over view-models built on the stand-in's fixtures). `app` gathers them and hands them to `gallery`, which lays them out under the 4 Components boards' sections: Model, context, sources; The turn; Asking and failing; Making media.
- `app/`: passes the gallery a `mount(start, props)` function, the preset list, and the specimens. It opens the gallery on `?gallery` or its key, outside the shell.
- Tests under `lib/design/prototype/tests/presentation/gallery/` (Guardrails).
- Docs: the `gallery` module's line on the Presentation layer card; the design's `DEVELOPER.md` (how a board or a specimen gets into the gallery); `MILESTONES.md` on ship.

**Reference.**
- The recon of 2026-11-14: the boards report §1 (inventory, drift), §2 (what each kind becomes), §3 (`canvas.json`), §4 (`tokens.json`); the prototype map §6 (`themes()`, `colorGroups()`, `rootVars()`).
- The old files: `lib/design/spec/json/canvas.json`, `json/tokens.json`, `html/ColorTokens.dc.html`, `html/TypeSpace.dc.html`, `html/Components*.dc.html`; [the old design guide](../../../../design/spec/CLAUDE.md) ("What a change touches", the hand-sync rules this item ends).
- The Mac app's styles: `lib/src/darwin/source/presentation/styles/` (`Palette.swift`, 23 tokens; `Typography.swift`, 11 sizes), which the generated pages make easy to compare.
- The law: [layer enforcement](../../../adrs/gui/layer-enforcement.md), [presentation MVVM](../../../adrs/darwin/presentation-mvvm.md), [module schemes and previews](../../../adrs/darwin/module-schemes-and-previews.md) (previews over recorded documents only).
- Siblings: [1a](../../../adrs/design/react-design-prototype.md) (the design's ADR 0001, which names `gallery` as the one Presentation module the Mac app lacks), [1c](../../../assistant/design/DEVELOPER.md#the-old-prototypes-baseline) (the baseline, Exploration's pictures among it; shipped), [1d](../../../assistant/design/DEVELOPER.md#the-cli-stand-in) (presets, `still`), [1e](../../../assistant/design/DEVELOPER.md#styles-the-shell-and-the-first-screen) (tokens, shell, routing), [1i](../../../assistant/design/DEVELOPER.md#settings) (the shared contrast math: `styles`' `ratio()` and `contrastBadge()`; shipped 2026-11-20), [1r](build-boards-as-pages.md) (the Contract, Layout and Processes pages), [1s](parity-proof.md), [1t](retire-the-old-prototype.md).

**Decisions made** (dated):
- 2026-11-14: The user's call: rebuild the prototype in React, TypeScript and Vite with the production GUIs' layers, built to one self-contained `lib/design/main.html`. A design-queue item in v0.2.1.
- 2026-11-14: The user's call: "Mostly review there; make the canvas view-only." The canvas cannot host the build, so review moves to the built file and the canvas retires (1t). This gallery is that review surface.
- 2026-11-14: The recorded default: the gallery is a screen inside the app reproducing the canvas's pages and rows; the foundations are generated from the tokens.
- 2026-11-14: The recorded default: parity is judged on layout, states, controls and wording, not pixels or data.
- 2026-11-14: The recorded default: the data is the Mac app's recorded CLI fixtures; a state nobody recorded gets a hand-made file in the CLI's shape, named `*-handmade*` (1d). So a specimen is built from fixtures, never from hand-written props.

**Open calls:**
- [default: the board table holds every Build and Foundations board from `canvas.json` from the start. An entry whose preset or specimen has not landed shows a plain card, "Not ported yet: item 1x". So feature items never edit the gallery; their frames go live when their presets register.]
- [default: every start preset gets a frame, including those with no old board (6 in the recon, 9 in today's tree, plus new ones such as 1i's pane presets). They go at the end of their row, titled in the canvas's "Place · state" form, marked as having no old board. 1s judges each against its old pair and settles it.]
- [default: Exploration is not ported. The gallery has two pages, Build and Foundations. The five boards stay in git history and in 1c's baseline pictures; 1t deletes the files.]
- [default: the Build row's Contract, Layout and Processes entries show 1r's pages when they land, and the "Not ported yet" card until then.]
- [default: frames mount when scrolled into view and unmount when far off screen, so 70-odd app roots never run at once.]
- [default: the gallery opens on `?gallery` and on one key, handled by `app` outside the shell's key map. The key is picked from those the browser leaves free, and a test fails if the shell ever binds it.]
- [default: the board table is converted from `canvas.json` once, not read at build time, since 1t removes that file. A test checks it against `canvas.json` while the old file exists.]
- (consumed decision: 1e) the tokens module, the theme service and the key map; (consumed decision: 1r) the Build pages; (consumed decision: 1s) which frames are judged against the baseline; (consumed decision: 1t) deleting the old boards.
- No [user] call.

**Guardrail(s).**
- **Table against canvas:** every Build and Foundations board in `canvas.json` has an entry, with the same page, row, height and title, and their 6 headings match. A planted missing board and a planted changed title each fail, naming the board.
- **Every preset has a frame:** each preset in the stand-in's table appears in exactly one entry. A planted new preset with no entry fails.
- **Still frames:** each screen frame, mounted for 30 ticks, renders the same output as at tick 0 (the clock is frozen), and no frame throws or shows `undefined` or `NaN`.
- **Contrast is computed:** the color page's ratios equal `ratio()` over `tokens.json`'s values for all 75 pairs. A planted override changes the shown ratio with no edit to the page. The old board's hand-entered ratios are compared once and any difference is listed in the record.
- **Drift fixed:** the type page's spacing starts at 2 px, read from the tokens; `status.warning`'s use reads "Context at 80% and above, waiting for you".
- **Layering (1b):** planted imports from `gallery` into `app`, `shell` or a place each fail the check. A planted React import in `gallery/ViewModels/` fails.
- **Key clash:** a planted shell binding of the gallery's key fails.

**Acceptance criteria:**
- [ ] `lib/design/main.html?gallery` opens on the Build page with the canvas's headings and rows; the Foundations tab shows the color, type, component and logo boards.
- [ ] Every screen frame shows the real app at its preset, frozen; the App board is live; clicking a frame opens it at full size.
- [ ] The prop switcher changes a board's theme and, on screens, the `apogee` mode.
- [ ] The color page shows 25 tokens × 3 themes with computed ratios; the type page shows sizes, spacing (from 2 px), radii, layout and icons, all read from the tokens.
- [ ] Each Components section shows the real components of every ported module, and a "Not ported yet" card for the rest.
- [ ] The local checks pass: typecheck, unit tests, the layering check, and the staleness check.

**Scope note.** Earmarked for v0.2.1 (design track 1); its gate, 1e, shipped 2026-11-18 ([Milestone A](../../../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react)). Best built late, since its frames and specimens fill in as 1u, 1v and 1g to 1p land; 1s waits on it. Out of scope: the Contract, Layout and Processes pages (1r), the side-by-side parity report (1s), deleting the old boards and the canvas (1t), the tokens module itself (1e), any new screen or state, and Exploration.
