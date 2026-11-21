# APP-9a · The iPhone app

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Apple apps · iOS |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.5 |
| **Component** | The shell's compact layouts, every place's views, the Sessions library (cross-device continuation) |
| **Labels** | `ios` `compact-layout` `remote-only` `cross-device` |
| **Epic** | Track 9 · the iPhone, the foldable iPhone and the three Apple platforms whole |
| **Blocked by** | [8b](../../ipados/v0.2.5/ipad-app.md) |
| **Blocks** | [9b](foldable-iphone.md) |
| **Reporter** | The user's vision, through the Apple-platforms spike and brainstorm (2026-11-07) |
| **Created** | 2026-11-07 |

---

## Summary

The app has one shape, a wide window with a sidebar beside the place, and an iPhone in portrait is 375 to 440 points wide. This item delivers the iPhone app: every place in compact layouts (a tab bar and stacks, chat first), remote-only over the person's own server, pairing on first launch, one parity table, and a conversation started on the Mac continued on the iPhone through the server.

## Description

### User story

**As** a person using Apogee with their own Apogee server,
**I want** to pick up my phone, open Apogee and be in my conversation, with every place of the Mac app within reach by touch,
**so that** Apogee feels like Claude on every device I own, served by my own strong machine.

### Background

The user's vision, 2026-11-07: Apogee as a private, self-hosted harness that feels like Claude on every device the person owns, with one strong machine of theirs serving their desktops and phones. On the iPhone that means a person picks up their phone, opens Apogee, and is in their conversation, with every place of the Mac app within reach. The iPhone is a client of the person's own Apogee server only (the GUI SPEC's [mobile applications](../../../../assistant/gui/SPEC.md#core-surfaces)).

By this item, the code builds for iOS ([8a](../../v0.2.5/app-builds-for-ios-and-ipados.md)), the app reaches a server ([7a](../../v0.2.4/app-connection-local-or-server.md)), every place works against one ([7c](../../v0.2.4/every-place-against-a-server.md)), and the iPad has its regular-width app with pairing ([8b](../../ipados/v0.2.5/ipad-app.md)). What remains is the phone's size.

This item delivers:
- **The iPhone app**: every place of the app in compact layouts (a tab bar and stacks), opening on the conversation first.
- **Remote only**: the server reached over 7a's connection, which is the only transport on the phone.
- **Pairing on first launch**: 8b's pairing in a phone-sized flow.
- **The same parity table**: every place, the console, the monitor and Settings reachable by touch.
- **A session continued across devices**: a conversation started on the Mac against the server is opened and continued on the iPhone, through the server, and back.

### Evidence

Measured at HEAD (`4f92316d`) on 2026-11-07.

**W10 — no compact layouts.** The app has one shape: a wide window with a sidebar beside the place.

1. **The window is at least 960 × 640 points** (`source/presentation/styles/Metrics.swift:26-27`, applied at `source/presentation/app/Views/ApogeeApp.swift:83`). An iPhone in portrait is 375 to 440 points wide. The Settings window asks for 860 (`settings/Views/SettingsView.swift:58`) and the console for 720 × 480 (`console/Views/ConsoleView.swift:127`). Training and Knowledge each set a fixed 380-point page beside a list at least 380 and 360 points wide (`training/Views/TrainingScreen.swift:201`, `:213`; `knowledge/Views/KnowledgeScreen.swift:125`, `:137`), wider together than any phone.
2. **The narrowest layout the shell knows is still a sidebar.** `ShellLayout` (`shell/ViewModels/ShellViewModel.swift:111-135`, the struct from `:114`) has three cases: the full 256-point sidebar, a 56-point column of icons below 900 points (`collapseBelow`, from `tokens.json` `layoutPx.collapseBelow`, `lib/design/spec/json/tokens.json:365-368`), or hidden. `ShellView` lays out an `HStack` of sidebar and place (`shell/Views/ShellView.swift:31-34`). There is no tab bar, no navigation stack and no size-class read: a search for `horizontalSizeClass`, `NavigationStack`, `NavigationSplitView` and `TabView` over `source/` finds none.
3. **A narrow window loses whatever sits in a side pane, so it cannot just be shrunk.** Below 900 points `sidePanes` is false (`ShellViewModel.swift:128`, set at `:131-133`). The Sessions inspector's button is then disabled (`sessions/Views/SessionsScreen.swift:30`, `:113`, and its ⌥⌘I at `:373-374`), a backend's details are not drawn (`models/Views/ModelsScreen.swift:56`), a stored set's details are not drawn (`models/Views/StoreView.swift:18`), and a graph node's explanation is not drawn (`graph/Views/GraphScreen.swift:88`). On a phone those would have no way in at all. That would be a parity loss under [ADR 0003](../../../../adrs/gui/parity.md).
4. **The keyboard carries many acts.** There are 77 `.keyboardShortcut` call sites under `source/`, and the shell's `KeyMap` lists the design's whole keyboard. Without a hardware keyboard, every act needs a control a finger can reach.
5. **The design draws no phone.** No file in `lib/design/spec` mentions an iPhone, an iPad or a mobile layout (a case-insensitive search for `iphone|ipad|mobile` matches none).
6. **Context, owned elsewhere (not this item's walls).**
   - The project builds macOS only: `SDKROOT = macosx` in both configurations (`Apogee.xcodeproj/project.pbxproj:7889`, `:8744`). That is 8a's.
   - The served API has no pairing: `apogee serve --help` on the branch binary (`apogee 0.2.0 (4f92316d)`, a scratch `APOGEE_HOME`) names no pairing or device flag (a search for `pair|device` returns 0). `apogee __machine-schema` (53,726 bytes) has no transport or pairing field, and its one `device` property is the `ready` event's runtime (`cuda`, `mps`, `cpu`). That is CLI [59a](../../../cli/v0.2.3/paired-devices.md) and [59b](../../../cli/v0.2.3/machine-mode-over-the-network.md).
   - A served session is already a chat session, written to the file `apogee chat` writes under the same id, so a conversation started over HTTP continues with `--resume` ([http-api.md:210-214](../../../../reference/cli/http-api.md#sessions)). The machine mode names each session's chat by `chat_id` ([machine-mode.md:107-116](../../../../reference/cli/machine-mode.md#which-chat-chat_id)). Continuing a conversation on another device builds on both.

## Acceptance criteria

- [ ] **Given** the app on an iPhone (the Simulator's smallest and largest current phones), **when** it opens, **then** it opens on its last conversation; every place, the console, the monitor and Settings are reached by touch, and no inspector, details or explanation is unreachable.
- [ ] **Given** a first launch with nothing paired, **when** the app opens, **then** it leads through pairing to Chats, and a refused pairing says why in the server's words.
- [ ] **Given** a conversation started in the Mac app pointed at the server, **when** the iPhone opens its library, **then** the conversation is listed, opens with its earlier turns, takes a new turn whose answer streams with its thinking, tool narration and cards, and then continues on the Mac with that turn in place.
- [ ] **Given** a turn in progress in the conversation open on the phone, **when** the phone is backgrounded mid-turn and the person returns, **then** the conversation is redrawn from the server's record without losing a turn.
- [ ] **Given** the Mac app in local mode, **when** it is used, **then** it behaves exactly as before: local stays its default, it opens no network connection unless the person points it at a server (7a's opt-in), and the Mac's suite is unchanged.
- [ ] Every compact view has its named `#Preview`. The design canvas carries the phone boards. The full suite is green.
- [ ] A live pass: the dev build in the iOS Simulator paired with a sandboxed `apogee-dev serve` over `~/.apogee-dev` and a mock backend (the user's live pass on a device is optional and said).

## Constraints

- **Anti-goal: nothing here may make standalone use need a server** (GUI SPEC → [No server requirement](../../../../assistant/gui/SPEC.md#non-goals)). The Mac app over pipes to its local CLI, and the CLI alone, stay complete and the default. Compact layouts are Presentation views selected by size class. The Mac's local transport, its discovery and its settings are untouched. Cross-device continuation covers conversations that live on the server. A Mac in local mode keeps its chats in its own `~/.apogee`, uploads nothing and contacts no server.
- **The machine-mode client rule, as amended 2026-11-07** ([GUI CLAUDE.md → The GUI is a machine-mode client](../../../../assistant/darwin/CLAUDE.md#-the-gui-is-a-machine-mode-client)): on the iPhone the machine mode arrives only over the authenticated connection to the person's own `apogee serve`. It is the same protocol, decoded with the same tolerance, with no capability of the app's own. The phone never listens. Its connection is outbound, and serving stays the server's job (the CLI's [never-listens rule](../../../../assistant/cli/CLAUDE.md#-interactive-turns-never-open-a-listening-socket): interactive = pipes, serving = server).
- **Secrets are unleakable** (the CLI's [invariant](../../../../assistant/cli/CLAUDE.md#-secrets-are-unleakable-by-construction); the GUI rule's last line): the pairing credential is held in the iPhone's Keychain by 8b's mechanism and never appears in a view, a log, a preview or a recorded fixture. Keys for providers are still entered through the CLI's own flows on the server.
- **Additive protocol** (the CLI's [ADR 0006](../../../../adrs/cli/backwards-compatibility.md)): the phone tolerates every event and field it does not know, and never needs a protocol change that breaks an older front end. A server without the cross-device fields still opens its chats from the library (the M7 path), without the "continued from" line.
- **Parity** (the GUI's [ADR 0003](../../../../adrs/gui/parity.md); the CLI's [ADR 0002](../../../../adrs/cli/mode-parity.md) on the far side): one `ParityTable` for every size. The phone adds no carve-out of its own. A place that is awkward on a small screen is still a place.
- **The layers and MVVM** ([ADR 0001](../../../../adrs/gui/layer-enforcement.md), [ADR 0002](../../../../adrs/darwin/presentation-mvvm.md), [ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md)): compact is a view concern. View-models stay shared, SwiftUI-free and unchanged in meaning. Size class is read in Views and handed to the shell's view-model as a value. Compact views live in their own modules' `Views/`, shared pieces in `styles`, and every compact state has a named `#Preview`.
- **The dev channel** (the CLI docs' [Development runs on the dev channel](../../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)): a development build of the iPhone app is the dev identity (8a's and M18's identifier) and pairs only with an `apogee-dev serve` over `~/.apogee-dev`. It never pairs with the person's production server.

## Technical approach

All under `lib/src/darwin/` (the design row is the repo's `lib/design/spec/`).

| File | Change |
|---|---|
| `source/presentation/shell/ViewModels/ShellViewModel.swift` | `ShellLayout` gains a `compact` form, chosen from the horizontal size class handed in (compact means tabs and stacks; regular means 8b's iPad shell, and the Mac is unchanged). `sidePanes` stays a question for regular layouts. In compact, a side pane is presented, never dropped. |
| `source/presentation/shell/ViewModels/CompactNavigation.swift` (new) | The phone's map. It defines the tabs, each tab's root, the stack of each, where each of the eleven `Destination`s, the console, the monitor and Settings is reached, and the route kept in `Preferences` across launches. It is pure and tested. |
| `source/presentation/shell/Views/CompactShell.swift` (new) | The `TabView` with a `NavigationStack` per tab. `ShellView` picks it in compact. The toast and overlays are presented as sheets. |
| `source/presentation/styles/Metrics.swift` and `Components/` | The compact metrics (gutters, row heights, the input bar's), and a shared `PaneSheet` that presents a side pane (inspector, details, explain) as a sheet or a pushed page. It is promoted to `styles` on its second use, per ADR 0002. |
| Each place's `Views/` | Compact variants only where the regular view cannot reflow: the session window (transcript, the input bar above the keyboard, question and permission cards full-width), Sessions (the library as the Chats tab's root, the inspector as a sheet), Models and its store (details pushed), Graph (the canvas full-screen, explain as a sheet), Training and Knowledge (their fixed 380-point run and record pages pushed), Settings as a place (8b's) and the console (full-screen, on-screen keyboard). A place's view-model changes only where a compact act needs a new intent, such as "present the inspector". |
| `source/presentation/sessions/` | The cross-device continuation. The library lists the server's chats (7a and CLI [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md)), an opened chat resumes by `chat_id` over 59b's transport, and its earlier turns are drawn from `chats show` above the M7 divider. The "continued from" line uses the server's words where it sends them. |
| `source/presentation/app/` | The compact scene's composition. There is no `Settings` scene on iOS, so Settings is a place (8b's). |
| `lib/design/spec/` | Phone boards (every design update lands there; read `spec/CLAUDE.md` first): the chat first, the tabs and More, a side pane as a sheet, pairing, and a conversation continued from the Mac. Also the compact values in `json/tokens.json`'s `layoutPx` and `html/BuildLayout.dc.html`. |

**Reference.** [8b](../../ipados/v0.2.5/ipad-app.md) (the regular-width app and pairing this item reuses), [8a](../../v0.2.5/app-builds-for-ios-and-ipados.md) (the iOS build and the transport exclusion), [7a](../../v0.2.4/app-connection-local-or-server.md) and [7c](../../v0.2.4/every-place-against-a-server.md) (the connection, and every place against a server, which this item makes phone-sized), [7b](../../macos/v0.2.4/mac-offers-its-files-and-shell.md) (the Mac's device tools, which the phone does not offer). On the CLI side: [59a](../../../cli/v0.2.3/paired-devices.md) (pairing), [59b](../../../cli/v0.2.3/machine-mode-over-the-network.md) (the transport), [59c](../../../cli/v0.2.3/many-sessions-at-once.md) (many sessions, and two devices on one chat), [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md) (reads), [59e](../../../cli/v0.2.3/tasks-outlive-the-connection.md) (tasks outliving a dropped connection), [60b](../../../cli/v0.2.4/device-tools.md) (device tools). Then [9b](foldable-iphone.md) (the foldable's two screens, which build on this item's compact shell) and [9c](../../v0.2.5/three-apple-platforms-whole.md) (v0.2.5 whole). Precedents: M7 (a reopened conversation's turns from `chats show`), M10 (a session known by the CLI's `chat_id`), the shell's window law (`ShellLayout`, the design's decision 10), and the served session as a chat session (http-api.md → Sessions).

## Testing

All hermetic: scripted fakes, recorded documents, no binary, no network, no window.

- **`ShellViewModelTests`** (`tests/presentation/shell/ShellViewModelTests.swift`) — the compact form from a compact size class; regular unchanged. The `ShellLayout` regular and Mac cases are pinned unchanged.
- **`CompactNavigationTests`** (new, `tests/presentation/shell/CompactNavigationTests.swift`) — every `Destination`, the console, the monitor and Settings can be reached; chat comes first; the route is restored and survives a relaunch; a side pane is presented, never dropped.
- **`ParityTests`** (`tests/presentation/shell/ParityTests.swift`) — the table unchanged: one table for every size, with no compact-only carve-out, and every screen place reachable in compact.
- **`KeyMapTests`** (`tests/presentation/shell/KeyMapTests.swift`) — every key-bound act has a touch control in compact.
- **The sessions tests** (`tests/presentation/sessions/`) — cross-device continuation by `chat_id` over a recorded server document: a recorded `chats list` from a server holding a chat begun on another device, the open resuming by its `chat_id`, the earlier turns drawn, a turn sent; and an older server's fallback, a server lacking the new fields still opening it.
- **Previews** — `#Preview`s run under a compact trait for each compact state, over the modules' `Previews/` fakes.
- **Secrets** — a planted pairing credential never appears in a fixture, a preview or a log line.
- **The layering check** stays green (no UIKit type below Presentation; size class read only in Views).

## Open questions

| Question | Status | Answer |
|---|---|---|
| How does the phone navigate? | `[default]` | A tab bar of four. **Chats** (the Sessions library as its root, the last open conversation pushed on launch, so the conversation comes first), **Tasks**, **Models**, and **More**. More is a stack listing the other places, the console, the monitor and Settings in the sidebar's order. A tab is not a new route: each maps to a `Destination` the shell already keeps. |
| What makes a layout compact? | `[default]` | The horizontal size class is compact, not a point width. On the Mac nothing reads it, so the Mac's `ShellLayout` is byte-identical. |
| What becomes of a side pane in compact? | `[default]` | It becomes a sheet (the Sessions inspector, a graph node's explanation) or a pushed page (a backend's or a stored set's details, Training's and Knowledge's panes). None is ever disabled the way `sidePanes` disables it today. |
| What does first launch with nothing paired show? | `[default]` | The app opens on 8b's pairing flow laid out for compact, then on Chats. A failed or refused pairing is drawn in the server's words, with Try again. |
| What happens when the phone goes to the background? | `[default]` | Going to the background drops the connection (iOS suspends the app), with no background execution on the phone. On return the app reconnects and redraws the open conversation from the server's record. What the server does with a turn left running is a consumed decision (next row), and the phone draws what they say. |
| What does the server do with a turn left running when the phone goes away? | `(consumed decision)` | 59b for a chat turn, 59e for a task. |
| May two devices hold one chat at once, and what is the first one told? | `(consumed decision)` | 59c owns it. |
| How does the app show 59c's answer on two devices holding one chat? | `[default]` | The app renders 59c's answer. Where the server says a chat moved to another device, the window says so in its words and offers Continue here. Against a server that says nothing, opening a chat that another device holds shows the refusal it sends. |
| Do a desktop's device tools stay with a session after its driver moves to the phone? | `(consumed decision)` | 60b owns it. |
| How does the phone show a tool call on a desktop's files? | `[default]` | The phone draws a tool call on a desktop's files like any other, with its permission card answerable from the phone, and shows the CLI's refusal when the desktop is gone. |
| Does a conversation from a Mac in local mode appear on the phone? | `[default]` | Never. Moving a local chat to the server is not part of this item and is not queued. |
| Does the phone get push notifications? | `[default]` | No push notifications. A finished task is seen on the next foreground read. Notifications need the Apple Developer Program, which is [8c](../../v0.2.5/iphone-and-ipad-distribution.md)'s [user] call, so they wait for it. |
| The iOS target (universal for iPhone and iPad) and its dev-channel identity; distribution | `(consumed decision)` | 8a owns the iOS target, universal for iPhone and iPad, and its dev-channel identity; 8c owns distribution. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item: the vision, Apogee feeling like Claude on every device the person owns, served by their own strong machine; iPhone and iPad are clients of the person's Apogee server only, with no on-device core; the server is an add-on, never the only way the app runs, so nothing may make the CLI or the Mac app need a server, a network or an account; the foldable iPhone ("iPhone duo") is in scope, as its own item (9b) on this one's shell; v0.2.5 is the target where macOS, iPadOS and iOS fully work, reached in successive versions, with Windows and Android after it (a recorded default); no customer accounts, billing or metering: the server admits the person and their devices only. | the user |
| 2026-11-07 | Placed in v0.2.5 (track 9), first of the track and gated on 8b. The phone reuses the iPad's iOS shell, Settings place and pairing, and adds only the compact size. 9b's foldable switches between this item's compact shell and 8b's regular one, so it follows this item. | the spec |
| 2026-11-07 | Where the work happens (the user's call C): server tools by default, plus a connected desktop's own files and shell. The iPhone offers no device tools of its own (iOS starts no shell), so a session driven from the phone runs on the server's tools, plus whatever a connected desktop offers through 60b. A desktop's device tools touch that desktop's own files: a change asked for from the phone to a project on the Mac lands in the Mac's files, never a server copy. | the user |
| 2026-11-18 | Moved here from v0.3.0 when the releases were merged; the id is unchanged. | the user |

## Out of scope

- The foldable's folded and unfolded switching (9b)
- Distribution to a device beyond the Simulator and a development install (8c)
- Push notifications
- Moving a local Mac chat to the server
- Any on-device model or core
- Android (after v0.2.5, a recorded default)

## Links

- **Blocked by:** [8b](../../ipados/v0.2.5/ipad-app.md) · **Blocks:** [9b](foldable-iphone.md)
- **Gated through 8b on:** [8a](../../v0.2.5/app-builds-for-ios-and-ipados.md), [7a](../../v0.2.4/app-connection-local-or-server.md) and [7c](../../v0.2.4/every-place-against-a-server.md), and CLI [59a](../../../cli/v0.2.3/paired-devices.md), [59b](../../../cli/v0.2.3/machine-mode-over-the-network.md), [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md) and [59e](../../../cli/v0.2.3/tasks-outlive-the-connection.md).
- **Reads, not gated:** CLI [59c](../../../cli/v0.2.3/many-sessions-at-once.md)'s answer on two devices holding one chat, which no gate in the chain requires: against a server without it, the app shows the refusal the server sends (the open question above).
- **Relates to:** [7b](../../macos/v0.2.4/mac-offers-its-files-and-shell.md), CLI [60b](../../../cli/v0.2.4/device-tools.md), [8c](../../v0.2.5/iphone-and-ipad-distribution.md), [9c](../../v0.2.5/three-apple-platforms-whole.md); M7, M10, M18; [ADR 0003](../../../../adrs/gui/parity.md).
