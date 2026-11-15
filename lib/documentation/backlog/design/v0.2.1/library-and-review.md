# Library and Review in the React prototype

**What / why.** This item ports two places of the old prototype into the React app: the **Library** (agents, symphonies, MCP servers) and **Review** (merge-request reviews, sweep targets, the review skills). They are small, self-contained screens. They share one sheet host (the editor and the add-target sheet), so they port together. Measured on 2026-11-14 (the recon):
- **Library** in `lib/design/spec/html/main.html`:
  - the `library` section of `renderVals()`, 6193–6251 (59 lines), which builds `lb` (6217);
  - the `ui.isLibrary` markup, 1148–1210 (63 lines), with the symphony page at `syShown` 1175–1194;
  - three tabs, agents, symphonies and mcp, the same three as the Mac app (`LibraryScreen.swift:12-14`).
- **Review**:
  - the `review` section, 6140–6166 (27 lines), which builds `rv` (6145);
  - the `ui.isReview` markup, 1212–1277 (66 lines), with its tabs `runsOn`, `targetsOn` and `skillsOn` (1224–1276);
  - the post sheet, `ui.reviewPostOpen`, 2318–2327 (10 lines).
- **Shared parts:**
  - two kinds from the `sheets` section (7300–7563): `editor` and `target`;
  - two kinds from the `confirm` section (5625–5781): `editorClose` and `mcpRemove`;
  - one step of `tick()`: the editor's save, which lands after 4 ticks (recon §4, step 15).
- **Presets (10):**
  - `library`, `library:symphonies`, `library:mcp`, `library:refused`, `library:image` (5 boards), plus `library:unsaved`, which has no board;
  - `review`, `review:post`, `review:targets`, `review:skills` (4 boards).
- **The Mac modules this mirrors:**
  - `library`, 5 files and 2,142 lines: `LibraryViewModels.swift` 739, `LibraryScreen.swift` 653, and `Previews/` 750;
  - `review`, 5 files and 1,423 lines: `ReviewViewModel.swift` 427, `ReviewScreen.swift` 771, and `Previews/` 225.
- **The data is close to the CLI's already.**
  - Review's words are the recordings': the finding, the counts and the merge request come from `review-run-dry.json`, the two targets from `review-targets.json`, and the last sweep from `review-sweep.json`.
  - Library's lists are shortened copies. The prototype lists 4 agents where `agents-list.json` holds 8, and 6 symphonies where `symphonies-list.json` holds 9.
  - So the port shows more rows, and parity here is about structure (critic V3).
  - 26 recorded files under `lib/src/darwin/tests/business/reads/Fixtures/` cover these screens: `agents-*`, `symphon*`, `mcp-*` and `review-*`.

Line numbers are the recon's (the working tree of 2026-11-14), and the file keeps moving under other design work. At HEAD (`48f46831`) the same blocks sit at `review` 5845–5871, `library` 5898–5956, `confirm` 5362–5511, `sheets` 6998–7259, `ui.isLibrary` 1122–1184, `ui.isReview` 1186–1251 and `ui.reviewPostOpen` 2290–2299. The section markers (`/* ---------- library ---------- */`) are the stable anchors, and 1c's baseline fixes the lines.

