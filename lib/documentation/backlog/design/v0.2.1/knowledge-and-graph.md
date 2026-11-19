# Knowledge and Code graph in the React prototype

**What / why.** Item 1o ports two places from the old prototype into the React app. **Knowledge** has two tabs: decision records (list, ask a question, open a record, its raw conversation) and collections (list, search, sources). **Code graph** draws a code graph (Functions and Files views, a file tree, the selected node's callers and calls) and a collection's prose graph (summary, community, decision, busiest entities), with its More menu: export, save the report, dedupe, delete. It is one of the feature items of track 1 (the user's call, 2026-11-14: rebuild the prototype in React with the production GUIs' layers). Measured on 2026-11-14:
- **The source in `main.html`, 282 lines** (line numbers at HEAD `231fb049`, the last commit to touch the file, 7,300 lines):
  - `renderVals()` sections: knowledge 6094–6139 (46), which builds `kno`; graph 6140–6225 (86), which builds `gr`.
  - Markup: `ui.isKnowledge` 1058–1120 (63), with `kno.recOn` 1070–1100 and `kno.colOn` 1101–1119; `ui.isGraph` 1483–1569 (87), with `gr.codeShown` 1503–1539 and `gr.proseShown` 1540–1568.
  - Shared pieces it uses: the generic sheet's `savePanel` (7220–7230) and `dedupe` (7231–7248) kinds, and the confirm's `graphDelete` kind (5467–5471).
  - Canned data: `graphData()` (3718), two hand-placed layouts with fixed x and y (9 nodes and 8 edges for functions, 6 and 5 for files); `knRecs` (6097), `treeFiles` (6149), `gItems` (6157). State: `kn` (2473), `gView` and `gBuilt` (2570–2571), `gNode`, `graphGone`.
  - The recon measured the working tree, where the same blocks sit at 6389–6434, 6435–6520, 1084–1146 (1096–1126, 1127–1145) and 1509–1595 (1529–1565, 1566–1594). Another session's uncommitted design work keeps moving them; the blocks match HEAD, only their position changes. [1c](../../../assistant/design/DEVELOPER.md#the-old-prototypes-baseline)'s baseline (shipped, at `b375fac9`) pins the numbers the port works from.
- **4 start presets, each with a Screen board** (Build page, row y 2720):
  - `knowledge`: `ScreenKnowledge` (x 18240), a question asked and the rejected record open (`applyStart` 2691).
  - `knowledge:collections`: `ScreenCollections` (x 19760), `notes` searched (2691).
  - `graph`: `ScreenGraph` (x 15200), the Functions view (2692).
  - `graph:prose`: `ScreenGraphProse` (x 16720), the `notes` prose graph (2677).
- **Half of it is already the CLI's words.** Knowledge and the prose graph were copied from recordings, reshaped into invented keys (`from`, `link`, `at` against the recorded `provenance`, `downstream_link`, `timestamp`):
  - the 3 records and their ids are `knowledge-list.json`'s, and the asked order with its 0.596 score is `knowledge-query.json`'s;
  - the collections line ("knowledge 3 sources · 3 chunks", "notes 5 · 5") is `collections-list.json`, and the search lines are `collection-query.txt`;
  - "notes · 4 entities · 5 relations · 1 community" is `graph-prose-stats.json`, and the dedupe lines for `notes` are `graph-dedupe-dry-run-merges-handmade.txt` and `graph-dedupe-merged-handmade.txt`.
- **The code graph is invented** (critic V3). "~/code/ingest · 214 files · 1,284 symbols" with 2,973 edges has no recording. The recorded code graph is `demo`: `stats.json` counts 4 Python files, 16 nodes and 21 edges, while `graph-export-html.txt` and `graph-dedupe-dry-run.txt` count 9 entities. `graph-delete.txt` names a graph called `spare`. So the code graph visibly shrinks in the port, and its recordings do not all agree.
- **10 actions are toasts** ending "(prototype)": capture, export, link, move and delete a record; ingest, remove a source and delete a collection; find communities; open in editor.
- **The Mac app's modules:**
  - `knowledge`, 6 files, 1,830 lines: `RecordsViewModel` (363), `CollectionsViewModel` (243), `KnowledgeScreen` (744, 30 `#Preview`s), 3 `Previews/` files. 6 test files.
  - `graph`, 6 files, 1,900 lines: `GraphViewModel` (442), `GraphLayout` (113), `GraphScreen` (671, 22 `#Preview`s), 3 `Previews/` files. 5 test files, and `tests/presentation/graph/Fixtures` with 5 files (`stats`, `query`, `explain`, `neighbors`, `show`).
  - The prototype's save panel and dedupe sheet are the Mac's `ExportFormat` and `Dedupe` (`GraphViewModel.swift:18`, `:49`).
  - Their previews cite 29 recorded files (15 knowledge, 14 graph). `tests/business/reads/Fixtures` holds 40 files named `knowledge*`, `collection*` or `graph*`, 2 of them hand-made.

**Core constraint(s).**
- **The prototype is the source of truth.** Port its layout, states, controls and wording. Where the Mac app differs, follow the prototype and name the difference in the record. The differences known now:
  - the Mac's graph has a search field, a graph picker and "New graph…"; the prototype has the Functions and Files views, the file tree, "Ask about this" and "Open in editor";
  - the Mac runs Capture and Ingest as real forms, Export from a More menu, and Link from a field; the prototype shows toasts.
- **The architecture is 1a's ADR.** View-models are plain TypeScript classes that never import React, with 1d's runners injected. `graph` and `knowledge` import only `shell` and `styles`, as on the Mac. "Ask about this" opens a new session with a file chip and a draft; it is a callback the app root fills, never an import of `session`.
- **Data is the CLI's own.** Fixtures are read in place from `lib/src/darwin/tests/**/Fixtures`. A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*` ([ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md), "the binary's own").
- **Destructive steps ask first.** Delete the graph goes through the confirm with the CLI's words. Dedupe shows its dry run and offers "Merge them" only when the run names a group.
- **The old prototype stays frozen** and is the reference. No new state is drawn here.

**Seam + files.**
- `presentation/knowledge/` under `lib/design/prototype/`, laid out per [1b](../../../assistant/design/DEVELOPER.md):
  - `ViewModels/`: `RecordsViewModel` (list, branch filter, ask, selection, raw conversation, the record's actions) and `CollectionsViewModel` (list, selection, search, sources, the collection's actions).
  - `Views/`: the screen with its two tabs, the record pane, the collection pane.
  - `Previews/`: records listed; a question answered with scores; a record open; its raw conversation shown; a branch with none; collections read; a collection searched.
- `presentation/graph/`:
  - `ViewModels/`: `GraphViewModel` (views, selection, neighbours, the More menu, export, report, dedupe, delete, rebuild) and `GraphLayout` (node positions computed from the document, as the Mac's).
  - `Views/`: the screen, the file tree, the canvas (nodes and SVG edges), the selected node's pane, the prose pane, the save panel and dedupe sheet contents.
  - `Previews/`: Functions view; Files view; prose graph; More open; save panel; dedupe with nothing to merge; dedupe offering a merge; merging; merged; delete asked; graph deleted.
- The stand-in's preset table ([1d](cli-stand-in.md)): 4 entries, each `{fixtures, route, clock}`, naming the files above. The tab and view (`knowledge:collections`, `graph:prose`) are the route's, so no third view-model holds them.
- The confirm host and generic sheet host ([1e](styles-shell-and-the-first-screen.md)): `graphDelete`'s content, and the `savePanel` and `dedupe` contents.
- Tests, mirrored under the prototype's test tree: view-model tests per intent, and the per-preset test.
- Docs: the design's MILESTONES.md on ship.

**Reference.**
- The recon (2026-11-14): the prototype map §1 (sections 19 and 20, sheets 27, confirm 11; markup blocks), §2 (this row), §3 (`graphData`, `knRecs`, `treeFiles`, `gItems`) and §5 (presets); the boards report §1; the darwin report §3 and §4 (fixtures); the critic's V3 and R13.
- The Mac modules: `lib/src/darwin/source/presentation/{knowledge,graph}/`, their tests under `lib/src/darwin/tests/presentation/{knowledge,graph}/`.
- The boards: `lib/design/spec/html/ScreenKnowledge.dc.html`, `ScreenCollections.dc.html`, `ScreenGraph.dc.html`, `ScreenGraphProse.dc.html`.
- [Layer enforcement](../../../adrs/gui/layer-enforcement.md), [presentation MVVM](../../../adrs/darwin/presentation-mvvm.md), [module schemes and previews](../../../adrs/darwin/module-schemes-and-previews.md); [the old design guide](../../../../design/spec/CLAUDE.md) (the 30-tick check).
- Siblings: [`react-design-prototype.md`](../../../adrs/design/react-design-prototype.md) (1a's ADR 0001), [`cli-stand-in.md`](cli-stand-in.md) (1d), [`styles-shell-and-the-first-screen.md`](styles-shell-and-the-first-screen.md) (1e, the gate; the sidebar's Code graph ⌘5 and Knowledge ⌘8 places), [`session-screen.md`](session-screen.md) (1f, where "Ask about this" lands), [`parity-proof.md`](parity-proof.md) (1s).

**Decisions made** (dated):
- 2026-11-14: The user's call: the prototype is rebuilt in React, TypeScript and Vite, with the production GUIs' four layers and MVVM modules, so a new app ports from it. It builds to one self-contained `lib/design/main.html` that opens with a double-click. This is a design-queue item in v0.2.1.
- 2026-11-14: The recorded default: the data is the Mac app's recorded fixtures, read in place; an unrecorded state gets a `*-handmade*` file in the CLI's shape. View-models are plain TypeScript.
- 2026-11-14: The recorded default: parity with the frozen old prototype is judged on layout, states, controls and wording, not pixels or data.
- 2026-11-14: The split: Knowledge and Code graph are item 1o, gated on 1e, two modules in one item. The recon's R13 grouped them with Library and Review (1n); both are small, and the prose graph is a collection's graph (`notes`), so they share data.

**Open calls:**
- [default: the code graph is the recorded `demo` graph (`stats.json`, `query.json`, `explain.json`). Recordings that name another size or graph (`graph-export-html.txt`'s 9 entities, `graph-delete.txt`'s `spare`) are used as recorded and listed in the record. One capture of export, report, delete and dedupe against `demo` would align them; the user runs it, after this item.]
- [default: node positions come from `GraphLayout`, computed from the document, as on the Mac. The prototype's hand-placed x and y fit only its invented nodes. The canvas keeps the prototype's look: card nodes, the selected one accented, its neighbours full strength, the rest faded.]
- [default: the Files view and the file tree are built from the recorded documents' file nodes and `file` fields. If no recording holds the import edges the Files view draws, a `graph-files-handmade.json` in `graph query`'s shape is made and named.]
- [default: the 10 "(prototype)" actions stay toasts in the prototype's words, naming the command. The Mac's real forms are named as differences and not ported; drawing them is new design, after 1o.]
- [default: the `knowledge` preset keeps the 3-record list and opens the rejected record, as the prototype does. All 3 records carry a `raw_ref`, but the only recorded raw conversation (`knowledge-info-raw.txt`) belongs to another record, `kr-20261026T142411Z-2e8272` in `knowledge-list-one.json`. So "Show the conversation" on the rejected record reads a `knowledge-info-raw-1e2354-handmade.txt` in that file's exact shape, holding the prototype's three lines. The recorded one backs a separate preview.]
- [default: like the prototype, neither place draws "Apogee isn't installed"; the Mac's do. The gap is named, a follow-up item draws it.]
- (consumed decision: 1d) where hand-made files live, and the preset type.
- (consumed decision: 1e) the confirm and sheet hosts, the More menu's scrim, the toast and routing.
- (consumed decision: 1s) the side-by-side for the 4 presets.

**Guardrail(s).**
- **The per-preset test** (the successor of the old guide's 30-tick `node -e` check). For each of the 4 presets it builds the place over the stand-in and advances 30 ticks. It fails on a throw, or on `undefined` or `NaN` in any view-model output. Proven on a planted fixture with a missing field.
- **View-model tests, without React:**
  - asking orders the records by `knowledge-query.json`'s scores and shows them to three places (0.596, 0.000, 0.000); the branch filter shows "Rejected" alone;
  - the raw toggle shows the recorded text; a collection search shows `collection-query.txt`'s lines;
  - selecting a node fills CALLED BY and CALLS from the recorded edges; Files switches the labels to IMPORTED BY and IMPORTS;
  - the save panel refuses an empty name; Save raises the recorded line with the chosen file name;
  - dedupe on `demo` shows Done only; on `notes` it offers "Merge them", then merging, then the merged note;
  - delete asks, then shows the gone state; Rebuild brings the graph back;
  - `GraphLayout` places a planted 40-node document with no overlap, inside the canvas.
- **1b's layering check stays green.** A planted `import … from 'react'` in `graph/ViewModels/` fails it, and so does a planted import of `knowledge` from `graph`.
- **A fixture check.** Every file the 4 presets name exists, and each traces to a recorded file or a `*-handmade*` one.

**Acceptance criteria:**
- [ ] `lib/design/main.html?start=<preset>` opens each of the 4 presets with the old prototype's regions, controls and wording.
- [ ] Both Knowledge tabs, both graph views, the prose graph, the More menu, the save panel, the dedupe sheet and the delete confirm work live.
- [ ] Every value on screen comes from a recorded or `*-handmade*` fixture.
- [ ] The per-preset test, the view-model tests, the layering check and the staleness check pass locally.
- [ ] The record names every place the port follows the prototype where the Mac app differs, and every recording that names another graph.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on [1e](styles-shell-and-the-first-screen.md). Out of scope:
- the confirm and sheet hosts, the menu scrim, the toast, and the sidebar's Knowledge and Code graph places (1e);
- the Mac-only parts named above (search, graph picker, New graph, the capture, ingest and export forms), and a missing-CLI state;
- any change to the Mac app or its fixtures, beyond a new `*-handmade*` file where 1d puts it;
- recording new CLI output, which the user runs;
- parity screenshots (1s).
