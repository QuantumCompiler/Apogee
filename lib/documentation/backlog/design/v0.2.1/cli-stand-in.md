# The CLI stand-in: the app's three lower layers, answering from the CLI's own recordings

**What / why.** The React prototype draws every screen from what the CLI says, as the Mac app does. In a browser there is no CLI to run, so this item builds a stand-in for it: the Infrastructure, Data and Business layers in TypeScript. It answers every read, command, job and chat from the CLI's recorded output. Measured 2026-11-14:
- **Today's prototype has no CLI at all.** Its data is invented: 0 `object:` keys and 0 `*_at`, `rounds_used` or `seed_source` keys in its script (recon, critic V3). Its 123 state keys hold shapes the CLI never prints, and Monitor's CPU and GPU are sine waves.
- **The Mac app reaches the CLI through three services only**, all in Business:
  - `CommandRunning` (`lib/src/darwin/source/business/reads/CommandRunning.swift:7-22`), five methods: `read`, `command`, `command(input:)`, `report`, `readKeepingStderr`.
  - A read returns a `ReadOutcome` (`ReadRunner.swift:90-98`), five cases: `.document`, `.failed`, `.timedOut`, `.undecodable`, `.couldNotStart`. A command returns a `CommandOutcome` (`CommandRunning.swift:90-96`), four cases: `.done`, `.failed`, `.timedOut`, `.couldNotStart`.
  - `JobStarting` / `JobRunning` (`business/jobs/SupervisedJob.swift:177-194`) give a stream of `JobUpdate` (`:129-138`): `.said`, `.event`, `.busy` and `.ended`, always last.
  - `ChatStarting` (`business/session/ChatSession.swift:45-49`) finds the binary and opens a `ChatSession` (`:8-35`): `events`, `send`, `answer`, `attach`, `detach`, `set`, `compact`, `cancel`, `close`, `stop`, `terminateNow`.
- **The Mac app's previews already answer these from recordings.** `PreviewRunner` (`presentation/models/Previews/PreviewSupport.swift:13-37`) answers a read by the longest argument-prefix match, and an unanswered read as `.couldNotStart`. `PreviewStore` (`:43-70`) lays later answers over earlier ones; of two equally long prefixes, the later answer wins. `previewStream` (`:151-153`) turns a recorded stream into job updates.
- **The recordings are plain files, in no language's format.** 613 files, 2.8 MB, in 8 `Fixtures/` folders under `lib/src/darwin/tests/`: `business/reads` 462, `data/protocol` 57, `business/media` 41, `presentation/tasks` 22, `business/jobs` 20, and `presentation/` `graph` 5, `models` 5, `shell` 1. By kind: 325 `.json`, 118 `.txt`, 87 `.err`, 77 `.jsonl`, 4 `.exit`, 2 `.out`. Every `.json` parses as one document and every `.jsonl` line parses (re-checked at HEAD). Swift tests load them by path, unchanged. 10 are hand-made and say so in their names (`*-handmade*`, `*-by-hand*`).
- **Some documents exist only inside Swift tests:** 324 single-line `#"{…}"` JSON literals and 7 `#"""` block literals (recon, critic G5; re-counted at HEAD). Monitor's `system` document is one: `tests/presentation/monitor/MonitorViewModelTests.swift:10`, copied into `source/presentation/monitor/Previews/MonitorPreviews.swift:13`, whose comment says "no `system` read is recorded".
- **The prototype's clock and its CLI switch:**
  - `tick()` runs every 400 ms (`lib/design/spec/html/main.html:2733` at HEAD) unless `still` is set.
  - The `apogee` prop is `installed`, `missing` or `config-typo` (the `data-props` line, `:2462`; read at `:4287-4290`).
  - Its config-typo refusal (`:4290`) is the recorded `business/reads/Fixtures/agents-list-bad-config.err` word for word, with the verb swapped and "(exited with status 1)" added.
  - These lines move: another session is editing the file (7,300 lines at HEAD, 7,780 in the working tree). Item [1c](old-prototype-baseline.md)'s baseline fixes them.

**Core constraint(s).**
- **Every answer is the binary's own.** A rule answers with a recorded file, or a slice of one. A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`. Never a shape made up (ADR 0004's rule, [module-schemes-and-previews.md](../../../adrs/darwin/module-schemes-and-previews.md) §3).
- **Read in place, never copied.** The Mac app's `Fixtures/` folders stay the home of every recording. When a fixture is re-recorded there, the next build carries it.
- **Bundled at build time.** The built `lib/design/main.html` opens from a double-click, where a page cannot read files. So every recording a preset uses is inlined by Vite, and only those: imported by name, never a glob over the tree.
- **Hermetic.** The stand-in starts nothing, reads nothing from disk at run time, and reaches no network.
- **The layers' law** ([layer-enforcement.md](../../../adrs/gui/layer-enforcement.md)). The stand-in sits in Infrastructure, Data and Business, and imports no React. View-models see only the service interfaces, never the stand-in's insides, so a later port swaps in a real CLI under the same interfaces.
- **Swift's names, in TypeScript.** Each module, interface, case and method keeps the Mac app's name (`CommandRunning`, `ReadRunner`, `ReadOutcome.failed`, `JobUpdate.ended`), so a reader moves across the two apps without a glossary.