**Core constraint(s).**
- **The old prototype is the source of truth.** The port copies its behavior: the regions, tabs, controls, states and wording of the 10 presets. Where the Mac app does more, this document names the difference and leaves it alone (see Open calls).
- **The Mac module's shape and names.** Views, ViewModels and Previews per module (`adrs/darwin/presentation-mvvm.md`). View-models are plain TypeScript classes that never import React. They get their services injected: the stand-in's command runner from 1d, never a fixture path.
- **Every string comes from a recording or from the prototype's own words** (`adrs/darwin/module-schemes-and-previews.md:46`: never shapes made up). A file nobody recorded is hand-made in the CLI's exact shape and named `*-handmade*`.
- **The old prototype stays frozen** from 1c's baseline. This item reads it and never edits it.
- **The design's rules** ([`spec/CLAUDE.md` → The design's rules](../../../../design/spec/CLAUDE.md#the-designs-rules)): every color is a token, every control is reachable from the keyboard, and the copy is plain.

**Seam + files.** All paths are under the prototype's presentation layer, as 1b lays it out.
- `library/ViewModels/`, mirroring `LibraryViewModels.swift`:
  - `AgentsViewModel` (`:217`), `SymphoniesViewModel` (`:383`) and `MCPViewModel` (`:609`);
  - `DefinitionEditor` (`:11`), with its close reasons `unsaved` and `refusedOnDisk`;
  - `LibraryListing` (`:175`), with the states not read, read, missing and failed.
- `library/Views/`: `LibraryScreen` (the tab bar, a 300 px list, the page) and the editor sheet's body.
- `review/ViewModels/`: `ReviewViewModel`, with `Tab` (`runs`, `targets`, `skills`), `TargetForm`, `Pending` and `Listing`. These mirror `ReviewViewModel.swift:19-140`.
- `review/Views/`: `ReviewScreen`, the post sheet and the add-target sheet's body. The target sheet builds `apogee review targets add …` live from its fields, with the lenses sorted, as the prototype's `target` kind does.
- `library/Previews/` and `review/Previews/`: one named preview per visible state. Each is built through the view-models' public API over the stand-in, in the form 1a's ADR sets.
- **The stand-in's presets** (1d's preset type), each with the files it reads:

  | Preset | Recorded files |
  |---|---|
  | `library` | `agents-list.json`, plus the triage prompt (hand-made, below) |
  | `library:symphonies` | `symphonies-list.json`, `symphony-play.json` (the played state) |
  | `library:mcp` | `mcp-list.json`, `mcp-presets.json` |
  | `library:unsaved` | `agents-list.json`, the triage prompt with one typed line, then `editorClose` (unsaved) |
  | `library:refused` | `symphonies-list.json`, `symphony-show.json` (notes-digest), its spec (hand-made), `symphony-show-unknown-key.err`, then `editorClose` (refusedOnDisk) |
  | `library:image` | `symphonies-list.json` (`describe-answer` takes an image) |
  | `review` | `review-run-dry.json` |
  | `review:post` | `review-run-dry.json`, with the post sheet open |
  | `review:targets` | `review-targets.json`, `review-sweep.json` |
  | `review:skills` | `review-skills.json`, `review-skill-show.json` (security), `review-skill-show-ai-ml.json` |

- **The CLI-missing and config-typo modes** read `agents-list-bad-config.err`, `symphonies-list-bad-config.err`, `mcp-list-bad-config.err` and `review-targets-bad-config.err`.
- **Hand-made files**, in the stand-in's own fixture folder:
  - the triage prompt;
  - notes-digest's spec;
  - the five skill shows nobody recorded (see Open calls).
- **Wiring:** both modules register with 1e's composition root and shell (Library ⌘0, Review ⇧⌘R). Their sheets and confirms render through 1e's sheet host and confirm host.

**Reference.**
- **The Mac app's work on these screens** ([darwin MILESTONES](../../../assistant/darwin/MILESTONES.md)): 4h (Library and Review do what their buttons say), 4k (an MCP server's removal, asked first) and 6f (the editor keeps edits, and Play gives an image to a symphony that takes one).
- **The Mac previews** to match state for state: `LibraryScreen.swift:458-652` and `ReviewScreen.swift:520-770`. Their builders `LibraryPreview.agentEditor(typed:saved:saving:closing:)` and `refusedSymphony(closing:)` are in `LibraryPreviews.swift`.
- **The Mac tests** whose cases the TS tests mirror: `tests/presentation/library/LibraryEditorCloseTests.swift`, `MCPRemoveTests.swift`, and `tests/presentation/review/ReviewViewModelTests.swift`.
- **The law:** [`adrs/gui/layer-enforcement.md`](../../../adrs/gui/layer-enforcement.md), [`adrs/darwin/presentation-mvvm.md`](../../../adrs/darwin/presentation-mvvm.md) and [`adrs/darwin/module-schemes-and-previews.md`](../../../adrs/darwin/module-schemes-and-previews.md).
- **The old check this item's tests replace:** [`spec/CLAUDE.md` → Checking a change](../../../../design/spec/CLAUDE.md#checking-a-change), which runs every start preset for 30 ticks.
- **Siblings:** [`cli-stand-in.md`](cli-stand-in.md) (1d), [`styles-shell-and-the-first-screen.md`](styles-shell-and-the-first-screen.md) (1e), [`gallery-and-foundations.md`](gallery-and-foundations.md) (1q) and [`parity-proof.md`](parity-proof.md) (1s).

