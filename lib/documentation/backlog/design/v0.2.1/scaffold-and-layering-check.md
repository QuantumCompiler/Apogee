# Scaffold the React app and its layering check

**What / why.** Item 1b of design track 1 lays down the empty React app that every later item builds into. Its checks hold the app's law from the first commit. Measured 2026-11-14:
- **The repo has no JavaScript tooling.** No `package.json`, lock file, `tsconfig`, `vite.config` or `.nvmrc` is tracked. The only `.gitignore`, at the repo root, has no `node_modules/` or `dist/` line. The tracked JS and TS is `lib/design/spec/html/support.js` (469 lines) plus 8 CLI test files.
- **The toolchain on this Mac:** Node v26.8.1 and npm 11.19.0. pnpm is absent.
- **The Mac app's law is checked by a test, not just trusted.** Its layering check, `lib/src/darwin/tests/architecture/layering/`, is 1,229 lines of Swift in 4 files:
  - a module map read from the Xcode project (`ModuleMap.swift`, 250 lines);
  - the source tree on disk or planted (`SourceTree.swift`, 57);
  - 12 rules, each failing with one readable sentence (`LayeringCheck.swift`, 474);
  - 25 `@Test`s (`LayeringTests.swift`, 448). One runs on the real project (:14-19). The other 24 each plant one violation in a miniature project and expect its exact sentence.
- **CI never reads the design** (`lib/design/spec/README.md:5`). The CLI keeps its lint as a local gate because "CI builds the executable for each platform and nothing else" (`assistant/cli/CLAUDE.md:230`).

The new prototype is only a good source for the next app if its layers are as real as the Mac app's. So 1b lays down the folders, the one-file build and a TypeScript copy of the Swift layering check, all behind one `npm run check`. It ports no screen. The app it builds is an empty window that opens with a double-click.

**Core constraint(s).**
- **The law is 1a's ADR** ([`lib/documentation/adrs/design/react-design-prototype.md`](../../../adrs/design/react-design-prototype.md), written by 1a, shipped 2026-11-18), which mirrors the GUI's four layers ([ADR 0001](../../../adrs/gui/layer-enforcement.md)) and the Mac app's MVVM modules ([ADR 0002](../../../adrs/darwin/presentation-mvvm.md)). The check enforces that ADR. It does not invent rules of its own.
- **One file, opened from disk.** The build writes one self-contained `lib/design/main.html`, with its code and styles inlined. Browsers block a `file://` page's separate scripts, so nothing in it may load another file.
- **The build writes only `lib/design/main.html`.** It never touches `lib/design/spec/`, which stays the old prototype until 1t retires it.
- **Checks run locally, before a commit.** `changed.sh`, `.github/workflows/` and the required checks are untouched, and the README's "CI never reads it" stays true.
- **No downloads without consent.** `npm ci` fetches the packages. Playwright's browser is a one-time download of about 150 MB, approved by the user when the item builds, before it runs.
- **The prototype never runs `apogee`.** Its data is recorded (1d), so it needs no binary and never touches `~/.apogee` or `~/.apogee-dev`.

