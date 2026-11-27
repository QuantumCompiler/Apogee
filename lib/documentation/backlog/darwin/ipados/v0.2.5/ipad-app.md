# APP-8b · The iPad app

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Apple apps · iPadOS |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.5 |
| **Component** | The iPad app: the shell and its layouts, Settings, keys and pointer, first launch and pairing |
| **Labels** | `ipados` `layout` `pairing` `parity` |
| **Epic** | Track 8 · the iPad and getting the app onto iPhone and iPad |
| **Blocked by** | [8a](../../v0.2.5/app-builds-for-ios-and-ipados.md) |
| **Blocks** | [9a](../../ios/v0.2.5/iphone-app.md) |
| **Reporter** | The Apple-platforms spike and brainstorm (2026-11-07), on the user's vision |
| **Created** | 2026-11-07 |

---

## Summary

The app has one shape, a Mac window, and nothing in it knows an iPad: portrait iPads lose four side panes, the Mac's scenes, Settings and Esc do not exist on iPadOS, and first launch looks for a local binary an iPad cannot run. This item delivers the iPad app: every place in iPad layouts at every Split View, Slide Over and Stage Manager size, the Mac's keys and the pointer, and a first launch that pairs with the person's own server, all under one `ParityTable`.

## Description

### User story

**As** a person using Apogee who runs it on their own strong machine,
**I want** to open Apogee on my iPad and find the Mac app's places, sessions and keys at every size the iPad gives a window, paired with my own server,
**so that** Apogee feels like Claude on every device I own, served by my own machine.

### Background

