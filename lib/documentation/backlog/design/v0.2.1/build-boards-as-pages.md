# The build boards as pages in the React design app

**What / why.** The old prototype has three Build boards that are documents, not screens. They are written as hand-placed HTML, so every new row, size or count is a hand edit, and they have already drifted. This item turns them into pages of the React design app: their tables become typed data, their numbers come from the tokens, and their counts are computed. Measured on 2026-11-14 (the recon, re-checked against HEAD `48f46831` and the working tree):
- **BuildContract** (`html/BuildContract.dc.html`, "Build · how the app talks to Apogee", 1440 × 5600) is the most-edited design file: 16 commits.
  - **Size:** 115 lines and 73,239 bytes at HEAD; 118 lines and 76,905 bytes in the working tree. Whole sections sit on single lines: the contract map is two lines of 20,185 and 26,723 characters (working tree, `:73` and `:76`).
  - **Sections:** Starting up (5 steps), The contract part by part, Gaps to close in the CLI, Decisions the design makes (10), Calls for you (3). At HEAD they start at `:59`, `:70`, `:77`, `:93`, `:98`; in the working tree at `:59`, `:70`, `:80`, `:96`, `:101`.
  - **The contract map:** rows of THE DESIGN SHOWS · HOW THE APP GETS IT · STATUS, under 11 part labels (SESSIONS through SETTINGS AND SYSTEM). The count moves: 99 rows at HEAD, 101 in the recon's copy, 104 in the working tree. Each status is a chip: `ready`, `app`, `gap G<n>` or `verify`.
  - **The gaps:** 24 rows, G1–G24, each with the CLI change, its fallback and a "when": Closed 16, Later 4, Before v1 4.
  - **The 4 stat cards are typed by hand, and drift.** At HEAD they read 77 ready, 12 app, 8 gaps, 0 assumed, while the rows hold 79 ready, 12 app, 8 gap. The working tree's cards (83 / 13 / 8 / 0) happen to match its rows.
- **BuildLayout** (`html/BuildLayout.dc.html`, 108 lines, 22,662 bytes, 8 commits, 1440 × 3700). Seven sections: the window diagram at ¾ scale (`:46`), Regions and sizes (`:54`, 11 rows), Navigation and keys (`:61`, 13 rows), States every screen draws (`:66`, 6 rows), Native controls (`:73`, 16 rows), Type (`:81`, 14 sizes), Space, shape and motion (`:86`).
  - **Its numbers repeat the tokens by hand.** Sidebar 256, header 52, inspector 360, the detail panes 380 · 360 · 340 · 300, the list columns, the content widths, Settings 1080 × 780 and the dialogs are all `tokens.json` `layoutPx`. Spacing, control heights, radii and motion repeat `spacingPx`, `controlHeightPx`, `radiusPx` and `motion`. `spec/CLAUDE.md`'s "What a change touches" table asks for both to be edited together.
  - **Two widths are in no token:** Menus 380 · 340 (thinking, switch model). The toast is "bottom centre", with no number.
  - **The native-controls table** maps each piece of the design to SwiftUI, Qt Quick and WinUI 3. Its header reads `SWIFTUI · MACOS 14` (`:75`). The real floor is macOS 27: the Xcode project's `MACOSX_DEPLOYMENT_TARGET` is 27.0, and BuildContract's own "Calls for you" answers "macOS 27 or later" (recon G13).
- **BuildProcesses** (`html/BuildProcesses.dc.html`, 49 lines, 13,118 bytes, 1440 × 1040) is a diagram of boxes and connectors: the app, its `apogee` children, and the files it reads and writes.
  - **74 hard-coded hex values and 0 tokens.** They are 7 Graphite Dark values: `text.tertiary` 22, `text.primary` 20, `text.secondary` 13, `surface.card` 9, `line.strong` 8, `status.accent` 1, `surface.window` 1. It breaks the design's "every color is a token" rule, and draws dark whatever the theme.
- **Contract and Layout each carry their own copy of the theme table:** 75 hex values each, three themes on lines 15–17, pinned to dark (`class="ap t-dark"`).
- **Where they sit:** the canvas's Build page, row 0: Contract at x −3040, Layout at −1520, the App at 0, Processes at 1520 (`json/canvas.json`).

**Core constraint(s).**
- **A row is a data edit.** Adding a contract row, a gap, a decision or a native control changes one typed entry. No markup is copied.
- **Counts are computed, never typed.** The stat cards are counted from the rows' statuses and the gaps' "when".
- **One source per number.** Every size BuildLayout shows reads the tokens module 1e generates from `tokens.json`. Every key reads the shell's key map (1e), handed in by `app`.
- **The gallery's import rule holds** (1q): `gallery` imports `styles` and nothing else. It never imports `shell` or `app`; `app` hands the pages the key map, as it hands the gallery everything else it shows.
- **Every color is a token.** No hex in these pages. They follow the app's theme like every other page.
- **The words are kept.** The text of every row, gap, decision and call is ported as written at 1c's baseline. Only the macOS header changes.
- **The architecture holds.** The pages are views over plain TypeScript view-models that never import React. The data is plain typed modules with no React either.

