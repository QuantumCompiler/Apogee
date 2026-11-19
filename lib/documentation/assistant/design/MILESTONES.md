# Apogee design — Milestones

The shipped record of the design prototype (`lib/design/`): each milestone says what landed, how it was verified, and what was decided along the way. Work in progress lives in [the backlog](../../backlog/design/README.md); the plan is [ROADMAP.md](ROADMAP.md).

The design's earlier history, drawn as `.dc.html` boards, is recorded in the Apple apps' [MILESTONES.md](../darwin/MILESTONES.md).

## Milestone A — The design prototype in React

**Goal.** Track 1 (opened 2026-11-14, the user's call): the design prototype rebuilt in React and TypeScript, with the production GUIs' layers and screen modules, its data the CLI's own recordings, built into one `lib/design/main.html` that opens by double-click. The claude.ai canvas is retired once the new app covers every board.

### 2026-11-18 — `design-app-architecture-and-law` (backlog item 1a): the design app's law

**Why.** Measured by the recon of 2026-11-14: the prototype was one 7,604-line file with no layers, one class holding every screen's data and logic, and its data was invented. The Mac app, by contrast, had a layered shape held by a test and 613 recordings of the real binary. A Design canvas cannot host a React build, and the old prototype was still moving. Before any code, the new app needed its law, and the old prototype needed holding still. Docs only.

- [x] **The design's ADR 0001** (`lib/documentation/adrs/design/react-design-prototype.md`, new): "The design prototype is a layered React app built into one file", accepted 2026-11-18. It is in the house format: Status, Date, Scope, Context, Decision, Consequences and Enforcement, plus an Adoption section as darwin ADR 0004 has. Its Decision:
  - four layers mirroring the Mac app module for module: Infrastructure `clock` and `process`; Data `protocol`; Business `reads`, `jobs`, `session` and `media`, behind the Mac app's interfaces; Presentation `styles`, `shell`, `app`, the 16 places and `gallery`;
  - the Mac app's module names, folders in lowercase, and its view-model class names;
  - MVVM inside each place;
  - view-models plain TypeScript classes that never import React, their services injected, `app` the composition root, and a shell that imports no place;
  - the binary's own recorded documents, read in place and bundled, and a `*-handmade*` file for a state nobody recorded;
  - one committed, self-contained `lib/design/main.html`, with a staleness check;
  - local gates, no CI job;
  - the design's five rules, word for word;
  - the canvas retired by 1t.

  Its Enforcement names the seven rules 1b's layering check holds, each to its own planted test, then the Mac app's other rules 1b carries over, and the staleness check proven on a planted stale `main.html`.
- [x] **The ADR index** (`adrs/design/README.md`): row 0001, and the "None yet" line gone.
- [x] **The freeze rule** (`assistant/design/CLAUDE.md`): the interim rule is marked decided by 1a and tightened into three points: frozen from 1c's baseline, fixes only; new design waits for 1e; an item queued elsewhere lands where the prototype is (10a is gated on 1e). The forward pointer is now a link to the ADR. The section keeps its heading, so the Apple apps' M19 link to it still resolves.
- [x] **The old prototype's guide** (`lib/design/spec/CLAUDE.md`): two lines at its top. They say it is frozen from 1c's baseline and takes fixes only, so its new-screen row no longer applies, and they link the freeze rule and the ADR. The rest of the file is 1t's.
- [x] **The queue**: 1a's document and index row deleted. 1b's gate opened (🟢) in the index, the overview, the ROADMAP and its own Scope note. The overview's counts (23 to build, 19 of them the design's) and "can start today" (design 1b, mod 1a) updated. The 13 sibling documents that linked 1a's document now link the ADR, and 1c's freeze line links the freeze rule.

Decisions the item recorded before the build (2026-11-14):
- **The user's call:** rebuild the prototype in a compiled web framework with the production GUIs' layered architecture, "a similar prototype that is written with the same type of layered architecture as the actual production GUI frameworks". It stays the design's cross-platform source of truth.
- **The user's call:** React, TypeScript and Vite ("React, go with your pick").
- **The user's call:** it opens by double-clicking a file ("I just want to essentially be able to click main.html"), so the build is one self-contained HTML file.
- **The user's call:** "Mostly review there; make the canvas view-only." The canvas cannot host a React build, so the built file becomes the review surface and the canvas is retired at the end (1t).
- **The user's call:** a design queue and home of its own (`backlog/design`, `assistant/design`, `adrs/design`), released in v0.2.1.
- **The recorded default, which the user let stand:** the source at `lib/design/prototype/`; the build writes one committed `lib/design/main.html`; a check fails when it is stale against its source.
- **The recorded default:** the data is the Mac app's 613 fixture files, read in place at build time and bundled. A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`. View-models are plain TypeScript classes that never import React.
- **The recorded default:** the checks (typecheck, unit tests, layering, staleness) run locally before a commit, not in CI, as the CLI's lint does. `changed.sh` and the workflows are untouched.
- **The recorded default:** the old prototype is frozen from 1c's baseline and takes fixes only. New design waits for 1e and is drawn there. An item queued elsewhere lands where the prototype is when it builds, and says so (10a is gated on 1e; 11a landed in the old files on 2026-11-18).

The open calls' defaults, taken as stated (2026-11-18):
- The design's ADRs number from 0001 in their own series, as the mod's do. The record cites the GUI's and darwin's ADRs and amends none.
- No `discovery` or `preferences` module: the `process` stand-in's three modes take discovery's place, and the person's choices (theme, route) live in view-model state. A module joins when something real needs it.
- Module names follow the Mac app's exactly, folders in lowercase, so a file's path says which Swift module it mirrors. View-model class names follow the Mac app's where one exists.
- The note in `lib/design/spec/CLAUDE.md` is two lines at its top; the rest of that file is 1t's to rewrite.

