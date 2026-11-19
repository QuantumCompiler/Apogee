# Styles, the shell, the composition root and the first screen

**What / why.** This is the first item that draws anything in the React app. It builds the three modules every screen sits on, `styles`, `shell` and `app`, and ports one small screen, the system monitor, end to end: tokens, view-model, view, fixture, preset, test and the built file. Measured on 2026-11-14:
- **The tokens have no single source.** `lib/design/spec/json/tokens.json` holds 25 colors in 3 themes (groups of 3, 3, 3, 9, 5 and 2), 14 type sizes, the fonts, 4 text-size factors, 9 radii, 13 spacing steps, 6 control heights, 18 layout sizes, 4 motion rules and the fixed values. No script generates it. Its colors are kept equal to `main.html`'s `themes()` and `colorGroups()` by hand: re-checked today, 0 of 75 values differ (only the letter case of the hex does). The Mac app copies the values by hand too, and lacks 2 colors and 3 type sizes.
- **The prototype's theme machinery** is small and portable: `rootVars()` writes 25 color variables, 14 `--t-*` sizes (times the text-size factor, rounded to 0.5 px), `--font-ui`, `--font-mono` and `color-scheme`; `lum()` and `ratio()` compute contrast; `withOverride()` keeps a theme's edits. The helmet adds 41 classes under `.ap`, breakpoints at 900 and 600 px, focus rings and a reduced-motion rule. The file reads 40 distinct variables 1,223 times.
- **The shell:** the sidebar (markup 81–170) lists 11 places, the same 11 as the Mac app's `Destination` (`Destination.swift:8-19`), on ⌘1–⌘0 and ⇧⌘R. Beside them sit the system strip (its summary is the monitor's numbers), the theme cycle, Settings, the toast (cleared after 12 ticks), the menu scrim, and the hosts the sheets (12 kinds) and confirms (15 kinds) open in.
- **The composition root:** the prototype has none apart from its data. The Mac app's `AppViewModel.swift` builds about 20 view-models from optional services (`:71-74`), wires the links between places with closures (`models.useInSession`, `:206`), calls each place's `appeared()` on a route change (`:227`) and hands every place the binary (`connectPlaces()`, `:347`).
- **The monitor is the right first screen:** 50 lines of `renderVals()` (section 1, 4507–4556) and 36 of markup (`ui.monitorOpen`, 2068–2103), one preset (`monitor`), and a Mac module of 4 files and 286 lines. Its data is invented: CPU and GPU from sine waves, a fixed 36 GB machine and 17.8 GB of "others". The CLI's `system` document gives CPU use, memory, Apogee's own bytes and held models, the GPU's name and model budget, and the store's size (`lib/documentation/reference/cli/machine-mode.md:1869-1878`). That read has no fixture file: it exists only as a Swift literal (`MonitorViewModelTests.swift:10`, copied in `MonitorPreviews.swift:12`). Item 1d extracts it to `tests/presentation/monitor/Fixtures/system-handmade.json`.

When this item ships, `lib/design/main.html` opens by double-click and shows the monitor, and new design is drawn here from then on.

**Core constraint(s).**
- **The layer law** ([ADR 0001](../../../adrs/gui/layer-enforcement.md)) **and MVVM** ([ADR 0002](../../../adrs/darwin/presentation-mvvm.md)), as the design's ADR 0001 (item 1a) restates them:
  - view-models are plain TypeScript classes that never import React;
  - `styles` depends on no sibling module;
  - the shell never imports a feature module; it takes a screen-builder function, as `ShellView.swift:13-25` does;
  - only `app` knows every module.
- **One source for tokens.** `tokens.json` is the only place a color, size, font, radius or motion value is written. Views use variables; a hex outside the fixed values (scrim, shadows, window controls) is refused.
- **Honest data** ([ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md) §3). Every number on the monitor comes from a document the stand-in (1d) serves in the CLI's shape. Nothing is synthesized.
- **Mirror the prototype.** It is the source of truth: the monitor keeps its regions, states, controls and words. Where the Mac app differs it is named, not followed.
- **It works from `file://`.** One self-contained file, with no server and no network: routing, fonts and data all work from disk.
- **The design's rules** carry over unchanged ([`lib/design/spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md#the-designs-rules)).

**Seam + files.** Paths are under `lib/design/prototype/` in 1b's layout; the Business-layer services are 1d's.
- `presentation/styles/`:
  - **Tokens:** a build step reads `tokens.json` and emits a typed tokens module and one CSS custom-property block per theme. Nothing generated is committed.
  - **Appearance:** theme choice (dark, light, contrast, automatic with its light-uses and dark-uses picks), text size, interface font, code font and per-theme overrides, as one plain value behind a small theme service; `rootVars(appearance)`, `lum`, `ratio` and `withOverride` as pure functions. This item builds the state, the service and its effect. The Settings pane that drives it is 1i's.
  - **Shared CSS:** the helmet's 41 classes, the breakpoints, the focus ring and the reduced-motion rule.
  - **Components:** only what two modules use (promote on second use). None is moved here ahead of need.
