# APP-10b · The server window: administering this Mac's server (split first)

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Apple apps · macOS |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | The Apogee Server window (`BusinessServer`, `PresentationServer`, the app's scenes, the parity table) |
| **Labels** | `split-first` `admin-mode` `parity` `second-window` |
| **Epic** | Track 10 · the server window |
| **Blocked by** | [10a](design-the-server-window.md), [CLI 62a](../../../cli/v0.2.4/serve-status-and-runtime-record.md), [CLI 62c](../../../cli/v0.2.4/serve-section-in-config.md), [CLI 62h](../../../cli/v0.2.4/local-admin-reads.md) |
| **Blocks** | [10c](people-screens.md) |
| **Reporter** | The user's call, from the admin-mode and people spike (2026-11-14) |
| **Created** | 2026-11-14 |

---

## Summary

The Mac that runs the person's Apogee server has nowhere in the app to administer it: the app has one window, its keys are spent, `serve` is carved out whole, and `devices` and `apps` have no screen. This item builds a second **"Apogee Server"** window, offered only on that Mac, that drives the **local** CLI over pipes, with five places (Server, Devices, Apps, Activity and Audit), and changes the parity law so `serve` becomes partly a screen and partly refused and `devices` and `apps` become screens of this window. It is split first into three documents.

## Description

### User story

**As** a person using Apogee whose Mac runs their Apogee server,
**I want** a separate Apogee Server window on that Mac that administers the server through the local CLI (its status, how clients connect, the background service, paired devices and apps, activity and the audit),
**so that** I can run my server from the app, while a Mac that doesn't serve, and the CLI alone, stay exactly as complete as today and need no server.

### Background

The user's call, 2026-11-14, from the admin-mode and people spike: the Mac that runs the person's Apogee server gets an **admin mode**. It is a separate **"Apogee Server"** window in the macOS app, offered only on that Mac, that administers its server by driving the **local** CLI over pipes ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Core surfaces, item 3; [CLI SPEC](../../../../assistant/cli/SPEC.md) → Core surfaces, item 4, "Revised again 2026-11-14").

[10a](design-the-server-window.md) draws the window. This item builds it: a second `Window` scene with its own composition (reads and commands only, no session), its own Presentation module in MVVM, and five places, **Server, Devices, Apps, Activity and Audit**, each built as 10a draws it. People is [10c](people-screens.md)'s, later in the same release. The window also changes the parity law: `serve` becomes partly a screen and partly refused, and `devices` and `apps` become screens of this window.

### Evidence

**A10 — the app has one window, no second mode, nowhere to put devices and apps, and a parity table that classifies only top-level words.** This item closes the spike's wall A10. Re-measured 2026-11-14 at `4acf4a2b`, plus a probe on the built binary (`apogee 0.2.0 (b37b0a9f, 2026-11-09, macos-arm64)`, `lib/src/cli/build/macos-arm64/source/apogee`) in a scratch `APOGEE_HOME` under the session scratchpad, mock backend only, `serve --bind 127.0.0.1 --port 0` killed after:

