# APP-M19 · Copy in the Xcode pane says it copied

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | Apple apps · macOS |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Settings › Xcode (the `settings` module, with the toast promoted from `shell` to `styles`) |
| **Labels** | `design-parity` `accessibility` |
| **Epic** | — |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | Design pass (2026-11-14, its whole-design check of the v0.2.1 states) |
| **Created** | 2026-11-14 |

---

## Summary

Pressing **Copy** on a field in Settings › Xcode writes the value to the pasteboard and says nothing, and a row with no label has a Copy that VoiceOver reads as "Copy " with nothing after it. The design says "Copied to the clipboard" and names every button. This item makes Copy say it in the window where it was pressed, as the design draws it, gives the Settings window its own toast for every line Settings says while it is in front, and names every Copy for what it copies ("Copy the next entry" for a row with no label).

## Description

### User story

**As** a person using Apogee who registers it as an agent in Xcode,
**I want** each Copy in Settings › Xcode to tell me it worked, in the window I pressed it in, and to say what it copies,
**so that** I can paste the fields into Xcode's Add an Agent sheet knowing they were copied, by sight or by VoiceOver.

### Steps to reproduce

1. Open Settings › Xcode with the recorded registration (the Add an Agent fields, each with Copy).
2. Press **Copy** next to any field.
3. With VoiceOver on, move to the Copy button on the `PATH=…` row (the second environment variable, which has no label).

### Actual result

- The value is on the pasteboard, and nothing on screen changes.
- VoiceOver reads the `PATH=…` row's button as "Copy " with nothing after it.
- Any line Settings says (Register, a key, a fix) shows in the main window's toast, seen only if the bottom of the main window is in view.

### Expected result

- "Copied to the clipboard" shows at the bottom of the Settings window, as the design draws it.
- Every Copy is named "Copy" plus the field's label, or "Copy the next entry" for a row with no label.
- While Settings is in front, every line Settings says shows in its window.

### Background

