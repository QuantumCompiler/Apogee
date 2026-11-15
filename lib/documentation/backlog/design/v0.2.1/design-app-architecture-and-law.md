# The design app's architecture and law

**What / why.** The user's call, 2026-11-14: rebuild the design prototype as a compiled web app with the same layered shape as the production GUIs, so a new app in a new framework ports from it module for module. The prototype stays the design's one cross-platform source of truth. Before any code is written, the new app needs its law: what its layers and modules are, where its data comes from, what it builds into, and what holds the old prototype still while the port runs. This item writes that law. It is docs only.

Measured by the recon of 2026-11-14:
- **Today's prototype is one file with no layers.** At the recon, `lib/design/spec/html/main.html` was 7,604 lines and 796,776 bytes. It held 123 state keys, 127 methods, and one 3,126-line `renderVals()` (4475–7600) that returns 33 objects. Every screen's data and logic sits in one class.
- **Its data is invented.** The script has 0 `object:` keys and 0 `*_at`, `rounds_used` or `seed_source` keys (critic V3). The Mac app's previews, by contrast, read only the binary's own recorded documents (ADR 0004 §3).
- **The recorded documents already exist.** The Mac app's tests hold 613 fixture files under `lib/src/darwin/tests/**/Fixtures`, 10 of them named `*-handmade*` or `*-by-hand*` (re-counted at HEAD). They are plain JSON, JSONL and text, so any language can read them.
- **The Mac app is the shape to copy.** It has 8 lower modules (`process`; `protocol`, `preferences`, `discovery`; `reads`, `jobs`, `session`, `media`) and 19 Presentation modules: `styles`, `shell`, `app` and 16 places. Its layering check is 1,229 lines with 25 tests: one runs on the real project, and each of the other 24 plants one violation.
- **The canvas cannot host the new app.** A Design-type board must be `<x-dc>` markup, "never script-built", with no iframes (critic V1). A React root is script-built UI.
- **The old prototype is still moving.** Later the same day the working tree held 7,780 lines, 70 start presets and 5 untracked boards, from another session's design work. A port needs a reference that stands still.

