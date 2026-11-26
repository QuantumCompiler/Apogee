# ADR 0001 — The design prototype is a layered React app built into one file

**Status:** Accepted · **Date:** 2026-11-18

**Scope.** The design prototype (`lib/design/`), the reference every Apogee GUI app is built from. This record applies the GUI's law to it ([GUI ADR 0001](../gui/layer-enforcement.md), [darwin ADR 0002](../darwin/presentation-mvvm.md) and [darwin ADR 0004](../darwin/module-schemes-and-previews.md)) and amends none of them. The design's records number from 0001 in their own series, as [the mod's](../mod/README.md) do.

## Context

Measured by the recon of 2026-11-14, and re-checked on the `v0.2.1` branch at `bf699c32` (2026-11-18):

- **Today's prototype is one file with no layers.** `lib/design/spec/html/main.html` was 7,604 lines and 796,776 bytes at the recon. It is 8,082 lines and 836,809 bytes now, with 78 start presets and 81 boards beside it. At the recon it held 123 state keys, 127 methods and one 3,126-line `renderVals()` that returns 33 objects. Every screen's data and logic sits in one class.
- **Its data is invented.** Its script has no `object:` key and no `*_at`, `rounds_used` or `seed_source` key, then and now. The Mac app's previews, by contrast, read only the binary's own recorded documents ([ADR 0004 §3](../darwin/module-schemes-and-previews.md#3-previews-are-hermetic-as-the-tests-are)).
- **The recorded documents already exist.** The Mac app's tests hold 613 fixture files under `lib/src/darwin/tests/**/Fixtures`: 325 `.json`, 118 `.txt`, 87 `.err`, 77 `.jsonl`, 4 `.exit` and 2 `.out`. 10 of them are named `*-handmade*` or `*-by-hand*`. They are plain JSON, JSONL and text, so any language can read them.
- **The Mac app is the shape to copy.**
  - Below Presentation it has 8 modules: `process` in Infrastructure; `protocol`, `preferences` and `discovery` in Data; `reads`, `jobs`, `session` and `media` in Business.
  - It has 19 Presentation modules: `styles`, `shell`, `app` and 16 places.
  - Its layering check, `lib/src/darwin/tests/architecture/layering/`, is 1,247 lines of Swift in 4 files. `LayeringTests.swift` holds 25 tests: 5 on the real project and 20 on a planted miniature. 18 of the 25 plant a violation and expect its exact sentence. The rest prove the real project and the miniature clean, and pin how sources are read.
  - The same test bundle also holds the icon-set check (the Apple apps' 11a), which is not part of the layering check.
- **The canvas cannot host a compiled app.** A Design-type board must be `<x-dc>` markup, never built by script, with no iframes. A React root is UI built by script.
- **The old prototype keeps moving.** Another session's work took it from 7,604 to 7,780 lines on 2026-11-14 alone, and 11a added a board on 2026-11-18. A port needs a reference that stands still.

The user's calls, 2026-11-14 (track 1 of [the design queue](../../backlog/design/README.md#index)):
- Rebuild the prototype in a compiled web framework, with the same layered architecture as the production GUIs, so a new app in a new framework ports from it. It stays the design's one cross-platform source of truth.
- React, TypeScript and Vite.
- It opens by double-clicking one file ("I just want to essentially be able to click main.html"). A browser blocks a `file://` page's separate scripts, so the build is one self-contained HTML file.
- "Mostly review there; make the canvas view-only." The canvas cannot run the build, so the built file becomes the review surface and the canvas is retired.
- A queue and docs home of its own, released in v0.2.1.

## Decision

### 1. Four layers, mirroring the Mac app module for module

The prototype's source is at `lib/design/prototype/`. Each module is one folder, `source/<layer>/<module>/`. It keeps the Mac app's module name, with its folder in lowercase, so a file's path says which Swift module it mirrors: `source/presentation/sessions/` is `PresentationSessions`. A view-model keeps the Mac app's class name where one exists. GUI ADR 0001's law holds unchanged: a module depends only on its own layer or below, and React is confined to Presentation.

| Layer | Modules | The Mac app's |
|---|---|---|
| Infrastructure | `clock`, the 400 ms tick, stopped by `still`; `process`, the stand-in for running `apogee` | `InfrastructureProcess`; `clock` has no Mac module (it is the old prototype's `tick()`) |
| Data | `protocol`, the CLI's documents and events as types, decoded tolerantly | `DataProtocol` |
| Business | `reads`, `jobs`, `session`, `media` | `BusinessReads`, `BusinessJobs`, `BusinessSession`, `BusinessMedia` |
| Presentation | `styles`, `shell`, `app`; the 16 places `session`, `sessions`, `models`, `monitor`, `console`, `suites`, `tasks`, `review`, `library`, `training`, `knowledge`, `graph`, `video`, `compose`, `voice` and `settings`; and `gallery` | The 19 modules of the same names. `gallery` is the one Presentation module the Mac app lacks |

- **`process` never starts a process.** It answers an argument list from the bundled recordings, in one of three modes: `installed`, `missing` and `config-typo`.
- **There is no `discovery` or `preferences` module.** The `process` stand-in's three modes take discovery's place. The person's choices, such as the theme and the route, live in view-model state. A module joins a layer when something real needs it.
- **Business keeps the Mac app's interfaces and names:**
  - `CommandRunning` has its five methods: `read`, `command`, `command` with input, `report` and `readKeepingStderr` ([`CommandRunning.swift:7–22`](../../../src/darwin/source/business/reads/CommandRunning.swift)).
  - A read ends in one of `ReadOutcome`'s five cases: `document`, `failed`, `undecodable`, `timedOut` and `couldNotStart` ([`ReadRunner.swift:90–98`](../../../src/darwin/source/business/reads/ReadRunner.swift)).
  - A job is a stream of `JobUpdate`s: `said`, `event`, `busy`, and `ended`, which is always last ([`SupervisedJob.swift:129–138`](../../../src/darwin/source/business/jobs/SupervisedJob.swift)).
  - A chat comes from `ChatStarting` as a `ChatSession`, and replays a recorded machine-mode `.jsonl`.

### 2. MVVM inside each place

Every Presentation module but `styles` has `Views/`, holding its React components, and `ViewModels/`, as darwin ADR 0002 has it. `Components/`, `Models/` or `Previews/` exist only when real files need them. A view binds to its view-model, the view-model consumes Business, and a view never reaches Business. `styles` owns the shared visual vocabulary and leans on no sibling. A component a second module wants is promoted to `styles`, never copied.

Every view a module exports has a named preview for each state a person sees differently, as darwin ADR 0004 §2 and §3 have it. A preview is built through its view-model's public API, over fakes that answer with the binary's recorded documents, and it lives in the module's `Previews/`. The file form of a preview is 1b's to decide.

### 3. View-models are plain TypeScript

- **A view-model is a plain TypeScript class that never imports React.** So it is tested without a browser, as the Mac app's view-models are tested without SwiftUI ([its contributor guide](../../assistant/darwin/CLAUDE.md#code-style)).
- **Its services are injected as interfaces, through its constructor.** It never builds a stand-in itself.
- **`app` is the composition root.** The running app's stand-ins are built there and nowhere else in its source, as the Mac app's `AppViewModel` composes its places. A place's `Previews/` and the tests build their own over the same interfaces, as the Mac app's previews and tests do (ADR 0004 §3).
- **The shell never imports a place.** `app` hands it each place's screen, as [`ApogeeApp.swift`](../../../src/darwin/source/presentation/app/Views/ApogeeApp.swift) hands `ShellView` its screens.

### 4. The data is the binary's own

- **Every document the app shows is the binary's own:** the Mac app's recorded fixtures, read in place at build time and bundled into the build. They stay in the Mac app's tree, and a fixture re-recorded there reaches the next build.
- **A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`**, as the Mac app's 10 hand-made files already say what they are. A shape is never made up. This is ADR 0004's preview rule, applied to the whole app.

### 5. The output is one committed file

- **The build writes one self-contained `lib/design/main.html`**, committed, with its code, styles, data and fonts inlined. It opens by double-click from disk, with no server.
- **A check fails when that file is stale against its source.**
- **Review happens on the built file.**

### 6. The gates are local

Typecheck, the unit tests, the layering check and the staleness check run on this machine before a commit, as one local command. No CI job runs them. The prototype is not an input to any build, and CI never reads it ([`lib/design/README.md`](../../../design/README.md), which carries the old prototype's README's sentence since 1t). This is the CLI's own precedent: its lint is a local gate because CI "builds the executable for each platform and nothing else" ([`assistant/cli/CLAUDE.md`](../../assistant/cli/CLAUDE.md)). `lib/scripts/changed.sh` and the workflows do not name the prototype.

### 7. The design's five rules carry over

Word for word, from the old prototype's guide, "The design's rules", which [the design's guide → The design's rules](../../assistant/design/CLAUDE.md#the-designs-rules) carries since 1t:

> - **Every part maps to the CLI.** The app drives `apogee` over the machine-mode pipes and through the same commands a person types ([machine-mode.md](../../reference/cli/machine-mode.md)). Before drawing something, find the event or command it comes from in that document or the binary's `--help`. If none exists, draw it with its fallback and add the gap to the contract board; never design as if it existed.
> - **Honest by default.** Say what the CLI says, in its words where it has them. Never invent a time estimate, a size or a count the CLI doesn't report; mark anything billed or sent off the machine.
> - **Every color is a token.** Use `var(--surface-window)` and its 24 siblings, never a hex. The only fixed values are the modal scrim, the shadows and the window controls. Type sizes are `var(--t-…)` and fonts `var(--font-ui)` and `var(--font-mono)`, so Settings › Appearance changes everything.
> - **Keyboard first, accessible as drawn.** Real `<button>`, `<input>` and `<label>` elements, an `aria-label` on icon-only buttons, a key for every destination, and text at 4.5:1 against what it sits on.
> - **Copy is plain words:** short sentences, no jargon a person wouldn't use, no emoji.

Where a rule names a part of the old prototype, such as the contract board or the token names, the React app's counterpart takes its place once its item ports it. The rule itself does not change.

### 8. The canvas is retired

Once the React app covers every board, the claude.ai design canvas is retired, and the built file is the one review surface. That happens in item 1t, after 1s's parity proof and the user's look at its report.

## Consequences

- **A later app ports module for module.** Each Swift module has one folder here with the same name, and each view-model one class. A Linux or Windows app finds the same split, whatever its language.
- **New design is drawn in React** once the first screen exists (1e). Until 1t, the old prototype is the port's frozen reference and takes fixes only (the interim rule, [1a's record](../../assistant/design/MILESTONES.md#2026-11-18--design-app-architecture-and-law-backlog-item-1a-the-design-apps-law)).
- **A state with no recording is never drawn from invention.** It waits for a recording, or gets a hand-made file named as one.
- **The Mac app's fixtures become an input to the design's build.** A fixture re-recorded for the Mac app's tests leaves `main.html` stale until it is rebuilt, and the staleness check says so. The Mac app's tests read the same files, unchanged.
- **The cost:** an npm toolchain under `lib/design/prototype/`, and a built file committed beside its source.
- **The canvas's review moves to the file.** The canvas's comment threads are carried into the backlog before it closes (1t).
- **The pipelines do not change.** `changed.sh`, the workflows and the required checks never name the prototype.

## Enforcement

The checks land with the scaffold, item 1b, so the law has teeth from the first commit, as GUI ADR 0001's did for the Mac app. 1b builds a TypeScript copy of the Mac app's layering check. It reads a module map, the one declaration of each module's layer and of what it may depend on. Each rule fails with one readable sentence, and each has its own test that plants the violation in a clean miniature, as the Mac app's do. The rules this record makes:

- an **upward edge**: a module depending on a module of a higher layer;
- an import from a module the map **does not declare** as a dependency;
- **React below Presentation**, or in any `ViewModels/` file;
- a place missing **`Views/` or `ViewModels/`**;
- **`styles` importing a sibling** Presentation module;
- **`shell` importing a place**;
- a **layer card** whose **Modules:** line disagrees with the map.

The check also carries over the Mac app's other rules where a TypeScript tree has them, as 1b lists them (from GUI ADR 0001 and darwin ADR 0004):
- an empty map is refused;
- each module is one folder at `source/<layer>/<module>/`, and every such folder is in the map;
- nothing is imported from outside the prototype but its declared packages;
- the entry point is one file that mounts `app`;
- every module has its mirroring test folder;
- every view a module exports has a preview.

**The staleness check** fails when the committed `lib/design/main.html` does not match its sources. It is proven on a planted stale `main.html`.

The data rule and the five rules have no check of their own yet. Review holds them, as review holds ADR 0004 §3's rule in the Mac app. An item that can check one mechanically, such as a color outside the tokens or a hand-made file not named as one, adds the check and names this record.

How the map is declared, the staleness check's mechanism, the package manager and the local command's name are 1b's to decide.

## Adoption

Accepted 2026-11-18, not yet built: `lib/design/prototype/` does not exist yet. Until 1b lands the check, this record and review hold the line. When it lands, this record gains a dated **Landed** note, as GUI ADR 0001 records its own.

**Landed 2026-11-18** (1b, the scaffold): the module map is `lib/design/prototype/modules.json`, each module's folder under `source/` and the modules it may import; the package manager is npm, every version pinned in a committed lock file; the local command is `npm run check`, which runs the typecheck, the unit tests, the layering check, the staleness check and one Playwright smoke test that opens the built file from disk (§6's four gates, plus that test). The check is `lib/design/prototype/tests/architecture/layering`, which reads imports with TypeScript's parser and fails each rule of the Enforcement section above (a place missing `Views/` and one missing `ViewModels/` both planted), with the Mac app's other rules where a TypeScript tree has them. Every rule was verified against a planted violation. The staleness check is a SHA-256 over the build's inputs, written into `main.html` and recomputed, proven on a planted stale `main.html` and a planted one-character source edit. 18 of the 19 Presentation modules hold a stub view, view-model, preview and test, and `styles` a stub file and its test; the lower layers have only their cards until 1d.

**2026-11-18** (1e, the first screen): rule 3 of §7, "every color is a token", and its type-size, font and motion siblings are checked, as the Enforcement section asks of an item that can: `lib/design/prototype/tests/architecture/tokens/` refuses a color, a type size, a font or a motion time written in `source/` but through tokens.json's variables (its fixed values excepted), and the token check holds the generated CSS, and the built file's, to tokens.json value for value. The layering check's rule 4 gains the tokens root, `@tokens`, the one way to tokens.json, reached only from `styles`, and reads `.css` files' `@import`, `url()` and `image-set()` too.

**2026-11-21** (1q, the gallery): `gallery`, the Presentation module the Mac app lacks, joins the map on `styles` alone; `app` hands it the app to mount, the specimens and the logo, and planted imports from it into `app`, the shell or a place each fail, as a React import in its view-models does. The layering check's rule 4 gains **the assets root**, `@assets/<file>`, the one way to `lib/design/assets` (one file by its full name, served by the build as a `data:` URL), reached only from `presentation/app` and the tests; the staleness hash covers each asset the source names. The app's icons are now one set in `styles`, and a test holds that no view draws one of its own.

**2026-11-21** (1r, the Build pages): the Enforcement section's React rule reaches `Models/` too. A Presentation module's `Models/` folder holds plain typed data -- 1r's contract, layout, native-controls and processes tables, 1q's board table -- and the layering check now fails a React import or a `.tsx` file there as it fails one in `ViewModels/`, proven on a planted miniature. No `Models/` file held React before the rule.

**2026-11-21** (1t, the retirement): §8 is done. After 1s's parity proof and the user's word on its report, the old one-file prototype is deleted (kept in git history; 1c's baseline re-makes its pictures from `b375fac9`), `tokens.json` moved beside the built file as `lib/design/tokens.json`, and the claude.ai canvas retired with one last note pointing to `lib/design/main.html`. Three links above named files that went with it and were repointed, their words unchanged: §6's to `lib/design/README.md`, which carries the sentence "not an input to any build, and CI never reads it"; §7's to the design's guide, which carries the five rules word for word; and the Consequences' interim rule to 1a's record in MILESTONES.md, the guide's section having gone with the freeze. The layering check gains the retired path: nothing reads the old prototype's folder but the parity scripts, from git, proven on a planted import of `../spec/json/tokens.json`.
