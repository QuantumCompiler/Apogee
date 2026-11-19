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