**Seam + files.** All under `lib/design/prototype/`, in 1b's layout. The modules are 1a's map: no `discovery` or `preferences` module, and `business/media` is 1k's.
- `source/infrastructure/process/`: the stand-in's "child". It takes an argument list and answers from a rule table with what the CLI printed: stdout, stderr and an exit code, or a timeout. It never spawns anything.
  - The rule table: the longest prefix wins; of two equally long, the later rule wins (`PreviewStore`'s rule).
  - A rule can hold several answers in order, one per call, the last repeating. This is how an install fails, then succeeds (1k).
  - The three modes. `missing`: no binary is found and nothing runs. `config-typo`: every call exits 1 with the recorded refusal, its verb swapped, as `main.html:4290` does.
  - The `Preset` type sits here, so a place's `Previews/` can reach it (a place may not import `app`). A preset is `rules` (reads, commands, jobs and chats), `route` (a plain string; how the page picks it is 1e's), `clock` (a start tick and `still`) and `apogee` (the mode).
- `source/infrastructure/clock/`: the 400 ms tick. `still` freezes it. Tests drive it by hand (`advance(n)`). It also holds "now", fixed per preset.
- `source/data/protocol/`: decoding, as tolerant as Swift's. Text becomes one JSON document or none (`ReadDocument`). A stream line becomes a job event or a session event. An unknown event is kept as unknown, never thrown.
- `source/business/reads/`: `CommandRunning`, `ReadOutcome`, `CommandOutcome`, `ReadWithStderr`, and `ReadRunner` over the process stand-in, turning what it printed into outcomes as Swift's does:
  - a document, a failure (stderr plus exit), a timeout or undecodable stdout, so all five read outcomes can be shown;
  - a read nobody answered is `.couldNotStart`, naming the arguments, as `PreviewRunner` does, and is logged for the per-preset tests;
  - `report` keeps a document on a non-zero exit; `readKeepingStderr` returns the recorded `.err` beside it; `command(input:)` records the input it was given.
- `source/business/jobs/`: `JobStarting`, `JobRunning`, `JobUpdate`, `JobEnd`. A job replays a recorded `.jsonl` as events, or a `.txt` as said lines, on the clock. It ends with the exit its rule names (a recorded `.exit` where one exists). `stop()` ends it as interrupted.
- `source/business/session/`: `ChatStarting` (with `locateBinary()`, answered by the process mode) and `ChatSession`. A chat replays a recorded machine-mode `.jsonl`: the `session` line first, then one turn per `send`. It waits at each `question` until `answer`.
- `source/presentation/app/Previews/`: the shared mode rules, holding the `agents-list-bad-config.err` import the `config-typo` mode uses.
- `vite.config.ts`: one alias, `@recorded` → `lib/src/darwin/tests`, imported with `?raw` so the text stays byte for byte, and allowed in the dev server's file access. `fixtures/`: the design's own hand-made files, the second fixture root. 1b's staleness hash gains both roots.
- `lib/src/darwin/tests/presentation/monitor/`: Monitor's `system` literal moves to `Fixtures/system-handmade.json` (the first use of the extraction rule below, for 1e's pilot). `MonitorViewModelTests.swift` loads it as `LibraryTests.swift:5-8` loads its fixtures; `MonitorPreviews.swift`'s comment names the file. The tree becomes 614 files, 11 hand-made.
- `tests/<layer>/<module>/`, mirroring each module: the stand-in's own unit tests, listed under Guardrails.
- Docs: the three lower layer cards' **Modules** lines; `assistant/design/DEVELOPER.md` (how a preset names its recordings, and where a hand-made file goes); `assistant/design/MILESTONES.md` on ship.

