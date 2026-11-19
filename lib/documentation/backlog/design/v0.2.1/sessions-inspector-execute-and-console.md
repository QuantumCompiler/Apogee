# Sessions: the list, the inspector, the execute door and the console

**What / why.** The second half of the Sessions place in the React prototype. 1f ports the session itself (the transcript and the composer). This item ports what sits around it: the sidebar's list of saved sessions, the inspector beside the session, the "New execute session" door, and the console. It covers about 350 prototype lines and 2 start presets (recon, critic R6). The parts, in `lib/design/spec/html/main.html`:
- **The session list.** The `sidebar` section, 5020–5103 (84 lines; `nav` built at 5060), its session-list half only. It draws the date groups (Today, Yesterday, Previous 7 days, Earlier), the search field ("No sessions match …", "N matching · groups open while you search"), the private session's row, the three new-session buttons (⌘N, ⇧⌘N, ⌥⌘N), and each row's model glyph (local, cloud API billed, subscription). Each row has a menu: Details…, Rename…, Compact… (Stop compacting while one runs) and Delete…. The markup is the header buttons at 85–87 and `<nav class="ap-list" aria-label="Sessions">` at 90–127. The places bar (129 on) and the system strip are 1e's shell.
- **The inspector.** The `inspector` section, 4913–5019 (107 lines; `ins` at 4972). Its markup is `chat.inspectorOn`, 762–856, inside the session screen's block. It has six sections that fold (session, turn, context, model, helpers, recall; `insOpen` keeps 6 keys) and an "expand all" button. Its turn rows and status are written per session scenario (taskgroup, retry, wal, crash, pr, kv). **It reads the session's state, so this item waits for 1f.**
- **The execute door.** `the execute door` section, 6167–6192 (26 lines; `ex` at 6175), and the `ui.executeOpen` sheet, 2329–2346 (18 lines). It offers a suite, a model, a folder and "Orchestrate". It shows the command exactly as it will run, then Cancel or Open.
- **The console.** The `console` section, 4557–4582 (26 lines; `con` at 4561), and the `ui.consoleOpen` panel, 2105–2116 (12 lines). It opens with ⇧⌘K. It has its lines, the suggestions Tab offers, a running bar with Stop, and three refusals: `serve`, `reset` and `uninstall` each print "Not run here: …" with a reason.
- **Two presets:**
  - `execute`: the door over the `play` session, with suite `coding`, folder `~/code/ingest` and orchestrate on. Its board is `ScreenSessionExecute`, "Sessions · the execute door".
  - `console`: `apogee models list` output, then `apogee reset` refused, with `models ` typed and ten suggestions offered. Its board is `ScreenConsole`, "Console · any command, as typed".
- **The sheets and confirms these parts open:**
  - the chatInfo and rename sheets (the `sheets` section, 7300–7563; the recon maps both to `sessions`);
  - the deleteChat, compactChat and forget confirms (the `confirm` section, 5625–5781).
- **Today's data is invented** (critic V3). `chatDefs()` holds 13 chats shaped `{id, title, group, time, model, ctx}`. The CLI's `chats list` row is `{id, updated, turns, name, backend, model, origin}` (`reference/cli/machine-mode.md:1412`).
- **Line numbers** are from the recon's snapshot of the working tree (7,604 lines, 2026-11-14). Another session is still editing the file: it had 7,780 lines when this was written, and HEAD (`48f46831`; the file last changed in `231fb049`) has 7,300. So the `/* ---------- name ---------- */` section markers are the anchors, and 1c's frozen baseline fixes the final numbers.

**Core constraint(s).**
- **The prototype is the source of truth.** The port copies its layout, states, controls and wording. Where the Mac app differs, this document names the difference, and the prototype's version wins.
- **The Mac app's shape.** Two feature modules, as in `lib/src/darwin/source/presentation/`:
  - `sessions` depends on `session`, `shell` and `styles`;
  - `console` depends on `shell` and `styles`.
  - View-models are plain TypeScript classes. They never import React, and they get their services injected (ADR [presentation-mvvm](../../../adrs/darwin/presentation-mvvm.md), the four layers of [layer-enforcement](../../../adrs/gui/layer-enforcement.md)).