- `presentation/shell/`:
  - `ViewModels/`: `ShellViewModel` (route over the 11 places, open menu, overlays, toast and its expiry on the stand-in's clock), `Destination` (the 11, in `Destination.swift`'s order and names) and `KeyMap` (the design's key list as data, as `KeyMap.swift` holds it).
  - `Views/`: `ShellView` (sidebar, content, the 900 px collapse to 56 px), `Sidebar` (places with running badges, the system strip, theme cycle, Settings, and an empty slot for the session list), and the hosts: toast, sheet, confirm, menu scrim.
  - **Routing:** reads `start` and `still` from the query and from the hash (see Open calls).
- `presentation/app/`: `AppViewModel`, the composition root. It builds the stand-in's services once, builds each view-model, wires the cross-place closures (the monitor's "open the library" goes to Models' store tab) and calls `appeared()` on each route change. The entry file only mounts it.
- `presentation/monitor/`:
  - `ViewModels/MonitorViewModel`: `connect`, `opened`, `closed` and `read`, as the Mac app names them. It reads `system` through the stand-in every 2 s (5 ticks) while open and keeps the last 30 readings for the history bars.
  - `Views/MonitorView`, and a `Previews/` equivalent for the `monitor` preset and the stand-in's missing and config-typo modes.
- The `monitor` preset wired into the stand-in: the `system` fixture 1d extracts, a short run of readings for the history bars, and one failed read for the stale-GPU state.
- `lib/design/main.html`: rebuilt and committed.
- Docs on ship: `assistant/design/MILESTONES.md`, and `assistant/design/CLAUDE.md`'s "Where the design stands" (new design is drawn in the React app now).

**Reference.**
- The prototype, `lib/design/spec/html/main.html`. Line ranges are the recon's (working tree, 2026-11-14 morning, 7,604 lines). The file moves under other sessions (7,780 lines at writing; 7,300 at HEAD `48f46831`), so find each part by its marker. 1c's baseline fixes the numbering.
  - **The monitor:** section `/* ---------- monitor ---------- */` 4507–4556 (the same text at HEAD, 4314–4363); block `ui.monitorOpen` 2068–2103 (HEAD 2042–2077); preset `monitor` in `applyStart()`, which opens it over Sessions with the system strip open and the task-group answer at progress 30.
  - **The theme machinery:** `themes()` 3284, `colorGroups()` 3298, `lum()`/`ratio()` 3309–3322, `withOverride()` 3337, `rootVars()` 3347–3361 (at HEAD each is 118 lines earlier: `themes()` 3166, `rootVars()` 3229).
  - **The shell:** helmet 10–78; sidebar markup 81–170 and section 6 (5020–5103, places at 5052, `cycleTheme` 5099); section 28 `ui` (7564–7599); menu scrim 2066; confirm host 2478–2485; toast 2487.
- The Mac app: `presentation/monitor` (MonitorViewModel, MonitorView), `shell/ViewModels/{Destination,KeyMap,ShellViewModel}.swift`, `shell/Views/ShellView.swift`, `app/ViewModels/AppViewModel.swift`, `styles/{Palette,Typography,Metrics}.swift`.
- Siblings: [react-design-prototype.md](../../../adrs/design/react-design-prototype.md) (1a's ADR 0001), [scaffold-and-layering-check.md](scaffold-and-layering-check.md) (1b), [cli-stand-in.md](cli-stand-in.md) (1d), [settings-and-appearance.md](settings-and-appearance.md) (1i), [gallery-and-foundations.md](gallery-and-foundations.md) (1q), [parity-proof.md](parity-proof.md) (1s), [retire-the-old-prototype.md](retire-the-old-prototype.md) (1t).
- Cross-queue: the Apple apps' 10a ([design-the-server-window.md](../../darwin/macos/v0.2.4/design-the-server-window.md)) is to be gated on this item and drawn in the React app. That gate belongs in 10a and the darwin index; this item changes no gate.

**Decisions made** (dated):
- 2026-11-14: The user's call: the prototype is rebuilt in a compiled web framework with the production GUIs' four layers and MVVM modules, so it stays the design's cross-platform source of truth.
- 2026-11-14: The user's call: React, TypeScript and Vite.
- 2026-11-14: The user's call: the canvas becomes view-only and is retired at the end; the review surface is the built file itself.
- 2026-11-14: The user's call: the build is one self-contained HTML file that opens by double-click, with code, styles, data and fonts inlined.
- 2026-11-14: The user's call: the design has its own queue; this item is in release v0.2.1, track 1.
- 2026-11-14: The recorded default: `tokens.json` becomes the single source the prototype reads, and the colors, sizes and fonts are generated from it.
- 2026-11-14: The recorded default: data is the Mac app's recorded fixtures, read in place at build time; view-models are plain TypeScript that never imports React.
- 2026-11-14: The recorded default (the freeze rule, 1a): the old prototype is frozen from 1c's baseline and takes fixes only. From this item on, a new screen or state is drawn in the React app.

**Open calls:**
- [default: routing reads `start` and `still` from both the query (`?start=monitor&still=true`, as the old prototype's links do) and the hash (`#start=monitor&still=true`). Both work from `file://`.]
- [default: until 1f ports Sessions, opening the file with no `start` shows the monitor, so a double-click shows a working screen. 1f restores `chat` as the default, as the old `data-props` has it.]
- [default: a place not yet ported shows a plain placeholder naming the place and the item that ports it. Each feature item removes its own; 1s checks none is left.]
- [default: the monitor draws every region the prototype draws, from the `system` reading. A value that reading doesn't carry is drawn the honest way:
  - **GPU:** the name and model budget, not a percent. The CLI reports no GPU use, so there are no GPU history bars; the stale state stays, driven by a failed read.
  - **Store rows by format:** summed from the store listing the stand-in serves, when a recorded listing carries format and size; otherwise the single `disk.store_bytes` total.
  - **"Others":** memory used minus Apogee's own bytes and its children's.
  - 1s lists each as an intended difference. A wanted value the CLI lacks becomes a gap on the contract board, not a number made up here.]
- [default: Unload and Stop on a monitor row call a composition closure. Until Sessions (1f, 1g), Video (1k), Compose (1l) and Voice (1m) fill it, the closure raises the toast the prototype raises. The Mac monitor has no Unload: named, not followed.]
- [default: the stand-in's missing and config-typo modes give the monitor a failed state in the Mac app's words ("Apogee isn't installed, so the system can't be read."). The prototype draws none: an addition, listed by 1s.]
- [default: the key map is the design's list as data. The shell handles every key the browser passes to the page. A key the browser may keep (Chrome and Safari use ⌘1–⌘9 for tabs) stays in the labels; its test drives the key map, not a real keypress.]
- [default: fonts come from npm font packages installed with 1b's dependencies, cut to the weights the old fonts link loads, and inlined as woff2.]
- [default: generated hex values keep `tokens.json`'s letter case (upper); comparisons against the old prototype ignore case.]
- (consumed decision: 1b) the folder layout, the layering check and the local check runner; (consumed decision: 1d) the `system` fixture, the clock and the modes; (consumed decision: 1g) the session list in the sidebar and the console toggle; (consumed decision: 1i) the Appearance pane; (consumed decision: 1q) the foundations pages; (consumed decision: 1t) where `tokens.json` lives at the end, and `Palette.swift:39`'s citation of it.

**Guardrail(s).**
- **Token check:** the generated CSS equals `tokens.json`, value for value. Proven on a planted edit to the generated output.
- **Hex scan:** a planted `#fff` in a view fails; the fixed values are the only exceptions.
- **`rootVars` parity:** for 3 themes × 4 sizes × 3 interface fonts × 3 code fonts, the output equals the frozen old `rootVars()`. `ratio()` gives the old prototype's contrast for all 25 rows.
- **One-time proof:** `tokens.json`'s colors equal the frozen `themes()` and `colorGroups()` (75 values; groups 3, 3, 3, 9, 5, 2), recorded in MILESTONES.
- **Layering check (1b), on planted violations:** `react` imported in `monitor/ViewModels/`, `shell` importing `monitor`, and `styles` importing `shell` each fail with a sentence naming the file.
- **Routing tests:** query and hash forms, a `file://` URL, `still`, and an unknown preset.
- **Shell view-model tests:** each of the 11 places by its key, the toast clearing after 12 ticks, the scrim closing an open menu.
- **`MonitorViewModel` tests** as the Mac app's: reads while open, stops when closed, the failure words.
- **The `monitor` preset test** (the successor of the 30-tick check): no throw, and no `undefined` or `NaN` in what the view reads, in each of the 3 modes.
- **The staleness and single-file checks (1b)** pass on the rebuilt `lib/design/main.html`.

**Acceptance criteria:**
- [ ] Double-clicking `lib/design/main.html` opens it from disk and shows the monitor over the shell, with no console errors and no network request.
- [ ] `?start=monitor&still=true` and `#start=monitor&still=true` both show the frozen monitor; its numbers are the recorded `system` document's.
- [ ] Each of the 11 places opens by click; the unported ones show their placeholder.
- [ ] The sidebar's theme cycle re-colors the whole window in all 3 themes, and the 4 text sizes scale every `--t-*` size.
- [ ] No color, size or font is written anywhere but `tokens.json`; the token check and hex scan are green.
- [ ] Every local check is green: typecheck, unit tests, layering, staleness.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on [1d](cli-stand-in.md). Items 1f, 1h–1k and 1n–1r are gated on this item. Out of scope:
- every other screen (1f–1p), the session list and console (1g), the Settings › Appearance pane (1i);
- the gallery and foundations pages (1q), the parity screenshots (1s);
- any change to the old prototype beyond fixes, and any CI job.