The user's vision, 2026-11-07: Apogee as a private, self-hosted harness that feels like Claude on every device the person owns, with one strong machine of theirs serving their desktops, tablets and phones. On the iPad that means opening Apogee and finding the Mac app's places, sessions and keys, at every size the iPad gives a window. The iPad is a client of the person's own Apogee server only (the GUI SPEC's [mobile applications](../../../../assistant/gui/SPEC.md#core-surfaces)).

By this item, the code builds for iPadOS ([8a](../../v0.2.5/app-builds-for-ios-and-ipados.md)), the app reaches a server ([7a](../../v0.2.4/app-connection-local-or-server.md)) and every place works against one ([7c](../../v0.2.4/every-place-against-a-server.md)). What remains is the iPad's own shape, and a first launch that has no local CLI to find.

Measured at HEAD (`4f92316d`) on 2026-11-07. Paths are under `lib/src/darwin/`; a path that starts at a module (`shell/…`, `settings/…`) is under its `source/presentation/`.

**What this item delivers.**
- **The iPad app**: every place in iPad layouts. A sidebar beside the place where the window is wide enough, and drawn over it where it is not. Side panes beside the place or presented as sheets, and never disabled. The layouts hold at every Split View, Slide Over and Stage Manager size.
- **Keys and pointer**: the Mac's keys with a hardware keyboard, listed by iPadOS, and the pointer's hover and secondary click. Every key-bound act also has a control a finger reaches.
- **Remote only**: on first launch the app pairs with the person's server. With no server, it says how to set one up. A paired iPad opens on Sessions.
- **The same parity table**: one `ParityTable` for the Mac and the iPad, every screen row's place mounted, no carve-out of the iPad's own.

### Evidence

**W10 — no iPad layouts.** The app has one shape, a Mac window, and nothing in it knows an iPad.

1. **The window asks for more than most iPads have.** The window's minimum is 960 × 640 points (`source/presentation/styles/Metrics.swift:26-27`, applied at `source/presentation/app/Views/ApogeeApp.swift:83`). Settings asks for 860 × 600 (`settings/Views/SettingsView.swift:58`) and the console for 720 × 480 (`console/Views/ConsoleView.swift:127`). The iPad sizes come from the Simulator's own device profiles (`/Library/Developer/CoreSimulator/Profiles/DeviceTypes/<device>.simdevicetype/Contents/Resources/capabilities.plist`, `main-screen-width`/`-height` over `main-screen-scale`). In points they are: iPad mini (A17 Pro) 744 × 1133, iPad (A16) and iPad Air 11-inch (M4) 820 × 1180, iPad Pro 11-inch (M5) 834 × 1210, and iPad Pro 13-inch (M5) 1032 × 1376. In portrait, every one of them except the 13-inch is narrower than 960. Split View, Slide Over and Stage Manager make the window narrower still, at whatever width the person drags it to.
2. **The shell's width law sends every portrait iPad to its narrow layout, where side panes vanish.** `ShellLayout` (`shell/ViewModels/ShellViewModel.swift:114-135`) collapses below 900 points (`collapseBelow`, `:116`, from `lib/design/tokens.json:380`, re-checked 2026-11-21). Below that the sidebar becomes a 56-point column of icons and `sidePanes` turns false (`:131-133`). Then the Sessions inspector's button is disabled (`sessions/Views/SessionsScreen.swift:30`, `:113`, and its ⌥⌘I at `:374`). A backend's details are not drawn (`models/Views/ModelsScreen.swift:56`), nor a stored set's details (`models/Views/StoreView.swift:18`), nor a graph node's explanation (`graph/Views/GraphScreen.swift:88`). So on an 834-point iPad Pro in portrait, those four have no way in. That is a parity loss under [ADR 0003](../../../../adrs/gui/parity.md).
3. **The Mac's scene, Settings and Esc do not exist on iPadOS.** The iPhoneOS 27.1 SDK's SwiftUI interface (`SwiftUI.swiftmodule/arm64e-apple-ios.swiftinterface`) marks four of the app's pieces `@available(iOS, unavailable)`:
   - the one `Window` scene (`ApogeeApp.swift:54`);
   - the `Settings` scene (`:99`);
   - `openSettings`, which the sidebar's Settings row calls (`shell/Views/Sidebar.swift:10`, `:41`);
   - `onExitCommand`, Esc stopping a turn (`session/Views/SessionWindow.swift:29`).

   Two more are absent from that interface altogether: the sidebar divider's `pointerStyle(.frameResize…)` (`shell/Views/ShellView.swift:40`) and `NSApplicationDelegateAdaptor` (`ApogeeApp.swift:45`), which owns ⌘Q's wait for the sessions (`app/Views/AppDelegate.swift:21`). 8a makes the code compile. What the iPad shows in their place is this item's.
4. **Keys and the pointer are the Mac's.** Under `source/` there are 77 `.keyboardShortcut` call sites, 58 `.help(` tooltips in 20 files, and 2 `.contextMenu`s (`tasks/Views/TasksScreen.swift:83`, `sessions/Views/SessionList.swift:168`). The console's Tab, ↑, ↓ and Esc are `onKeyPress` (`console/Views/ConsoleView.swift:97-113`). A search for `horizontalSizeClass`, `hoverEffect`, `UIDevice`, `NavigationStack` and `TabView` finds none. Nothing in the app distinguishes a finger from a pointer, or a hardware keyboard from none.
5. **First launch looks for a local binary.** With none found, the session shows `DiscoveryOutcome.installHint`, which is a `curl … install.sh | bash` line (`source/data/discovery/BinaryDiscovery.swift:146-147`, drawn at `source/presentation/session/ViewModels/SessionViewModel.swift:405-408`). An iPad can neither run that line nor spawn the binary it installs. There is nothing to pair with yet. On the branch binary (`apogee 0.2.0 (4f92316d)`, a scratch `APOGEE_HOME`), `apogee serve --help` names `--bind`, `--allow-remote` and `--print-admin-token`, and no pairing or device flag (a search for `pair` returns 0). A non-loopback bind needs `--allow-remote` because "the inference plane is unauthenticated" ([http-api.md:25](../../../../reference/cli/http-api.md)). Pairing is CLI [59a](../../../cli/v0.2.3/paired-devices.md)'s.
6. **The design draws no iPad.** No file in the old one-file prototype matched `ipad|iphone|tablet` (case-insensitive), and none in the design app that replaced it does (its source and `lib/design/tokens.json`, re-checked 2026-11-21, design 1t).

**Context, owned elsewhere (not this item's walls).** The project builds macOS only: `SDKROOT = macosx` in both configurations (`Apogee.xcodeproj/project.pbxproj:7889`, `:8744`). That is 8a's, as is `MediaPlayer`, an `NSViewRepresentable` over `AVPlayerView` (`styles/Components/MediaPlayer.swift:11`). What each place reads and runs against a server, such as media folders, the monitor's `system` and the store's verbs, is 7c's.

## Acceptance criteria

- [ ] **Given** the iOS Simulator on the iPad mini (A17 Pro) and the iPad Pro 13-inch (M5), in both orientations, in a Split View half and third, in Slide Over and in a small Stage Manager window, **when** I move through the app, **then** every place, the console, the monitor and Settings are reached, and the Sessions inspector, a backend's and a stored set's details, and a graph node's explanation all open.
- [ ] **Given** a hardware keyboard (the Simulator's), **when** I press ⌘1–⌘9, ⌘0, ⇧⌘K, ⌥⌘M, ⌥⌘I, ⌘, and Esc, **then** each does what it does on the Mac, and iPadOS lists them. **Given** no hardware keyboard, **then** every one of those acts has a control.
- [ ] **Given** a pointer, **when** it moves over buttons and rows, **then** they show hover; **when** I secondary-click, **then** the Tasks and Sessions menus open.
- [ ] **Given** first launch with nothing paired, **when** I pair, **then** the app leads through pairing to Sessions; **when** a pairing is refused, **then** the app says why in the server's words; **given** no server, **then** the app says how to set one up and names no hosted service.
- [ ] **Given** a paired iPad, **when** I press Forget, **then** the credential is removed, and the next launch opens on pairing.
- [ ] **Given** the Mac app in local mode, **then** it behaves exactly as before, and the Mac's suite is unchanged.
- [ ] Every iPad view has its named `#Preview`. The design app draws the iPad boards (presets and gallery entries), its local gate green. The full suite is green.
- [ ] A live pass: the dev build in the iPad Simulator, paired with an `apogee-dev serve` over a scratch `APOGEE_HOME` and a mock backend, drives a session with its thinking, tool narration, question and permission cards. The user's pass on their own iPad is optional and said, and needs 8c.

## Constraints

- **Anti-goal: nothing here may make standalone use need a server** (GUI SPEC → [No server requirement](../../../../assistant/gui/SPEC.md#non-goals)). The CLI alone, and the Mac app over pipes to its local CLI, stay complete and the default. The iPad's shell, Settings place, pairing and Keychain store are compiled for iPadOS, or chosen at runtime by width there. The Mac's `ShellLayout` cases, its `Window` and `Settings` scenes, its discovery, its ⌘Q wait and its settings are byte-identical. A Mac that never pairs never sees any of this.
- **The machine-mode client rule, as amended 2026-11-07** ([GUI CLAUDE.md](../../../../assistant/darwin/CLAUDE.md)): on the iPad the machine mode arrives only over 7a's authenticated connection to the person's own `apogee serve`. It is the same protocol with the same tolerant decoding and no capability of the app's own. The iPad runs no model and holds no CLI (no on-device core, the user's call).
- **Never listens; serving is the server** (the CLI's [Interactive turns never open a listening socket](../../../../assistant/cli/CLAUDE.md)): the iPad's connection is outbound. Only `apogee serve` owns a port. Pairing never makes the iPad a listener, and needs no discovery service on the iPad.
- **Secrets are unleakable** (the CLI's [Secrets are unleakable by construction](../../../../assistant/cli/CLAUDE.md); the GUI rule's last line): the credential 59a issues lives in the iPad's Keychain only. It has no type that a view, `Preferences`, a log, a preview or a recorded fixture serializes. Provider keys are still entered through the CLI's own flows, never on the iPad.
- **Additive protocol** (the CLI's [ADR 0006](../../../../adrs/cli/backwards-compatibility.md)): the iPad tolerates every event and field it does not know, and needs no protocol change that breaks an older front end. A server that predates a field still pairs and drives sessions.
- **Parity** (the GUI's [ADR 0003](../../../../adrs/gui/parity.md); the CLI's [ADR 0002](../../../../adrs/cli/mode-parity.md) on the far side): one `ParityTable`. Layout is never a reason for a row to change. A pane too wide to sit beside becomes a sheet, never a gap.
- **The layers and MVVM** ([ADR 0001](../../../../adrs/gui/layer-enforcement.md), [ADR 0002](../../../../adrs/darwin/presentation-mvvm.md), [ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md)): the layout law stays in `ShellLayout`, which is pure, SwiftUI-free and handed the width as a value. iPad views live in their modules' `Views/` and shared pieces in `styles`. The Keychain sits in Infrastructure, below everything that asks it. Every iPad state has a named `#Preview` over `Previews/` fakes.
- **The dev channel** (the CLI docs' [Development runs on the dev channel](../../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel); [M18](../../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel) for the app): a development build of the iPad app is the dev identity (8a's and M18's identifier, "Apogee Dev"). It pairs only with an `apogee-dev serve`, never the person's production server.

## Technical approach

Paths under `lib/src/darwin/`, except the design row, which is under `lib/design/`.

| File | Change |
|---|---|
| `source/presentation/shell/ViewModels/ShellViewModel.swift` | `ShellLayout` gains the iPad's form, keyed by a platform value the composition hands in (the Mac's is unchanged). On the iPad, the sidebar is `.beside` or `.over` (drawn over the place, closed on a pick), and `sidePanes` becomes a presentation, `.beside` or `.sheet`, never off. The iPad's breakpoints are its own tokens, and the Mac's `collapseBelow` stays 900. |
| `source/presentation/shell/Views/ShellView.swift`, `Sidebar.swift` | The over-the-place sidebar with its toolbar button and a leading-edge swipe. On the iPad, no drag-resize divider and a fixed 256-point width. Settings opens the iPad's Settings place in place of `openSettings`. |
| `source/presentation/styles/` (`Metrics.swift`, `Components/`) | The iPad's minimums and breakpoints, and a shared `PaneSheet` that presents a side pane as a sheet. 9a reuses `PaneSheet`. The design's sizes come from `lib/design/tokens.json` as today. |
| `sessions/Views/SessionsScreen.swift`, `models/Views/ModelsScreen.swift`, `models/Views/StoreView.swift`, `graph/Views/GraphScreen.swift` | Each place with a side pane: the Sessions inspector (its ⌥⌘I enabled at every width), a backend's and a stored set's details, and a graph node's explain. Each asks `layout.sidePanes` how to present, not whether to. |
| `source/presentation/settings/` | Settings as a sheet over the window on the iPad, its panes listed beside their content, reached from the sidebar's row and ⌘,. On iPadOS it adds Settings › Server: the paired server, its state, Forget, and Pair another. The Mac's `Settings` scene is untouched. |
| `source/presentation/app/` | The iPad's scene, one `WindowGroup` with multiple scenes off, composed as the Mac's `Window` is. There is no ⌘Q wait on the iPad. |
| `source/presentation/app/ViewModels/FirstLaunch.swift` (new) | A pure state that goes from nothing paired, to pairing, to refused or unreachable, to paired, and holds the no-server guidance. |
| `source/presentation/app/Views/PairingView.swift`, `Views/ServerSetupView.swift` (new) | First launch's views, drawing 7a's connection form with no local choice. |
| `source/infrastructure/keychain/` (new, compiled on both platforms) | `KeychainStore`, the one place the pairing credential is written, read and deleted (Security framework). The Mac touches it only when the person opts into 7a's server connection; a Mac that never pairs never reads or writes it. If 7a has already landed a credential store for the Mac, the iPad uses that one and this module is not added. |
| `source/presentation/session/Views/SessionWindow.swift`, `console/Views/ConsoleView.swift` | Esc as a key press that works on both platforms, and the console's keys unchanged with a hardware keyboard. With none, the console offers Tab and history as controls. |
| The shell's and screens' buttons and rows | Hover and secondary click: the system hover effect, `.help` kept, and the two context menus reachable by long-press and secondary click. |
| `lib/design/prototype/` | Read [the design's guide](../../../../assistant/design/CLAUDE.md) first; every design update lands in the design app, as presets, their documents and gallery entries, checked by its local gate. iPad boards: landscape with the sidebar and inspector beside, portrait with the sidebar over and the inspector as a sheet, a narrow Split View, Settings as a sheet, pairing, and no server yet. Also the iPad values in `lib/design/tokens.json`'s `layoutPx`, the layout page's data (`gallery/Models/layout.ts`), and pairing's row in the contract page's (`gallery/Models/contract.ts`). |

**Reference.**
- In this table: [8a](../../v0.2.5/app-builds-for-ios-and-ipados.md) (the iPadOS build, its dev identity and the iOS test bundle) and [8c](../../v0.2.5/iphone-and-ipad-distribution.md) (getting the app onto a real iPad).
- Before it: [7a](../../v0.2.4/app-connection-local-or-server.md) (the connection and its server form, which the iPad uses without its local choice), [7c](../../v0.2.4/every-place-against-a-server.md) (every place against a server) and [7b](../../macos/v0.2.4/mac-offers-its-files-and-shell.md) (the Mac's device tools, which the iPad does not offer).
- After it: [9a](../../ios/v0.2.5/iphone-app.md) (the compact shell, which takes over the iPad's narrowest windows too and reuses `PaneSheet`, Settings as a place and this pairing), [9b](../../ios/v0.2.5/foldable-iphone.md) (the unfolded screen, which uses this item's layout) and [9c](../../v0.2.5/three-apple-platforms-whole.md) (v0.2.5 whole).
- On the CLI side: [59a](../../../cli/v0.2.3/paired-devices.md) (pairing: what is exchanged, and the words), [59b](../../../cli/v0.2.3/machine-mode-over-the-network.md) (the transport), [59c](../../../cli/v0.2.3/many-sessions-at-once.md) (many sessions), [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md) (reads and commands), [59e](../../../cli/v0.2.3/tasks-outlive-the-connection.md) (tasks outliving a dropped connection).
- Precedents: the shell's window law (`ShellLayout`, the design's `layoutPx`), M18's dev identity, and the not-found screen's honest install line (1b).

## Testing

All hermetic: scripted fakes, recorded documents, no binary, no network, no window.

- **`tests/presentation/shell/ShellViewModelTests.swift`** — the iPad form at the five measured widths in both orientations, and at Split View and Slide Over widths. A side pane is never off. The Mac's cases are pinned unchanged.
- **`tests/presentation/shell/ParityTests.swift`** — the same table and fixture, run in 8a's iOS test bundle as well, so no iPad-only carve-out can appear.
- **`tests/presentation/shell/KeyMapTests.swift`** — every shell binding has a control on the iPad; it also pins a control for every key-bound act on the iPad.
- **`tests/presentation/app/FirstLaunchTests.swift`** (new) — over a scripted fake of 7a's connection and 59a's recorded pairing replies: nothing paired leads to pairing; a refusal is shown in the server's words; an unreachable host is named, with Try again; no server leads to the setup guidance; paired leads to Sessions; Forget returns to pairing.
- **`tests/infrastructure/keychain/`** — the store over a fake keychain, with a planted credential never reaching the fake `KeyValueStore`, `Preferences`, a preview, a fixture or a log line.
- **`tests/architecture/layering`** — the Security framework only in Infrastructure, and UIKit, where used, only in Presentation.
- **`#Preview`s** at the measured iPad sizes for each iPad state.
- **The setup text's fixture** fails when the CLI's pairing help changes.
- **The Mac suite** stays green with its scene, discovery and termination untouched.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What is the iPad's width law? | `[default]` | At 1100 points or more, the sidebar and side panes sit beside the place. This covers every iPad in landscape: 1133 is the narrowest measured. Below 1100, the sidebar is drawn over the place and side panes are sheets. This covers every portrait iPad and Split View halves. The same form holds down to the narrowest Slide Over and Stage Manager window until 9a's compact shell takes compact widths. Both numbers go in `tokens.json` as `layoutPx.ipadBesideFrom`, and the Mac's `collapseBelow` is unchanged. |
| Is the iPad's shell the app's own, or `NavigationSplitView`? | `[default]` | The shell stays the app's own `HStack` shell, as on the Mac, not `NavigationSplitView`. The design's sidebar (places, sessions, the system strip, Settings) is the shell's own, and one `ShellLayout` keeps both platforms' law in one tested value. |
| How many windows? | `[default]` | One window. There is a single scene with multiple scenes off, as the Mac holds one `Window` because a second would mean a second live session. More windows under Stage Manager wait on 59c's many sessions and are not queued. |
| How does Settings open on the iPad? | `[default]` | Settings on the iPad is a sheet over the window, opened from the sidebar's Settings row and ⌘,. It holds the Mac's panes plus Server (the paired server, Forget, Pair another). |
| How do keys work? | `[default]` | Every `KeyMap.shell` binding and every screen's `.keyboardShortcut` works with a hardware keyboard, through the same `.commands`, so iPadOS lists them. Esc stops a turn on both platforms. No act is reachable only by a key. |
| How does the pointer work? | `[default]` | Buttons and rows take the system hover effect, and `.help` tooltips stay. The two context menus open by secondary click or long-press. The sidebar is a fixed 256 points on the iPad, so the Mac's resize pointer and drag have no counterpart. |
| What does first launch with nothing paired show? | `[default]` | It opens on pairing, which is 7a's server form with no local choice. Success leads to Sessions. A refusal is drawn in the server's words, an unreachable host is named, and both offer Try again. |
| What does "No server yet" say? | `[default]` | In a few lines: a server is your own Mac or Linux machine running Apogee; install it with the install line the Mac's not-found screen shows (`DiscoveryOutcome.installHint`); then start serving and pair with 59a's commands, written as the CLI's `--help` writes them. A recorded fixture of that help holds the text, refreshed by script as the parity fixture is. It offers no hosted server, no account and no link to sign up. |
| How many paired servers at a time? | `[default]` | One paired server at a time. Pair another replaces it, and Forget deletes the credential from the Keychain. |
| What kind of Keychain item is the credential? | `[default]` | A Keychain item that is this-device-only and readable after first unlock. It never syncs through iCloud Keychain, so a lost or replaced iPad is re-paired, and the server can revoke it under 59a. |
| How does the dev build show itself and keep off a release server? | `[default]` | The dev build shows "Apogee Dev", and its pairing and setup text name `apogee-dev`. Where 59a's handshake reports the server's channel, the dev build refuses a release server in those words. Where it does not, Settings › Server shows the version line the server reports, and the live pass checks the channel by hand. |
| What happens on quit and in the background? | `[default]` | There is no ⌘Q wait on the iPad, because the session lives on the server. Going to the background drops the connection. On return the app reconnects and redraws the open conversation from the server's record. |
| What does pairing exchange (a code, a QR or approval at the server), and what words do its refusals use? | `(consumed decision)` | 59a owns them. The iPad draws them. |
| What is the connection's form, its states and the Mac's credential home? | `(consumed decision)` | 7a owns them. The iPad uses them without the local choice. |
| How does each place behave against a server? | `(consumed decision)` | 7c owns it, including the carve-out reasons that say "run it at a prompt", which are written for a server there. |
| What happens to a chat turn or a task left running when the connection drops? | `(consumed decision)` | 59b owns a chat turn's, and 59e owns a task's. The iPad draws what they say. |
| What are the iPadOS target and distribution? | `(consumed decision)` | 8a owns the iPadOS target, universal for iPhone and iPad, with its identifier, its test bundle and its icon, one 1024-pixel image from `lib/design/assets/Logo.png`. 8c owns distribution, including the Apple Developer Program, which is a `[user]` call there. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item: the vision, Apogee feeling like Claude on every device the person owns, served by their own strong machine, mimicking frontier harnesses with local models; iPhone and iPad are clients of the person's Apogee server only, with no on-device core, while the Mac and the CLI stay standalone; the server is an add-on, never the only way the app runs, so nothing may make the CLI or the Mac app need a server, a network or an account; the SPEC amendment, the GUI reaches the CLI over stdio locally, or over an authenticated connection to the person's own `apogee serve`; no customer accounts, billing or metering, the server admits the person and their devices only, so "no server yet" never offers a hosted one or a sign-up; v0.2.5 is the target where macOS, iPadOS and iOS fully work. | the user |
| 2026-11-07 | Placed in v0.2.5 (track 8), gated on 8a. The iPad is regular width, so most of the Mac's layout reflows onto it and the new work is the narrow and overlay forms, Settings, keys and pairing. That makes it the step before the iPhone. 9a builds its compact shell on this item's `ShellLayout`, `PaneSheet`, Settings place and pairing, so it follows. | the spec |
| 2026-11-07 | Where the work happens (the user's call C): server tools by default, plus a connected desktop's own files and shell. The iPad offers no device tools of its own (iPadOS starts no shell), so a session driven from the iPad runs on the server's tools, plus what a connected Mac offers through [60b](../../../cli/v0.2.4/device-tools.md) and 7b. | the user |
| 2026-11-21 | Re-worded by design 1t, which retired the old one-file prototype and the claude.ai canvas: this item's design is drawn in the design app (presets, their documents and gallery entries, checked by its local gate), its sizes in `lib/design/tokens.json`, never in `lib/design`'s old folder or on the canvas. | design 1t (default) |


## Out of scope

- The compact (iPhone) shell, which also takes the iPad's compact widths once it lands (9a)
- The foldable iPhone (9b)
- Installing on a real iPad, TestFlight and the App Store (8c)
- More than one window or more than one paired server
- Push notifications
- Any on-device model or core, and device tools offered by the iPad
- Android, which follows v0.2.5 (a recorded default)

## Links

- **Blocked by:** [8a](../../v0.2.5/app-builds-for-ios-and-ipados.md), and through it on [7a](../../v0.2.4/app-connection-local-or-server.md) and CLI [59a](../../../cli/v0.2.3/paired-devices.md), [59b](../../../cli/v0.2.3/machine-mode-over-the-network.md) and [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md) · **Blocks:** [9a](../../ios/v0.2.5/iphone-app.md)
- **Follows from:** [7c](../../v0.2.4/every-place-against-a-server.md) (v0.2.4), which owns each place's behavior against a server and lands first in version order
- **Relates to:** [8c](../../v0.2.5/iphone-and-ipad-distribution.md), [7b](../../macos/v0.2.4/mac-offers-its-files-and-shell.md), [9b](../../ios/v0.2.5/foldable-iphone.md), [9c](../../v0.2.5/three-apple-platforms-whole.md), CLI [59c](../../../cli/v0.2.3/many-sessions-at-once.md), [59e](../../../cli/v0.2.3/tasks-outlive-the-connection.md) and [60b](../../../cli/v0.2.4/device-tools.md), [M18](../../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel), 1b (the not-found screen's install line), the GUI SPEC's [mobile applications](../../../../assistant/gui/SPEC.md#core-surfaces) and [No server requirement](../../../../assistant/gui/SPEC.md#non-goals), [ADR 0003](../../../../adrs/gui/parity.md)