**Seam + files.**
- `lib/design/prototype/`, the presentation `gallery` module (1a's module list; folder shape per 1b). 1b has it join with 1q; if this item builds first, it creates the module.
  - `Models/`: the typed data.
    - `contract.ts`: startup steps; contract rows `{part, shows, how, status, gap?}` with `status` one of `ready`, `app`, `gap`, `verify`; gaps `{id, title, change, fallback, when, note?}` with `when` one of `Closed`, `Later`, `Before v1`; decisions; calls.
    - `nativeControls.ts`: the 16 rows `{design, swiftui, qtQuick, winui}` and the column headers.
    - `layout.ts`: the Regions, keys and states rows. Each region names its token keys, never a number. Each key row names its key-map entries.
    - `processes.ts`: the diagram's boxes, their text and their connectors.
  - `ViewModels/`: `BuildContractViewModel` (groups rows by part, counts the stat cards, links each `gap` chip to its gap), `BuildLayoutViewModel` (reads the tokens, and the key map it is given), `BuildProcessesViewModel`.
  - `Views/`: the three pages and their row components. Inline code in a cell is written with backticks in the data and drawn by one shared span. No raw HTML strings.
  - `Previews/`: one named preview per page, in each theme.
- The tokens (1e): `tokens.json` `layoutPx` gains the two menu widths. The generated module and CSS follow.
- The app root (1e): the three pages get their own addresses, so they open before the gallery exists. `app` passes the key map in.
- Docs: design `DEVELOPER.md` (from 1b) gains the recipes "a part needs something from the CLI: add a row, and a gap if the CLI lacks it" and "a size or key: edit the token or the key map; the layout page follows". `MILESTONES.md` and `ROADMAP.md` on ship.

**Reference.**
- The boards: `lib/design/spec/html/BuildContract.dc.html`, `BuildLayout.dc.html`, `BuildProcesses.dc.html`, at 1c's frozen revision. The change table in [`spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md) ("What a change touches"), whose BuildContract and BuildLayout rows 1t rewrites to point here.
- The recon of 2026-11-14: boards §1 (the three rows), §2 (their React form), §3 (canvas row 0), §6 (the native-controls table and BuildLayout's sections), §7 (commits per file), critic G13.
- `tokens.json` (`layoutPx` 18 keys, `spacingPx`, `controlHeightPx`, `radiusPx`, `motion`, `type.scalePx`) and the Mac app's mirror of it in `lib/src/darwin/source/presentation/styles/Metrics.swift`.
- The law: [layer enforcement](../../../adrs/gui/layer-enforcement.md), [presentation MVVM](../../../adrs/darwin/presentation-mvvm.md), [module schemes and previews](../../../adrs/darwin/module-schemes-and-previews.md), [the darwin guide](../../../assistant/darwin/CLAUDE.md).
- Siblings: [`react-design-prototype.md`](../../../adrs/design/react-design-prototype.md) (1a's ADR 0001), [`DEVELOPER.md`](../../../assistant/design/DEVELOPER.md) (1b's scaffold, shipped), [`DEVELOPER.md` → the old prototype's baseline](../../../assistant/design/DEVELOPER.md#the-old-prototypes-baseline) (1c's baseline, shipped), [`DEVELOPER.md` → styles, the shell and the first screen](../../../assistant/design/DEVELOPER.md#styles-the-shell-and-the-first-screen) (1e), [`gallery-and-foundations.md`](gallery-and-foundations.md) (1q), [`parity-proof.md`](parity-proof.md) (1s), [`retire-the-old-prototype.md`](retire-the-old-prototype.md) (1t).
- Cross-queue: darwin 10a ([`design-the-server-window.md`](../../darwin/macos/v0.2.4/design-the-server-window.md), `:61-66`, `:197-198`) adds a SERVER WINDOW section, new gaps from G25 and decisions to the contract, and a second window to Layout and Processes. In the React app those are entries in this item's data.

**Decisions made** (dated):
- 2026-11-14: The user's call: rebuild the prototype in React, TypeScript and Vite, with the production apps' four layers and MVVM feature modules. The build is one self-contained `lib/design/main.html` that opens with a double-click. This is a design-queue item in v0.2.1.
- 2026-11-14: The user's call: the canvas becomes view-only and is retired at the end, so these pages, not the canvas boards, are where the build spec is read.
- 2026-11-14: The recorded default: the old prototype is frozen from 1c's baseline; the ported words are the baseline's.
- 2026-11-14: The recorded default: parity is judged on layout, states, controls and wording, not pixels.
- 2026-11-14: The split: the native-controls header reads `SWIFTUI · MACOS 27`, the real floor (recon G13).
- 2026-11-18: From design 1e (the monitor, [Milestone A](../../../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react)): six values the monitor wants that the CLI's `system` document does not carry. Each is drawn the honest way in the React app, and each becomes a gap on the rebuilt contract page (the frozen `BuildContract.dc.html` is not edited):
  - **The GPU's use.** `system` reports the GPU's name and its model budget, never a percent, so the monitor draws no GPU percent and no GPU history bars.
  - **What each held model takes.** `apogee.models_held` names the backends and gives no size, so a loaded model's row has no GB, and no weights-and-cache split or window.
  - **What Apogee's other processes hold.** `apogee.process_bytes` is the reading process's own footprint. A session's child and the media runs are not counted anywhere, so "other apps" is memory used minus that one figure, and Apogee's share reads a few MB.
  - **The store by format.** `disk.store_bytes` is one total. The store listing carries `bytes` on some rows only (`store-list.json`), so the monitor draws the one total, not the five format rows.
  - **The volume's name.** `disk` gives the volume's free and total bytes, not its name, so the footer reads "free on the store's volume" where the old one said "on Macintosh HD".
  - **Which models the sessions hold.** A one-shot `system` reports only its own process's `models_held`, which is always empty ("none by this process -- a session holds its own"), so the monitor cannot list the loaded models. It says "None by this process" in the CLI's words, never "No models loaded".

**Open calls:**
- [default: the data is converted once from the frozen boards by a throwaway script, then edited by hand as data. The script is not kept. The one-time proof (every row, gap, decision and call text-equal to the baseline's board) is recorded in MILESTONES.]
- [default: data as typed TypeScript modules, not JSON, so a misspelled status or `when` fails the typecheck.]
- [default: the two menu widths join `tokens.json` `layoutPx` as `menu.thinking` 380 and `menu.switchModel` 340. The toast stays words only.]
- [default: the Navigation and keys section keeps BuildLayout's 13 grouped rows and its wording. Each row names the key-map entries it shows, and the keys are drawn from the map.]
- [default: the pages follow the app's theme. Parity with the baseline is judged in dark, the boards' only theme.]
- [default: each page has its own address, `main.html?page=build:contract`, `build:layout` and `build:processes`. If 1q has landed first with another address form, its form wins.]
- [default: BuildProcesses keeps its wording, including "SwiftUI first", since it describes the Mac app the design is built for. Only its colors move to tokens.]
- [default: darwin 10a's contract, layout and processes additions land as entries in this item's data. 10a's gate stays 1e, as the split and 1a record; the order is kept by building this item before 10a reaches its Build steps, not by a new gate.]
- (consumed decision: 1q) the gallery frame, its Build page and row 0 (Contract, Layout, the App, Processes, in that order), and titles from `canvas.json`; (consumed decision: 1e) the tokens module and the key map; (consumed decision: 1s) the side-by-side judgement; (consumed decision: 1t) deleting the three boards and rewriting the change table.

**Guardrail(s).**
- **Counts:** a test that the stat cards equal the rows counted by status. A planted extra `ready` row raises the card by one with no other edit.
- **Gaps:** a test that every `gap` row names a gap that exists and is not Closed, and that every open gap is named by at least one row. Proven on a planted `gap G99` and a planted open gap no row names.
- **Sizes:** a test that every number the layout page shows comes from the tokens module. A planted change to `layoutPx.sidebar` changes the page's Sidebar row and the window diagram.
- **Keys:** a test that every key-map entry appears in a key row. A planted extra key in the map fails until a row names it.
- **The floor:** a test that the native-controls header's macOS version equals `MACOSX_DEPLOYMENT_TARGET` in `lib/src/darwin/Apogee.xcodeproj/project.pbxproj`, read only. A planted `14` fails.
- **Colors:** 1e's hex scan covers these pages; a planted `#938F86` in the processes view fails.
- **Layering (1b):** no React import in `Models/` or `ViewModels/`.

**Acceptance criteria:**
- [ ] The three pages open in `lib/design/main.html` at their addresses, and appear in the gallery's Build page row 0 beside the App.
- [ ] Every contract row, gap, decision, call and native control matches the 1c baseline's text; the stat cards show computed counts.
- [ ] The layout page's sizes, spacing, radii, motion, type sizes and keys come from the tokens and the key map; the window diagram is drawn from them at ¾ scale.
- [ ] The native-controls header reads `SWIFTUI · MACOS 27`.
- [ ] The processes diagram uses tokens only and follows the theme; no hex remains in the three pages.
- [ ] Adding a contract row is one data edit, shown in the design `DEVELOPER.md` recipe.
- [ ] The local checks are green: typecheck, unit tests, the layering check and the build staleness check.

**Scope note.** Earmarked for v0.2.1 (design track 1); its gate, 1e, shipped 2026-11-18 ([Milestone A](../../../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react)). Out of scope: the gallery frame and its other pages (1q), the color and type pages (1q), the side-by-side report (1s), deleting the old boards and rewriting `spec/CLAUDE.md`'s change table (1t), and any new row or gap: new design waits for the port, and 10a's server window is its own item.