GUI 5a (shipped 2026-11-08, [Milestone F](../../../../assistant/darwin/MILESTONES.md#milestone-f--apogee-in-xcode-the-apps-side)) gave Settings › Xcode the fields Xcode's Add an Agent sheet takes, each with a Copy button. On 2026-11-14 a design pass drew the app's v0.2.1 states in the design's old one-file prototype (which design 1t retired on 2026-11-21; the design app draws the same states). Its whole-design check found that the design tells the person a Copy worked and the app does not. It was confirmed in the source the same day (Evidence below).

So wiring Copy to `say` alone would put "Copied to the clipboard" in a window the person may not be looking at. This item makes Copy say it in the window where it was pressed, as the design draws it, and names every Copy for what it copies. While Settings is in front, every other line Settings says shows there too: Register's and Remove's, a key's, a fix's or a bootstrap's, and the binary override's (`SettingsViewModel.swift:285`). That is how the design draws Register's line, over its dialog (`main.html` line 6242, with the toast at line 2509).

### Evidence

Confirmed in the source on 2026-11-14, the day of the design pass (paths under `lib/src/darwin/source/presentation/`):

1. **Copy says nothing.** `Button("Copy") { field.copy() }` (`settings/Views/XcodePane.swift:162`) writes the pasteboard and stops there. `RegistrationField.copy(to:)` (`:213-220`) clears the pasteboard and sets the string, and nothing on screen changes. 5a recorded this as a decision for veto: "Copy says nothing in the app, as a code block's Copy doesn't" (MILESTONES, Milestone F, 2026-11-08). But a code block's Copy does say something: its label reads "copied" for two seconds (M8; `styles/Components/MarkdownText.swift:204-216` and `:235-239`).
2. **A row with no label has a Copy named "Copy ".** The button's accessibility label is `"Copy \(field.label)"` (`XcodePane.swift:163`). `RegistrationField.fields(of:)` gives the second and later arguments, and the second and later environment variables, an empty label (`:207-210`). So VoiceOver reads their button as "Copy " with nothing after it. In the recorded registration that is the `PATH=…` row.
3. **The design.** Settings › Xcode (start `settings:xcode`, board `ScreenSettingsXcode`) shows a toast, "Copied to the clipboard", on every Copy. It names each button "Copy" plus the field's label, or "Copy the next entry" for a row with no label (the old prototype's `main.html` on 2026-11-14: `xcodeFields`, line 6249; the button's `aria-label`, line 2309; the design app draws the same, in the `settings` module's `XcodeSettings`, since design 1i). It leaves that row's label column blank too, so the blank column is as drawn. The design's code blocks say the same words when copied (line 8073).
4. **The app's toast lives in the main window.** `XcodeSettings` already has `say` (`settings/ViewModels/XcodeSettings.swift:48`), and Settings passes it on (`settings/ViewModels/SettingsViewModel.swift:243`) to the shell's toast (`app/ViewModels/AppViewModel.swift:186`). But the shell draws its toast only at the bottom of the main window (`shell/Views/ShellView.swift:61-69`). Settings is a window of its own, the `Settings` scene (`app/Views/ApogeeApp.swift:99-102`), the design's own mapping for macOS (MILESTONES, Milestone B: "Settings is a `Settings` scene"). That window draws no toast. So every line Settings says today, from Register, a key or a fix, shows in the other window, and the person sees it only if the bottom of the main window is in view. In the design, Settings is a dialog over the app, and the toast sits above it (`main.html` lines 2140-2142 and 2509).

## Acceptance criteria

- [ ] **Given** Settings › Xcode is open, **when** I press Copy on any field, **then** its value is on the clipboard and "Copied to the clipboard" shows at the bottom of the Settings window for about five seconds.
- [ ] **Given** Settings is in front, **when** Settings says any line, **then** it shows in the Settings window, not in the main window.
- [ ] **Given** Settings is closed or behind another window, **when** Settings says a line, **then** the main window shows it, as it does today.
- [ ] **Given** the Copy buttons in Settings › Xcode, **when** their names are read, **then** every one is named for what it copies: "Copy Name" through "Copy Environment Variables", and "Copy the next entry" for a row with no label; none is named "Copy ".
- [ ] The Interpreter row still has no Copy, Copy runs no command, and nothing else in the pane changes.
- [ ] The suite is green, and each new state has its named preview.

## Constraints

- **Built as drawn.** The design is the reference: the design app, which draws this pane as the old prototype did ([the design's guide](../../../../assistant/design/CLAUDE.md)). The words and the button names are the design's. The toast is the app's own, moved unchanged: it does not look like the design's, and restyling it would change every toast in the app (see Out of scope). Any other wording or look in this pane, such as a label that changes after a click or a more specific name, is a design change first, made in the design's queue and never in this item.
- **The pasteboard is written from the view, never from the view-model.** That is 5a's rule (`XcodePane.swift:182-185`), the way `MarkdownCode.copy(to:)` does it (`MarkdownText.swift:59`). View-models stay free of SwiftUI and AppKit ([ADR 0002](../../../../adrs/darwin/presentation-mvvm.md); the layering check fails AppKit in a `ViewModels/` file). The acknowledgment goes through a plain view-model call, so a test can pin it with no window and no pasteboard.
- **Honest.** Copy says it copied only when the pasteboard took the value. Copy runs no command and reaches nothing: the pane runs no verb it does not run today (`XcodeSettingsTests.paneVerbs`), and nothing reaches Xcode (5a).
- **One toast, promoted.** Settings draws the shell's own toast view, promoted to `styles` now that a second module needs it, never copied ([ADR 0002](../../../../adrs/darwin/presentation-mvvm.md); the Presentation layer card). `styles` depends on no other Presentation module, so the toast view takes its text, not the shell's `Toast`.
- **The toast is never the only place.** The shell's rule (`shell/ViewModels/ShellViewModel.swift:87-88`): "Never the only place something is said: the screen that did it shows it too." A Copy's result is the pasteboard, and its value stays drawn in its row, so here the toast only says the Copy worked. The rule holds.
- **Every view can be seen on its own** ([ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md)). Each state a person sees differently has a named `#Preview`, inside `#if DEBUG`, built over the module's recorded documents.
- **Hermetic tests in Swift Testing.** Swift 6, no warnings, and a doc comment on every public declaration ([darwin CLAUDE.md → Code Style](../../../../assistant/darwin/CLAUDE.md#code-style)).

## Technical approach

Paths under `lib/src/darwin/`.

| File | Change |
|---|---|
| `source/presentation/settings/ViewModels/XcodeSettings.swift` | A new `copied()`, which says "Copied to the clipboard" (one constant, the design's words) through `say`. |
| `source/presentation/settings/Views/XcodePane.swift` | The Copy button (`:161-166`) writes the pasteboard, then calls `xcode.copied()` if the pasteboard took the value. Its accessibility label is a new, pure `RegistrationField.copyLabel`: "Copy " plus the row's label, or "Copy the next entry" for a row with none. `copy(to:)` reports whether the pasteboard took the value (`@discardableResult`). |
| `source/presentation/settings/ViewModels/SettingsViewModel.swift` | The Settings window's own toast, `toast` and `dismissToast(_:)`, working as the shell's do and using the shell's `Toast`, built through its new public initializer (`ConsoleViewModel` already imports `PresentationShell` the same way, for the parity table). `windowActive(_:)`, told whether the Settings window is the active one, the window in front that takes the keyboard; and `windowClosed()`, which counts as not active and drops a toast that is showing. One route for every line Settings says, both its own `say?(…)` calls (`:285`, `:651-736`) and `xcode.say` (`:243`): to the window's toast while the window is active, otherwise to the shell's `say`, which `AppViewModel` still sets (`app/ViewModels/AppViewModel.swift:186`). A toast that is showing when the window stops being active stays for its own time, because the window still draws it. |
| `source/presentation/settings/Views/SettingsView.swift` | The toast at the bottom of the window, timed by `Toast.lifetime` as `ShellView` times its own. The view reads SwiftUI's `@Environment(\.appearsActive)` (macOS 15 and later, under the app's macOS 27 floor) and passes it to `windowActive(_:)` on every change, the first included, and calls `windowClosed()` from `.onDisappear`. The new preview, "Settings › Xcode after Copy: Copied to the clipboard at the bottom of the window", over the pane's recordings: it calls `windowActive(true)` itself before `xcode.copied()`, because a preview canvas may not appear active. A canvas that reports itself not active leaves the toast showing, because only closing drops it. In a live canvas the toast ends on its own time, as in the window. |
| `source/presentation/shell/ViewModels/ShellViewModel.swift` | `Toast` gains a public `init(id:text:)` with a doc comment. Its automatic initializer is internal, so today only the shell can make one (`:91`, and the toast's preview); the Settings window needs to make its own. 5a gave `MCPPreset` a public initializer for the same reason (MILESTONES, Milestone F). |
| `source/presentation/styles/Components/Toast.swift` (new) | `ToastView`, moved from `shell/Views/ShellView.swift:151-166` with its preview "The toast" (`:197-200`), taking its text. `shell` draws it from `styles`, and `Toast` stays in `shell/ViewModels/ShellViewModel.swift:102-109`, gaining a public `init(id:text:)`. |
| `tests/presentation/settings/XcodeSettingsTests.swift` | See Testing. |
| `tests/presentation/settings/SettingsViewModelTests.swift` | See Testing. |

**Reference.**
- 5a's pane and its Copy test (`XcodeSettingsTests.swift:183-205`), which uses its own named pasteboard on each run.
- The shell's toast and its test (`ShellViewModelTests/toast`: one line at a time, and only its own time ends it).
- M8's code block, the app's other Copy, which stays as it is.

**Docs when it ships:**
- darwin `MILESTONES.md`: Milestone F, after 5a, saying 5a's "Copy says nothing" is reversed;
- the darwin `CLAUDE.md` codebase map: `styles`, `shell`, `settings`;
- `DEVELOPER.md`'s `PresentationStyles`, `PresentationShell` (with `Toast`'s public initializer) and `PresentationSettings` rows;
- the Presentation layer card, with its **Modules** line in plain words, because the layering check reads every backticked word on that line as a module name.

## Testing

All hermetic: no window, no binary, and never the person's own pasteboard.

- **`XcodeSettingsTests`, `copied()`.** `copied()` says exactly "Copied to the clipboard", once per Copy, through the suite's `Said` recorder (`:51`, `:57-59`), the window not active, as the suite's model starts. It runs nothing, so the test that the pane never connects (`:272`) still passes with Copy in its story.
- **`XcodeSettingsTests`, the button names.** Over the recorded registration (`tests/business/reads/Fixtures/xcode-acp-registration.json`), the rows with something to copy are named "Copy Name", "Copy Executable", "Copy Arguments", "Copy Environment Variables" and "Copy the next entry" (the `PATH` row). The same document with a second argument added names that row "Copy the next entry" too. No name ends in a space.
- **`XcodeSettingsTests`, the pasteboard test** (`:183-205`) keeps its checks and also pins what `copy(to:)` reports: `true` for each row with something to copy, and `false` for the Interpreter, whose pasteboard still holds "before".
- **`SettingsViewModelTests`:**
  - after `windowActive(true)`, `xcode.copied()` becomes the window's toast, "Copied to the clipboard", and the shell's recorder hears nothing. One of Settings' own lines, the binary override's (`setBinaryOverride`), does the same. So both `xcode.say` and Settings' own calls go through the one route, and a build that leaves `xcode.say` sending straight to the shell (`SettingsViewModel.swift:243` as it is today) fails;
  - a line said while the window is active becomes the window's toast, a shell `Toast` built through its public initializer, and the shell's recorder hears nothing;
  - a line said while the window is open but not active goes to the shell's recorder, and no toast is kept;
  - while the window is closed, the shell's recorder hears the line and no toast is kept;
  - closing the window drops a toast that is showing, and the window going inactive does not;
  - each toast ends only on its own time, and a newer one stays (as `ShellViewModelTests/toast` pins for the shell's).
- **Existing toast tests.** Every existing Settings toast test passes unchanged, because the window starts closed and not active, as at launch. That includes `XcodeSettingsTests`' `Said`, `RegisterAndHostsTests`' `Toasts` (`:35-37`) and the rest.
- **The layering check** stays green: the toast view sits in `styles` with its preview, `styles` depends on no other Presentation module, and every view has its preview.
- **Gate:** `lib/scripts/cicd.sh --app darwin --test`.
- **A live look** is optional and the user's to take, on a development build once M18 ([`app-dev-channel.md`](app-dev-channel.md)) has shipped ([darwin CLAUDE.md → Development runs on the dev channel](../../../../assistant/darwin/CLAUDE.md#development-runs-on-the-dev-channel)).

## Open questions

| Question | Status | Answer |
|---|---|---|
| What is Copy's acknowledgment: the design's toast, or a label that changes to "copied"? | `[default]` | The acknowledgment is the design's toast, "Copied to the clipboard", with the same words for every field. It is not a label that changes to "copied" the way a code block's does: the design draws no such change for this pane, and drawing one would be a design change first. |
| Which window shows the lines Settings says? | `[default]` | The Settings window draws its own toast while it is the active window, the one in front, for every line Settings says, Copy's included. Copy is pressed in the Settings window, which is then the window in front. Otherwise a line goes to the main window as it does today. So a fix that finishes after Settings has closed, or while the main window is in front of it, is still seen. Rejected: the shell's toast alone, which shows in the other window; routing by whether Settings is open, which would send a fix's line (`SettingsViewModel.swift:729-736`) to a Settings window left open behind the main window, where today it shows in front; only Copy's line in Settings and the rest in the main window, which gives one window's lines two places to appear; both windows at once, which shows the same line twice when both are visible. |
| What is the Copy on a row with no label named? | `[default]` | A row with no label has a Copy named "Copy the next entry", the design's words. A more specific name, such as the variable's own (`Copy PATH`), would be a design change first. |
| When does Copy say it copied, and what is said if the pasteboard refuses? | `[default]` | Copy says it copied only when the pasteboard took the value (`setString` returns `true`). If it did not, nothing is said, because an error line would need words the design does not have. |
| Does this item make VoiceOver read the toast aloud? | `[default]` | No. Nothing in the app makes VoiceOver announce anything (no `AccessibilityNotification` anywhere in `source/`). The toast carries only `.updatesFrequently` (`ShellView.swift:164`), while the design's toast is a `role="status"` region (`main.html` line 2509), which screen readers read out. Having VoiceOver read every toast is a change to the whole shell: not queued, and a later item only on the user's word. Meanwhile a VoiceOver user hears each Copy button named for what it copies, but hears no acknowledgment. |
| What does Copy copy? | `(consumed decision)` | 5a: the Name and the Executable whole, each argument on its own, each environment variable as `NAME=value`, and nothing for the Interpreter. 5a flagged it to be settled by the user's paste into Xcode, and it is not reopened here. |
| Does a code block's Copy change? | `(consumed decision)` | M8: a code block's Copy keeps its "copied" label and shows no toast. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Found by that day's design pass (its whole-design check of the v0.2.1 states) and confirmed in the source the same day. | — |
| 2026-11-14 | Maintenance, as M19, claimable at any time by name, no gate. | the user |
| 2026-11-14 | This item reverses 5a's decision "Copy says nothing in the app" (MILESTONES, Milestone F), which was taken for veto. The user asked for Copy's acknowledgment to be specced, and the decision rested on the belief that a code block's Copy says nothing, which is not what the app does. | the user |
| 2026-11-14 | The design is built as it stands. No design change rides on this item, because the prototype is frozen for the port. | the spec |
| 2026-11-21 | Re-worded by design 1t, which retired the old one-file prototype and the claude.ai canvas: the design is the design app, which draws Settings › Xcode's toast and Copy names as the old did (`XcodeSettings`, design 1i); the evidence keeps its 2026-11-14 measurements of the old `main.html`, and its links name the design's guide, the interim rule having gone with the freeze. | design 1t (default) |


## Out of scope

- Having VoiceOver read toasts aloud, which would change every toast in the app (not queued).
- The code block's Copy, whose "copied" label differs from the design's toast (M8's choice; not queued, and a later item only on the user's word).
- The toast's look, which differs from the design's (`main.html` line 2509) for every toast in the app.
- What Copy copies (5a's call, settled by the user's paste into Xcode).
- The toasts of the places in the main window.
- Any design change.

## Links

- **Blocked by:** — · **Blocks:** —
- **Reverses:** GUI 5a's decision that "Copy says nothing in the app" ([MILESTONES → Milestone F](../../../../assistant/darwin/MILESTONES.md#milestone-f--apogee-in-xcode-the-apps-side), 2026-11-08).
- **Relates to:** M8 (a code block's Copy, the app's other Copy); M18 ([`app-dev-channel.md`](app-dev-channel.md), a development build for the optional live look); [ADR 0002](../../../../adrs/darwin/presentation-mvvm.md); [ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md); [the design's guide](../../../../assistant/design/CLAUDE.md); MILESTONES, Milestone B ("Settings is a `Settings` scene").