1. **One window, by rule.** `ApogeeApp.swift:52-54` reads "One window: the app holds one live session, and a second window would mean a second", then declares one `Window(model.title, id: "main")`. The only other scene is `Settings` (`:99`). The application target `Apogee` builds `source/main/` alone. A mode separate from the harness has no scene to live in.
2. **One route, one sidebar, and the keys are spent.** `Destination` has 11 cases (`Destination.swift:8-20`), and its comment records ⌘1–⌘9, ⌘0 and ⇧⌘R as "the number keys spent" (`:4-6`). The shell keeps one `route` (`ShellViewModel.swift:14`), read from one preferences key, `route` (`ShellViewModel.swift:33`; `Preferences.swift:18`, `:55-56`). Adding admin places to `Destination` would put them in the harness's own sidebar and key map.
3. **The keys are app-wide.** `ShellCommands` binds every place's key with `.keyboardShortcut` inside `Commands` (`ShellCommands.swift:24-25`, `:37-38`, over `KeyMap.shell`, `KeyMap.swift:84-91`). `FocusedValue`, `focusedValue` and `focusedSceneValue` appear nowhere in `source/` (grep, 0 hits). With a second window focused today, ⌘1 would move the *harness* window's route. 10a's default that "only the focused window takes them" is something this item must make true.
4. **`devices` is queued as carved out, and `apps` has no classification.** 7a's recorded default classifies `devices` "as carved out beside `serve`" because "a pairing code is minted only on the server's own terminal" ([7a](../../v0.2.4/app-connection-local-or-server.md) (Open questions, the default classifying `devices` as carved out beside `serve`)). 59a says otherwise: the code "appears only on the server's own terminal, or on a local front end over pipes (`devices pair --json`)" ([paired-devices.md](../../../cli/v0.2.3/paired-devices.md) (Open questions, the default that a pairing code is never minted over HTTP)). No GUI document classifies `apps` (59f's `apps pair|list|scope|revoke`, [paired-apps-with-scopes.md](../../../cli/v0.2.3/paired-apps-with-scopes.md) (Technical approach, `apps_cmd.h/.cpp`: the `apps pair`, `list`, `scope` and `revoke` verbs)). On the probe, `devices list` and `apps list` exit 109 ("The following arguments were not expected"), and `__complete ""` answers 30 words, neither among them. When 59a and 59f land, the parity suite forces both words to be classified. The recorded default would then carve out exactly the screen admin mode needs.
5. **Parity is top-level, so `serve` cannot be split.** "The classification is top-level; only a refusal may name two words" ([ADR 0003](../../../../adrs/gui/parity.md):44). `serve` is carved out whole as "a daemon the app would orphan when it quits" (`Parity.swift:83`). A refusal matches by prefix (`path.starts(with: $0.words)`, `Parity.swift:123-124`), so refusing bare `serve` also refuses `serve status`. The fixture records top-level words only (`refresh-parity-fixture.sh:23`, `__complete ""`). The census pins 24 screens, 0 queued, 2 console, 4 carve-outs and 3 plumbing over 33 rows (`ParityTests.swift:36-42`). On the probe, `__complete serve ""` answers flags only (`--all-backends --allow-remote --bind --help …`), so a 0.2.0 binary offers the window no verb at all.
6. **Nothing the window would read exists yet, and the port is not a way around that.** On the probe:
   - `serve status --output-format json` exits 109;
   - `config get serve` answers "no such config key: 'serve'" (exit 1) in a home with a config (in a fresh home it says only that there is no config file);
   - while the server ran, the home held `config/admin-token`, `config/config.json`, `config/config.json.lock` and one log file, and nothing that says a server is running or on what port;
   - with the bearer, `/v1/admin/status`, `/connections`, `/devices` and `/apps` answered 404 and `/v1/admin/backends` answered 200; without it, 401.

   The admin plane is no fallback in any case: the app never connects to a server on its own machine ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Non-goals). The window waits for CLI 62a (`serve status` over a runtime record), 62c (the config's `serve` section) and 62h (the local admin reads), and is tolerant of the rest.
7. **What already works, and this item builds on it.**
   - **Every place reaches the CLI through one runner.** `ReadRunner(spawner:)` sits over `ProcessSpawner()` (`ReadRunner.swift:19-25`), and `AppViewModel` composes each place over it (`AppViewModel.swift:86-153`), adopting a binary found later through `connectPlaces()` (`:347`). A server window on its own Mac is ordinary reads and commands against the local binary, with no new transport.
   - **Settings is the pattern.** Its panes run "over the verbs a person types, each change read back" (`SettingsViewModel.swift:8-10`; the six panes at `:18-27`).
   - **There is precedent for a second window.** 7a plans "New Window on <server>" as a second composition, retiring the one-window rule for that window ([7a](../../v0.2.4/app-connection-local-or-server.md) (Technical approach, `source/presentation/app/`: "New Window on <server>" opens a second composition, retiring `ApogeeApp`'s one-window rule for a server window only)).
   - **Admin lists cannot show a secret.** 59a's `DeviceView` and 59f's `AppView` structurally lack the hash ([paired-devices.md](../../../cli/v0.2.3/paired-devices.md) (Constraints, Secrets are unleakable by construction: `DeviceView` structurally lacks the hash); [paired-apps-with-scopes.md](../../../cli/v0.2.3/paired-apps-with-scopes.md) (Constraints, Secrets are unleakable by construction: the app's view structurally lacks the hash)).

### Split first

This document is one item's whole design, built as three documents, each gated as shown. A second scene, two new modules, five places with their sheets, a preview for every state ADR 0004 asks for, and a change to the parity law are more than one session can build to the docs-first bar. Final ids are assigned at the split, and nothing already queued is renumbered.

1. **The window, its way in, and Server** (`server-window-and-server-place.md`, gated on 10a, CLI 62a and CLI 62c). This part includes:
   - the `Window("Apogee Server", id: "server")` scene and the Apogee menu's "Apogee Server…" (⌥⌘S), offered only when the local CLI says this Mac serves;
   - keys routed to the focused window, for the harness window's keys too;
   - the composition over the local `ProcessSpawner`;
   - the `BusinessServer` and `PresentationServer` modules, with their schemes, layer cards and layering-check rules;
   - the window's own route and sidebar;
   - **Server**: 62a's status; How clients connect, a form over 62c's `serve.*` keys with read-back; 62b's "applied now" or "needs a restart" where the binary has reload, and "applies when the server restarts" where it does not; 62d's service controls where the binary has them, and "run `apogee serve` at a prompt" where it does not;
   - the parity change: `serve` split into verb rows, bare `serve` refused exactly, the fixture's second-level words, [ADR 0003](../../../../adrs/gui/parity.md) amended, the census extended;
   - the fixture recorder (the spike's probe; see Testing).
2. **Devices and Apps** (`server-window-devices-and-apps.md`, gated on (1) and CLI 62h, which is itself gated on 59a and 59f). This part includes:
   - `devices` and `apps` classified as screens of `PresentationServer`, amending 7a's default ([7a](../../v0.2.4/app-connection-local-or-server.md) (Open questions, the default classifying `devices` as carved out beside `serve`));
   - the lists, with pending pairings;
   - the pairing sheet: the code, the QR the app draws from 62h's payload, the addresses, the port, the pin and the CLI's expiry, flipping to "Paired as <name>" on read-back;
   - rename, revoke (asked first, naming the apps that go with a device) and cancelling a pending pairing;
   - an app's scope beside what it asked for, Grant, and the scope editor;
   - 62g's "May administer from this device" where the binary has it.
3. **Activity and Audit** (`server-window-activity-and-audit.md`, gated on (1), tolerant of CLI 62e and 62f). This part includes:
   - each place shown when the binary offers its read, and otherwise a line naming the release that brings it;
   - Activity polled while visible, metadata only, with the honesty line;
   - Audit, newest first, filtered by who and by kind, read-only;
   - the parity rows for 62e's `serve activity` and 62f's verb.

## Acceptance criteria

- [ ] **(1)** **Given** a Mac whose local CLI says it serves, **when** I open the Apogee menu, **then** it has "Apogee Server…" on ⌥⌘S, and it opens a second window titled "Apogee Server" with a sidebar of Server, Devices, Apps, Activity and Audit on ⌘1–⌘5.
- [ ] **(1)** **Given** a Mac that does not serve, or an older binary, **when** the app runs, **then** there is no item and the harness window is exactly today's.
- [ ] **(1)** **Given** any state, including while the harness window is connected to a server, **when** the server window reads or runs a command, **then** it drives only the local binary over pipes; and **given** a running server, **when** I quit the app, **then** the server is left running.
- [ ] **(1)** **Given** the server window is on Server, **when** it reads, **then** it shows 62a's status.
- [ ] **(1)** **Given** Server's How clients connect form, **when** I change a field, **then** it changes `serve.*` through `config set` and reads the change back, saying "applied now" or "needs a restart" where the binary reloads, and "applies when the server restarts" where it does not.
- [ ] **(1)** **Given** a binary with 62d's verbs, **when** I look at Server, **then** its service controls appear, with Stop and Restart asked first; **given** a binary without them, **then** it says to run `apogee serve` at a prompt.
- [ ] **(1)** **Given** the server window is key, **when** I press ⌘1, **then** the harness window's place never moves; and the reverse.
- [ ] **(1)** `ParityTable` holds `serve status` (and the verbs present) as `PresentationServer` screens, and refuses bare `serve` with its reason. ADR 0003 is amended, the verbs fixture is recorded, and the census test is extended.
- [ ] **(2)** **Given** the server window, **when** I open Devices, **then** it lists paired devices and pending pairings.
- [ ] **(2)** **Given** Devices, **when** I press Pair, **then** it shows the code, a QR the app draws from 62h's payload, the addresses, port, pin and the CLI's expiry, and **when** the read-back shows it redeemed, **then** it turns to "Paired as <name>".
- [ ] **(2)** **Given** a paired device or a pending pairing, **when** I rename it, revoke it (asked first, naming the device's apps) or cancel the pending pairing, **then** the change is made.
- [ ] **(2)** **Given** the server window, **when** I open Apps, **then** it shows each scope beside what the app asked for, with Grant and the scope editor.
- [ ] **(2)** `devices` and `apps` are `PresentationServer` screens in `ParityTable`, and 7a's line is marked amended.
- [ ] **(3)** **Given** a binary with 62e's and 62f's records, **when** I open Activity or Audit, **then** each shows them, metadata only with the honesty line; **given** a binary without them, **then** it shows a line naming the release that brings them.
- [ ] `BusinessServer` and `PresentationServer` are on the layer cards and the layering check's map, with `PresentationServer.xcscheme` committed and a preview per state. The suite passes hermetically, the fixtures recorder runs clean in a throwaway home, and no code or secret is found outside the pairing sheet.
- [ ] The darwin CLAUDE.md Codebase Map, DEVELOPER.md, ADR 0003 and MILESTONES are updated in the same change as each part.

## Constraints

- **Nothing makes standalone use need a server or an account (the anti-goal).** The user's standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete and need no server ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Non-goals, No server requirement; 9c proves it at v0.2.5, [three-apple-platforms-whole.md](../../v0.2.5/three-apple-platforms-whole.md)). On a Mac that does not serve, the window is not offered: there is no menu item, no key, no sidebar entry, and no line in the harness window about serving. The harness window's places, keys, route and launch reads are byte-for-byte today's, apart from one status read made after the harness's own launch reads. That read creates nothing in `APOGEE_HOME`, needs no server, and is silent against an older binary.
- **Never listens; serving = server.** The window drives the local `apogee` over pipes, never the port on its own Mac ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Non-goals, No HTTP path for local work; [CLI CLAUDE.md](../../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). Its runner sits over `ProcessSpawner` directly, never 7a's `RoutingSpawner`, so it reaches this Mac's binary even while the harness window is connected to a server. It never opens a socket or a `URLSession`. The window never starts `apogee serve` as its own child. Start, stop and restart are 62d's service verbs, under which the server is launchd's child, so quitting the app leaves the server running. That is the answer to the carve-out's reason (`Parity.swift:83`).
- **One config mutation path** ([CLI CLAUDE.md](../../../../assistant/cli/CLAUDE.md) → One config mutation path). Every change is the CLI verb that makes it: `config set serve.*` (62c), the service verbs (62d), `devices rename|revoke` and `devices pending cancel` (59a, 62h), `apps scope|rename|revoke` (59f, 62h), and 62g's mark. Each change is read back, as Settings' panes are. The app never writes a file in `APOGEE_HOME`.
- **Secrets are unleakable** ([CLI CLAUDE.md](../../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction; [darwin CLAUDE.md](../../../../assistant/darwin/CLAUDE.md) → The GUI is a machine-mode client). The pairing code and the QR payload's `secret` exist only in the pairing sheet's view-model, for the sheet's life, from the stdout of the `pair` that minted them (62h). They are never in `Preferences`, the system log, a toast, the console's history, a list, an activity or audit row, or a fixture (see Testing). No view-model has a field for a token or a hash.
- **Additive, tolerant client** (the CLI's [ADR 0006](../../../../adrs/cli/backwards-compatibility.md); [GUI SPEC](../../../../assistant/gui/SPEC.md) → Principles). The window works against whatever the local binary offers. A place or control whose verb the binary lacks shows 10a's fallback line naming the release that brings it, never an empty place and never an error. Unknown fields in every document are kept and ignored.
- **Parity** (the CLI's [ADR 0002](../../../../adrs/cli/mode-parity.md); the GUI's [ADR 0003](../../../../adrs/gui/parity.md)). Everything in the window is a verb a person can type at a prompt. `ParityTable` stays the one source of the console's refusals, and the census and the fixture move in the same change as the rows.
- **The layers** ([ADR 0001](../../../../adrs/gui/layer-enforcement.md); [ADR 0002](../../../../adrs/darwin/presentation-mvvm.md); [ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md)):
  - Business: `BusinessServer` holds the typed documents, the argv and what this Mac serves. It is UI-free and imports Foundation only.
  - Presentation: `PresentationServer` holds `Views/` and `ViewModels/` (`@MainActor @Observable`, Foundation and Observation only), `Components/` and `Previews/`, and binds to `styles`.
  - `app` composes the window.
  - The harness's `shell` is not the server window's shell. A visual used by both is promoted to `styles` on its second use, never copied.
  - Every view has a named `#Preview` per visible state, over the binary's recorded documents, inside `#if DEBUG`. `PresentationServer` has its own shared scheme.
- **The words.** The places are Server, Devices, Apps, Activity and Audit. "Connections" stays 7a's word for the servers a Mac connects to. "Role" appears nowhere in the window, because in Apogee it means model roles.
- **Metadata, never contents.** No place shows a prompt, an answer, a file name or a chat's text. Activity says plainly that anyone who controls this Mac can still read its disk (10a's line).
- **The dev channel** ([darwin CLAUDE.md](../../../../assistant/darwin/CLAUDE.md#development-runs-on-the-dev-channel); [M18](../maintenance/app-dev-channel.md)). A development build's window administers `apogee-dev`'s server over `~/.apogee-dev`, never the release server. Tests are hermetic. Any probe uses a scratch `APOGEE_HOME`, never `~/.apogee` or `~/.apogee-dev`, and never `launchctl`.

## Technical approach

Paths under `lib/src/darwin/`.

| File | Change |
|---|---|
| `source/business/server/` (new) | New module, `BusinessServer`, with its test bundle `tests/business/server/` in the shared scheme, and a line on the Business layer card |
| `source/business/server/` · `ServeStatus` | 62a's document, typed where the binary types it, every field kept. It carries running or stopped, since when, the address and port, TLS and the pin's first groups, devices-only, the backends served and tools. It also carries 62b's drift (served beside on disk) and 62d's service state where present |
| `source/business/server/` · `ServeSettings` | 62c's `serve.*` keys read by `config get serve --output-format json`, and the `config set` argv for each |
| `source/business/server/` · `PairedDevices`, `PairedApps`, `PendingPairing`, `PairingTicket` | 59a's, 59f's and 62h's documents. `PendingPairing` has no field for a code. `PairingTicket` is the `pair` document (code, `qr_payload`, addresses, port, pin, expiry): its own type, non-`Codable` outward, never persisted |
| `source/business/server/` · `ActivityRows`, `AuditEntries` | 62e's and 62f's documents |
| `source/business/server/` · `ServerOffer` | Whether this Mac serves (62a's answer, read as 62a defines it), and which verbs the binary has, from `__complete <word> ""` |
| `source/business/server/` · `ServerVerbs` | Every argv the window runs, in one place |
| `source/presentation/server/` (new) | New module, `PresentationServer`, with `tests/presentation/server/`, its own shared scheme `xcshareddata/xcschemes/PresentationServer.xcscheme`, and its name on the Presentation layer card's Modules line |
| `source/presentation/server/ViewModels/` | `ServerWindowViewModel` (route, sidebar, the places' lifecycle, polling only while visible); `ServerPlaceViewModel`, `DevicesViewModel`, `AppsViewModel`, `PairingViewModel`, `ScopeEditorViewModel`, `ActivityViewModel` and `AuditViewModel`; `ServerPlace` (the window's own five-case route); and `ServerKeyMap` (⌘1–⌘5) |
| `source/presentation/server/Views/` | `ServerWindowView`, one view per place, `PairSheet`, `ScopeSheet`, and the asked-first confirmations |
| `source/presentation/server/Components/` | `PairingQRCode` (CoreImage's `CIQRCodeGenerator` over the payload's exact bytes, drawn and never written to disk) |
| `source/presentation/server/Previews/` | Fakes and slices of the recorded documents |
| `source/presentation/app/Views/ApogeeApp.swift` | A second scene, `Window("Apogee Server", id: "server")`. The comment at `:52-53` is revised: the one-window rule is about sessions, and the server window holds none. The `.commands` gain "Apogee Server…" in the Apogee menu (after Settings), present only while `ServerOffer` says this Mac serves, opening the window with `openWindow(id: "server")` |
| `source/presentation/app/ViewModels/AppViewModel.swift` | Composes the server window's view-models over a `ReadRunner(spawner: ProcessSpawner())` of its own and the jobs core. The status read runs at launch after the harness's reads, when the app becomes active, and after every server-window command. The window joins `connectPlaces()` (`:347`), so a binary adopted later reaches it |
| `source/presentation/app/ViewModels/Termination.swift` | An in-flight server-window command (a service restart, say) finishes within the existing bound. Nothing at quit stops the server |
| `source/presentation/shell/Views/ShellCommands.swift` (and `SessionCommands`) | The harness's keys act on the focused harness window, through `focusedSceneValue`, and are disabled while the server window is key. The server window publishes its own commands the same way |
| `source/presentation/shell/ViewModels/Parity.swift` | `CommandPlace` gains verb rows; the most specific row wins; a carved-out word that has verb rows refuses exactly its own words. The rules and the new rows per part follow the table |
| `source/data/preferences/Preferences.swift` | A `serverRoute` key beside `route` (additive). The window's place is remembered apart from the harness window's |
| `scripts/refresh-parity-fixture.sh` | Also records `tests/presentation/shell/Fixtures/command-verbs.txt`, the second-level words `__complete <word> ""` answers for every family the table splits, flags dropped |
| `scripts/refresh-server-fixtures.sh` (new) | Records the window's documents from a real binary (see Testing) |
| `tests/architecture/layering` | The two new modules on the map, with the rules in Testing |

**`Parity.swift` in full.** `CommandPlace` gains verb rows (`CommandPlace("serve status", .screen(module: "PresentationServer"), …)`). The most specific row wins. A carved-out word that has verb rows refuses exactly its own words, with any options after them, and never a verb that has its own row. New rows:

- in (1): `serve status`, plus `serve reload` (62b) and 62d's service verbs where the binary has them; bare `serve` stays carved out;
- in (2): `devices` and `apps` as `PresentationServer` screens, replacing 7a's carve-out row;
- in (3): 62e's `serve activity` and 62f's verb.

`config`'s note gains the window's How clients connect (`config set serve.*`, PresentationServer).

**Reference.**

- The track:
  - [10a](design-the-server-window.md), the boards this builds from (`ScreenServer*.dc.html`, BuildLayout's window and keys, BuildProcesses' launchd child, the contract map's SERVER WINDOW rows and gaps);
  - [10c](people-screens.md), People in this window, gated on this item.
- The CLI words it reads:
  - [62a](../../../cli/v0.2.4/serve-status-and-runtime-record.md), `serve status`;
  - [62b](../../../cli/v0.2.4/serve-reload-and-served-vs-disk.md), reload and the drift;
  - [62c](../../../cli/v0.2.4/serve-section-in-config.md), `serve.*`;
  - [62d](../../../cli/v0.2.4/serve-as-a-background-service.md), the service;
  - [62e](../../../cli/v0.2.4/connections-and-activity.md), `serve activity` (its consumed decision: how the window shows activity and how often it polls);
  - [62f](../../../cli/v0.2.4/audit-log.md), the audit;
  - [62g](../../../cli/v0.2.4/administering-local-and-marked-devices.md), the mark;
  - [62h](../../../cli/v0.2.4/local-admin-reads.md), pending pairings, the QR payload, rename, and the JSON policy reads (its consumed decisions: drawing the QR, the polling cadence, and classifying `devices` and `apps`);
  - [59a](../../../cli/v0.2.3/paired-devices.md), [59f](../../../cli/v0.2.3/paired-apps-with-scopes.md) and [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md).
- The app's side of a server:
  - [7a](../../v0.2.4/app-connection-local-or-server.md), the client side (Connections, the pair sheet, `RoutingSpawner`, "New Window on <server>");
  - [7c](../../v0.2.4/every-place-against-a-server.md), each command's remote meaning;
  - [7d](../../v0.2.4/apogeekit-swift-package.md) and [7e](../../v0.2.4/maintaining-apogeekit.md), ApogeeKit, whose public interface only grows.
- In-house precedents:
  - Settings' panes over the verbs with read-back (`SettingsViewModel.Pane`);
  - the monitor overlay, read every two seconds while open and never while closed;
  - the Models store's asked deletes (`DeletePlan`);
  - 2a's parity law and its fixture;
  - 5a, which carved out `acp` with its registration read for Settings › Xcode, a command that is partly a place and partly refused, done there by a note rather than a row.
- Prior art (the spike's `roles-prior-art`, 2026-11-14, from each product's official documentation unless marked): Plex's one owner with managed members (community pages) and Home Assistant's owner, admins and users shaped what 10c builds on this window; Nextcloud's "Devices & sessions" list (community pages), showing name, created and last used and never the secret, shaped Devices and Apps here.

**Docs when it ships** (in the same change as each part ships): [ADR 0003](../../../../adrs/gui/parity.md) amended (dated: verb rows, exact refusal of a split word, the verbs fixture); the darwin [CLAUDE.md](../../../../assistant/darwin/CLAUDE.md) Codebase Map and DEVELOPER.md (the modules and the scene); [7a](../../v0.2.4/app-connection-local-or-server.md)'s carve-out default marked amended by this item.

## Testing

- **Hermetic view-model tests** (Swift Testing, `lib/scripts/cicd.sh --app darwin --test`), over a scripted runner and the recorded documents, never the binary or a window:
  - each place's states;
  - a refused read drawn in the CLI's words;
  - each fallback against a binary that lacks the verb;
  - every change read back;
  - a change queued behind another;
  - the asked confirmations naming what they drop.
- **The anti-goal pinned:**
  - with `ServerOffer` false, the app's commands contain no "Apogee Server…" item;
  - the harness composition's reads are today's plus the one status read, and that read runs after them;
  - `KeyMap.shell` and `Destination` are unchanged (snapshot);
  - against a fixture of 0.2.0's 109 refusal, nothing is said anywhere.
- **Never the port:**
  - a recording spawner shows every server-window spawn is the local binary's argv;
  - the layering check (`tests/architecture/layering`, with the two new modules on its map) refuses `Network`, `URLSession` and `RoutingSpawner` in both new modules: `BusinessServer` and `PresentationServer` may not import `Network`, may not reference `URLSession`, and may not link `RoutingSpawner`'s module, pinned on a planted miniature as the check's other rules are;
  - a test shows the window's runner reaches the local binary while a fake active connection names a server.
- **Keys:** a test drives ⌘1 with the server window key and shows the harness route unchanged, and the reverse.
- **Parity (part 1):**
  - `serve` and `serve --port 0` are refused with the carve-out's reason;
  - `serve status` is not refused;
  - a planted unclassified verb under a split family fails by name;
  - a vanished verb fails;
  - the census is extended to the rows each part adds;
  - `command-verbs.txt` is refreshed from the binary that has them.

  In part (2), `devices pair` is not refused, and 7a's carve-out row is gone.
- **Secrets:**
  - after a `PairingTicket` is shown and the sheet closes, a test finds the code and the payload's secret in no view-model, no `Preferences` key (an in-memory `KeyValueStore`), no log line the app writes, and not in the console's history;
  - a grep over `Previews/` and the fixtures finds no live-shaped code or secret outside the one marked placeholder.
- **Previews** (ADR 0004): a named `#Preview` for every state 10a draws (`server`, `server:stopped`, `server:pending`, `server:devices`, `server:pair`, `server:apps`, `server:scope`, `server:activity`, `server:audit`, `server:older`), over slices of the recorded documents. `PresentationServer.xcscheme` builds the module and runs its tests alone.
- **The spike's probe is the regression fixture.** `scripts/refresh-server-fixtures.sh` re-runs it to record the window's documents: a throwaway `APOGEE_HOME` under `$TMPDIR`, the mock backend, `serve --bind 127.0.0.1 --port 0` killed after, and every read the window makes, recorded into `tests/business/server/Fixtures/`.
  - It never runs `launchctl` or a service verb that installs or starts anything. The service-installed states are copied byte for byte from 62d's own recorded test documents.
  - It replaces the minted code and the payload's `secret` with one fixed, marked placeholder of the same shape. This is the one rewrite, and the fixtures' README says so.
  - The same probe, run by hand on the gating binaries, is how each part shows its gates have landed: `__complete serve ""` answers `status`, `config get serve` answers, and `__complete ""` includes `devices` and `apps`.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What does "this Mac serves" mean, and what are the status fields? | `(consumed decision)` | [62a](../../../cli/v0.2.4/serve-status-and-runtime-record.md)'s; the window reads them in that document's words |
| Which changes reload and which need a restart? | `(consumed decision)` | [62b](../../../cli/v0.2.4/serve-reload-and-served-vs-disk.md)'s; the window reads it in that document's words |
| What are the `serve.*` keys? | `(consumed decision)` | [62c](../../../cli/v0.2.4/serve-section-in-config.md)'s; the window reads them in that document's words |
| What are the service verbs, and the LaunchAgent's label per channel? | `(consumed decision)` | [62d](../../../cli/v0.2.4/serve-as-a-background-service.md)'s; the window reads them in that document's words |
| What are the activity and audit documents and verbs? | `(consumed decision)` | [62e](../../../cli/v0.2.4/connections-and-activity.md)'s and [62f](../../../cli/v0.2.4/audit-log.md)'s; the window reads them in those documents' words |
| What is the mark? | `(consumed decision)` | [62g](../../../cli/v0.2.4/administering-local-and-marked-devices.md)'s; the window reads it in that document's words |
| What are the pending rows, `qr_payload` and rename? | `(consumed decision)` | [62h](../../../cli/v0.2.4/local-admin-reads.md)'s; the window reads them in that document's words |
| The look, the place names, the menu item and its key (⌥⌘S), the window's keys (⌘1–⌘6, People's ⌘6 arriving with 10c), and every fallback line? | `(consumed decision)` | [10a](design-the-server-window.md)'s |
| What is the remote meaning of the new rows (refused on a connection, except as 62g allows from a marked device)? | `(consumed decision)` | [7c](../../v0.2.4/every-place-against-a-server.md)'s classification, extended by the CLI's 62g |
| Where does People go, and does it join `PresentationServer` or become a module of its own? | `(consumed decision)` | [10c](people-screens.md)'s |
| Is the window a single `Window` or a `WindowGroup`? | `[default]` | The window is a SwiftUI `Window` (one instance), not a `WindowGroup`. Re-choosing the menu item brings it forward. Closing it ends its polls and nothing else. |
| When is the menu item offered, and when is the offer read? | `[default]` | The menu item is offered while `ServerOffer` says this Mac serves, as 62a defines it (a server running from this home, a `serve` section configured, or a service installed). The read runs at launch after the harness's reads, when the app becomes active, and after each server-window command. If the offer goes away while the window is open, the window stays and says the server is no longer set up here, rather than vanishing under the person. Against a binary without `serve status`, there is no item and nothing is said, as on a Mac that doesn't serve. The first setup stays the CLI's (10a's scope note). |
| How does the window learn what the binary offers? | `[default]` | What the binary offers is read once per binary, again when its file changes (4k's `BinaryFingerprint`), from `__complete serve ""`, `__complete devices ""`, `__complete apps ""` and 62f's word. This is the completion protocol the parity fixture comes from. An exit 109 on a read is the backstop, and the place then shows its fallback line. |
| Which Presentation module holds the window's chrome? | `[default]` | One Presentation module, `PresentationServer`, holds the window's own sidebar, route and five places. The harness's `shell` is not reused for the window's chrome, because `ShellView` is built around `Destination` and the session list (`ApogeeApp.swift:55-56`, the list at `:56`). |
| Where do the window's typed documents live? | `[default]` | A new Business module, `BusinessServer`, rather than growing `reads`. The harness's places never link it, and it stays out of ApogeeKit's public products (7d, 7e): administering a server is the Mac app's and not a client capability, and the package's public interface only grows, so it is not added there speculatively. |
| How do keys reach the right window? | `[default]` | Keys go to the focused window through `focusedSceneValue`. The harness's commands are disabled while the server window is key, and the server window's are disabled while the harness window is. The Apogee menu's ⌥⌘S works from either. |
| What polls, and how often? | `[default]` | Only the visible place polls, every two seconds as the monitor does, and only while the window is visible (not closed, minimized or fully occluded). The pairing sheet reads `devices pending` (or `apps`'s) every second while it is open, up to the CLI's expiry. 62e's default that the status and activity reads don't count themselves keeps the window off its own table. |
| What happens to a change made while another runs, and which changes are asked first? | `[default]` | A change made while another runs is queued and made in order, never dropped, as Settings does. Stop, Restart and removing the service are asked first, naming the connected devices and apps they will drop (62e's count when the binary has it, otherwise "anything connected"). |
| What are the window's title and subtitle? | `[default]` | The window's title is "Apogee Server". Its subtitle is the binary's version line, as the harness window's is (`ApogeeApp.swift:85`), so a dev build says `dev`. |
| Must M18 ship first? | `[default]` | M18 ships first. Without it, a development build's window would administer the release server. If M18 has not shipped when part (1) is taken, the builder takes M18 first, as 7a records. |
| Are the window's commands written to the console's history? | `[default]` | The window's commands run through the same jobs core as the console but are not written to the console's history, so a `pair` line (whose stdout is the code) never lands there. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the admin-mode and people spike; the user's calls that shape it are the rows below, through "Already written". | the spec |
| 2026-11-14 | **An admin mode** is a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes. | the user |
| 2026-11-14 | **Administering is local by default.** The owner may mark chosen paired devices as allowed to administer remotely (62g), which revises 59d's default that the admin plane keeps one token for any network caller. Otherwise the admin plane stays local-only. | the user |
| 2026-11-14 | **The window installs the server as a background service** (launchd), and starts, stops and restarts it. | the user |
| 2026-11-14 | **Placement:** the server console (CLI track 62; darwin/macos 10a and 10b) and people and access (CLI track 63; darwin/macos 10c), both in v0.2.4. Nothing already queued is renumbered. | the user |
| 2026-11-14 | **Defaults the user took:** the words "people" and "access" (Owner, Member), never "roles", which in Apogee means model roles; live changes applied on the spot (reload), with what still needs a restart said; an audit kept 90 days of who, what and when, without request contents. | the user |
| 2026-11-14 | **People, later in v0.2.4 (10c and CLI track 63, not this item):** a household or a small team, designed for both. The owner sees members' metadata (devices, apps, activity, which model, when), never chat contents, and is told honestly that whoever controls the server Mac can still read its disk. Members use the server's local models only, unless the owner grants a household cloud key with a limit. The other defaults the user took: collections are shared libraries handed out per person, plus private ones of a member's own; notes and saved decisions are personal by default; a member's apps are paired by the owner at the server Mac, capped at that member's allowance; tasks queue per person rather than being refused. This item builds none of it, but its window is where People lands (⌘6), and its Activity and Audit already show metadata only. | the user |
| 2026-11-14 | **The People model (the agent's default, from the prior art):** the owner is implicit (the OS account that owns `APOGEE_HOME` on the server Mac), and members each have an allowance in 59f's `AppScope` shape, set from presets (Full, Family, Limited) rather than groups; a guest is a member on the Limited preset with nothing kept. | default |
| 2026-11-14 | **Standing law:** remote use is an add-on. The CLI alone and the Mac app over pipes stay complete and need no server. | the user |
| 2026-11-14 | **Already written:** the SPEC amendments (CLI SPEC → Core surfaces 4, "Revised again 2026-11-14"; GUI SPEC → Core surfaces 3). | — |
| 2026-11-14 | darwin/macos 10b, v0.2.4, gated on 10a (the boards it builds from), CLI 62a (whether this Mac serves, and what the server is doing), CLI 62c (how clients connect, as config) and CLI 62h (the reads Devices and Apps need, behind which sit 59a and 59f). It sits after 10a because the design's law draws before it builds. It sits before 10c because People is a place in this window. Activity, Audit, reload, the service and the mark are tolerant (62b, 62d, 62e, 62f, 62g), so the window ships the day its four gates do and each of those places lights up when its verb arrives. macOS only (`backlog/darwin/macos/`): iPhone and iPad never offer it. | the spec |
| 2026-11-14 | **Split first** into the three documents above. A second scene, two modules, five places, the pairing and scope sheets, a preview per state, and a change to the parity law and its ADR are more than one session builds to the bar. | the spec |
| 2026-11-14 | **7a's default is amended.** `devices` (and `apps`, unclassified until now) are screens of the server window, not carve-outs. 59a mints a code for "a local front end over pipes" ([paired-devices.md](../../../cli/v0.2.3/paired-devices.md) (Open questions, the default that a pairing code is never minted over HTTP)), and the server window is that front end. The amendment lands in part (2), with 7a's line marked in the same change. | the spec |
| 2026-11-14 | **The parity law goes below the top level for a family it splits** (ADR 0003 amended in part (1)). This is the only way `serve status` can be a screen while bare `serve`, the daemon, stays refused with its reason. Every other row stays top-level. | the spec |
| 2026-11-18 | Moved here from v0.2.6 when the releases were merged; the id is unchanged. | the user |

## Out of scope

- People, presets, allowances, keys and limits (10c, CLI 63a and 63c)
- Making a Mac that doesn't serve into a server from the app (the first setup stays the CLI's)
- The client side's Connections pane, pair sheet and indicator (7a)
- A server window opened over a connection from a marked device (62g lets the CLI administer from one; a window for it is not queued)
- The window on iPhone or iPad (never offered)
- The Linux and Windows apps' server windows (after v0.2.5)
- Any CLI change

## Links

- **Blocked by:** [10a](design-the-server-window.md), [CLI 62a](../../../cli/v0.2.4/serve-status-and-runtime-record.md), [CLI 62c](../../../cli/v0.2.4/serve-section-in-config.md), [CLI 62h](../../../cli/v0.2.4/local-admin-reads.md) · **Blocks:** [10c](people-screens.md)
- **Split parts' gates:** (1) on 10a, 62a and 62c; (2) on (1) and 62h; (3) on (1), tolerant of 62e and 62f.
- **Tolerant of:** [CLI 62b](../../../cli/v0.2.4/serve-reload-and-served-vs-disk.md), [CLI 62d](../../../cli/v0.2.4/serve-as-a-background-service.md), [CLI 62e](../../../cli/v0.2.4/connections-and-activity.md), [CLI 62f](../../../cli/v0.2.4/audit-log.md), [CLI 62g](../../../cli/v0.2.4/administering-local-and-marked-devices.md)
- **Amends:** [7a](../../v0.2.4/app-connection-local-or-server.md)'s default that carves out `devices`; [ADR 0003](../../../../adrs/gui/parity.md) (the parity law goes below the top level for a split family)
- **Follows from:** the admin-mode and people spike (2026-11-14), closing its wall A10; [GUI SPEC](../../../../assistant/gui/SPEC.md) → Core surfaces 3; [CLI SPEC](../../../../assistant/cli/SPEC.md) → Core surfaces 4
- **Relates to:** [M18](../maintenance/app-dev-channel.md) (ships first); [7c](../../v0.2.4/every-place-against-a-server.md), [7d](../../v0.2.4/apogeekit-swift-package.md), [7e](../../v0.2.4/maintaining-apogeekit.md); [59a](../../../cli/v0.2.3/paired-devices.md), [59f](../../../cli/v0.2.3/paired-apps-with-scopes.md), [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md); 9c ([three-apple-platforms-whole.md](../../v0.2.5/three-apple-platforms-whole.md)), which proves standalone use needs no server at v0.2.5
