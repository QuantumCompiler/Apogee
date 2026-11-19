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
