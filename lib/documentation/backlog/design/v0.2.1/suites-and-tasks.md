# Suites and Tasks in the React design app

**What / why.** This item ports two places of the old prototype into the React design app: Suites (a main model plus helpers, and whether they fit this Mac) and Tasks (the ledger's tasks, a task's rounds, grants and checks, and the New task sheet). Measured on 2026-11-14:
- **Their size in the old prototype** (`lib/design/spec/html/main.html`), in the recon's 7,604-line copy:
  - the `suites` section of `renderVals()`, 6022–6056 (35 lines, builds `sui`), and the `ui.isSuites` block, 1398–1438 (41);
  - the `tasks` section, 6057–6139 (83 lines, builds `tsk` and the sheet's `ntv`), the `ui.isTasks` block, 1440–1507 (68), and the `ui.newTaskOpen` sheet, 2348–2381 (34).
- **At HEAD** (7,300 lines, last changed in `231fb049`): `suites` 5727–5761, `tasks` 5762–5844, `ui.isSuites` 1372–1412, `ui.isTasks` 1414–1481, the sheet 2320–2353. Same sizes as the recon's; only the positions moved. Helpers: `recipes()` 3546, `suites()` 3619, `taskView()` 3627, `suiteFoot()` 3831.
- **Tasks is still growing.** Another session's uncommitted work is catching the prototype up to the Mac's shipped 6g ("the Tasks screen shows what the ledger holds"). Its line numbers moved twice while this was written (7,780, then 7,786 lines), so only its shape is recorded here:
  - the `tasks` section grows to about 114 lines and `ui.isTasks` to 75;
  - new helpers `tasksStart()`, `taskGrants()` and `taskRefusal()`;
  - a new `tick()` step finishes a resume after 4 ticks, so the recon's "tasks are not stepped by `tick()`" no longer holds;
  - a new confirm kind, `taskRefused`.
  - The builder reads every range from 1c's frozen baseline.
- **Presets.** HEAD has 4: `suites`, `tasks`, `newtask`, `newtask:recipe` (the recon's count). The working tree adds 5: `tasks:denied`, `tasks:more` (a ledger of 60, the newest 50 shown), `tasks:resuming`, `tasks:refused` and `tasks:resumerefused`.
- **Boards.** 6 on the canvas: `ScreenSuites`, `ScreenTasks`, `ScreenNewTask`, `ScreenNewTaskRecipe`, and the untracked `ScreenTasksDenied` and `ScreenTasksRefused`. `tasks:more`, `tasks:resuming` and `tasks:resumerefused` have no board.
- **The data is invented** (recon §3, critic V3):
  - 4 canned tasks keyed `{id, goal, kind, status, base, since, rounds, suite, started, writes, cmds, haltAt}`. The CLI's `task list` row is `{id, status, interrupted, rounds_used, rounds_budget, goal}` (machine-mode.md:1053–1059).
  - Each task's plan, rounds and tool uses are tables inside `taskView()`, keyed by `kind`.
  - 3 canned suites (`coding`, `research`, `big`). The recorded suites have other names, such as `cloud`, `fast`, `pair`, `shared` and `vast`.
  - Only `recipes()` uses real names (capture-decisions, changelog-draft, digest-folder), with camelCase `requireFiles`.
- **The recordings exist.** In `lib/src/darwin/tests/`:
  - `presentation/tasks/Fixtures`: 22 files, all `task-*` (15 json, 4 jsonl, 2 err, 1 txt). The recon counted 19 `task-*`; it is 22 today. All are committed. They cover the list (live, all, newest 50, interrupted, killed), status (exhausted, halted, interrupted, killed, resumed, stalled, uses), the run and resume streams, and both refusals (`task-run-refused.err`, `task-resume-refused.err`).
  - `business/reads/Fixtures`: `task-recipes.json`, `task-recipe-show.json`, and 35 `suites-*` files (`config get`, `models status --suite`, typed footprints, `models info`, `system`).
- **The Mac modules:** `suites` (5 files, 1,001 lines; `SuitesViewModel`, `SuitesScreen`, `SuiteDetail`, 14 previews) and `tasks` (5 files, 1,879 lines; `TasksViewModel`, `NewTaskForm`, `TaskRunState`, `TaskUseWords`, and views `TasksScreen`, `TaskList`, `TaskDetail`, `NewTaskSheet`, `TaskStatusWords`, 24 previews). 10 test files between them.
- **Where they differ** (recon G12): the prototype has New suite… and a Rerun with more rounds; the Mac has neither. The Mac has empty states ("No suites yet", "No tasks yet", "No task chosen") that no prototype preset shows.

**Core constraint(s).**
- **The prototype is the source of truth.** Every control, state and word it shows stays, including New suite… and Rerun. Where the Mac app differs, the view-model says so in a comment. Names follow the Mac's where they exist.
- **A run's view is its events.** Rounds, grants, allowed and denied uses come from a recorded stream or a recorded `task status`, never from `taskView()`'s tables. Controls are the verbs a person types: `task halt`, `task cancel`, `task resume`, `task run`.
- **Refusals are the CLI's words.** A start or resume the CLI refuses shows its stderr's last line, as recorded, with OK alone.
- **The suite's fit is the CLI's verdict** (`models status --suite <name>`), never one worked out in the view.
- **View-models are plain TypeScript.** They never import React and take their services through the constructor.
- **No module imports another place.** Open a chat under a suite, Switch the session to it, and the default suite all reach Sessions and Models through the app root.

**Seam + files.** Paths under `lib/design/prototype/`, in 1b's layout.
- `source/presentation/suites/`:
  - `ViewModels/SuitesViewModel.ts`: the list, the chosen suite, its members and fit, the default, and the two session actions as injected closures.
  - `Views/`: `SuitesScreen`, `SuiteDetail`.
  - `Previews/`: one per preset.
- `source/presentation/tasks/`:
  - `ViewModels/`: `TasksViewModel` (list, Show all, selection, halt, cancel, resume, rerun), `NewTaskForm` (goal or recipe, input, grants, checks, rounds, suite, and the command line it shows), `TaskRunState` (a live run folded from its events).
  - `Views/`: `TasksScreen`, `TaskList`, `TaskDetail`, `NewTaskSheet`, and the `taskRefused` alert.
  - `Previews/`: one per preset.
- The CLI stand-in (1d): each preset's fixtures wired in, read in place. A state nobody recorded gets a `*-handmade*` file in the CLI's exact shape.
- The app root (1e): wires the suite actions to Sessions, the default suite to Models, and the Tasks badge in the sidebar.
- Tests under `tests/presentation/suites/` and `tests/presentation/tasks/`.

**Reference.**
- The old prototype: the sections, blocks and helpers above; [`spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md) ("Checking a change").
- The Mac modules: `lib/src/darwin/source/presentation/suites/` and `.../tasks/`, and their tests; [darwin MILESTONES](../../../assistant/darwin/MILESTONES.md) 4i (training and suites parity) and 6g (the Tasks screen).
- The CLI's documents: [machine-mode.md](../../../reference/cli/machine-mode.md) (`task list`, `task status`, `task run`).
- The law: [layer enforcement](../../../adrs/gui/layer-enforcement.md), [presentation MVVM](../../../adrs/darwin/presentation-mvvm.md), [module schemes and previews](../../../adrs/darwin/module-schemes-and-previews.md), [the darwin guide](../../../assistant/darwin/CLAUDE.md).
- Siblings: [`react-design-prototype.md`](../../../adrs/design/react-design-prototype.md) (1a's ADR 0001), [`DEVELOPER.md` → the old prototype's baseline](../../../assistant/design/DEVELOPER.md#the-old-prototypes-baseline) (1c's baseline, shipped), [`DEVELOPER.md` → the CLI stand-in](../../../assistant/design/DEVELOPER.md#the-cli-stand-in) (1d), [`styles-shell-and-the-first-screen.md`](styles-shell-and-the-first-screen.md) (1e), [`session-screen.md`](session-screen.md) (1f), [`models-screens.md`](models-screens.md) (1h), [`parity-proof.md`](parity-proof.md) (1s).

**Decisions made** (dated):
- 2026-11-14: The user's call: rebuild the prototype in React, TypeScript and Vite, with the production apps' four layers and MVVM feature modules. The build is one self-contained `lib/design/main.html` that opens with a double-click. This is a design-queue item in v0.2.1.
- 2026-11-14: The recorded default: the prototype's behavior is ported, in the Mac module's shape (recon G12).
- 2026-11-14: The recorded default: the old prototype is frozen (fixes only) from 1c's baseline, and new design waits for 1e.
- 2026-11-14: The recorded default: data comes from the Mac app's recorded fixtures, read in place; an unrecorded state gets a `*-handmade*` file. Parity is judged on layout, states, controls and wording, not on pixels or data.

**Open calls:**
- [default: port every Suites and Tasks preset in 1c's baseline: all 9 if the 5 new `tasks:*` presets are committed before 1c records it, else the 4 at HEAD.] (1c recorded all 9 at `b375fac9` on 2026-11-18: `suites`, `tasks`, `tasks:denied`, `tasks:more`, `tasks:resuming`, `tasks:refused`, `tasks:resumerefused`, `newtask`, `newtask:recipe`.)
- [default: the canned rows give way to recorded ones. The list shows `task-list-*.json`, a task's page its `task-status-*.json`. A live task replays `task-run.jsonl` on the clock, Stop after this round replays `task-run-halted.jsonl`, and Resume replays `task-resume.jsonl`. The suites are the recorded `suites-*` set.]
- [default: Rerun with more rounds (doubling, at most 32) starts a new run with the same goal and grants, replaying `task-run.jsonl`. It is named as prototype-only in the view-model.]
- [default: New suite… keeps the prototype's toast. It has no CLI verb behind it yet.]
- [default: the New task sheet's command line and its "The same run as typing" expansion are built by `NewTaskForm`, ported line for line from the section, with unit tests on each flag.]
- [default: Folder in the sheet stays read-only, showing the preset's folder.]
- (consumed decision: 1d) the stream replay, the clock and the preset table. (consumed decision: 1e) the confirm host, the toast and the sidebar badge. (consumed decision: 1f) `chat:suite` and the suite sheet in a session. (consumed decision: 1c) the baseline's revision, which fixes the preset list (it captures every preset, board or not). (consumed decision: 1s) gallery boards for `tasks:more`, `tasks:resuming` and `tasks:resumerefused`.

**Guardrail(s).**
- **Per-preset test** (the successor of the 30-tick check), for every preset in both "installed" and "missing" modes: build the view-model on the stand-in, step the clock 30 ticks, and walk everything the views read. No throw, no `undefined`, no `NaN`.
- **Behavior tests:**
  - Show all appears only past 50 rows and lists all 60 (`task-list-newest-50.json`, `task-list-all.json`);
  - halt stops after the round in flight; cancel stops now; a halt sent while a resume starts up is kept;
  - an interrupted task shows Resume and no stops, and an exhausted one shows no Resume;
  - a refused start leaves no row, and a refused resume keeps its line on the task's page, each with the recorded `.err` text verbatim;
  - a use the grants cover lands under Allowed, one they don't under Denied, with its round;
  - the sheet's Start is disabled with no goal, or with a recipe that takes input and has none;
  - a suite over budget relabels both actions "…anyway" and adds `--force`.
- **Data test:** a planted task row or suite name written into a view or view-model fails it; every row traces to a fixture.
- **The layering check (1b):** no React in `ViewModels/`, and neither module imports `session`, `sessions` or `models`. A planted import of each fails.

**Acceptance criteria:**
- [ ] `lib/design/main.html?start=<preset>` opens every Suites, Tasks and New task preset, and each matches the 1c baseline on layout, states, controls and wording.
- [ ] A live task plays through its rounds from a recorded stream; halt, cancel, resume and rerun each behave as in the old prototype.
- [ ] Every row comes from a recorded fixture or a `*-handmade*` file; no task or suite data is written into the source.
- [ ] Open a chat under a suite and Switch the session to it reach Sessions through the app root.
- [ ] The local checks are green: typecheck, unit tests, the layering check, and the build staleness check.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on 1e. Out of scope: the session's own suite sheet and `chat:suite` (1f), the Roles tab and the Models side of the default suite (1h), the side-by-side report (1s), gallery boards for the board-less presets (1s), and any change to the Mac app's Suites or Tasks. The Mac's empty states ("No suites yet", "No tasks yet", "No task chosen") are new design, drawn in the React app after the port, not here.
