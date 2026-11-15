# The parity proof: the React app against the old prototype, preset by preset

**What / why.** Before the old prototype is deleted ([retire-the-old-prototype.md](retire-the-old-prototype.md)), someone has to show that the React app draws everything it drew. This item is that proof. For every start preset it puts two still pictures side by side, the old prototype at 1c's frozen revision and the new built file at the same preset, and lists every difference. Each difference is classed as intended or as a defect, and every defect is fixed before this item ships. The user looks at the report before 1t retires anything. Measured 2026-11-14:
- **The presets.** The recon read 65 `start` options: 59 with a Screen board and 6 without (`chat:compacting`, `library:unsaved`, `models:notran`, `settings:fixing`, `settings:fixfailed`, `settings:nosearch`). The list is still moving, re-checked while writing this:
  - at HEAD (`48f46831`): 57 presets (`main.html:2462`), 56 boards;
  - in the working tree: 70 presets (`main.html:2497`), 61 boards, 9 without one (the 6 above plus `tasks:more`, `tasks:resuming`, `tasks:resumerefused`).
  - So this item works from the list 1c's `baseline.json` records, never a number written here.
- **No preset opens Training's Regimes tab.** The old `applyStart()` ignores a `training:regimes` sub and opens Runs (`main.html:2757–2758`, working tree). 1j adds the preset to the new app, so the old side of that pair has to be reached by a click.
- **The old data is invented** (critic V3: 0 `object:` keys, 0 `*_at`, `rounds_used` or `seed_source` keys in the script). The new app shows the CLI's recorded documents. Names, counts, sizes and transcripts will differ on nearly every screen, so pixel or string identity is impossible. Parity is structural: the same regions, states, controls and wording.
- **Some differences are already decided** by feature items, for example: compose's stopped project keeps "Stopped" while its document says `cancelled` (1l); the inspector stays at the prototype's 304 px against BuildLayout's 360 (1g); every preset's sidebar now lists the recorded chats (1g). These must be listed and cited, not rediscovered as defects.
- **Besides the screens**, 1c captures the 14 other boards once per prop value. 9 are rebuilt and judged here: the 3 Build boards (1r), ColorTokens, TypeSpace and the 4 Components boards (1q). The 5 Exploration boards are not ported (1q's default), so they have no new side.

**Core constraint(s).**
- **The old side is 1c's baseline,** re-made by its script from the recorded revision. Never the canvas, never a working-tree copy.
- **The new side is the built file,** opened from disk as the user opens it: `file://…/lib/design/main.html?start=<preset>&still=true`. Not the dev server.
- **Same frame on both sides:** Chromium, 1440 × 900, the clock stopped, the browser clock fixed and `Math.random` seeded as 1c does, fonts loaded before the picture.
- **Judged on structure, not pixels or data.** A difference in data is intended. A difference in a region, a state, a control or the wording around the data is a defect unless a shipped record names it.
- **No defect ships.** Each is fixed in the module that owns it, with a test where one fits.
- **The old prototype stays frozen.** A bug found in it is noted in the report. It is fixed there only under 1c's rule: a fix, then a re-record of the baseline.
- **Nothing committed but text.** Images and the report page stay out of git.

**Seam + files.** Everything under `lib/design/prototype/` unless named otherwise.
- `scripts/parity.ts`, new, run as `npm run parity`:
  - re-makes or reuses 1c's baseline images (checked against its hash list);
  - captures the new build at every preset in `baseline.json`, at each `apogee` mode 1c kept, plus the presets the new app added (1i's four pane presets, 1j's `training:regimes`) and the gallery's pages for the 9 rebuilt boards;
  - for `training:regimes`, captures the old side by loading `training` still and clicking the Regimes tab;
  - reads each page's outline with Playwright's ARIA snapshot (landmarks, headings, tabs, buttons, fields, menus, dialogs, their labels, and the text around them), and diffs the two outlines;
  - matches each difference against `verdicts.json`, and fails on any difference with no verdict, or with a verdict of defect;
  - writes `report/index.html` beside the images in the gitignored `.parity/` folder (1c's), and the summary table as Markdown.
- `parity/verdicts.json`, new, committed: per preset, each difference with its class and its reason:
  - `data`: real CLI data replacing invented data, naming the fixture;
  - `fix`: a documented change, citing the item and its record in [MILESTONES.md](../../../assistant/design/MILESTONES.md);
  - `defect`: allowed only while the item is in progress; each is fixed and removed before ship.
- The gallery's board table (1q): each board-less preset in `baseline.json` and `training:regimes` gets its board, judged against its old pair and titled in the prototype's words. 1q already gives every preset a frame at the end of its row; this item settles those frames and adds any the table lacks.
- Fixes for defects, in the modules that own them, each with its view-model test where the defect was behavior.
- `package.json`: the `parity` script. It is not part of `npm run check`.
- Docs: the design's `DEVELOPER.md` (1b writes it; this item adds how to run the proof and read the report); MILESTONES.md on ship, with the summary table; [ROADMAP.md](../../../assistant/design/ROADMAP.md).

**Reference.**
- The recon of 2026-11-14: prototype map §5 (presets), boards §0–§3 (the 14 other boards, `canvas.json` rows and titles), critic V3 and its R16.
- The baseline and its rules: [old-prototype-baseline.md](old-prototype-baseline.md) (1c), its `baseline.json`, its capture and its frozen-revision re-record.
- The old guide's 30-tick check ([`spec/CLAUDE.md` → Checking a change](../../../../design/spec/CLAUDE.md)), whose successors are the feature items' per-preset tests. This item checks how the result looks, not whether it throws.
- The feature items whose records cite their intended differences: [sessions-inspector-execute-and-console.md](sessions-inspector-execute-and-console.md), [models-screens.md](models-screens.md), [settings-and-appearance.md](settings-and-appearance.md), [training-screens.md](training-screens.md), [compose-screens.md](compose-screens.md), [voice-screen.md](voice-screen.md), [library-and-review.md](library-and-review.md), [knowledge-and-graph.md](knowledge-and-graph.md), [suites-and-tasks.md](suites-and-tasks.md), [gallery-and-foundations.md](gallery-and-foundations.md), [build-boards-as-pages.md](build-boards-as-pages.md).

**Decisions made** (dated):
- 2026-11-14: The user's call: rebuild the prototype in React, TypeScript and Vite with the production GUIs' layers, so the prototype stays the design's cross-platform source of truth. This proof is what lets the old one go.
- 2026-11-14: The user's call: "Mostly review there; make the canvas view-only." The built file is the review surface, so the new side is captured from the built file, and the canvas plays no part.
- 2026-11-14: The user's call: the build is one self-contained `lib/design/main.html` that opens with a double-click. So the new side is opened from disk.
- 2026-11-14: The user's call: parity screenshots by Playwright, downloaded once as a dev dependency (about 150 MB), approved by the user when the item builds. 1c installs it; this item downloads nothing new.
- 2026-11-14: The user's call: a design queue of its own; this item is design 1s, in v0.2.1.
- 2026-11-14: The recorded default: parity is judged on layout, states, controls and wording, not pixels or data, since the old data is invented.
- 2026-11-14: The recorded default: the old prototype is frozen from 1c's baseline, fixes only.
- 2026-11-14: The recorded default: checks run locally, not in CI. `changed.sh` and the workflows are untouched.
- 2026-11-14: The split: this item owns the gallery boards for the board-less presets and `training:regimes`, once each pair is judged. 1q builds the gallery and gives every preset a frame; the presets themselves come from their feature items (`training:regimes` from 1j).

**Open calls:**
- [default: the report is a generated HTML page outside git, in the gitignored `.parity/report/` beside 1c's images (not a session scratchpad, which the script cannot know), opened from disk and sent to the user. It is not published as an artifact: about 140 pairs of pictures at 1440 × 900 would likely pass an artifact's 16 MB. The summary table goes in the milestone record. Images are never committed.]
- [default: the outline diff is the measure, the pictures are for the eye. A pixel overlay may be shown as a reading aid, never as pass or fail.]
- [default: a difference that is neither data nor named in a shipped record is a defect, even when the new way looks better. The builder either matches the old prototype, or writes the reason into the report and asks at the user's look. Nothing is kept silently.]
- [default: text that comes from a document (a name, a count, a date) is classed `data` by naming its fixture. Text the view writes itself (a label, a heading, a note, a refusal's wording around the CLI's words) must match.]
- [default: a preset the new app adds and the old prototype opens by the same start string (1i's `settings:roles`, `:web`, `:memory`, `:keyboard`, through `applyStart()`'s pane fall-through, `main.html:2805` in the working tree) is compared the same way. One it cannot open is reached by a scripted click, written in `parity.ts` beside the preset.]
- [default: `npm run parity` stays out of `npm run check`. It needs the archived old revision served and takes minutes. 1t runs it again before deleting anything.]
- [default: `verdicts.json` stays committed until 1t, which decides whether it is kept as history.]
- (consumed decision: 1c) the baseline's revision, preset list, capture settings and re-record rule. (consumed decision: 1q) the gallery's form, its rows, and where a board with no old pair sits and how it is titled ("Place · state", at the end of its row). (consumed decision: 1r) the Build boards as pages. (consumed decision: 1t) the deletion, the canvas, and what happens to `verdicts.json`.

**Guardrail(s).** Unit tests beside the script (1b's runner), each on a planted case:
- the outline differ: a planted missing button, a renamed tab and a lost dialog each show as a difference; a changed fixture name inside a row shows as text, not as a control;
- a planted difference with no verdict fails `npm run parity`, naming the preset and the line;
- a verdict marked `defect` fails it;
- a `fix` verdict that cites no item fails it;
- a preset in `baseline.json` with no counterpart in the new app fails, naming the preset;
- gallery coverage stays 1q's "every preset has a frame" test in `npm run check` (a planted preset with no frame fails); this item adds no second copy.

A real run is made twice; the two outline sets must match.

**Acceptance criteria:**
- [ ] `npm run parity` captures every preset in `baseline.json`, the kept `apogee` modes, the new presets and the 9 rebuilt boards, and passes.
- [ ] The report shows each pair side by side with its differences, each classed `data` or `fix` with its reason. No defect is open.
- [ ] Every board-less preset at the baseline's revision, and `training:regimes`, has a gallery board in the built file.
- [ ] MILESTONES.md carries the summary table: each preset, its differences by class, and the defects fixed.
- [ ] The user looks through the report and says so, before 1t starts.
- [ ] The local checks are green, and nothing under `lib/design/spec/` changes in this item.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on 1c ([old-prototype-baseline.md](old-prototype-baseline.md)), 1g, 1h, 1i, 1j, 1l, 1m, 1n, 1o, 1p, 1q and 1r. Out of scope: deleting or rewriting the old prototype and its guide (1t), the canvas and its comments (1t), new design (it waits for the port to finish), pixel or data identity, CI, and any change to the Mac app.
