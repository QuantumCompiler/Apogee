# APP-M20 · A confirmed action runs: every dialog's button acts on the answer it showed

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | Apple apps · macOS |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Every confirmation dialog: Sessions, Models and its store, Review, Training, Code graph, Knowledge, Library |
| **Labels** | `confirmation-dialogs` `swiftui` `silent-failure` |
| **Epic** | — |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The user (bug report, 2026-11-21: deleting a session from its right-click menu never works) |
| **Created** | 2026-11-21 |

---

## Summary

Pressing the confirming button of a "Delete …?" (or any other "…?") dialog closes the dialog and does nothing: no command runs, nothing changes, nothing is said. The dialog's own closing clears the question the button was answering, and the action, started a moment later, finds no question and returns. Twenty-three buttons in twelve dialogs share the shape, deleting a session among them. This item makes every such button act on the answer it was built with, and pins the dialog's real order of events in the tests.

## Description

### User story

**As** a person using the Apogee app,
**I want** a button I press in a confirmation dialog to do what it says,
**so that** a session, a model, a record or a backend I chose to delete is deleted, and a switch, a compaction, a post or a pipeline run I confirmed happens.

### Steps to reproduce

1. In the session list, right-click any session, the open one or a saved one, and choose **Delete…**.
2. The Sessions place shows "Delete “<name>”?" with `apogee chats delete <id> -- the conversation and its turns are removed, and this can't be undone.`
3. Press **Delete**.

### Actual result

- The dialog closes. The session stays in the list and its file stays on disk.
- No `apogee chats delete` runs, and nothing is said: no toast, no "That didn't work".

### Expected result

- `apogee chats delete <id>` runs, the list is read again without the session, and the toast says "Deleted <name>", as `SessionLibraryViewModel.delete(_:)` already does when it is reached.
- The open session, when it was the one deleted, ends first and a new one opens in its place (unchanged).

### Background

The user reported it on 2026-11-21 from the development build of the app, which drove the release `apogee` 0.2.0 over `~/.apogee`, as every development build did until M18 ([Milestone A](../../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel)) shipped later the same day. They thought the old CLI might be the cause. It is not: the CLI deletes correctly (Evidence 1), and the fault is in the app's dialogs, the same in every build since v0.2.0.

Every confirmation in the app keeps its question in its view-model (`pending`, `pendingDelete`, `pendingRemove`, `removing`) and presents a SwiftUI `confirmationDialog` whose `isPresented` is a binding over it: `get` is "the question is set", and `set(false)` clears it. The confirming button starts a `Task` that calls the view-model's `confirm…()`, which reads the question back from the view-model. The probe below shows SwiftUI's order on a press: the button's action runs, then the dialog closes and calls the binding's `set(false)`, which clears the question, and only then does the `Task` run, so `confirm…()` finds nothing and returns. The view-model tests never press a dialog: they call `askToDelete` and then `confirm()` directly, an order the dialog never produces, so every one passes.

### Evidence

Measured, not guessed (2026-11-21; paths under `lib/src/darwin/`):