**Decisions made** (dated):
- 2026-11-14: The user's call. The prototype is rebuilt in React, TypeScript and Vite, with the production GUIs' four layers and MVVM screen modules. It stays the design's cross-platform source of truth.
- 2026-11-14: The user's call. The build is one self-contained `lib/design/main.html` that opens with a double-click. The design has its own queue, and this is v0.2.1.
- 2026-11-14: The user's call. The canvas becomes view-only and is retired at the end (1t); review happens in the built file. Parity screenshots are Playwright's, in 1s.
- 2026-11-14: The recorded default. Data is the Mac app's 613 recorded fixture files, read in place at build time. A state nobody recorded gets a `*-handmade*` file in the CLI's shape. View-models are plain TypeScript classes that never import React.
- 2026-11-14: The recorded default. Each module is ported against the frozen old prototype. Parity is judged on layout, states, controls and wording, not on pixels or data.
- 2026-11-14: Library and Review are one item: two modules sharing the sheet host, 10 presets (the split, track 1).

**Open calls:**
- [default: **dead buttons stay dead.** Some of the prototype's controls only raise a toast or do nothing: New agent…, Delete…, Disable, the Reviews tab's Review button, Sweep…, and a target's Remove…. Each becomes a view-model intent that does exactly that, toast word for word. The Mac app does more:
  - an agent's create form and delete;
  - an MCP server's enable, disable and register;
  - a review run, its posting, sweeps and target removal (`ReviewViewModel` `Pending`, `Sheet`).

  Drawing those flows is new design, after this item.]
- [default: the post sheet's Post closes the sheet, as the prototype's does. The posted run (`review-run-posted.json`) and the refused run (`review-run-refused.json`) are recorded but not drawn. New states wait for design after the port.]
- [default: rows pick as the prototype's do. Symphony rows pick; agent and MCP rows have an empty `pick`, and the first row stays current, so the agents tab always shows `triage`'s page. The Mac app picks on every tab; drawing other agents' pages is new design, after this item.]
- [default: two files exist only as Swift string literals (recon G5). They are copied into hand-made files, each citing the lines it came from:
  - the triage prompt the binary scaffolds (`LibraryPreviews.swift:34-42`);
  - notes-digest's spec (`:93-105`).

  The refused text is made in code by adding the `temprature` line, as both prototypes do.]
- [default: the skills tab shows a prompt for each of the 7 skills, and only `security` and `ai-ml` have a recorded `review skills show`. The other five are hand-made in `review-skill-show.json`'s exact shape, with the prompt text from the prototype's `reviewSkills()`. A recording replaces each one when Taylor next captures.]
- (consumed decision: 1d) the preset type, the clock, the `apogee` modes, and where hand-made files live.
- (consumed decision: 1e) the sheet host, the confirm host, the shell's places and keys.
- (consumed decision: 1q) the gallery rows for these 9 boards, and whether `library:unsaved` gets one.
- (consumed decision: 1s) the side-by-side parity screenshots.

**Guardrail(s).**
- **A per-preset test** for each of the 10 presets, the successor of the old 30-tick check. It builds the view-models from the preset's files, advances 30 ticks, and walks every value the views read. It fails on a throw, an `undefined` or a `NaN`. A planted preset that names a missing file fails and names that file.
- **View-model tests**, mirroring the Mac cases:
  - Close with unsaved typing asks "Discard your changes to …?";
  - Close after a refused save offers put it back, leave it, or keep editing;
  - Close during a save, or with nothing to lose, closes at once;
  - the save is read-only until its verdict, 4 ticks later;
  - the image row's two states.
- **Review tests:**
  - the target sheet's command line is built from its fields (a planted unsorted lens list comes out sorted);
  - Add stays off until a name and a repository or owner are given;
  - the tab switches;
  - the missing and config-typo notes on both screens.
- **The layering check (1b) stays green.** A planted `import React` in `library/ViewModels/` fails it.

**Acceptance criteria:**
- [ ] Each of the 10 presets opens in the built `lib/design/main.html`. Each shows the same regions, tabs, controls, states and wording as the same preset in the old prototype, apart from listed data differences such as the longer lists (the user's look).
- [ ] The editor flows behave as the prototype's: asked over unsaved typing, three choices after a refused save, and a 4-tick save before the verdict.
- [ ] The add-target sheet shows its `apogee review targets add …` line live, and Add puts a row on the targets tab.
- [ ] The CLI-missing and config-typo modes show the recorded notes on both screens.
- [ ] Every preset's state has a named preview. The local checks pass: typecheck, unit tests, the layering check, and the staleness check.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on [1e](styles-shell-and-the-first-screen.md). Out of scope: Knowledge and Graph (1o); the sheet and confirm hosts themselves (1e); the parity screenshots (1s) and the gallery entries (1q); any new state or flow the Mac app has and the prototype lacks; any change to the old prototype, the Mac app or its recorded fixtures.