Decisions taken while building (2026-11-18, an unattended group run), each for veto:
- **The Mac app's figures are re-counted at HEAD**, and the ADR uses them. The document's were from before the Apple apps' 11a. Its layering check is 1,247 lines in its 4 files: `LayeringCheck.swift` 474, `LayeringTests.swift` 448, `ModuleMap.swift` 268 and `SourceTree.swift` 57. 11a added 18 lines to `ModuleMap.swift`; it was 1,229 before. `LayeringTests.swift` has 25 tests: 5 in its suite on the real project and 20 in its suite on a planted miniature. 18 of the 25 plant a violation. The document said 1 and 24. The same bundle also holds 11a's icon-set check (`IconSetCheck.swift`, `IconSetTests.swift`), which is not the layering check.
- **A job is the Mac app's four `JobUpdate` cases:** said, event, busy and ended (`SupervisedJob.swift:129-138`). The document named three; 1d already lists four.
- **`gallery` is the one *Presentation* module the Mac app lacks.** `clock`, in Infrastructure, has no Mac module either (it is the old prototype's `tick()`), and the ADR says so.
- **"`app` is the only place that builds the stand-ins" is written as the running app's composition.** A place's `Previews/` and the tests build their own over the same interfaces, as 1b's and 1d's recorded defaults and ADR 0004 §3 have them.
- **The Enforcement names the document's seven rules,** each to its own planted test. It then names the Mac app's other rules 1b carries over where a TypeScript tree has them: an empty map refused, folders and placement, nothing from outside, one entry file, mirror test folders and previews. So 1b's check holds no rule the record does not name. How the map is declared, the staleness mechanism, the package manager and the local command's name stay 1b's.
- **The ADR states the preview rule its Enforcement checks.** Each exported view has a named preview per visible state, built through the view-model's public API over fakes answering with recorded documents, in `Previews/`, as darwin ADR 0004 §2 and §3 have it. The document named `Previews/` only as a folder. 1n's document cites "the form 1a's ADR sets", and 1b's rule 11 checks it. The file form stays 1b's.
- **The five rules are quoted word for word.** Where a rule names a part of the old prototype (the contract board, the token names), the React app's counterpart takes its place once its item ports it. The quoted `machine-mode.md` link is re-pointed so it resolves from the ADR's folder.
- **The ADR is dated 2026-11-18**, the day it was accepted. Its Context dates the calls it records 2026-11-14.
- **The gate for this docs-only item** is the scratch link check the document names, run over every Markdown file under `lib/documentation` and `lib/design`, plus a scratch acceptance check and a look that nothing outside the named documents changed. The design queue's checks are not in `lib/scripts/cicd.sh`, which builds only `cli` and `darwin`. This item changed neither, so neither suite ran.
- **The siblings' links are re-pointed, not left broken.** 14 links in 13 design documents named 1a's document. They now link the ADR, and 1c's freeze line links the freeze rule, so deleting the document breaks no link.
- **No codebase map was updated.** The design home has none, and its `DEVELOPER.md` is 1b's to create. The one new file, the ADR, is linked from `assistant/design/CLAUDE.md`, the ADR index, the ROADMAP and the overview.

**Verified.**
- **The scratch acceptance check** (`accept.py`, run from the repository root) has 63 fixed assertions plus one per changed path. Before the docs were written, 61 of the 63 failed. Afterwards all 63 pass, along with every per-path assertion. The assertions cover:
  - the ADR's title, status and date, and its four sections;
  - every name in `ls lib/src/darwin/source/presentation` (19) plus `gallery`, and the seven lower modules;
  - the five rules, and the data rule, the one file, the staleness check, the local gates, the retirement, the React-free view-models and the composition root;
  - each Enforcement line;
  - the index row, the freeze rule marked decided and pointing to the ADR, and the frozen note at the top of `lib/design/spec/CLAUDE.md`;
  - nothing changed but Markdown, and nothing under `lib/src/`, `.github/`, `lib/scripts/changed.sh` or `lib/design/prototype`.
- **The scratch link check** (`linkcheck.py`: relative links and their `#` anchors, GitHub's heading slugs) over every Markdown file under `lib/documentation` and `lib/design`: 153 files with 0 broken links before the change, and 153 files with 0 broken links after it. No link in the repository points at 1a's document any more.
- **The figures, counted from the files:**
  - 19 Presentation module folders;
  - `grep -c '@Test'` on `LayeringTests.swift` gives 25;
  - `wc -l` on the check's four files gives 1,247;
  - 613 fixture files, 10 of them hand-made (325 `.json`, 118 `.txt`, 87 `.err`, 77 `.jsonl`, 4 `.exit`, 2 `.out`);
  - the target names in `project.pbxproj`;
  - the old prototype at `bf699c32`: 8,082 lines, 836,809 bytes, 78 start presets, 81 boards, and 0 `object:`, `*_at`, `rounds_used` or `seed_source` keys;
  - the cited lines `CommandRunning.swift:7-22` and `ReadRunner.swift:90-98`, re-read.
- **`git status`**: only Markdown changed. 20 tracked files are modified, this one among them; 1 is deleted (1a's document); and the ADR is new. No code, no install, no CI file and no `changed.sh` line.

**Not done.**
- **The ADR is accepted, not landed.** `lib/design/prototype/` does not exist yet, and 1b builds the check its Enforcement names.
- **1b's document still cites the pre-11a figures** for the Mac app's check: 1,229 lines, `ModuleMap.swift` at 250, one test on the real project and 24 planted. They were left alone, since that document is another item's spec. 1b re-measures: 1,247 lines, `ModuleMap.swift` at 268, 5 + 20 tests, 18 of them planting a violation.
- **`lib/design/spec/CLAUDE.md`'s "What a change touches" table and `README.md`** are as they were, apart from the two-line note (1t rewrites them). The canvas was not touched: these `.md` files never go to it.
- **No commit**: the run's owner commits.

### 2026-11-18 — `scaffold-and-layering-check` (backlog item 1b): the scaffold and its layering check

**Why.** Measured 2026-11-14: the repository had no JavaScript tooling (no `package.json`, lock file, `tsconfig` or `.nvmrc`), and the root `.gitignore` had no `node_modules/` or `dist/` line. The Mac app's law is held by a test, `lib/src/darwin/tests/architecture/layering/`, not just trusted, and the new prototype is only a good source for the next app if its layers are as real. So 1b lays down the folders, the one-file build and a TypeScript copy of the Swift layering check, all behind one `npm run check`. It ports no screen: the app it builds is an empty window that opens from disk.

- [x] **The toolchain** (`lib/design/prototype/`, new). npm, every version pinned exactly in `package.json` and in a committed `package-lock.json` (81 packages, every one from `registry.npmjs.org`):
  - dependencies `react` and `react-dom` 19.3.1;
  - dev dependencies `typescript` 6.0.3, `vite` 8.3.5, `@vitejs/plugin-react` 6.2.1, `vite-plugin-singlefile` 2.3.4, `vitest` 5.1.0, `@playwright/test` 1.64.1, `@types/node` 26.6.6, `@types/react` and `@types/react-dom` 19.3.0.

  `.nvmrc` says 26 and `engines` says `26.x`. The scripts are `dev`, `build`, `typecheck`, `test`, `layering`, `stale`, `e2e`, and `check`, which runs typecheck, test, layering, stale and e2e in that order and never rebuilds.
- [x] **The configs:** `tsconfig.json` (strict, `noUncheckedIndexedAccess`, no emit), which also covers `scripts/` and `parity/` for 1c and 1s; `vite.config.ts` with the single-file plugin; `vitest.config.ts` with three projects, so each script runs its own share once; `playwright.config.ts`, Chromium only.
- [x] **The entry:** `main.html`, the entry page Vite names the built file after, and `source/main.tsx`, one file that imports only `presentation/app` and calls its `start`.
- [x] **The tree.** The three lower layers hold only their cards, which say they have no module yet and name what 1d adds, in words. `source/presentation/` holds the 19 modules: `styles`, `shell`, `app` and the 16 places. Every one but `styles` has `Views/`, `ViewModels/` and `Previews/` with one stub each, plus one test under `tests/presentation/<module>/`. `styles` has one stub file and its test. Each stub says its module's name and nothing more, under the Mac app's main view and view-model names (`SessionsScreen` over `SessionLibraryViewModel`, `ShellView` over `ShellViewModel`). `app`'s view is the empty window: a `<main>` labelled Apogee with its name as a heading, drawn in the browser's default style, since tokens are 1e's.
- [x] **The module map**, `modules.json`. Its edges copy the Mac app's Presentation edges, which a grep of the Swift imports confirmed: `styles` depends on nothing; `shell`, `session` and `compose` on `styles`; `sessions` on `session`, `shell` and `styles`; the other 13 places on `shell` and `styles`; `app` on all 18 others.
- [x] **The four layer cards**, `source/<layer>/CLAUDE.md`, modelled on the Mac app's Presentation card: position, **Modules:**, shape, previews, entry point, may import, enforced by, changing the layer.
- [x] **The layering check**, `tests/architecture/layering/`, file for file as the Swift one is. It is 1,133 lines in 4 files: `LayeringCheck.ts` 548, `layering.test.ts` 436, `SourceTree.ts` 96 and `ModuleMap.ts` 53. `violations()` returns its sentences in a stable order. It reads imports with TypeScript's own parser, so `import`, `export … from`, `import()`, `require`, `import x = require` and `import('…')` types are all caught, and so are Vite's two other ways of pulling a file into the build, an `import.meta.glob` pattern and a `new URL('…', import.meta.url)`. `layering.test.ts` has 31 tests and one to-do:
  - 3 on the real tree: it passes, a dependency planted in the real `modules.json` fails, and the disk tree never reads `node_modules`, build output or dot files;
  - 23 on a planted miniature: it is clean, then 22 tests each plant one violation and expect its exact sentence. 18 are the document's, with the ADR's "a place missing `Views/` or `ViewModels/`" planted both ways. The review added 4: a map with no entry at `source/<layer>/<package>/`, a Vite glob and a `new URL` leaving the prototype, and a map edge or an import naming no module;
  - 5 pinning how a file is read: the import forms, the glob's patterns, `new URL`, the exported components (a renamed default among them) and package names;
  - the to-do, "every layer has a module" (the Swift test at `LayeringTests.swift:21`), which 1d writes.
- [x] **The built file's checks**, `tests/bundle/`: staleness (4 tests), one file (2) and determinism (1). Beside them is `tooling/`, the build's own code. `sources.ts` holds the input list, the hash and the staleness sentence. `oneFile.ts` is the Vite plugin that writes the hash into the page, fails the build if Vite emits anything but `main.html`, and writes it to `lib/design/main.html`.
- [x] **The smoke test**, `e2e/opens-from-disk.spec.ts`. Chromium opens `file://…/lib/design/main.html`. The window mounts under `#root`, the title is Apogee, the console logs no error, and the page loads nothing but itself.
- [x] **`lib/design/main.html`**, built and left for the run's owner to commit: 220,150 bytes, with React inlined, one inline module script and no other file.
- [x] **The root `.gitignore`:** `lib/design/prototype/node_modules/`, `dist/`, `test-results/` and `playwright-report/`, each spelled out under the prototype.
- [x] **Docs:**
  - `assistant/design/DEVELOPER.md`, new: the layout, install, develop, build and check, the module map and the 12 rules, and the built file;
  - `assistant/design/CLAUDE.md`: the checks line now names `npm run check`, a "Laid down so far" line, and a codebase map;
  - the design's ADR 0001 gains its dated **Landed** note;
  - the queue: this document and its index row deleted; 1c and 1d opened (🟢) in the index, the overview, the ROADMAP and their Scope notes; the overview's counts (22 to build, 18 of them the design's) and "can start today" (design 1c, design 1d, mod 1a) updated. The 10 links in 9 sibling documents that named this document now name `DEVELOPER.md`, or Milestone A for the two Scope notes, so deleting it breaks no link. 1e's document gains the `.css` scan its layering guardrail now needs.

Decisions the item recorded before the build (2026-11-14):
- **The user's call:** the prototype is rebuilt with the production GUIs' layered architecture (four layers, MVVM modules), so a new app in a new framework ports easily from it. This item's check is what keeps that true.
- **The user's call:** React, TypeScript and Vite.
- **The user's call:** the build is one self-contained HTML file that opens by double-click ("I just want to essentially be able to click main.html").
- **The user's call:** Playwright is downloaded once as a dev dependency, with the download approved when this item builds. 1c's baseline and 1s's parity shots use it.
- **The recorded default:** the source is at `lib/design/prototype/`, and the build writes one committed `lib/design/main.html`. A check fails when it is stale against its source.
- **The recorded default:** the checks run locally, not in CI. `changed.sh` and the workflows are untouched.

The open calls' defaults, taken as stated (2026-11-18):
- npm, with the committed lock file and `npm ci`; versions pinned exactly. npm was present and pnpm was not.
- `vite-plugin-singlefile` inlines the code and styles. Fonts (1e) and the bundled fixtures (1d) will be inlined by their own items and held by the same one-file test.
- Staleness by hash: a SHA-256 over every input to the build, sorted by path, written into `<meta name="apogee-design-sources">` in `main.html`. The `stale` test recomputes it and, on a mismatch, fails saying to run `npm run build`. `check` does not rebuild.
- The map is one file, `modules.json`.
- Imports are read with TypeScript's own parser, not a regex.
- Each Presentation module but `styles` starts with one stub view, view-model, preview and test, and `styles` with one stub file and its test, so every rule bites on the real tree from day one. The empty app draws none of them.
- A preview is a named export in a `Previews/*.preview.tsx` file that draws one view over a view-model built on fakes, one export per visible state. Rule 11 passes when each exported view is drawn as `<Name` in some file under its module's `Previews/`.
- No ESLint or Prettier: strict TypeScript is the gate.
- `check` includes the one Playwright smoke test, Chromium only (about 0.4 seconds once the browser is warm).

Decisions taken while building (2026-11-18, an unattended group run), each for veto:
- **The user's answers before the run:** the npm registry may be used and Playwright's Chromium downloaded once into `~/Library/Caches/ms-playwright`; nothing else is downloaded or installed, and nothing globally. A Playwright test that opens the built file from `file://` stands in for the double-click look. **You try the file afterwards:** double-click `lib/design/main.html` in Finder.
- **The browser download was larger than the document's "about 150 MB".** It is the standard `npx playwright install chromium` for Playwright 1.64.1. It fetched Chrome for Testing 156 (185.8 MiB), its headless shell (99 MiB) and FFmpeg (1.4 MiB), about 286 MiB in all, which take 572 MB on disk. It was run once. 1c's document now says the browser is present, and pins its `playwright` at 1.64.1 so it reuses it.
- **TypeScript 6.0.3, not 7.0.2.** npm's latest, TypeScript 7, exposes its parser only under `unstable/*` exports, with no `createSourceFile`. The open call's default reads imports with TypeScript's own parser, and 6.0.3 is the last release with the stable API.
- **Every package is at its newest version at least two weeks old** (published by 2026-11-04). The newest `vitest`, 5.1.1, was published the morning of the build.
- **The bundle tests live in `tests/bundle/`, not `tests/build/`.** The root `.gitignore`'s `build/` line would have ignored `tests/build/` without a sound (`git check-ignore` confirmed it). A new folder name was more conservative than a negation in the repository-wide rules. `git check-ignore` passes `tests/bundle/`.
- **The hash's inputs name `main.html`, not `index.html`:** the entry page is `main.html`. The full list is `main.html`, `package.json`, `package-lock.json`, `tsconfig.json`, `vite.config.ts`, and every file under `source/` and `tooling/`, with dot files left out. Test and Playwright files, `vitest.config.ts`, `playwright.config.ts` and `modules.json` do not change the build, so they are not inputs. The path, its length and its bytes are hashed for each file, so a rename counts too.
- **The `stale` script runs the whole `bundle` project:** staleness, one file and determinism, the three checks on the built file the document puts in one folder. `test` runs every `*.test.ts(x)` in the prototype outside `tests/architecture/` and `tests/bundle/`, so the tests 1c and 1s put beside their scripts run too, and `check` runs each test once.
- **The ADR and the document differ in two places, and the ADR's version was followed.** The ADR names "a place missing `Views/` or `ViewModels/`", so both are planted. Its §6 names four local gates, and `check` runs five: the document's default adds the Playwright smoke test. The Landed note says so.
- **The Mac app's figures are re-measured.** The document's figures were from before 11a: 1,229 lines, one real-project test and 24 planted. At HEAD the Swift check is 1,247 lines in its four files, and `LayeringTests.swift` has 25 tests: 5 on the real project and 20 on the miniature, 18 of them planting a violation.
- **Rule 2 closes two holes Swift never had.** A Swift target builds whole folders, but Vite bundles any file it is pointed at. So a folder directly under `source/` that is not a layer fails, and so does a file at a layer's root other than its card.
- **The shell's rule is checked on the map, as `styles`' is.** The ADR names "`shell` importing a place" as a rule of its own, while the Mac app catches it only as an undeclared import. Here a map edge from `shell` to a place fails even when declared, and an undeclared import fails as any other does.
- **Node built-ins count as dev dependencies.** They are allowed in tests and config and refused in `source/`, which runs in a browser. `package.json` cannot list them, so rule 4 says how they are treated.
- **The entry point imports nothing but `presentation/app`, not even `react-dom`.** `app`'s `Views/ApogeeApp.tsx` exports `start(element)`, which mounts the app, as `ApogeeMain.swift` imports only `PresentationApp` and calls `ApogeeApp.main()`.
- **What counts as an exported component:** a capitalized function or class, or a capitalized constant holding a function, a class or a call (`memo`, `forwardRef`). It counts when it is exported where it is declared, by `export { … }`, or as the default. A renamed default, `export { View as default }`, counts as `View`, since a preview draws `<View`. Types and plain values do not count, and neither does `start`.
- **A module's mirror folder needs a `*.test.ts` file in it,** since git keeps no empty folder.
- **The one-file test's "no stylesheet `<link href>`" is any `<link href>` that is not a `data:` URI.** A favicon would load a file too.
- **The smoke test also checks that the page loads nothing but itself.** That is the "nothing in it may load another file" constraint, tested at run time.
- **The build's own code sits in `tooling/`,** a folder the document does not name. It is config, as rule 4 counts it, and an input to the hash. The build writes Vite's output to `dist/` (ignored) and copies the one page to `lib/design/main.html`. Writing straight into `lib/design/` would have put any stray asset beside the logo in `lib/design/assets/`. `APOGEE_DESIGN_OUT` gives the determinism test another destination, so a test never writes the committed file.
- **Vite 8's config loader asked for file extensions** on the config's own imports, so `vite.config.ts` and `tooling/` import with `.ts`, which `allowImportingTsExtensions` permits.
- **Rule 4's fixture-root exception is 1d's.** Until 1d declares the root and its loader, an `@recorded` import fails as an unlisted package.
- **No `.npmrc`.** `DEVELOPER.md` says to add a package with `npm install --save-exact`.
- **The review's fixes (2026-11-18), before commit:**
  - **Rule 4 reads Vite's other ways in.** A source file could pull a file from outside the prototype into the build with `import.meta.glob` or `new URL('…', import.meta.url)`, and neither the check nor the hash saw it. Now both are read, so leaving the prototype fails as any relative import does, and the hash's inputs cover everything the build can read again. A glob spanning modules fails as "not a module of this map".
  - **CSS `@import` and `url()` wait for 1e,** the first item with a `.css` file. Its document now says the check must scan `.css` files then.
  - **A renamed default export is drawn by its own name** (above). Before, the check asked a preview to draw `<default`, which no preview can.
  - **Three violation sentences had no planted test:** a map edge to a name that is not a module, an import of a path that is not a module, and a map with no entry at `source/<layer>/<package>/`. The review found the first two and the rerun mutation pass the third. Each is planted now.
  - **`npm run test` runs every test in the prototype** outside the other two projects' folders, not only those under `tests/`. 1c and 1s put their tests beside their scripts, where they would have been skipped without a word. `tsconfig.json` includes `scripts/` and `parity/`, so those scripts are typechecked. `tsconfig.json` is an input to the hash, so `main.html` was rebuilt: only its source hash changed.

**Verified.**
- **Test first.** Each test was watched failing before the code that passes it:
  - `layering.test.ts` failed with `LayeringCheck` missing; then 22 of its first 25 passed and the 3 real-tree tests failed until the tree existed;
  - the 19 stub tests failed with their view-models missing;
  - the bundle tests failed: the staleness file could not load `tooling/sources`, the determinism build failed, and the one-file test found no `lib/design/main.html` (its planted-detector test, which needs no build, passed);
  - the smoke test failed with no browser installed, then passed against the built file;
  - the review's fixes: 5 tests failed before `importsIn` and `exportedComponents` changed (the glob and `new URL`, each planted and read, and the renamed default, which gave `default`). The 2 tests for sentences the code already had passed, and each failed once its push or return was disabled. In a scratch copy, a planted failing `scripts/baseline.test.ts` and a type error in `scripts/baseline.ts` passed `npm run test` and `tsc` unseen before the config change, and failed both after it.
- **The rules have teeth.** The first mutation pass claimed every condition was tested, but it missed three sentences. After the review's fixes, a scratch mutation pass in a copy disabled each of the check's 39 conditions in turn: its 31 violation sentences and the 8 ways it reads a file's imports and exports. Every one made at least one planted or reading test fail, and none survived. Before the third sentence was planted, the same pass left exactly one survivor: the map with no entry at its place.
- **`npm install`** added 49 packages. **`npm ci`** reinstalled them from the lock file. Only `fsevents`, which is optional, has an install script, and npm 11 did not run it.
- **`npm run build`** printed `dist/main.html 220.15 kB │ gzip: 68.96 kB` and finished in about 65 ms. Under `lib/design/`, a SHA-256 listing of every file outside `prototype/` was identical before and after (89 files), apart from the new `main.html`. A rebuild after `npm ci` was byte-identical, and so was a separate build compared against the committed file.
- **`npm run check`**, exit 0:
  - `tsc --noEmit` clean;
  - `unit` 19 of 19 passed;
  - `layering` 31 passed and 1 to-do;
  - `bundle` 7 of 7 passed;
  - Playwright 1 passed, in 373 ms.
- **Stale, end to end, in a scratch copy of `lib/design/`:**
  - a one-character edit to a comment in `AppViewModel.ts` made `npm run check` exit 1;
  - exactly one test failed: "lib/design/main.html is stale: it was built from sources sha256-7048…, and they are now sha256-703a…. Run npm run build in lib/design/prototype, and commit the new main.html" (rerun after the review's fixes);
  - after `npm run build` there, `check` exited 0.
- **`npm run dev`**, run once through Vite's API, served `/main.html` and `/source/main.tsx` with status 200.
- **The look, stood in for:** Playwright's Chromium opened the file from disk and drew a white window with the heading "Apogee". Its `#root` held `<main data-apogee-app="" aria-label="Apogee"><h1>Apogee</h1></main>`.
- **`git status`:** 99 untracked files under `lib/design/` (98 in `prototype/` and `main.html`), none of them in `node_modules/`, `dist/`, `test-results/` or `playwright-report/`; `git check-ignore` names `.gitignore` lines 34–37 for each.
- **Unchanged:** `git diff HEAD` over `lib/design/spec`, `lib/scripts/changed.sh`, `.github` and `lib/src` is empty. `lib/src/darwin` was not changed, so the darwin suite was not run.
- **The scratch link check** over every Markdown file under `lib/documentation` and `lib/design`: 157 files. No link names the deleted document, and no link this item touched is broken. The 3 flagged are text in the CLI's `MILESTONES.md`, which this item did not change.

**Not done.**
- **Your look.** Double-click `lib/design/main.html` in Finder: it should open in your browser with no server and show the empty window, the word Apogee. The Playwright test stood in for this look during the run.
- **"Every layer has a module"** is a to-do in `layering.test.ts`, for 1d.
- **CSS `@import` and `url()` are not read yet.** No `.css` file exists, so 1e, the first item with one, makes the check scan `.css` files (noted on its document).
- **The fixture-root exception, the fixture roots in the hash and the `@recorded` alias** are 1d's. Fonts are 1e's, inlined under the same one-file test.
- **No ESLint, Prettier or formatter**, as the open call's default said.
- **No CI job, `changed.sh` line or required check**, by design.
- **No commit**: the run's owner commits. Another session's version-swap edits were in the same working tree during this run, and it committed them mid-run (44c41826, which took lines of the overview and of two design documents). That commit took none of this item's lines, and this item touched only its own lines in the shared files.

### 2026-11-18 — `old-prototype-baseline` (backlog item 1c): the old prototype's baseline

**Why.** The React app is checked against the old prototype screen by screen (1s), and then the old files are deleted (1t). So the old prototype's look had to be captured first, at one recorded revision, in a form a script can re-make after the files are gone. Measured 2026-11-14: the preset list was moving (57 at HEAD, 70 in the working tree), another session's design work was not yet committed, the `apogee` prop's other modes existed nowhere as a picture, and the canvas was behind the repo (its `App.dc.html` matched `main.html` at `d539f88b`, 2026-10-26). So the baseline comes from the repo at a committed revision, never the canvas or a working tree.

- [x] **The script**, `lib/design/prototype/scripts/baseline.ts` (808 lines), run as `npm run baseline`:
  - `--record --reason "<why>"` refuses while `git status` shows anything under `lib/design/spec`, naming the files. Otherwise it takes the newest commit touching `lib/design/spec`, runs the 30-tick check there in all three `apogee` modes, shoots it, and writes `parity/baseline.json`, adding the reason to its history;
  - the default re-makes the recorded revision's images. It refuses a revision the repository does not hold, a tree hash or preset list that is not the revision's, a different Playwright version, any image drawing `support.js`'s error box, and any font that does not load. It fails when the modes kept are not the recorded ones, or when an image differs from the last run's hash list;
  - git is only read: `git archive` copies the revision into a temp folder, and every git command runs with `--no-optional-locks`. The archived `html/` folder is served by `python3 -m http.server` on a free port of 127.0.0.1;
  - it shoots every start preset at 1440 × 900 with `still=true`, the same preset at `missing` and `config-typo` (kept only where the hash differs from `installed`), and the 15 other boards full page at their `canvas.json` size, once per value of their prop menus;
  - it writes `screens/`, `boards/`, `hashes.txt` and an `index.html` contact sheet to `.parity/baseline/<revision>/`.
- [x] **Its tests**, `scripts/baseline.test.ts` (13 tests, run by `npm run test`). Each runs on a planted case in a temp git repository, with the real `support.js` read from git history, so they keep working after 1t:
  - the preset reader on a miniature `main.html` with `&amp;` and `&#39;` in `data-props`, decoded once;
  - a file with no start menu refused; a board's prop menus;
  - the 30-tick check, clean and with a preset that throws;
  - `--record` on a dirty `lib/design/spec` refuses and names both files (a changed one and an untracked one), and not a file outside it;
  - capture against a revision the repository does not hold refuses and names it;
  - a preset list that is not the revision's `data-props` list fails, naming the difference;
  - a preset drawing `support.js`'s error box fails, naming the preset and the error;
  - a failed Google Fonts link refuses the run;
  - a dynamically subset stylesheet is fetched again in a fresh context, and the static one is pinned;
  - a stylesheet that changed since the record refuses the capture;
  - record, then capture with `lib/design/spec` deleted: every preset, the modes that differ, each board per menu value, the same hash list twice, stale images removed, and a run that differs from the last named;
  - the committed `baseline.json`: its preset list is its revision's `data-props` list, and its tree is the revision's.
- [x] **The record**, `lib/design/prototype/parity/baseline.json`, committed:
  - revision `b375fac9de179d3f3b6cf44868c72cd54a1a8d36` (2026-11-18T21:22:58-07:00, the newest commit touching `lib/design/spec`);
  - tree `48f1f19b609e6015d6861f25f55892924d170d04`;
  - 78 presets;
  - the modes kept per preset: 12 presets keep a second or third mode, 19 images in all. `training`, `training:datasets`, `review`, `review:post` and `review:skills` add `missing`. `library`, `library:symphonies`, `library:mcp`, `library:unsaved`, `library:refused`, `library:image` and `review:targets` add `missing` and `config-typo`;
  - the 30-tick check: "78 start screens, no problems" in each of `installed`, `missing` and `config-typo`;
  - Playwright 1.64.1, the viewport, the fixed clock, time zone, locale and seed;
  - the four pinned Google Fonts stylesheets;
  - one history entry, recorded 2026-11-18.
- [x] **The freeze:** `lib/design/spec/CLAUDE.md`'s note from 1a now opens "Frozen at `b375fac9` (design 1c): fixes only" and says how a fix re-records. `lib/design/spec/README.md` gains the same banner. The design guide's interim rule names the revision.
- [x] **The toolchain:** `playwright` 1.64.1 as an exact-pinned dev dependency (one line each in `package.json` and the lock file; nothing downloaded), and the `baseline` script. The lock file is an input to the staleness hash, so `lib/design/main.html` was rebuilt with `npm run build`. Only its source hash changed.
- [x] **The layering rule's wording:** a dev dependency or Node built-in is refused only in `source/`. The check already allowed one in `scripts/`; its two sentences and `DEVELOPER.md`'s rule 4 now say "tests, scripts and config". A planted test pins it: `scripts/baseline.ts` importing `playwright` and `node:child_process` passes, and an unlisted package there still fails.
- [x] **The root `.gitignore`:** `lib/design/prototype/.parity/`.
- [x] **Docs:**
  - `DEVELOPER.md`: its layout gains `scripts/`, `parity/` and `.parity/`; its commands gain `npm run baseline`; a new section, "The old prototype's baseline";
  - `assistant/design/CLAUDE.md`: where the design stands, the interim rule and the codebase map;
  - the queue: this document and its index row deleted; 1s's gate in the index, the overview, the ROADMAP and its Scope note no longer names 1c; the overview's counts (21 to build, 17 of them the design's) and "can start today" (design 1d, mod 1a) updated;
  - 11 links in 9 sibling documents that named this document now name `DEVELOPER.md`'s section, or Milestone A for 1s's Scope note, so deleting it breaks no link;
  - 1s's document gains the capture settings its frames must match;
  - 1p's open call gains the fact it hinged on: all 9 Suites and Tasks presets are in the baseline.

Decisions the item recorded before the build (2026-11-14):
- **The user's call:** rebuild the prototype in React, TypeScript and Vite, with the production apps' four layers and screen modules; the old one is the reference until then.
- **The user's call:** the canvas becomes view-only and is retired at the end; the review surface is the built file. So the baseline never comes from the canvas.
- **The user's call:** parity screenshots by Playwright, downloaded once as a dev dependency, approved by the user when this item builds.
- **The user's call:** a design queue of its own; this item is design 1c, in v0.2.1.
- **The recorded default:** images are not committed; a script re-makes them from the recorded frozen revision.
- **The recorded default:** the old prototype is frozen, fixes only, from this item's revision; new design waits for 1e.
- **The recorded default:** parity is judged on layout, states, controls and wording, not pixels or data.
- **The recorded default:** checks run locally, not in CI; `changed.sh` and the workflows are untouched.

The open calls' defaults, taken as stated (2026-11-18):
- `git archive <revision> lib/design/spec` into a temp folder, not `git worktree add`. It leaves no worktree entry in the shared checkout.
- The revision is the newest commit touching `lib/design/spec` once it is clean. It was clean when this item built (the other session's work had been committed), so nothing waited.
- All three `apogee` modes are captured, and a `missing` or `config-typo` image is kept only when its hash differs from `installed`. No hand-picked list of screens.
- The 15 non-screen boards are captured too (Build, Foundations, Exploration), since 1q and 1r rebuild them and 1s judges them.
- The browser's clock is fixed to one instant, and `Math.random` is seeded by an init script, so two runs give the same images.
- The capture waits for the fonts (`document.fonts.ready`). If the Google Fonts link fails, the run is refused, never recorded with fallback fonts.
- A fix to the frozen prototype re-records the baseline at the fix's commit, with the reason in `baseline.json`'s history.
- Chromium only; the browser lives in Playwright's own cache outside the repo.

Decisions taken while building (2026-11-18, an unattended group run), each for veto:
- **The user's answers before the run:** the npm registry may be used; Playwright's Chromium may be downloaded once (it was already there from 1b, so nothing was downloaded); Google Fonts may be fetched during captures of the old prototype. The contact sheet is for you to look through after the run (Not done, below).
- **The revision is `b375fac9`, not HEAD (`eb2e436d`).** The document says the newest commit touching `lib/design/spec`, and that is 1a's commit. HEAD, 1b's commit, did not touch the spec, and both commits hold the same spec tree, `48f1f19b`. The banners this item adds are a later edit to `lib/design/spec`, made after the record, as `--record` refuses a dirty spec folder. They change no picture, so they need no re-record.
- **78 presets, read from the revision,** as the document says ("never a number written here"). At HEAD on 2026-11-14 there were 57.
- **`canvas.json`'s `App.dc.html` is skipped.** It has no file: it is the canvas's alias for `main.html`, whose presets are all captured.
- **A board is shot at its `canvas.json` width and height as the viewport, full page,** once per value of each enum prop. No board has more than one menu, so 32 board images.
- **Each image is shot in a page of its own, on its second load.** Two runs did not match without it. A first load fetches the fonts, and an image shot then can draw differently: `chat` kept two extra modes in the first record for that reason alone. One page for the whole run let one shot's glyphs reach the next: Inspector's dark image flipped between runs until each shot had its own page.
- **Chromium runs with `--num-raster-threads=1`.** With several raster threads, Inspector's Geist Mono (its `.mono` stack, `ui-monospace, 'SF Mono', 'Geist Mono'`, resolves to the web font in Chromium) drew differently in 3 of 12 separate launches, with the same font bytes at the same positions. With one thread, 14 of 14 launches matched. The browser stays Playwright's default headless shell. The full build in new headless mode was tried and dropped: its GPU raster varied the antialiasing on button borders (`chat`, `settings:backends`, `video:install`).
- **Google Fonts stylesheets are pinned, and `baseline.json` gains a `fonts` field.** The same stylesheet URL was sometimes served with faces from dynamically subset files (`fonts.gstatic.com/l/font?kit=…`) instead of the static versioned ones (`/s/<family>/v<N>/…`): 1 fresh context in 8. That draws different pixels. A shot that gets a subset is taken again in a fresh context, up to 10 times. Each static stylesheet's SHA-256 is recorded, and a capture refuses one that changed. The document's field list did not name this, or the clock, time zone, locale and seed, which are recorded beside the viewport for 1s.
- **The script routes no request.** Playwright's routing turns the browser's cache off, and with it off the fonts load late on every load. That brought the variance back (`settings:appearance`, Inspector). The tests route the network through the script's `prepareContext` hook, where the cache does not matter.
- **The fixed settings:** the clock at `2026-11-14T09:41:00Z`, time zone UTC, locale `en-US`, `Math.random` a seeded mulberry32 (seed 1), light color scheme, device scale 1, CSS animations stopped and the caret hidden at the shot. `main.html`'s `nowTime()` reads the clock, so the time zone is fixed too.
- **A font failure is any of:** a failed request or an HTTP status of 400 or more from `fonts.googleapis.com` or `fonts.gstatic.com`, a stylesheet that never loaded, or a font face in error. The stylesheet wait notes each one that settles from an init script, so one that failed before the wait began is still seen.
- **`support.js`'s error box is recognized by `fail()`'s own inline style:** a `<pre>` with a `#F2836F` border, a 24px margin and `pre-wrap`. A board's own `<pre>` uses tokens, so it never matches.
- **`--record` needs `--reason`.** Problems the 30-tick check finds are recorded as it prints them and do not refuse the record. A broken render still refuses, at the capture.
- **A capture refuses a different Playwright version.** Its Chromium draws differently, so the fix is `npm ci` or a re-record with a reason.
- **A run compares itself with the last run's `hashes.txt`** in the revision's folder and fails, naming the images, when any differ. It removes any `.png` it no longer writes in its own `screens/` and `boards/` folders. The folder then holds exactly what the hash list names, and nothing else in it is touched.
- **File names:** `screens/<preset>@<mode>.png` and `boards/<Board>@<prop>=<value>.png`, with `:` written `_` so Finder shows them as written. Two images that would share a name refuse the run.
- **`npm run baseline` is not part of `npm run check`.** It takes about 70 seconds and needs the network. Its tests are part of `check` and need neither: they run Chromium on planted pages, with Google Fonts routed in the test.
- **The record was taken five times during the build, each before anything was committed.** The first four were not reproducible, as above. `parity/baseline.json`, this item's own uncommitted file, was deleted and re-recorded each time, so its history holds the one record that stands.
- **The 1a note in `lib/design/spec/CLAUDE.md` was rewritten, not stacked under a second banner.** It is the same note, now naming the revision.
- **The images and the contact sheet are not committed** (the document's default). The contact sheet's path is under Not done, for you.

**Verified.**
- **Test first.** Each test was watched failing before the code that passes it:
  - the layering sentences failed with the old wording, then passed. The planted `scripts/` test passed on its first run, because the check already refused dev dependencies only in `source/`. It pins the scope rather than changing it;
  - `scripts/baseline.test.ts` failed with `baseline.ts` missing. Then 10 of 11 passed, and one real bug showed: board file names lost their `@`. The committed-baseline test waited for the record;
  - the two pinning tests failed (no `fonts` field; a changed stylesheet captured without a word) before the pinning was written;
  - the stale-image check failed before the pruning was written;
  - the last-run comparison failed on its message ("1 images differ") before that was fixed.
- **The guards have teeth.** A scratch mutation pass disabled each of the script's 12 guards in turn: the dirty refusal, the reachable check, the preset equality, the error box, the font failures, the subset retry, the changed-stylesheet refusal, mode keeping, pruning, entity decoding, the 30-tick check's catch, and the last-run comparison. All 12 made a test fail. The last-run comparison survived the first pass, and the test that kills it was added.
- **`npm run baseline -- --record --reason "design 1c: the first record; …"`**, exit 0, about a minute:
  - three lines of "78 start screens, no problems";
  - "recorded b375fac9de179d3f3b6cf44868c72cd54a1a8d36: 129 images". That is 78 presets at `installed`, 19 more in another mode and 32 board images, 25 MB in all.
- **Two runs match hash for hash.** `npm run baseline` ran four times after that record, in four separate launches. Each exited 0 and printed "the same as the last run, hash for hash: 129 images", in about 68 seconds. All four hash lists are byte-identical to the record's, and `shasum -a 256 -c hashes.txt` passes all 129. A fifth capture, after the script's last edits (a message and its header comment), matched too, in about 63 seconds.
- **Re-made without `lib/design/spec`.** A local `git clone` into the session scratchpad got this item's prototype files, `node_modules` linked rather than reinstalled, and its `lib/design/spec` deleted. `npm run baseline` there exited 0, and its `hashes.txt` is byte-identical to the checkout's, 129 images.
- **The determinism findings**, from scratch stress runs against the archived revision:
  - before the fixes, a second run differed from the first in 1 image of 131 (`boards/ComponentsPrompts@theme=dark.png`, 5 pixels one level apart);
  - 8 rounds in fresh contexts varied 27 of 37 images;
  - the same stylesheet URL was served as a subset in 1 of 8 fresh contexts;
  - Inspector alone varied in 3 of 12 launches, and in 0 of 14 with one raster thread;
  - the full build in new headless mode varied `chat`, `video:install` and `settings:backends` by up to 16 levels on button-border antialiasing, and `video:install` by up to 2 even with `--disable-gpu`.
- **`npm run build`** printed `dist/main.html 220.15 kB │ gzip: 68.97 kB`. `git diff` on `lib/design/main.html` is one line: the source hash, `sha256-7048…` to `sha256-90ef…`. `npm run stale` failed before the rebuild, as it should.
- **`npm run check`**, exit 0, 12.7 seconds:
  - `tsc --noEmit` clean;
  - `unit` 32 of 32 in 20 files, 19 of them the module stubs' and 13 this item's;
  - `layering` 32 passed and 1 to-do;
  - `bundle` 7 of 7;
  - Playwright 1 passed.
- **Nothing downloaded:** `~/Library/Caches/ms-playwright` still holds `chromium-1248`, `chromium_headless_shell-1248` and `ffmpeg-1013` (572 MB). `npm install --save-exact --save-dev playwright@1.64.1` added one line to `package.json` and one to the lock file. No `apogee` binary ran.
- **The scratch link check** over every Markdown file under `lib/documentation` and `lib/design`: 156 files. No link names the deleted document, and no link this item touched is broken. The 3 flagged are text in the CLI's `MILESTONES.md`, as before.
- **Unchanged:** `lib/src`, `.github`, `lib/scripts` and the canvas. `lib/src/darwin` was not changed, so the darwin suite was not run. `lib/scripts/cicd.sh` does not build the design.

**Not done.**
- **Your look at the contact sheet.** Open `/Users/taylor/Data/Development/Projects/Apogee/lib/design/prototype/.parity/baseline/b375fac9de179d3f3b6cf44868c72cd54a1a8d36/index.html` in a browser: 97 start-preset images (78 presets, plus the 19 other-mode images where a preset draws differently) and 32 board images, each linked to its full size. It is not committed, and `npm run baseline` in `lib/design/prototype` re-makes it.
- **The images are not committed**, by design. A machine without the network cannot re-make them, since the old boards link Google Fonts.
- **A font change at Google stops the capture.** If Google serves a different static stylesheet for a pinned URL, or stops serving its files, the capture refuses rather than draw a different picture. A re-record then needs `lib/design/spec` at a commit, and `--record` takes the newest commit touching it. After 1t deletes the folder, that commit is the deletion, so a re-record after 1t would need the script to take a named revision. 1t's and 1s's documents do not cover this yet.
- **The banners are uncommitted edits to `lib/design/spec`.** Once committed, that commit is the newest touching the spec. The baseline stays at `b375fac9`: the banners are docs, not a fix.
- **No CI job, `changed.sh` line or required check**, by design.
- **No commit**: the run's owner commits.