1. **The CLI deletes.** A sandbox home under the scratchpad (`APOGEE_HOME`, `apogee check --fix`), the user's three saved chats copied in (`~/.apogee/sessions/20261031-195317-0bee.json`, `20261114-200752-71b4.json`, `20261114-201004-0152.json`, read only). The installed `apogee 0.2.0 (fc380b4, 2026-10-31, macos-arm64)` ran `apogee chats delete 20261031-195317-0bee </dev/null`: it printed `deleted 20261031-195317-0bee`, exited 0, and the file was gone. The command the app builds (`source/presentation/sessions/ViewModels/SessionLibraryViewModel.swift:1125`, `["chats", "delete", chat.id]`) is that command. All three chats were still in the user's `~/.apogee/sessions` after their tries.
2. **The dialog loses the answer: the probe.** A throwaway macOS app (`swiftc`, Swift 6.4.2, the macOS 27 SDK) copying `SessionsScreen`'s dialog and `SessionLibraryViewModel`'s `confirm()` and `dismissPending()` exactly: an `@Observable` model, the same `Binding(get: { pending != nil }, set: { if !$0 { dismissPending() } })`, the same `Button("Delete", role: .destructive) { Task { await confirm() } }`. It set the question, found the dialog's real **Delete** `NSButton` in the app's windows and pressed it with `performClick`. Its log, every run:
   ```
   asked: pending = delete
   pressing the dialog's Delete (performClick)
   button action: pending = Optional(probe.Library.Pending.delete("20261031-195317-0bee"))
   binding set(false) -> dismissPending()
   confirm(): pending is nil -> returns, nothing deleted
   RESULT variant=current: deleted=[]
   ```
   The same probe with the answer captured in the action and handed to `confirm(answer)`: `binding set(false) -> dismissPending()`, then `confirm(answer): deleted 20261031-195317-0bee`, `deleted=["20261031-195317-0bee"]`. Three runs of each, the same result every time.
3. **Root cause, in the app.** `sessions/Views/SessionsScreen.swift:65-80`: the dialog's binding calls `library.dismissPending()` on `set(false)` (`:66`), and every button is `Task { await library.confirm() }` (`:71`, `:73`, `:75`, `:77`, `:79`). `SessionLibraryViewModel.confirm()` starts `guard let pending else { return }` (`:815-816`), and `dismissPending()` sets `pending = nil` (`:828-832`). So Delete, both kinds of switch, Compact and Forget never run from the dialog.
4. **The blast radius: every dialog built this way.** The same shape, a binding that clears the question and a button whose `Task` reads it back (paths under `source/presentation/`):
   - **Sessions** (`sessions/Views/SessionsScreen.swift:65`): Stop the turn and switch, End it and switch, Delete, Compact it, Forget every summary (`:71-79`), through `SessionLibraryViewModel.confirm()` (`:816`).
   - **Models** (`models/Views/ModelsScreen.swift:73`, `:89`): Delete its files, Delete its files and the backends that use them (`:78`, `:81-82`), through `ModelsViewModel.confirmDelete(andBackends:)` (`:507`); Remove the backend (`:94`), through `confirmRemove()` (`:525`).
   - **The model store** (`models/Views/StoreView.swift:40`): Delete, Delete it and the backends that run from it, Remove the backend, Register them (`:46-54`), through `StoreViewModel.confirm()` (`:714`).
   - **Review** (`review/Views/ReviewScreen.swift:47`): Post, Sweep and post, Remove (`:52-54`), through `ReviewViewModel.confirm()` (`:350`).
   - **Training** (`training/Views/TrainingScreen.swift:101`): its one confirming button (`:106`) for running and resuming a pipeline, running and resuming the cycle, a rollback, a dataset's delete and a halt, through `TrainingViewModel.confirm()` (`:1299`).
   - **Code graph** (`graph/Views/GraphScreen.swift:100`): Delete the graph (`:105`), through `GraphViewModel.confirmDelete()` (`:384`).
   - **Knowledge** (`knowledge/Views/KnowledgeScreen.swift:143`, `:322`): Delete the record and its archive (`:148`), through `RecordsViewModel.confirmDelete()` (`:283`); a collection's Delete (`:327`), through `CollectionsViewModel.confirmDelete()` (`:182`).
   - **Library** (`library/Views/LibraryScreen.swift:156`, `:249`, `:351`): an agent's Delete it and its files, Delete it, keep its files (`:161-162`), through `AgentsViewModel.confirmDelete(purge:)` (`library/ViewModels/LibraryViewModels.swift:298`); a symphony's Delete its config entry (`:254`), through `confirmDelete()` (`:590`); an MCP server's Remove its entry (`:356`), through `confirmRemove()` (`:698`).

   Twenty-three buttons in twelve dialogs in nine views. The probe ran Sessions' dialog; the other eleven are the same mechanism, read from the source.