**Seam + files.** Everything new sits under `lib/design/prototype/` unless named otherwise.
- `package.json` and a committed `package-lock.json`. Exact versions, no `^`. Scripts:
  - `dev` (Vite's server);
  - `build` (writes `../main.html`);
  - `typecheck` (`tsc --noEmit`, strict);
  - `test` (vitest);
  - `layering`;
  - `stale`;
  - `e2e` (Playwright);
  - `check`, which runs typecheck, test, layering, stale and e2e, in that order.
- `.nvmrc` (26), plus an `engines` field to match.
- `tsconfig.json` (`strict`, `noUncheckedIndexedAccess`), `vite.config.ts` with the single-file plugin, `vitest.config.ts`, `playwright.config.ts` (Chromium only).
- `main.html` (the entry page; Vite names the built file after it) and `source/main.tsx`: one file that mounts `presentation/app` and nothing else, as the Mac app's `source/main/` holds one file.
- The tree:
  - `source/infrastructure/`, `source/data/`, `source/business/`: empty in 1b, apart from their cards. 1d adds their modules.
  - `source/presentation/`: `styles`, `shell`, `app`, and 16 places named as the Mac app's modules: `session`, `sessions`, `models`, `training`, `settings`, `graph`, `knowledge`, `compose`, `console`, `library`, `review`, `suites`, `tasks`, `video`, `voice`, `monitor`.
  - Every module but `styles` has `Views/` and `ViewModels/`.
  - `gallery`, 1a's twentieth module, joins with 1q.
- `modules.json`: the module map, the one place that says each module's layer and what it may depend on. Its first edges copy the Mac app's Presentation edges (recon darwin §1):
  - `styles` depends on nothing;
  - `shell`, `session` and `compose` depend on `styles`;
  - `sessions` depends on `session`, `shell` and `styles`;
  - the other 13 places depend on `shell` and `styles`;
  - `app` depends on all 18 others.
- A layer card at `source/<layer>/CLAUDE.md` for each of the four layers, modelled on `lib/src/darwin/source/presentation/CLAUDE.md`. Each has a **Modules:** line, and the check reads every backticked word on it as a module name, so commands on that line are written plainly.
- `tests/architecture/layering/`, the TypeScript copy of the Swift check, file for file: `ModuleMap.ts`, `SourceTree.ts` (a disk tree and a planted miniature), `LayeringCheck.ts` (`violations(): string[]`, in a stable order) and `layering.test.ts`. The Mac app's 12 rules, where they apply:
  1. **An empty map** is refused.
  2. **Folders.** Each map entry is one folder, and every folder under `source/<layer>/` is in the map.
  3. **Placement.** A module sits at `source/<layer>/<package>`. The Swift naming rule (`<Layer><Package>` targets) has no TypeScript equivalent.
  4. **Nothing from outside.** A package import must be listed in `package.json`, with dev dependencies used only in tests and config. A relative import may not leave `lib/design/prototype/`. The one exception is the fixture root that 1d declares, and only 1d's loader may reach it.
  5. **Edges.** An import from a module the map does not declare fails. A dependency on a higher layer fails as an `upward edge`. `styles` may not lean on a sibling.
  6. **UI packages.** `react`, `react-dom` and any `react-*` or `@react-*` package, and any `.tsx` file, fail below Presentation and in any `ViewModels/` file.
  7. **MVVM shape.** Every Presentation module but `styles` has `Views/` and `ViewModels/` with a file in each, and no file at its root.
  8. **The entry point.** One file, `source/main.tsx`, which imports only `presentation/app`.
  9. **Mirror tests.** Every module has a matching test folder, `tests/<layer>/<package>/`.
  10. **Module schemes** do not apply: there is no Xcode. Vitest runs one module's tests by path.
  11. **Previews.** Every component a module's `Views/` or `Components/` exports is drawn by a preview in its `Previews/` (the form is an open call below). The Swift rule's debug-only and real-starter parts do not apply, because the prototype has no real binary to keep out.
  12. **Layer cards.** A card's **Modules:** line names exactly its layer's modules. A layer with none yet says so. (The Swift rule skips an empty layer; here all four cards exist from 1b.)
- `tests/build/`: the staleness test, the single-file test and the determinism test. `e2e/opens-from-disk.spec.ts`: the Playwright smoke test.
- Root `.gitignore`: `lib/design/prototype/node_modules/`, `dist/`, `test-results/`, `playwright-report/`.
- `lib/design/main.html`: the built file, committed.
- Docs:
  - `lib/documentation/assistant/design/DEVELOPER.md`, new: install (`npm ci`, then the one-time browser download), dev, build and check.
  - `assistant/design/CLAUDE.md`: the checks line at `:37` ("Its checks run locally before a commit (1b)") points at `npm run check`.
  - `assistant/design/MILESTONES.md` on ship.

**Reference.**
- The Swift check and its 12 rules: `LayeringCheck.swift:63-93` (`violations()`), imports `:209-244`, shape `:248-260`, previews `:374-435`, cards `:450-473`.
- [ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md): the Mac app's previews, which rule 11 copies.
- The old prototype's `node -e` check ([`spec/CLAUDE.md` → Checking a change](../../../../design/spec/CLAUDE.md)): every start preset, 30 ticks, flagging throws, `undefined` and `NaN`. Its successor is each feature item's per-preset test, not this item's.
- [`assistant/design/CLAUDE.md`](../../../assistant/design/CLAUDE.md): the design's home and its interim rule.

**Decisions made** (dated):
- 2026-11-14: The user's call: the prototype is rebuilt with the production GUIs' layered architecture (four layers, MVVM modules), so a new app in a new framework ports easily from it. This item's check is what keeps that true.
- 2026-11-14: The user's call: React, TypeScript and Vite.
- 2026-11-14: The user's call: the build is one self-contained HTML file that opens by double-click ("I just want to essentially be able to click main.html").
- 2026-11-14: The user's call: Playwright is downloaded once as a dev dependency, about 150 MB, with the download approved by the user when this item builds. 1c's baseline and 1s's parity shots use it.
- 2026-11-14: The recorded default: the source is at `lib/design/prototype/`, and the build writes one committed `lib/design/main.html`. A check fails when that file is stale against its source.
- 2026-11-14: The recorded default: the checks run locally (typecheck, unit tests, layering, staleness), not in CI. `changed.sh` and the workflows are untouched.

**Open calls:**
- [default: npm with the committed lock file and `npm ci`, as measured: npm is present and pnpm is not. Versions are pinned exactly.]
- [default: `vite-plugin-singlefile` inlines the code and styles. Fonts and the bundled fixtures are inlined by their own items (1e, 1d), held by the same single-file test.]
- [default: staleness by hash. The build takes a SHA-256 over every input to the build, sorted by path: `source/`, `index.html`, the configs, `package-lock.json`, and 1d's fixture roots once they exist. It writes the hash into a `<meta name="apogee-design-sources">` tag in `main.html`. The `stale` test recomputes it and, on a mismatch, fails saying to run `npm run build`. `check` does not rebuild.]
- [default: the map is one file, `modules.json`. Folders alone cannot say what a module may depend on, and TypeScript project references would split the build into 19 projects.]
- [default: imports are read with TypeScript's own parser, not a regex, so `import`, `export … from`, `import()` and `require` are all caught.]
- [default: each Presentation module but `styles` starts with one stub view, view-model, preview and test (a place's name, nothing more), and `styles` with one stub file and its test, so every rule bites on the real tree from day one. Feature items replace the stubs. The empty app draws none of them.]
- [default: a preview is a named export in a `Previews/*.preview.tsx` file that draws one view over a view-model built on fakes, one export per visible state, as ADR 0004's `#Preview`s are. Rule 11 passes when each exported view is drawn as `<Name` in some file under its module's `Previews/`, as the Swift rule looks for a construction after the first `#Preview`.]
- [default: no ESLint or Prettier in 1b. Strict TypeScript is the gate. A formatter is a later item if wanted.]
- [default: `check` includes the one Playwright smoke test (about 2 seconds, Chromium only), since opening from disk is the user's call this item must keep.]
- (consumed decision: 1a) the ADR's wording the cards cite, and the Enforcement list the planted tests match. (consumed decision: 1d) the lower layers' modules, their edges and the fixture root. (consumed decision: 1e) tokens, fonts, styles and the shell.

**Guardrail(s).**
- `layering.test.ts`:
  - the real tree passes;
  - the miniature is clean before anything is planted;
  - one planted violation per applying rule fails with its exact sentence: an empty map, a stray folder, a misplaced module, an unlisted package, an import leaving the prototype, an undeclared import, an upward edge, `styles` leaning on `shell`, `shell` importing a place, `react` imported in `business/`, `react` imported in a `ViewModels/` file, a `.tsx` view-model, a module missing `Views/`, a file at a module's root, a second entry file, a module without its test folder, a view without its preview, a card naming a module that is not there.
- "Every layer has a module" (the Swift `:21` test) lands with 1d, once the lower layers have modules.
- `stale`: passes on a fresh build, and fails on a planted one-character source edit.
- Single-file: the built `main.html` has no `<script src>`, no stylesheet `<link href>` and no chunk `import()`.
- Determinism: two builds from the same tree are byte-identical, so the committed file changes only when its sources do.
- `e2e/opens-from-disk.spec.ts`: Chromium opens `file://…/lib/design/main.html`, the app root mounts, and the console logs no error.

**Acceptance criteria:**
- [ ] In `lib/design/prototype/`, `npm ci` then `npm run build` writes `lib/design/main.html` and changes nothing else under `lib/design/`.
- [ ] Double-clicking `lib/design/main.html` in Finder opens it in the default browser with no server and draws the empty app window (the user's look).
- [ ] `npm run check` passes, and each planted violation fails it with its sentence.
- [ ] Every check named in 1a's ADR Enforcement section has its planted test here, checked line by line.
- [ ] Editing a source file without rebuilding makes `npm run check` fail, naming the stale `main.html`.
- [ ] After install, build and check, `git status` shows no `node_modules/`, `dist/` or test output.
- [ ] Four layer cards exist. All 19 Presentation modules sit where `modules.json` says, `styles` without `Views/`/`ViewModels/` and the rest with both.
- [ ] `lib/design/spec/`, `lib/scripts/changed.sh` and `.github/` are unchanged by this item.

**Scope note.** Earmarked for v0.2.1 (design track 1); its gate, 1a, shipped 2026-11-18, and 1a's ADR is the law this check holds. 1c and 1d are gated on this item. Out of scope:
- every screen and state (1e onward);
- the CLI stand-in and its fixtures (1d);
- tokens, fonts and the shell (1e);
- the old prototype's baseline shots (1c, using the Playwright installed here);
- any CI job, `changed.sh` entry or required check.
