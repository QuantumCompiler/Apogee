# Settings, including Appearance, in the React design app

**What / why.** This item ports the old prototype's Settings window into the React design app. That includes Appearance, whose choices restyle the whole app, and the Keyboard list. Measured on 2026-11-14 (the recon):
- **Its size in the old prototype** (`lib/design/spec/html/main.html`):
  - the `settings` section of `renderVals()`, 5782–6021: 240 lines building `set`, 118 fields;
  - the `ui.settingsOpen` markup block, 2118–2295: 178 lines;
  - inside it: `isAppearance` 2140–2181, `isBackends` 2205–2235, `isPermissions` 2237–2252, `isXcode` 2274–2289.
- **Line numbers are moving.** The ranges above are the recon's, on a 7,604-line working tree. Later the same day the file had 7,780 lines, with the section at 5928–6167 and the block at 2125–2302. HEAD's 7,300-line copy has them at 5512–5726 and 2092–2267. The builder re-reads every range against 1c's frozen baseline.
- **10 panes:** General, Appearance, Models & roles, Video & voice, Backends & keys, Permissions, Web access, Memory, Keyboard, Xcode (the `panes` list in the section).
- **9 start presets** (read back from `data-props` and `applyStart()`): `settings:general`, `:appearance`, `:media`, `:backends`, `:permissions`, `:xcode` have boards (`ScreenSettings*.dc.html`, the canvas's "Screens — settings" row). `settings:fixing`, `:fixfailed` and `:nosearch` have none. The `node -e` check from `spec/CLAUDE.md`, re-run on the working tree, gives no problems for all 9 at 30 ticks, nor for the 4 pane strings proposed below.
- **Panes with no preset of their own:** Models & roles, Memory, Keyboard, and Web access in its normal state (`nosearch` shows it with the search address unset). `applyStart()` already opens any pane by name (`settings:<pane>` falls through to `settingsPane: sub`).
- **The Mac app has fewer panes** (recon G12). Its `settings` module (7 files, 2,919 lines) has 6 panes: general, appearance, backends, media, permissions, xcode (`SettingsViewModel.swift:18–27`). It keeps some of the rest inside other panes: the Keyboard row in General (`SettingsView.swift:127`), allowed hosts and Recall in Permissions (`:440`, `:494`). Roles live in the Models tab.
- **Appearance is the prototype's alone.** The Mac's `ThemeSettings` (`SettingsViewModel.swift:760–822`) holds only the theme choice and the text size. The prototype adds:
  - per-element color overrides with hex editing, a contrast ratio and badge per row, and a per-row and per-theme reset;
  - the two font pickers, Interface and Code;
  - the Light uses / Dark uses mapping for Automatic.
- **The Keyboard list** is `set.keys`, 23 rows. Today a new key is a hand edit there (`spec/CLAUDE.md`, "What a change touches").
- **The data is invented** (critic V3). `provRows`, `healthDefs`, `xcodeRows` and the backend rows are written into the section by hand. The Mac app reads recorded documents for the same panes:
  - `xcode-*` (13 files), `providers-*` (19), `allowed-host*` (6) and `chats-recall*` (11), all under `lib/src/darwin/tests/business/reads/Fixtures/`.
  - Three reads have no file: `config get permissions` (a Swift literal, `settings/Previews/SettingsPreviews.swift:179`, and twice in `SettingsViewModelTests.swift`, `:36` and `:303`), `config get tools.search.url` (an inline `"\n"`, `SettingsPreviews.swift:42`), and `check --fix`'s run.

**Core constraint(s).**
- **The prototype is the source of truth.** All 10 panes and every Appearance control stay, even where the Mac app has fewer. Where the Mac app differs, the view-model names the difference in a comment.
- **Appearance drives styles; it does not own them.** The theme choice, the auto mapping, the overrides, both fonts and the text size live in 1e's styles module. This item's view-model only reads and changes them through an injected service. A change applies to the whole app at once, as `rootVars()` does today.
- **View-models are plain TypeScript.** They never import React. They take their services through the constructor, as the Mac's `SettingsViewModel` does.
- **A key is never kept.** Save key hands the draft to the stand-in's `auth add` and clears it. No state field holds it after that. This is the Mac's secrets law (`SettingsViewModel.swift`, header) and the prototype's copy ("nothing you type here is stored or sent").
- **The design's rules hold:** CLI words, tokens only, real controls with labels, contrast measured against what each element sits on.

**Seam + files.**
- `lib/design/prototype/`, the presentation `settings` module (folder shape per 1a and 1b):
  - `ViewModels/`: `SettingsViewModel` (the pane list and the plain panes), `ThemeSettings` (Appearance) and `XcodeSettings` (Xcode), named after the Mac's three. Prototype-only panes (Web access, Memory, Keyboard) sit in `SettingsViewModel` unless one grows big enough to stand alone.
  - `Views/`: the Settings window, one view per pane, the Register… sheet (`gs.kind` `registerProvider`), and the confirms the panes raise (`forget`, `xcodeRemove`).
  - `Previews/`: the module's named previews, one per preset and per pane, over the stand-in's fixtures.
- The styles module (1e): the theme service this item drives. Contrast math (`lum()` / `ratio()`, `main.html` after `colorGroups()`) lives there once and is shared with 1q's color page.
- The shell (1e): Settings opens as a window over the app, on ⌘,. The Keyboard pane reads the shell's key map, not a copy.
- The app root: fills the Models & roles pane with 1h's roles view through a slot, so `settings` never imports `models`.
- The CLI stand-in (1d): a fixture set per preset, read in place from the Mac app's `Fixtures/`. Missing reads get hand-made files in the CLI's exact shape, named `*-handmade*`.
- Tests: one view-model test per preset, and the behavior tests listed under Guardrails.

**Reference.**
- The old prototype: `main.html` settings section and `ui.settingsOpen` block (ranges above); `themes()`, `colorGroups()`, `withOverride()` and `rootVars()` (3284–3361 in the recon's copy); [`spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md).
- The Mac module: `lib/src/darwin/source/presentation/settings/` (7 files, 2,919 lines) and its 8 test files under `tests/presentation/settings/`.
- The law: [layer enforcement](../../../adrs/gui/layer-enforcement.md), [presentation MVVM](../../../adrs/darwin/presentation-mvvm.md), [module schemes and previews](../../../adrs/darwin/module-schemes-and-previews.md), [the darwin guide](../../../assistant/darwin/CLAUDE.md).
- Siblings: [`design-app-architecture-and-law.md`](design-app-architecture-and-law.md) (1a), [`cli-stand-in.md`](cli-stand-in.md) (1d), [`styles-shell-and-the-first-screen.md`](styles-shell-and-the-first-screen.md) (1e), [`models-screens.md`](models-screens.md) (1h), [`gallery-and-foundations.md`](gallery-and-foundations.md) (1q), [`parity-proof.md`](parity-proof.md) (1s).

**Decisions made** (dated):
- 2026-11-14: The user's call: rebuild the prototype in React, TypeScript and Vite, with the production apps' four layers and MVVM feature modules. The build is one self-contained `lib/design/main.html` that opens with a double-click. This is a design-queue item in v0.2.1.
- 2026-11-14: The user's call: the prototype stays the design's source of truth. So its behavior is ported, not the Mac app's (recon G12): all 10 panes and the full Appearance pane stay.
- 2026-11-14: The recorded default: data comes from the Mac app's recorded fixtures, read in place. A state nobody recorded gets a `*-handmade*` file in the CLI's shape. Parity is judged on layout, states, controls and wording, not on pixels or data.
- 2026-11-14: The split: Settings is item 1i, gated on 1e. Appearance's state lives in 1e's styles module; this item drives it.

**Open calls:**
- [default: the 3 reads with no file follow 1d's rules.
  - `config get permissions` is a Swift literal, so it is extracted to `tests/presentation/settings/Fixtures/`, named `*-handmade*`. The Swift tests and the preview read that file and behave the same.
  - `config get tools.search.url` (set and unset) and a `check --fix` run (ending well, and failing, for `fixing` and `fixfailed`) are recorded with the repo's capture scripts in a throwaway `APOGEE_HOME`, never `~/.apogee` or `~/.apogee-dev`. A run that cannot be recorded safely there gets a hand-made file in the CLI's shape, named `*-handmade*`.]
- [default: add presets for the panes that have none: `settings:roles`, `settings:web`, `settings:memory`, `settings:keyboard`. The old prototype already opens each by the same start string, so they can be compared against it.]
- [default: Automatic follows the viewer's light or dark setting, and its line says which it is now. Every preset pins it to dark, as the old prototype always behaves (`effTheme()`), so frames match the baseline.]
- [default: nothing persists across a reload, as in the old prototype. The copy about where a theme is saved stays as written.]
- [default: the Keyboard pane lists the shell's key map. The old prototype's 23 rows are checked against it once, and any row missing from the map is added to the map.]
- (consumed decision: 1d) where an extracted literal goes and how a new state is recorded; (consumed decision: 1e) the Appearance state's shape and the key map; (consumed decision: 1h) the roles view the Models & roles pane shows; (consumed decision: 1s) how the 4 new pane presets get their old-prototype frames.

**Guardrail(s).**
- A per-preset test for all 9 presets plus the 4 new ones: the view-model builds, no read throws, and no field reads `undefined` or `NaN` (the successor of the 30-tick `node -e` check).
- Appearance tests, driven through the pane's view-model:
  - an override changes the matching CSS variable for the whole app;
  - a value equal to the theme's own drops the override (`withOverride()`);
  - per-row reset and Reset all;
  - the contrast badge marks a planted low pair "too low" (under 3:1) and "low" (under 4.5:1);
  - each of the 4 text sizes scales the type by the prototype's factors (12/13, 1, 14/13, 15/13), rounded to 0.5 px; at Medium, prose is 14 px, body 13 and dense 12.
- A test that a saved key is in no state field afterwards (a planted key string is searched for in the whole view-model).
- A test that every key in the shell's map shows in Settings › Keyboard. A planted extra key appears with no edit to `settings`.
- The layering check (1b) passes: no React import in `ViewModels/`, and no `settings` import of `models`.

**Acceptance criteria:**
- [ ] `lib/design/main.html?start=settings:<preset>` opens each of the 9 presets, and each matches the 1c baseline on layout, states, controls and wording. The 4 new pane presets open their panes.
- [ ] All 10 panes open from the Settings sidebar, and ⌘, opens the window.
- [ ] Changing a color, a font or the text size in Appearance restyles every screen at once, and resets put it back.
- [ ] The Keyboard pane lists every key the shell binds.
- [ ] Every preset's data comes from a recorded fixture or a `*-handmade*` file; no settings data is written into a view or view-model.
- [ ] The local checks are green: typecheck, unit tests, the layering check, and the build staleness check.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on 1e. Out of scope: the Roles view itself (1h), the theme service and key map (1e), the color and type pages (1q), the side-by-side report (1s), and any change to the Mac app's Settings. New panes or states wait until the port is done.