5. **What already works.** Three dialogs whose action never reads back the question: Settings' Forget every summary (`settings/Views/SettingsView.swift:516-517`; `SettingsViewModel.forgetRecall()` clears `forgetting` first and does not guard on it, `:586-589`), Xcode's Remove (`settings/Views/XcodePane.swift:21-22`; `XcodeSettings.remove()`, `:239-242`), and the Library editor's closing dialog (`library/Views/LibraryScreen.swift:435-445`: Discard and Leave it run at once, and `restoreAndClose()` clears `closing` without guarding on it, `LibraryViewModels.swift:162-163`). Every "That didn't work" alert's OK is synchronous. The commands themselves, once reached, are right: each view-model's suite passes over them, and the CLI runs them (Evidence 1).
6. **Since when.** The shape was born with the first two places, the session library (`7cd77126`, "v0.2.0 : Session Library", 2026-10-20) and the models library (`b34cb9c0`, "v0.2.0 : Models Library", 2026-10-20), and every dialog since copied it. So the released v0.2.0 app has it too.
7. **Why the tests pass.** `tests/presentation/sessions/OverrideSkippedTests.swift:200-202` calls `askToDelete(chat)` then `confirm()` and finds `run chats delete <id>`; `SavedChatActsTests.swift:125-129` and `SessionLifecycleTests.swift:121-126` dismiss and then ask again before confirming. No test clears the question between the press and the action, which is the dialog's order.

## Acceptance criteria