- **Every string is the binary's** ([module-schemes-and-previews](../../../adrs/darwin/module-schemes-and-previews.md) §3). Data comes from the Mac app's recorded fixtures through the CLI stand-in (1d), read in place. A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`, as 10 of the Mac app's files already are.
- **The list shows on every screen.** It sits in the sidebar, so from this item on every preset's sidebar shows the recorded chats. 1s judges that change once.

**Seam + files.**
- **`presentation/sessions/`** (paths under `lib/design/prototype/`, shaped by 1b's scaffold):
  - `ViewModels/SessionLibraryViewModel`: the list, the row menu, the inspector, recall and the door. It mirrors the Mac's single view-model of that name (1,571 lines), with the door's fields as `ExecuteForm`, the Mac's name.
  - `Views/`: `SessionsScreen` (the Sessions place: 1f's session view with the inspector beside it), `SessionList`, `InspectorPane`, `RecallSummaries`, `ChatInfoSheet`, `RenameSheet` and `ExecuteDoorSheet`. These are the Mac's names (`SessionsScreen.swift:9`, `:148`, `:246`, `:290`; `SessionList.swift:8`, `:249`; `ExecuteDoor.swift:7`).
  - `Previews/`: one named preview per visible state.
- **`presentation/console/`:**
  - `ViewModels/ConsoleViewModel`, the Mac's name (412 lines);
  - `Views/ConsoleView`;
  - `Previews/`.
- **The reads, as the Mac makes them** (`SessionLibraryViewModel.swift`):
  - `chats list`, `chats info`, `chats recall`, `models list`, `models info`, `models status`;
  - `config get suites` and `config get models.default_suite` for the door (:772–781);
  - `symphonies list` before the door opens a session (:797–803);
  - `chats title`, `chats delete`, `chats compact` and `chats recall --forget --yes`.
- **Fixtures wired into the stand-in:**
  - **The list and inspector:** `chats-list.json`, `chats-info.json`, `chats-recall.json`, `chats-compact.txt`, `models-info-*.json` and `models-status-*.json` (all under `tests/business/reads/Fixtures/`).
  - **`execute`:** `symphonies-list.json` and 1f's `play` session. No recorded suite list names `coding` (`suites-config-get.txt` lists cloud, fast and pair) and no `config get models.default_suite` answer is recorded, so both are hand-made:
    - `suites-config-get-coding-handmade.txt`, holding `coding`;
    - `config-get-default-suite-handmade.txt`, holding `coding`, which the door's "The default suite (coding)" reads.
  - **`console`:** `config-scan.txt`, which the Mac's console previews already use. Hand-made:
    - `models-list-handmade.txt`: no recorded text `models list` exists, only `models-list-adapters.json`, `models-list-server-tools.json` and `models-list-bad-config.err`;
    - the `__complete models` answer with the preset's ten words.
- **Tests:**
  - **Per preset** (`execute`, `console`): no throw, and no `undefined` or `NaN` in what the views read. This is the successor of [spec/CLAUDE.md](../../../../design/spec/CLAUDE.md)'s 30-tick check.
  - **View-model tests:** the list's grouping and search, the row menu, compacting, the door's command line and the console's refusals.

**Reference.**
- **The Mac modules:**
  - `sessions`: `SessionsScreen` 620, `SessionList` 360 and `ExecuteDoor` 158 lines. Its `PreviewDocuments` is 4,355 lines citing 21 distinct fixtures.
  - `console`: 5 files, 755 lines.
  - Their tests: `tests/presentation/sessions/` (15 files, among them `ExecuteDoorTests`, `SavedChatActsTests` and `SessionRenameTests`) and `tests/presentation/console/` (`ConsoleSafetyTests`, `ConsoleViewModelTests`).
- **Where the Mac app differs:**
  - **Console completion.** The Mac asks the binary (`apogee __complete <words> <partial>`, `ConsoleViewModel.swift:259–274`). The prototype's preset sets the words.
  - **Console refusals.** The Mac refuses through the shell's parity table (`ParityTable.refusal`, `:343`). The prototype hard-codes three words.
  - **Inspector width.** The prototype draws the inspector at 304 px (markup 763). BuildLayout and `tokens.json` `layoutPx.inspector` say 360, and the Mac's inspector can be resized.
- **Siblings:**
  - [DEVELOPER.md → the CLI stand-in](../../../assistant/design/DEVELOPER.md#the-cli-stand-in) (1d): the stand-in, the clock, hand-made files;
  - [DEVELOPER.md → styles, the shell and the first screen](../../../assistant/design/DEVELOPER.md#styles-the-shell-and-the-first-screen) (1e): places, keys, the sheet and confirm host;
  - [session-screen.md](session-screen.md) (1f);
  - [gallery-and-foundations.md](gallery-and-foundations.md) (1q);
  - [parity-proof.md](parity-proof.md) (1s).
- The design home: [assistant/design/CLAUDE.md](../../../assistant/design/CLAUDE.md).

**Decisions made** (dated):
- 2026-11-14: The user's call: the prototype is rebuilt in React, TypeScript and Vite with the production GUIs' four layers and MVVM modules, so it stays the design's cross-platform source of truth.
- 2026-11-14: The user's call: the build is one self-contained HTML file that opens by double-click. The canvas becomes view-only and is retired at the end, so the `execute` and `console` boards are reviewed in the built file.
- 2026-11-14: The user's call: the design has its own queue; this item is in release v0.2.1, track 1.
- 2026-11-14: The recorded default: data is the Mac app's recorded fixtures, read in place, with `*-handmade*` files for unrecorded states. View-models are plain TypeScript that never imports React.
- 2026-11-14: The recorded default: parity is judged on layout, states, controls and wording, not pixels or data, against the old prototype frozen at 1c (fixes only).
- 2026-11-14: Item 1g of track 1, gated on 1f because the inspector reads the session's state.

**Open calls:**
- [default: one `SessionLibraryViewModel`, as in the Mac, rather than a separate inspector view-model. Split it only if it passes about 800 lines, and keep the Mac's names on the parts.]
- [default: `SessionsScreen` becomes the Sessions place's screen, holding 1f's session view with the inspector beside it, as the Mac's does. 1f's 13 presets then render through it, and their tests stay green.]
- [default: the date groups come from each row's recorded `updated`, measured against the stand-in's fixed "now", the way the Mac's `when(_:)` does (`:1363`). The model glyph comes from the backend's kind, as in `kind(of:)` (`:1357`).]
- [default: the inspector's turn rows come from 1f's replayed session events, not from per-scenario tables. Its model facts come from `models info` and its helpers from `models status`.]
- [default: the console asks the stand-in for completions through `__complete`, as the Mac does. A run line with a recorded answer prints it; any other line prints the prototype's "Done.".]
- [default: the console's refusals are the prototype's three, in its wording, kept in a small table in `ConsoleViewModel`. The Mac's full parity table is not ported here.]
- [default: the inspector is drawn at the prototype's 304 px. The 360 in BuildLayout goes on 1s's list of differences.]
- (consumed decision: 1d) where hand-made files live, the fixed "now", and the stand-in's `command` and `__complete`.
- (consumed decision: 1e) the places half of the sidebar, the keys, the sheet and confirm host, and the toast.
- (consumed decision: 1f) the header's inspector button (⌥⌘I), the `chat:*` session states, and the `endPrivate` confirm that leaving a private session raises.

**Guardrail(s).**
- **Per-preset tests** for `execute` and `console`.
- **`SessionLibraryViewModel` tests:**
  - the recorded chats fall into their date groups;
  - a search with no match says "No sessions match …";
  - Rename runs `chats title`, and Delete runs `chats delete` only after the confirm;
  - a compacting row shows its busy mark, and Stop says "Not compacted: stopped";
  - the door's line reads `cd ~/code/ingest && apogee execute --tools --suite coding --orchestrate`;
  - Orchestrate is disabled when no suite is named and none is the default. The prototype only states this rule in a comment (its `exCanOrch` is always true); the Mac enforces it (`canOrchestrate`, `SessionLibraryViewModel.swift:791`).
- **`ConsoleViewModel` tests:** `apogee reset`, `serve` and `uninstall` are refused with the prototype's reasons and never reach the stand-in; Clear empties the lines; Stop says "Stopped.".
- **Planted violations:** 1b's layering check fails on a planted `import React` in `SessionLibraryViewModel` and on a planted `console` → `sessions` import.

**Acceptance criteria:**
- [ ] The built `lib/design/main.html` at the `execute` preset shows "New execute session" over the play session, with the prototype's fields, note and command line.
- [ ] At the `console` preset it shows the console with the `models list` output, the refused `apogee reset`, `models ` typed and ten suggestions.
- [ ] Every screen's sidebar lists the recorded chats by date. Search filters them, and each row's menu opens Details, Rename, Compact and Delete.
- [ ] On every `chat:*` preset, ⌥⌘I opens the inspector with its six sections, and "expand all" opens and closes them.
- [ ] The local checks (typecheck, unit tests, layering, staleness) pass.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on 1f. Out of scope:
- the places bar, the system strip and the monitor (1e);
- the transcript and the composer (1f);
- the gallery rows for these boards (1q);
- the parity verdict (1s);
- porting the Mac's parity table or its resizable inspector;
- any new design: the old prototype stays frozen.
