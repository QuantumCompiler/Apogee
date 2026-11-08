# The foldable iPhone

**What / why.** The user's vision, 2026-11-07: Apogee as a private, self-hosted harness that feels like Claude on every device the person owns, with one strong machine of theirs serving their desktops and phones. The user's "iPhone duo" is the foldable iPhone. Folded, it is a phone; unfolded, it is nearly an iPad. The app has to adapt to both screens. By this item, the phone's compact layout exists ([9a](iphone-app.md)) and so does the iPad's regular one ([8b](../v0.2.5/ipad-app.md)). What remains is moving between them, in either direction and at any moment, without losing anything the person had in hand: an open session, a draft, a half-answered question, a running job's view. Measured at HEAD (`4f92316d`) on 2026-11-07. Paths are under `lib/src/darwin/source/presentation/`.

**W11 — a change of shape keeps only what a view-model holds.**
1. **The only shape change the app handles is the window's width, and it works because the state lives in view-models.** `ShellView` reports the width (`shell/Views/ShellView.swift:57`), `ShellViewModel.windowResized(width:)` keeps it (`shell/ViewModels/ShellViewModel.swift:81-85`), and `ShellLayout` derives the sidebar and `sidePanes` from it (`:111-135`). Narrowing hides the Sessions inspector but does not close it. `inspectorOpen` is a view-model property kept in `Preferences` (`sessions/ViewModels/SessionLibraryViewModel.swift:127-128`), and only the drawing is gated (`sessions/Views/SessionsScreen.swift:113`), so the pane returns when the window widens. Nothing reads a size class: a search for `horizontalSizeClass`, `verticalSizeClass`, `SceneStorage` and `scenePhase` over `source/` finds none.
2. **What a person makes is often held by a view, and a replaced view loses it.** There are 47 `@State` properties outside previews, in 24 files (76 under `source/`, less 29 `@Previewable` ones inside `#Preview` blocks). Most are cosmetic: a hover, a Copied tick, an expanded row. These are not:
   - the answers typed into a question card while the model waits (`session/Views/QuestionCardView.swift:13-14`);
   - the run-settings sheet's draft (`session/Views/SessionExtras.swift:211`);
   - the rename fields (`sessions/Views/SessionList.swift:237`, `models/Views/ModelsScreen.swift:603`, `models/Views/StoreView.swift:474`);
   - the Library and Knowledge tabs (`library/Views/LibraryScreen.swift:27`, `knowledge/Views/KnowledgeScreen.swift:20`);
   - the new-graph form (`graph/Views/GraphScreen.swift:454-456`);
   - a speech's playback, where the `AVAudioPlayer` itself is view state (`voice/Views/VoiceScreen.swift:157-158`);
   - a job's open log (`models/Views/DownloadsView.swift:74`);
   - the suite sheet's choice and its force toggle (`session/Views/SuiteSheet.swift:11-12`);
   - the register sheet's model name and the Permissions pane's host and web-search fields (`settings/Views/SettingsView.swift:313`, `:411-412`);
   - a record's link field (`knowledge/Views/KnowledgeScreen.swift:180`) and the MCP view's name fields (`library/Views/LibraryScreen.swift:250`);
   - the sweep sheet's post toggle (`review/Views/ReviewScreen.swift:488`).

   The key sheet's key is view state too (`settings/Views/SettingsView.swift:351`), and stays so (the default below).

   Already safe: the session's draft (`SessionViewModel.draft`, `session/ViewModels/SessionViewModel.swift:31`, bound at `session/Views/InputBar.swift:25`), the live session (`SessionLibraryViewModel.current`, `:116`), the route, the overlay and the toast (`ShellViewModel`), and every job's progress (its place's view-model).
3. **The session's end is tied to a view disappearing.** `.onDisappear { Task { await model.sessions.shutDown() } }` sits on the shell (`app/Views/ApogeeApp.swift:91`). On the Mac that view disappears only when the one window closes. A composition that swapped a compact shell for a regular one at that spot would end the open session on every fold: the turn cancelled, and the child or connection asked to go.
4. **A whole-tree rebuild is already in use.** `.appearance` gives the tree an `.id` of the theme and text size (`app/Views/ApogeeApp.swift:35`). So a theme change today drops every view's `@State`, which is the same loss a fold built that way would cause.
5. **The transcript's place is held nowhere.** `SessionWindow` scrolls to the last row only when the transcript's revision changes (`session/Views/SessionWindow.swift:82-86`). A window built anew starts at the top of its `ScrollView` until the next event arrives.
6. **The device can be tested, but not yet on this Mac.** Xcode 27.1 (`27A9275`) lists an `iPhone Duo` device type: `xcrun simctl list devicetypes -j` reads `com.apple.CoreSimulator.SimDeviceType.iPhone-Duo`, model `iPhone19,4`, minimum runtime iOS 27.1, and its profile marks `com.apple.CoreSimulator.display.resizableScene` as conditional on the runtime. The newest iOS runtime installed here is 27.0 (`xcrun simctl list runtimes`), so the Duo cannot boot until the user installs 27.1. The design draws no foldable: a search for `foldable`, `iPhone Duo` and `size class` in `lib/design/spec` finds 0 matches.

**Context, owned elsewhere (not this item's walls).** What folded looks like is 9a's compact shell, and what unfolded looks like is 8b's regular one. The iOS target is [8a](../v0.2.5/app-builds-for-ios-and-ipados.md)'s, and the connection is [7a](../v0.2.4/app-connection-local-or-server.md)'s. Going to the background drops the connection (9a's default). What the server does with a turn or a task left running is CLI [59b](../../../cli/backlog/v0.2.3/machine-mode-over-the-network.md)'s and [59e](../../../cli/backlog/v0.2.3/tasks-outlive-the-connection.md)'s. This item adds nothing to the protocol and has no CLI sibling.

This item delivers:
- **One route, two shells.** Unfolding moves from 9a's compact shell to 8b's regular shell, and folding moves back. The place, the selection and the open pane carry over each way.
- **Nothing a person made is lost.** The draft, a half-answered question card, the run-settings draft, a rename in progress, a playing speech, a running job's progress with its log open, and the transcript's place all survive a fold. A turn that is streaming keeps streaming.
- **No traffic.** A fold opens no connection, writes no line, re-reads nothing, and ends nothing.
- **The same on iPad.** An iPad window resized across the size-class boundary (Split View, Stage Manager) takes the same path.
- **Proof without a device.** View-model tests drive size-class transitions, and paired previews draw each crossing state in both size classes from one view-model.

**Core constraint(s).**
- **Anti-goal: nothing here may make standalone use need a server** (GUI SPEC → [No server requirement](../../assistant/SPEC.md#non-goals)). The Mac app over pipes to its local CLI, and the CLI alone, stay complete and the default. The Mac reads no size class (9a's default). Its width law (`ShellLayout`) and its window-close shutdown are unchanged. The state moved into view-models behaves on the Mac exactly as it does today.
- **The machine-mode client** (GUI [CLAUDE.md → The GUI is a machine-mode client](../../assistant/CLAUDE.md#-the-gui-is-a-machine-mode-client), amended 2026-11-07): the phone's one transport is 7a's authenticated connection to the person's `apogee serve`. A fold holds that connection as it is and opens none. The app never opens a listening socket (GUI SPEC → [Non-goals](../../assistant/SPEC.md#non-goals), the never-listens law).
- **Secrets are unleakable** (the CLI docs' [Secrets are unleakable by construction](../../../cli/assistant/CLAUDE.md#-secrets-are-unleakable-by-construction)):
  - the key sheet's key is "held by this sheet only until it is sent" (`settings/Views/SettingsView.swift:347-351`), and moving state out of views never moves it;
  - no key and no pairing credential enters a view-model, `Preferences`, the route record, a preview or a log line.
- **Additive protocol** ([CLI ADR 0006](../../../cli/adrs/backwards-compatibility.md)): no event, field or line is added. A fold is the app's concern alone.
- **Parity** ([ADR 0003](../../adrs/parity.md)): one table at every size. A fold never hides a place, a pane or an act. Whatever was reachable before the fold is reachable after it.
- **The layers** ([ADR 0001](../../adrs/layer-enforcement.md), [ADR 0002](../../adrs/presentation-mvvm.md), [ADR 0004](../../adrs/module-schemes-and-previews.md)):
  - the size class is read in Views and handed to `ShellViewModel` as a value, as the width is today;
  - view-models stay SwiftUI-free, and playback reaches the Voice view-model as an injected player, not as `AVAudioPlayer`;
  - every crossing state has its named `#Preview`.
- **The dev channel** (the CLI docs' [Development runs on the dev channel](../../../cli/assistant/CLAUDE.md#development-runs-on-the-dev-channel)): the live pass runs the dev build paired with an `apogee-dev serve` over `~/.apogee-dev`, never the person's production server.

**Seam + files.** All under `lib/src/darwin/`.
- `source/presentation/shell/ViewModels/ShellViewModel.swift`: `sizeClassChanged(_:)` beside `windowResized(width:)`. `ShellLayout` takes the size class handed in (9a's compact form, 8b's regular form). The route is one value that both shells render.
- `source/presentation/shell/ViewModels/AdaptiveRoute.swift` (new, pure): the map between 9a's `CompactNavigation` (the tab, each tab's stack, the pane presented as a sheet or a pushed page) and the regular route (the destination, the selection, the open pane), in both directions. Each tab's stack is kept while unfolded, so fold, unfold, fold returns to the same stack.
- `source/presentation/shell/Views/ShellView.swift` and 9a's `CompactShell.swift`: one root view that switches its layout inside itself. Neither shell owns the session's lifetime.
- `source/presentation/app/`: the iOS composition (8a's and 8b's) ends the sessions on the scene's end (9a's background drop), never on a shell view's disappearance. The Mac's `ApogeeApp.swift:91` is unchanged in behavior.
- Each place's `ViewModels/`: the state listed in W11.2 moves out of views.
  - `session`: a question card's draft answers, kept per card in the transcript's model, and the run-settings draft, both moved to `SessionViewModel`, along with the transcript's anchor (following the end, or the row at the top).
  - `session`: the suite sheet's choice and force toggle.
  - `sessions`, `models`: the rename fields.
  - `library`, `knowledge`: the tabs, MCP's name fields and a record's link field.
  - `settings`: the register sheet's model name and the Permissions pane's fields, never the key sheet's key.
  - `review`: the sweep sheet's post toggle.
  - `graph`: the new-graph form.
  - `models`: `DownloadsViewModel` gets the log-open flag.
  - `voice`: `VoiceViewModel` gets playback, over an injected player protocol whose `AVAudioPlayer` adapter lives in Views.
  - The cosmetic `@State` stays where it is.
- `source/presentation/session/Views/SessionWindow.swift`: the transcript restored to its anchor when it is built.
- `lib/design/spec/` (read `spec/CLAUDE.md` first; every design update lands there): a fold board pair. It shows the same conversation folded and unfolded, mid-turn with a draft, and the inspector as a sheet and as a pane.
- Tests (hermetic: scripted fakes, recorded documents, no binary, no network, no window):
  - `tests/presentation/shell/AdaptiveRouteTests.swift` (new): every `Destination` × open pane × tab round-trips both ways. A pushed details page becomes the selection with its pane open, and back. More with nothing pushed maps as the default below.
  - `tests/presentation/shell/ShellViewModelTests.swift`: the size class handed in picks the shell; the Mac's width law is pinned unchanged.
  - `tests/presentation/session/SizeTransitionTests.swift` (new): over `Scripted.swift`, a turn streaming while the size class alternates 100 times. It pins zero new launches or connections, zero lines written, zero reads and zero shutdowns. Events keep folding into the transcript, and the draft, a half-answered card, the run-settings draft and the anchor are intact.
  - `tests/presentation/sessions/SessionLifecycleTests.swift`: a size change never ends the session, and the scene's end does.
  - `tests/presentation/models/`: a job's progress and open log across transitions.
  - `tests/presentation/voice/`: playback continues across transitions, over a fake player.
  - `tests/architecture/`: the view-state check (default below).
  - `#Preview` pairs: for each crossing state, compact and regular drawn from one view-model instance, the size class set through the environment, over the modules' `Previews/` fakes.

**Reference.**
- Siblings: [9a](iphone-app.md) (the compact shell, `CompactNavigation` and the route kept across launches), [8b](../v0.2.5/ipad-app.md) (the regular shell this item unfolds into), [8a](../v0.2.5/app-builds-for-ios-and-ipados.md) (the iOS target), [7a](../v0.2.4/app-connection-local-or-server.md) and [7c](../v0.2.4/every-place-against-a-server.md) (the connection, and every place against a server), and [9c](three-apple-platforms-whole.md) (v0.3.0 whole, gated on this item).
- On the CLI side: [59b](../../../cli/backlog/v0.2.3/machine-mode-over-the-network.md) and [59e](../../../cli/backlog/v0.2.3/tasks-outlive-the-connection.md) (a turn or task left running when a connection drops), and [59c](../../../cli/backlog/v0.2.3/many-sessions-at-once.md) (one chat on two devices).
- Precedents:
  - the window law (`ShellLayout`, the design's decision 10), which already keeps the inspector open across a narrowing;
  - M10 (a session known by the CLI's `chat_id`, so a rebuilt view finds the same conversation);
  - M7 (earlier turns from `chats show`, the fallback after a reconnect, not after a fold).
- No external prior art was checked.

**Decisions made** (dated):
- 2026-11-07: Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item:
  - the vision, Apogee feeling like Claude on every device the person owns, served by their own strong machine;
  - "iPhone duo" means the foldable iPhone, and the app adapts to its folded phone screen and its unfolded near-iPad screen;
  - iPhone and iPad are clients of the person's Apogee server only, with no on-device core;
  - the server is an add-on, never the only way the app runs, so nothing may make the CLI or the Mac app need a server, a network or an account;
  - v0.3.0 is the target where macOS, iPadOS and iOS (the iPhone and the foldable) fully work;
  - the SPEC amendment is approved and written: the GUI reaches the CLI over stdio locally, or over an authenticated network connection to the person's own `apogee serve`;
  - paid features are a someday, not now: no customer accounts, billing or metering, and the server admits the person and their devices only.
- 2026-11-07: Placed in v0.3.0 (track 9), after 9a and gated on it. A fold moves between 9a's compact shell and 8b's regular shell, so both must exist first. 9c, v0.3.0 whole, is gated on this item.
- 2026-11-07: Where the work happens (the user's call C): server tools by default, plus a connected desktop's own files and shell. The foldable offers no device tools of its own (iOS starts no shell), and a fold changes nothing about which tools a session has.

**Open calls:**
- [default: the app keys on the horizontal size class alone. It never keys on a device model, a point width or a hinge or posture API (none was checked for this item). Folded is whatever iOS reports as compact, and unfolded whatever it reports as regular, which is why an iPad window resized across the boundary takes the same path.]
- [default: one route with two renderings. Regular to compact is 9a's map: the destination picks the tab, and a pane becomes a sheet or a pushed page. Compact to regular is its inverse. More with nothing pushed maps to the place last shown while unfolded, or to Sessions if none was.]
- [default: each tab's stack is kept while unfolded, so fold, unfold, fold returns to the same stack depth.]
- [default: what a person makes lives in view-models, and `@State` keeps only the cosmetic: a hover, a Copied tick, an expanded row, a closed date group. A view-state check in `tests/architecture/` lists every non-preview `@State` against an allow-list of cosmetic names, so a new one that holds a person's input fails until it is classified.]
- [default: the key sheet is the one exception. Its key stays in the sheet alone. If a fold closes the sheet, the typing is dropped, nothing has been saved, and the sheet reopens empty. The key is never moved to keep it.]
- [default: a sheet stays presented across a fold where its presenter survives. A side pane shown as a sheet folded is the open pane unfolded, and back. The console and the monitor follow the overlay `ShellViewModel` already holds.]
- [default: the transcript keeps its place. Following the end keeps following, and scrolled back keeps the row that was at its top. The input bar keeps focus if it had it.]
- [default: a fold is a trait change of the same scene, as an iPad resize is. If iOS 27.1 delivers the Duo's fold as a scene reconnect, which the live pass checks, 9a's route restoration and the server's record cover it, and this item adds nothing more.]
- [default: the theme's whole-tree `.id` (`ApogeeApp.swift:35`) stays. With a person's state in view-models, a theme change no longer loses any of it.]
- (consumed decision: 9a owns the compact shell, what folded looks like and backgrounding. 8b owns the regular shell. 59b and 59e own a turn or a task left running when the connection drops. 59c owns one chat on two devices.)
- [default: the foldable is the iPhone family, in the one bundle for iPhone and iPad that [8c](../v0.2.5/iphone-and-ipad-distribution.md) assumes. It needs no target, device family or entitlement of its own, so 8c's path reaches it unchanged.]
- (consumed decision: 8a owns the iOS target and the one-bundle-or-two call. 8c owns distribution.)

**Guardrail(s).**
- `AdaptiveRouteTests` pins every route's round trip in both directions.
- `SizeTransitionTests` pins a streaming turn across 100 transitions: no launch, connection, line, read or shutdown, and every piece of a person's state intact.
- `SessionLifecycleTests` pins that only the scene's end, never a view's disappearance, ends a session on iOS, and that the Mac's window close still does.
- The view-state check fails an unclassified `@State`.
- A planted key and pairing credential appear in no view-model, `Preferences` store, route record, fixture, preview or log line.
- The Mac's `ShellLayout` cases and its suite are pinned unchanged.
- Every crossing state has a `#Preview` pair.
- The layering check stays green: no UIKit type below Presentation, the size class read only in Views, and no AVFoundation type in a view-model.

**Acceptance criteria:**
- [ ] In the iPhone Duo Simulator (the iOS 27.1 runtime, installed by the user), the dev build is paired with a sandboxed `apogee-dev serve` over `~/.apogee-dev` and a mock backend. Mid-turn, with a draft typed and a question card half answered, unfolding keeps the draft, the answers and the transcript's place, and the answer keeps streaming. Folding back returns to the same tab and stack.
- [ ] The Sessions inspector open as a sheet folded is the open pane unfolded, and back. A backend's details pushed folded are its selection with details open unfolded.
- [ ] A server job's progress with its log open, and a playing speech, both survive a fold and an unfold.
- [ ] An iPad Simulator window resized across the size-class boundary in Split View gives the same results.
- [ ] The Mac app in local mode, with no server set, behaves exactly as before: it opens no connection, a window resize follows the same width law, and its suite is unchanged.
- [ ] Every crossing state has its `#Preview` pair. The design canvas carries the fold boards. The full suite is green.
- [ ] The user's live pass on a foldable device is optional, and said.

**Scope note.** Earmarked for v0.3.0 (track 9). Gated on 9a, and through it on 8b, 8a, 7a and 7c, and CLI 59a, 59b, 59d and 59e (9a's chain). 9c is gated on it. Out of scope:
- layouts aware of the hinge or posture, and content spanning the fold;
- multiple windows or scenes on iPad, and a second live session;
- restoration after the process ends, beyond 9a's route;
- distribution to a device (8c);
- Android foldables (after v0.3.0, a recorded default).