- [ ] **Given** a saved session, the open one or another, **when** I right-click it, choose Delete… and press Delete, **then** `apogee chats delete <id>` runs, the session leaves the list and its file is gone, and the toast says "Deleted <name>"; for the open session, a new one opens in its place first, as today.
- [ ] **Given** any of the twenty-three confirming buttons in Evidence 4, **when** it is pressed, **then** its command runs exactly as the view-model runs it today when reached, with the same arguments (`--yes`, `--and-backends`, `--purge` / `--keep-files` as the button chose).
- [ ] **Given** any of those dialogs, **when** Cancel (or Keep it) is pressed, or the dialog is dismissed with Esc, **then** nothing runs and the question is cleared, as today.
- [ ] **Given** a question was replaced by a newer one before the press (another row's Delete… chosen while the dialog was up), **when** the button is pressed, **then** it acts on the answer the dialog showed, never on a question the person did not see.
- [ ] Each view-model's tests confirm in the dialog's own order (the question cleared, then the answer handed in) and find the command run; the suite is green with no warning.
- [ ] No confirmation dialog in `source/presentation/` has a button whose action reads its question back from the view-model after the dialog has closed; the check in Testing fails a new one that does.

## Constraints

- **The command is unchanged.** Every button runs what its view-model runs today when reached: the arguments, the read back, the failure said in the CLI's words, the toast. This item changes how the answer reaches the action, nothing the action does.
- **The view-models stay free of SwiftUI** ([ADR 0002](../../../../adrs/darwin/presentation-mvvm.md); the layering check fails a UI framework in a `ViewModels/` file). The answer crosses as the view-model's own value (`Pending`, a `ChatSummary`, a name), never a binding.
- **A destructive action never acts on a question the person did not see.** The answer is the one the dialog was drawn for; a newer question set meanwhile is not taken in its place.
- **Asked first, still.** Every destructive verb keeps its dialog; nothing that asks today stops asking (the house's destructive manners).
- **Every view can be seen on its own** ([ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md)): the dialogs' named previews (`SessionsScreen.swift:421`, "A delete waiting on the person's word", and the others) keep building.
- **Hermetic tests in Swift Testing.** Swift 6, no warnings, and a doc comment on every public declaration ([darwin CLAUDE.md → Code Style](../../../../assistant/darwin/CLAUDE.md#code-style)).

## Technical approach

Paths under `lib/src/darwin/source/presentation/`.

| File | Change |
|---|---|
| `sessions/ViewModels/SessionLibraryViewModel.swift` | `confirm()` becomes `confirm(_ answer: Pending)`: it acts on `answer`, and clears `pending` only if it is still `answer`. The doc comment says why: the dialog's closing clears `pending` before the action runs. |
| `sessions/Views/SessionsScreen.swift` | The dialog's actions are built from the question it shows (`if let answer = library.pending { … }`), and each button hands that answer in: `Task { await library.confirm(answer) }`. |
| `models/ViewModels/ModelsViewModel.swift`, `models/Views/ModelsScreen.swift` | `confirmDelete(_:andBackends:)` and `confirmRemove(_:)` take the answer; the two dialogs hand it in. |
| `models/ViewModels/StoreViewModel.swift`, `models/Views/StoreView.swift` | `confirm(_:)` takes the answer, and `andBackends` travels with it rather than being set on the model inside the `Task` (`StoreView.swift:46`, `:49`). |
| `review/ViewModels/ReviewViewModel.swift`, `review/Views/ReviewScreen.swift` | `confirm(_:)` takes the answer. |
| `training/ViewModels/TrainingViewModel.swift`, `training/Views/TrainingScreen.swift` | `confirm(_:)` takes the answer. |
| `graph/ViewModels/GraphViewModel.swift`, `graph/Views/GraphScreen.swift` | `confirmDelete(_:)` takes the graph's name. |
| `knowledge/ViewModels/RecordsViewModel.swift`, `knowledge/ViewModels/CollectionsViewModel.swift`, `knowledge/Views/KnowledgeScreen.swift` | Each `confirmDelete(_:)` takes its record or collection. |
| `library/ViewModels/LibraryViewModels.swift`, `library/Views/LibraryScreen.swift` | The agents' `confirmDelete(_:purge:)`, the symphonies' `confirmDelete(_:)` and the MCP servers' `confirmRemove(_:)` take theirs. |
| `../../tests/architecture/layering/LayeringCheck.swift` | The dialog check (Testing). |
| `../../tests/presentation/<module>/…Tests.swift` | See Testing. |

The shape, in Sessions:

```swift
.confirmationDialog(pendingTitle, isPresented: Binding(get: { library.pending != nil }, set: { if !$0 { library.dismissPending() } }), titleVisibility: .visible) {
    if let answer = library.pending {
        switch answer {
        case .delete: Button("Delete", role: .destructive) { Task { await library.confirm(answer) } }
        // … the other cases, each handing in `answer`
        }
    }
    Button(keepTitle, role: .cancel) { library.dismissPending() }
}
```

**Reference.**
- The probe in Evidence 2, which is this fix in miniature: the answer captured where the button is built survives the dialog's closing.
- The three dialogs that already work (Evidence 5): their actions never read the question back.

**Docs when it ships:**
- darwin `MILESTONES.md`: the fix and the dialog check, under the milestone each place shipped in (Sessions and Models in Milestone A and B, the rest where their places are recorded), or one maintenance entry naming them all;
- `DEVELOPER.md`'s rows for the changed view-models (the `confirm…(_:)` signatures) and the layering check's list of checks;
- the darwin `CLAUDE.md`, a line under the Presentation rules: a dialog's button hands its answer to the view-model; it never reads the question back.

## Testing

All hermetic: scripted runners, no window, no binary.

- **The dialog's order, per view-model.** For each of the nine view-models, a test asks, clears the question as the dialog's binding does (`dismissPending()`, or the property set to `nil`), then awaits `confirm…(answer)`, and finds the command run with its arguments. For Sessions: `askToDelete(chat)`, `dismissPending()`, `confirm(.delete(chat))`, then `runner.calls` contains `run chats delete <id>` and the toast says "Deleted <name>". The same order on today's API (`confirm()` after `dismissPending()`) runs nothing: that is the bug, pinned.
- **The open session's delete.** `askToDelete(library.currentChat)`, cleared, confirmed with the answer: the open session's child stops, a new session opens, then `chats delete` runs (the order `SessionLifecycleTests/stopThenDelete` pins today).
- **A newer question.** Ask about chat A, ask about chat B, confirm with A's answer: A is deleted and B's question stays; confirming with B's afterwards deletes B.
- **Cancel stays a no.** Ask, `dismissPending()`, and nothing else: nothing runs (the existing `jobs.starts.isEmpty` style).
- **Existing tests.** Every existing test that calls `confirm()` (`OverrideSkippedTests.swift:201`, `SavedChatActsTests.swift:97-205`, `SessionLifecycleTests.swift:126`, `ReadsAsDocumentsTests.swift:146`, the training and store suites) moves to the answer-taking form and keeps its checks.
- **The dialog check** (`LayeringCheck`): every `.confirmationDialog` in `source/presentation/**/Views/` whose button starts a `Task` hands that `Task` a value captured outside it; a `Task { await model.confirm() }` or `confirmDelete()` with no argument inside a dialog fails, naming the file and line. Verified against a planted violation (today's `SessionsScreen.swift:75`).
- **Gate:** `lib/scripts/cicd.sh --app darwin --test`, the layering check included.
- **A live look** is optional and the user's to take, on a development build, "Apogee Dev", which drives `apogee-dev` since M18 shipped (2026-11-21, [Milestone A](../../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel)): right-click a throwaway session, Delete…, Delete.

## Open questions

| Question | Status | Answer |
|---|---|---|
| How does the answer reach the action? | `[default]` | The view-model's `confirm…` takes the answer the dialog was built with, and the view captures it where it builds the button. Rejected: a synchronous wrapper in the view-model that captures `pending` and starts its own `Task`, which works only because the action happens to run before the dialog closes (Evidence 2) and leaves the tests unable to await the work; and a binding whose `set(false)` does not clear the question, which leaves a stale question behind every dismissal. |
| What keeps a new dialog from repeating it? | `[default]` | A source check in the layering check (Testing), because the view-model tests cannot see SwiftUI's order. The probe stays a scratch tool, not a test: it needs a window, and the suite runs none. |
| Is the probe kept in the repository? | `[default]` | No. Its method and output are recorded in Evidence 2; it can be rebuilt from them. |
| Does the CLI change? | `(consumed decision)` | No: `apogee chats delete` and every other verb here already work (Evidence 1). |
| Does the development app drive `apogee-dev`? | `(consumed decision)` | M18's, shipped 2026-11-21 ([Milestone A](../../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel)); not this item's. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-21 | Reported by the user from the development app: deleting a session from its right-click menu never works. | — |
| 2026-11-21 | Diagnosed the same day: the CLI deletes (sandboxed run of the installed 0.2.0), and the dialog clears its question before its action runs (the probe); 23 buttons in 12 dialogs share the shape. | — |
| 2026-11-21 | Maintenance, as M20, claimable at any time by name, no gate: a bug's fix is queued in the maintenance table, where the user asked the investigation be finished. | the spec |
| 2026-11-21 | One item for every dialog, not one per place: one cause, one change of shape, and one check that holds them all. | the spec |

## Out of scope

- The CLI: every verb here already works.
- What any action does once reached: the commands, their read-backs and their wording stay as they are.
- The development app driving the release CLI (M18).
- Dialogs added after this item ships, beyond the check that refuses the shape.

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:** M18 ([Milestone A](../../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel), the development build for the live look); [ADR 0002](../../../../adrs/darwin/presentation-mvvm.md); [ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md); the session library and the models library (v0.2.0, 2026-10-20, [MILESTONES](../../../../assistant/darwin/MILESTONES.md)), where the shape began.
