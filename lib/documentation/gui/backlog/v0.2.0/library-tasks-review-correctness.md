# Library, Tasks, Knowledge and Review do what their buttons say

**What / why.** The parity wave's screens (2c–2g) shipped fast. The CLI/GUI audit (2026-10-22, three-lens verified) found nine correctness defects across them:

1. **Tasks' Resume runs in the wrong folder, so the CLI refuses it** (high). `TasksViewModel.swift:206-208` runs `task resume <id>` with `folder: nil`, which means the user's home (`SupervisedJob.swift:130`, `:174`), while `start()` passes the form's folder (`:200`). The CLI refuses a resume outside the task's recorded directory (`task_cmd.cpp:1491-1494`, enforced at `:571`). Resume therefore fails for every task started anywhere but home, including tasks the app started, and contradicts GUI MILESTONES:581. The folder is already read (`business/reads/Tasks.swift:91,116`). The TUI guards the same key on cwd (`tui_task.cpp:296-305`).
2. **Tasks offers Resume on exhausted tasks and never on killed-mid-round ones.** `canResume` (`TasksViewModel.swift:272-275`) includes `exhausted`, which the CLI categorically refuses (`business/tasks/runner.cpp:426-437`). A task killed mid-round keeps status `planning`/`running` with `interrupted: true` (`view.cpp:79-84`, `:165`), a field `TaskView` never parses. So the one state `task resume` exists for ("killed mid-round", `task_cmd.cpp:1245`) shows Halt/Cancel as if still running.
3. **The Library editor's save writes back a stale copy, discarding text typed during the save.** Both `save()`s (`LibraryViewModels.swift:126-140` agents, `:254-274` symphonies) copy the editor, write, await a child run, then assign the stale copy back (`:139`/`:272`). The TextEditor stays enabled and bound throughout (`LibraryScreen.swift:348`), so keystrokes typed mid-save are reverted and never reach the file, while the verdict says "Saved."
4. **A fresh install's Knowledge Records tab shows a read failure, not the empty state.** `knowledge list` exits 1 by design on a store that never captured (`knowledge.cpp:209-216`, the typo guard), and `RecordsViewModel` maps every failed read to `.failed` (`:91-101`). The designed empty state ("No decision records yet…", `:124`) can't show before the first capture. Collections gets it right (`embed list` exits 0 with `{"data":[]}`).
5. **A symphony play's document lands on whichever symphony is selected when it finishes.** `playIt()` (`LibraryViewModels.swift:213-236`) assigns `self.play` (`:230-231`) without re-checking the selection, and its spinner and Stop follow the user onto the other page. `CollectionsViewModel.search` (`:136-147`) has the same missing guard, though its sibling `readInfo` (`:116`) has it.
6. **Library and Review-targets refreshes swallow read failures silently.** Agents (`:93`), symphonies (`:200`), MCP list and presets (`:352-353`), and review targets and skills (`ReviewViewModel.swift:180-184`) keep the old list on a failed read, with nothing said. A wedged `mcp list` (which connects to every server) times out at 60 s (`ReadRunner.swift:18`) and is silently dropped. Records and Tasks map failures to a visible state.
7. **Review and Training surface a missing binary as a repeating modal alert.** `refresh()` sets `failure` when the binary is nil (`ReviewViewModel.swift:176-179`; Training the same), and `appeared()` re-runs it every visit, so every visit pops "That didn't work". Records, Collections and Tasks draw "Apogee isn't installed…" as empty text instead.
8. **MCP Register selects the new name even when the create failed.** `register(_:as:)` (`LibraryViewModels.swift:360-365`) sets `selected = name` unconditionally after a Void `change()` (`:385-394`). On a refused create, the detail pane vanishes and any prior selection is lost. `TrainingViewModel.createDataset` (`:275-281`) repeats it; `AgentsViewModel.create` (`:98-102`) shows the intended Bool-returning shape.
9. **The Add-target sheet has no lenses control**, though `TargetForm` carries `skills` and emits `--skills` (`ReviewViewModel.swift:66`, `:82`) and 2g's record claims it (GUI MILESTONES:1066). `TargetSheet` (`ReviewScreen.swift:425-440`) offers none, while the RunForm beside it has a lenses menu (`:146-153`).

**Core constraint(s).** **A button runs only what can succeed, where it must run**: resumes run in the task's recorded folder and are offered for exactly the states the CLI resumes. **Async results land where they started**: every completion re-checks its captured selection before assigning (the `readInfo` pattern), and editor saves never overwrite newer text (disable the editor during save, or merge). **One empty-state discipline**: a designed "nothing yet" refusal and a missing binary are empty states, and other failed reads are said, never silently kept. **Selection follows success**: selection changes only on a successful create. ADR 0002, scripted reads.

**Seam + files.** `presentation/tasks/ViewModels/TasksViewModel.swift` + `business/reads/Tasks.swift` (`folder` on resume; `interrupted` parsed; `canResume` from the CLI's set), `presentation/library/ViewModels/LibraryViewModels.swift` + `Views/LibraryScreen.swift` (save without stale write-back; selection guards; failed reads said; Bool `change` for MCP), `presentation/collections/ViewModels/CollectionsViewModel.swift` (search guard), `presentation/knowledge/ViewModels/RecordsViewModel.swift` (the no-collection refusal as empty), `presentation/review/` and `presentation/training/` (missing binary as empty text; the lenses menu on TargetSheet), `TrainingViewModel.createDataset`. Tests: one per finding over scripted documents and jobs.

**Reference.** GUI MILESTONES Milestone C (2c–2g); the TUI's task guard (`tui_task.cpp`); the CLI's resume rules (`runner.cpp`, `task_cmd.cpp`); the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call).

**Open calls:**
- [default: the editor is read-only while a save runs, then re-enabled with the saved text (simplest correct fix).]

**Guardrail(s).** Each finding pinned over scripted reads and jobs; no live binary in tests.

**Acceptance criteria:**
- [ ] Resume runs in the task's folder, offered for exactly the states the CLI resumes.
- [ ] No save loses typing; no async result lands on another selection; selection follows only successful creates.
- [ ] Fresh installs and missing binaries show empty states; failed reads are said; targets can pin their lenses; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated.
