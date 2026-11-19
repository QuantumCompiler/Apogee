# Apogee design — Developer Reference

How the design prototype is laid out, installed, built and checked. Its law is [the design's ADR 0001](../../adrs/design/react-design-prototype.md); how to work on it is [CLAUDE.md](CLAUDE.md). The old one-file prototype at `lib/design/spec/` keeps its own guide, [`lib/design/spec/CLAUDE.md`](../../../design/spec/CLAUDE.md), until 1t retires it.

## Layout

| Path | Contents |
|---|---|
| `lib/design/main.html` | **The built prototype, committed.** One self-contained file, its code and styles inlined, that opens by double-click with no server. Written only by `npm run build`; never edited by hand. It carries the hash of the sources it was built from in `<meta name="apogee-design-sources">`. |
| `lib/design/prototype/` | **The React and TypeScript source** (1b, 2026-11-18). Everything below is under it. |
| `package.json`, `package-lock.json` | npm, every version pinned exactly (no `^`), the lock file committed. Dependencies are what the app ships (`react`, `react-dom`); dev dependencies are the toolchain: TypeScript, Vite with `@vitejs/plugin-react` and `vite-plugin-singlefile`, Vitest, Playwright (`@playwright/test` for the smoke test, and `playwright` 1.64.1, the same version, for the baseline script), and the types. |
| `.nvmrc`, `engines` | Node 26. |
| `main.html` | The entry page, which Vite names the built file after. It mounts `source/main.tsx` in `#root`. |
| `source/main.tsx` | The entry point: one file that imports only `presentation/app` and calls its `start`. |
| `source/<layer>/` | The four layers, `infrastructure`, `data`, `business` and `presentation`, each with its card, `CLAUDE.md`. The three lower layers hold only their cards until 1d. |
| `source/presentation/<module>/` | The 19 Presentation modules: `styles`, `shell`, `app` and the 16 places. Every one but `styles` has `Views/`, `ViewModels/` and `Previews/`; each holds a stub that says its name, and the empty app draws none of them. `app/Views/ApogeeApp.tsx` is the empty window and its `start`. |
| `modules.json` | **The module map**: each module's layer and what it may depend on. |
| `tests/<layer>/<module>/` | Each module's own tests, mirroring its folder. |
| `tests/architecture/layering/` | **The layering check**, a TypeScript copy of the Mac app's (`lib/src/darwin/tests/architecture/layering/`), file for file: `ModuleMap.ts`, `SourceTree.ts`, `LayeringCheck.ts` and `layering.test.ts`. |
| `tests/bundle/` | The built file's checks: staleness, one file, determinism. (`tests/build/` would be ignored by the repository's `build/` rule.) |
| `e2e/opens-from-disk.spec.ts` | The Playwright smoke test: Chromium opens `lib/design/main.html` from disk. |
| `tooling/` | The build's own code: `sources.ts` (the build's inputs, their hash, the staleness sentence) and `oneFile.ts` (the Vite plugin that writes the hash into the page and the page to `lib/design/main.html`). |
| `scripts/baseline.ts`, `scripts/baseline.test.ts` | **The old prototype's baseline** (1c): the script `npm run baseline` runs, and its tests beside it, which `npm run test` runs. [Below](#the-old-prototypes-baseline). |
| `parity/baseline.json` | The baseline's record, committed: the frozen revision and everything a capture of it must reproduce. |
| `.parity/baseline/<revision>/` | The baseline's images, hash list and contact sheet, re-made by `npm run baseline`. Ignored by the root `.gitignore`, never committed. |
| `vite.config.ts`, `vitest.config.ts`, `playwright.config.ts`, `tsconfig.json` | The build; the three test projects (`unit`, `layering`, `bundle`); Chromium only; strict TypeScript with `noUncheckedIndexedAccess`. |
| `node_modules/`, `dist/`, `test-results/`, `playwright-report/` | The install, Vite's scratch build and Playwright's output. Ignored by the root `.gitignore`. |

## Install

In `lib/design/prototype/`, with Node 26 (`/opt/homebrew/bin/node` on this Mac):

```bash
npm ci                              # the packages, exactly as package-lock.json pins them
npx playwright install chromium     # once per machine: the smoke test's browser
```

The browser lands in `~/Library/Caches/ms-playwright`. Measured 2026-11-18 with Playwright 1.64.1, it downloads about 286 MiB (Chrome for Testing 185.8 MiB, its headless shell 99 MiB, FFmpeg 1.4 MiB) and takes 572 MB on disk. 1c's baseline and 1s's parity shots use the same browser.

Nothing is installed globally. A new package goes in with `npm install --save-exact <name>` (`--save-dev` for the toolchain), and its lock file change is committed with it. A package the app's source imports is a dependency; one only tests and config use is a dev dependency, and the layering check holds that.

## Develop, build and check