**Reference.**
- The Mac app's services and fakes: [module-schemes-and-previews.md](../../../adrs/darwin/module-schemes-and-previews.md) (ADR 0004), [presentation-mvvm.md](../../../adrs/darwin/presentation-mvvm.md), [assistant/darwin/CLAUDE.md](../../../assistant/darwin/CLAUDE.md).
- The CLI's documents and streams: [machine-mode.md](../../../reference/cli/machine-mode.md).
- The old prototype's clock and props: [the design guide](../../../../design/spec/CLAUDE.md) (`tick()`, and the 30-tick check the feature items' per-preset tests succeed).
- Siblings: [1a](../../../adrs/design/react-design-prototype.md) (the law and module map, the design's ADR 0001), [1b](scaffold-and-layering-check.md) (the folders, the layering check and its fixture-root exception), [1c](old-prototype-baseline.md) (the frozen line numbers), [1e](styles-shell-and-the-first-screen.md) (the Monitor pilot and preset routing), [1f](session-screen.md) (the chat recordings), [1k](video-and-runtime-consent.md) (`business/media` and the install sequence).

**Decisions made** (dated):
- 2026-11-14: The user's call: rebuild the prototype in React, TypeScript and Vite with the production GUIs' four layers, so a new app ports from it. The stand-in is its lower three layers.
- 2026-11-14: The user's call: the build is one self-contained HTML file that opens with a double-click. So every recording is bundled at build time.
- 2026-11-14: The recorded default (proposed, not corrected): the data is the Mac app's 613 recorded files, read in place and bundled. A state nobody recorded gets a `*-handmade*` file in the CLI's exact shape. View-models are plain TypeScript classes that never import React.
- 2026-11-14: The recorded default: the stand-in mirrors the Mac app's three services. A preset is rules, a route and a clock, plus the `apogee` mode.

**Open calls:**
- [default: a document that exists only as a Swift literal is extracted to a fixture file, in the change that first needs it.
  - The file goes in the Mac app's tree, beside its siblings: the test's own module `Fixtures/` folder (as `presentation/tasks` already has).
  - The Swift test reads the file instead of the literal, and behaves the same. The preview's copy stays (previews copy their documents, ADR 0004 §3), and its comment names the file.
  - A literal written by hand is named `*-handmade*`.
  - Monitor's `system` is extracted here, since 1e's pilot needs it first. That is this item's only darwin edit.]
- [default: a hand-made file the Mac app does not use goes in `lib/design/prototype/fixtures/`, laid out as the Mac app's tree (`business/reads/…`), named `*-handmade*`. New recordings go there too. The Mac app's folders gain only what its own tests read.]
- [default: recordings are imported only in `Previews/` files and tests, never in Infrastructure, Data or Business. The stand-in is handed its rules, as `PreviewRunner` is. These are the only files 1b's rule 4 lets reach `@recorded`.]
- [default: a job or chat replays one recorded line per tick, as the old prototype's training replay does. A preset may set a faster pace.]
- [default: a `still` preset applies its start ticks at once, then stops. "Now" is fixed per preset (2026-11-14 12:00 UTC unless the preset names another), so "3 minutes ago" never drifts in a screenshot.]
- [default: no new recordings in this item. Recording a state the Mac app lacks is the feature item's job, with the repo's capture scripts in a throwaway `APOGEE_HOME`.]
- (consumed decision: 1a) the module map and layer names. (consumed decision: 1b) the layering rules this item adds to. (consumed decision: 1e) how the page picks a preset. (consumed decision: 1f) recordings for the five hand-written chat scenarios.
- No [user] call.

**Guardrail(s).** Unit tests under `lib/design/prototype/tests/`, run by the local gate (1b):
- `process`: longest prefix wins; the later rule wins a tie; a sequence answers in order, then repeats its last.
- `reads`: each of the five `ReadOutcome` and four `CommandOutcome` cases comes from a rule; an unanswered read is `.couldNotStart` and logged; `report` and `readKeepingStderr` behave as Swift's.
- `jobs`: `business/jobs/Fixtures/train-pipeline-run.jsonl` (33 lines) replays in order, one line per tick, then `.ended`; `stop()` ends it interrupted.
- `session`: `data/protocol/Fixtures/mock-actor.jsonl` (24 lines) replays. The `session` line comes first, the turn waits at each `question`, and `answer` lets it go on.
- `clock`: 400 ms per tick; `still` holds the state after its start ticks.
- `modes`: `missing` finds no binary and runs no read; `config-typo` fails every read with the recorded refusal.
- `decode`: all 325 recorded `.json` files decode to one document, and every line of the 77 `.jsonl` files decodes, unknown events kept. These are read from the Mac app's tree at test time, with Node, not bundled.
- Layering: 1b's deferred "every layer has a module" test now passes. Planted violations each fail the check, naming the file: a `@recorded` import in Business, a glob over `@recorded`, and React in Data.

**Acceptance criteria:**
- [ ] The stand-in's tests pass in the local gate, against the Mac app's real recordings.
- [ ] A read of `["agents", "list"]` answered by `agents-list.json` returns the document the Swift tests decode; in `config-typo` mode it returns the CLI's refusal, word for word.
- [ ] A recorded job and a recorded chat replay on the clock, and a `still` preset does not move after it loads.
- [ ] The layering check rejects the three planted violations.
- [ ] Under `lib/src/darwin/`, only Monitor's test, its preview's comment and the new `system-handmade.json` change, and the Mac app's Monitor tests still pass.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on [1b](scaffold-and-layering-check.md). Out of scope: any screen, the styles and the shell (1e); the presets themselves and their fixtures (each feature item, 1e to 1p); `business/media` (1k); extracting any Swift literal but Monitor's (the item that needs each one); new recordings; and the parity screenshots (1s).