**Core constraint(s).**
- **The GUI's law, unchanged.** Four layers, dependencies point down, the UI framework confined to Presentation ([GUI ADR 0001](../../../adrs/gui/layer-enforcement.md)). Presentation modules are MVVM and share one `styles` module ([darwin ADR 0002](../../../adrs/darwin/presentation-mvvm.md)). This item applies them to the design app; it amends neither.
- **The design's five rules carry over word for word** ([`lib/design/spec/CLAUDE.md` → The design's rules](../../../../design/spec/CLAUDE.md#the-designs-rules)): every part maps to the CLI; honest by default; every color is a token; keyboard first and accessible as drawn; copy in plain words.
- **No made-up shapes.** Every document the app shows is the binary's own, or a hand-made file in the CLI's exact shape, named as hand-made. This is ADR 0004's preview rule, applied to the whole app.
- **Not an input to any build, and CI never reads it.** `lib/design/spec/README.md:5` stays true.

**Seam + files.**
- `lib/documentation/adrs/design/react-design-prototype.md`, new: the design's **ADR 0001**, "The design prototype is a layered React app built into one file". Status, Date, Context, Decision, Consequences, Enforcement, in the house format. Its Decision:
  - **Four layers, mirroring the Mac app module for module** under `lib/design/prototype/`:
    - Infrastructure: `clock` (the 400 ms tick, stopped by `still`) and `process`, the stand-in for running `apogee`. It never starts a process. It answers an argv from the bundled fixtures, in one of three modes: installed, missing, config-typo.
    - Data: `protocol`, the CLI's documents and events as types, decoded tolerantly.
    - Business: `reads`, `jobs`, `session` and `media`, behind the same interfaces as the Mac app. `CommandRunning` has its five methods (`CommandRunning.swift:7–22`), `ReadOutcome` its five cases (`ReadRunner.swift:90–98`), a job is a stream of said, event and ended, and a chat replays a recorded `.jsonl`.
    - Presentation: `styles`, `shell`, `app`, and the 16 places: `session`, `sessions`, `models`, `monitor`, `console`, `suites`, `tasks`, `review`, `library`, `training`, `knowledge`, `graph`, `video`, `compose`, `voice`, `settings`. Plus `gallery`, the one module the Mac app lacks (1q).
  - **MVVM inside each place:** `Views/` (React components), `ViewModels/`, and `Components/`, `Models/` or `Previews/` only when real files need them.
  - **View-models are plain TypeScript classes that never import React.** Their services are injected as interfaces through the constructor. `app` is the composition root and the only place that builds the stand-ins.
  - **The shell never imports a place.** `app` hands it each place's screen, as `ApogeeApp.swift` does.
  - **The data:** the Mac app's fixtures, read in place at build time and bundled. A state nobody recorded gets a hand-made file named `*-handmade*`.
  - **The output:** one self-contained `lib/design/main.html`, committed, with code, styles, data and fonts inlined. A check fails when it is stale against its source.
  - **The gates are local:** typecheck, unit tests, the layering check and the staleness check, run before a commit. No CI job.
  - **The canvas is retired** once the new app covers every board (1t). Review happens on the built file.
- `lib/documentation/adrs/design/README.md`: the empty table gains row 0001, and the "None yet" line goes.
- `lib/documentation/assistant/design/CLAUDE.md`: the drafted "interim rule" section (`:18–22`) tightened and marked **decided by 1a** (no longer a pending default). It names the ADR as the law of the new form, in place of the forward pointer at `:16`.
- `lib/design/spec/CLAUDE.md`: one short note at the top saying the file is frozen from 1c's baseline, fixes only, linking the freeze rule. Its "What a change touches" table still says a new screen adds a preset and a board, which the freeze now forbids.

**Reference.**
- The recon of 2026-11-14 (prototype map, boards, darwin architecture, tooling, critic). The figures above are its own, re-checked at HEAD where cited.
- The Mac app's law the ADR mirrors: [GUI ADR 0001](../../../adrs/gui/layer-enforcement.md), [darwin ADR 0002](../../../adrs/darwin/presentation-mvvm.md), [darwin ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md), [the darwin contributor guide](../../../assistant/darwin/CLAUDE.md) (view-models import no UI framework, `:114`).
- The local-gate precedent: the CLI's lint and formatting are local gates, CI builds the executable and nothing else ([`assistant/cli/CLAUDE.md`](../../../assistant/cli/CLAUDE.md), `:230` and `:245`, re-checked).
- Items queued elsewhere that touch the design: the Apple apps' 10a ([`design-the-server-window.md`](../../darwin/macos/v0.2.6/design-the-server-window.md)), 10c ([`people-screens.md`](../../darwin/macos/v0.2.7/people-screens.md)) and 11a ([`apogee-logo-as-the-app-icon.md`](../../darwin/v0.2.1/apogee-logo-as-the-app-icon.md)).

**Decisions made** (dated):
- 2026-11-14 — The user's call: rebuild the prototype in a compiled web framework with the production GUIs' layered architecture, "a similar prototype that is written with the same type of layered architecture as the actual production GUI frameworks". It stays the design's cross-platform source of truth.
- 2026-11-14 — The user's call: React, TypeScript and Vite ("React, go with your pick").
- 2026-11-14 — The user's call: it opens by double-clicking a file ("I just want to essentially be able to click main.html"). So the build is one self-contained HTML file, since a browser blocks a `file://` page's separate scripts.
- 2026-11-14 — The user's call: "Mostly review there; make the canvas view-only." The canvas cannot host a React build, so the built file becomes the review surface and the canvas is retired at the end (1t).
- 2026-11-14 — The user's call: a design queue and home of its own (`backlog/design`, `assistant/design`, `adrs/design`), released in v0.2.1.
- 2026-11-14 — The recorded default (proposed, the user let it stand): source at `lib/design/prototype/`; the build writes one committed `lib/design/main.html`; a check fails when that file is stale against its source.
- 2026-11-14 — The recorded default: the data is the Mac app's 613 fixture files, read in place at build time and bundled. A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`, as the Mac app's 10 already are. View-models are plain TypeScript classes that never import React.
- 2026-11-14 — The recorded default: the checks (typecheck, unit tests, layering, staleness) run locally before a commit, not in CI, as the CLI's lint does. `changed.sh` and the workflows are untouched.
- 2026-11-14 — The recorded default: the old prototype is frozen from 1c's baseline, taking fixes only. New design waits for the React app's first screen (1e) and is drawn there. An item queued elsewhere that touches the design lands wherever the prototype is when it builds, and says so in its record (10a is gated on 1e; 11a may land in the old file as a fix).

**Open calls:**
- [default: the design's ADRs number from 0001 in their own series, as the mod's do; the record cites the GUI's and darwin's ADRs and amends none.]
- [default: the stand-in has no `discovery` or `preferences` module. The `process` stand-in's three modes take discovery's place, and the person's choices (theme, route) live in view-model state. A module joins when something real needs it.]
- [default: module names follow the Mac app's exactly, folders in lowercase, so a file's path says which Swift module it mirrors. View-model class names follow the Mac app's where one exists.]
- [default: the note in `lib/design/spec/CLAUDE.md` is two lines at its top; the rest of that file is 1t's to rewrite.]
- (consumed decision: 1b) how the module map is declared, the staleness check's mechanism, the package manager, and the name of the local gate.
- (consumed decision: 1d) the stand-in's matching rules and the shape of a start preset.
- (consumed decision: 1q) the gallery's pages and rows, and foundations generated from the tokens.
- (consumed decision: 1s) parity judged on layout, states, controls and wording, not pixels or data.
- (consumed decision: 1t) what the canvas shows at retirement, and the check of its comments first.
- No [user] call.

**Guardrail(s).**
- The ADR's Enforcement names each rule 1b's layering check holds, one test per rule, each proven on a planted violation as 24 of the Mac app's 25 are: an upward edge; an import the module does not declare; React below Presentation or in any `ViewModels/` file; a place missing `Views/` or `ViewModels/`; `styles` importing a sibling; `shell` importing a place; a layer card that disagrees with the map. It also names the staleness check, proven on a planted stale `main.html`. 1b's acceptance checks this list against the ADR.
- The module list in the ADR is checked against `ls lib/src/darwin/source/presentation` at writing time: 19 names plus `gallery`.
- Every relative link in the three touched docs resolves (a scratch script, run once at close).

**Acceptance criteria:**
- [ ] `lib/documentation/adrs/design/react-design-prototype.md` exists as ADR 0001, accepted and dated, with the module map, the data rule, the one-file output, the local gates, the five rules and the canvas's retirement, and an Enforcement section naming 1b's checks.
- [ ] `adrs/design/README.md` lists it as row 0001.
- [ ] `assistant/design/CLAUDE.md` states the freeze rule as decided by 1a and points to the ADR; `lib/design/spec/CLAUDE.md` says at its top that it is frozen.
- [ ] No code, no install, no CI file and no `changed.sh` line is touched.

**Scope note.** Earmarked for v0.2.1 (design track 1); ungated, and every other item in the track builds on it. Out of scope: any code or package (1b on), the baseline screenshots (1c), rewriting `lib/design/spec/CLAUDE.md` and `README.md` (1t), and re-wording the Apple apps' 10a, 10c, 8b, 9a, 9b and 11a (1t, except 10a's new gate on 1e, which the queue's opening records).