| Command | Does |
|---|---|
| `npm run dev` | Vite's server, with fast refresh. Open <http://localhost:5173/main.html>. |
| `npm run build` | Writes `lib/design/main.html` (and Vite's scratch copy in `dist/`). Nothing else under `lib/design/` changes. Two builds of the same tree are byte-identical. |
| `npm run typecheck` | `tsc --noEmit`, strict, over the source, the tests and the configs. |
| `npm run test` | Every `*.test.ts(x)` in the prototype outside `tests/architecture/` and `tests/bundle/` (Vitest's `unit` project): the modules' own tests, and a script's tests beside it. One module's alone: `npx vitest run tests/presentation/<module>`. |
| `npm run layering` | The layering check (the `layering` project). |
| `npm run stale` | The built file's checks (the `bundle` project): staleness, one file and determinism. |
| `npm run e2e` | The Playwright smoke test. |
| `npm run check` | **The local gate:** typecheck, test, layering, stale and e2e, in that order. It never rebuilds. |
| `npm run baseline` | Re-makes the old prototype's images from `parity/baseline.json` (about 70 seconds). `npm run baseline -- --record --reason "<why>"` records a new revision. Not part of `check`: it needs the network for Google Fonts. |

**Before a commit, run `npm run check`.** A change to anything the build reads leaves `main.html` stale, and the check fails saying so: run `npm run build` and commit the new `main.html` with its sources. CI never runs any of this ([ADR 0001 §6](../../adrs/design/react-design-prototype.md#6-the-gates-are-local)); `lib/scripts/cicd.sh`, `changed.sh` and the workflows do not name the prototype.

## The module map and the layering check

`modules.json` is `{ "modules": { "<layer>/<module>": ["<layer>/<module>", …] } }`: each key is a module's folder under `source/`, and its list is every module it may import. Its first edges copy the Mac app's Presentation edges: `styles` depends on nothing; `shell`, `session` and `compose` on `styles`; `sessions` on `session`, `shell` and `styles`; the other 13 places on `shell` and `styles`; `app` on all 18 others.

The check reads the map, `package.json` and every file in the prototype, and reads imports with TypeScript's own parser (`import`, `export … from`, `import()`, `require`, `import x = require` and `import('…')` types, plus each `import.meta.glob` pattern and each `new URL('…', import.meta.url)`, Vite's other ways of pulling a file into the build). It reads no `.css` file yet: 1e, the first item with one, adds that. Each rule fails with one sentence naming the file or module:

1. **An empty map** is refused, and so is one with no entry at `source/<layer>/<module>/`.
2. **Folders.** Each map entry is one folder; every folder under `source/<layer>/` is an entry; `source/` holds only the four layers and the entry point; a layer's folder holds only its card and its modules' folders.
3. **Placement.** A module sits at `source/<layer>/<module>/`.
4. **Nothing from outside.** A package is one `package.json` lists. `source/` imports only dependencies: no dev dependency, no Node built-in. Tests, scripts (`scripts/`, 1c) and config may use both. No relative import, glob or `new URL` leaves `lib/design/prototype/`.
5. **Edges.** A dependency on a higher layer is an `upward edge`. A map entry naming no module fails, and so does a file importing a path in no module (`source/main.tsx`, `tests/`, a file loose in a layer). A file importing a module its own module does not declare fails. `styles` leans on no sibling, and the shell depends on no place, even when declared.
6. **React** (`react`, `react-dom`, `react-*`, `@react-*`, and any `.tsx` file) stays in Presentation, and out of every `ViewModels/` file.
7. **MVVM shape.** Every Presentation module but `styles` has `Views/` and `ViewModels/` with a file in each, and no file at its root.
8. **The entry point** is one file, `source/main.tsx`, importing only `presentation/app`.
9. **Mirror tests.** Every module has a test in `tests/<layer>/<module>/`.
10. **Schemes** do not apply: there is no Xcode. Vitest runs one module's tests by path.
11. **Previews.** Every component a Presentation module's `Views/` or `Components/` exports (a capitalized function, class, or constant holding a function or a `memo`/`forwardRef` call; a renamed default, `export { View as default }`, by its own name) is drawn, as `<Name`, by some file in its `Previews/`.
12. **Layer cards.** Each layer's `source/<layer>/CLAUDE.md` has a **Modules:** line whose backticked words are exactly its modules. A layer with none says so in words, with no backticks on that line.

`layering.test.ts` runs the check on the real tree, plants a dependency in the real map, and plants one violation per rule in a clean miniature, expecting its exact sentence.

**Adding a module:** its folder under `source/<layer>/`, its entry in `modules.json` with what it may depend on (and `app`'s entry gaining it, for a Presentation module), a test in `tests/<layer>/<module>/`, its name in backticks on its layer card's **Modules** line, and, in Presentation, `Views/` and `ViewModels/` with a preview in `Previews/` for every view it exports.

**A preview** is a named export in `Previews/<View>.preview.tsx` that draws one view over a view-model built on fakes, one export per state a person sees differently, as the Mac app's `#Preview`s are ([darwin ADR 0004](../../adrs/darwin/module-schemes-and-previews.md)).

## The built file

- **One file.** `vite-plugin-singlefile` inlines the code and styles, and `tooling/oneFile.ts` fails the build if Vite emits anything but `main.html`. The `bundle` project's one-file test holds the committed file to no `<script src>`, no `<link href>` but a `data:` one, and no chunk `import()`; the smoke test holds it to loading nothing but itself.
- **Staleness by hash.** The build takes a SHA-256 over every input, sorted by path: `main.html`, `package.json`, `package-lock.json`, `tsconfig.json`, `vite.config.ts`, and every file under `source/` and `tooling/`. Dot files are left out. It writes the hash into the page's `<meta name="apogee-design-sources">`. The staleness test recomputes it and, on a mismatch, fails naming `lib/design/main.html` and `npm run build`. 1d adds its fixture roots to the inputs (`tooling/sources.ts`).
- **Determinism.** The determinism test runs two production builds into a scratch folder (`APOGEE_DESIGN_OUT`, never over the committed file) and compares them byte for byte.

## The old prototype's baseline

The old prototype (`lib/design/spec/`) is frozen at one recorded revision, and its look at that revision is the reference 1s checks the port against. The images are never committed. `scripts/baseline.ts` re-makes them from git history, so they outlive 1t's deletion of the old files.

- **`npm run baseline -- --record --reason "<why>"`** refuses while `git status` shows anything under `lib/design/spec`, naming the files. Otherwise it takes the newest commit touching `lib/design/spec`, runs [the 30-tick check](../../../design/spec/CLAUDE.md#checking-a-change) there in all three `apogee` modes, shoots it, and writes `parity/baseline.json`, adding the reason to its history. Record only at a committed fix to the frozen prototype.
- **`npm run baseline`** (capture) reads `baseline.json` and re-makes its images. It refuses a revision this repository does not hold, a tree hash or preset list that is not the revision's, a different Playwright version, and any image that draws `support.js`'s error box. It fails when the modes kept are not the recorded ones, or when an image differs from the last run's hash list.
- **What it shoots.** Git is only read. `git archive` copies the revision's `lib/design/spec` into a temp folder: no checkout, no worktree, no optional lock on the shared index. That `html/` folder is served by `python3 -m http.server`, as [`spec/README.md`](../../../design/spec/README.md#opening-it) opens it. Then:
  - every start preset in the revision's `data-props`: `main.html?start=<preset>&still=true&apogee=installed`, at 1440 × 900;
  - the same preset at `missing` and `config-typo`, kept only where the image's hash differs from `installed`;
  - the 15 other boards, full page at their `canvas.json` size, once per value of their prop menus. `App.dc.html` is the canvas's alias for `main.html`, so its frames are the presets.

  At `b375fac9` that is 78 presets, 19 more images in another mode (12 presets), and 32 board images: 129 in all.
- **Two runs draw the same pixels.** Each of these was needed, measured 2026-11-18:
  - the clock is fixed at `2026-11-14T09:41:00Z`, in UTC and `en-US`, and `Math.random` is seeded;
  - each image is shot in a page of its own, on its second load, once its fonts are in the context's cache. A first, cold load draws differently (`chat` kept two extra modes that way);
  - Chromium rasterizes on one thread (`--num-raster-threads=1`). With several, tiles race to cache a glyph, and Inspector's Geist Mono came out differently in about one launch in four;
  - each Google Fonts stylesheet is held to the one recorded. The same URL is sometimes answered with dynamically subset faces (`fonts.gstatic.com/l/font?kit=…`) instead of the static versioned files (`/s/<family>/v<N>/…`), which draw differently (one fetch in eight). A capture that gets a subset tries again in a fresh context, up to 10 times. `baseline.json`'s `fonts` holds each static stylesheet's SHA-256, and a capture refuses one that changed.
- **It needs the network for Google Fonts** (every old board links `fonts.googleapis.com`). If a font does not load, the run is refused, never recorded in fallback fonts.
- **The output**, in `.parity/baseline/<revision>/`: `screens/<preset>@<mode>.png` (`:` written `_`), `boards/<Board>@<prop>=<value>.png`, `hashes.txt` (`shasum -a 256 -c hashes.txt` checks it there), and `index.html`, a contact sheet of every image. A run compares itself with the last run's `hashes.txt`, and removes any `.png` it no longer writes.
- **`parity/baseline.json`** holds the full revision hash, the commit's date, the tree hash of `lib/design/spec` at it, the preset list, the modes kept per preset, the 30-tick result per mode, the Playwright version, the viewport, the fixed clock, time zone, locale and seed, the pinned stylesheets, and the history of records with their reasons. A test beside the script holds its preset list to the revision's `data-props` and its tree to the revision's.
