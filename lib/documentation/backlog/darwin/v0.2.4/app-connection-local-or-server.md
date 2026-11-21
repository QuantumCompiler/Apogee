# APP-7a · The app's connection: local or your server

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Apple apps |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | The client core's transport (`ChildSpawner`) · Settings › Connections · the shell's connection indicator |
| **Labels** | `networking` `pairing` `transport` `settings` |
| **Epic** | Track 7 · the Mac as a client |
| **Blocked by** | [CLI 59b](../../cli/v0.2.3/machine-mode-over-the-network.md), [CLI 59d](../../cli/v0.2.3/reads-and-commands-over-the-network.md) (CLI [59a](../../cli/v0.2.3/paired-devices.md) through them) |
| **Blocks** | [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md), [7c](every-place-against-a-server.md), [7d](apogeekit-swift-package.md), [8a](../v0.2.5/app-builds-for-ios-and-ipados.md) |
| **Reporter** | The Apple-platforms spike and brainstorm (2026-11-07), on the user's vision |
| **Created** | 2026-11-07 |

---

## Summary

The Mac app's only way to reach the CLI is a spawned child process. This item adds a second way beside it, an authenticated connection to the person's own paired `apogee serve`, with Settings → **Connections** to pair, pick and forget servers and an indicator showing the active connection everywhere work happens. With no server paired, the app is exactly today's app.

## Description

### User story

**As** a person using Apogee on the devices they own,
**I want** the Mac app to reach either the CLI on this Mac over its pipes or my own `apogee serve` on another machine, chosen in Settings and shown wherever work happens,
**so that** my strong machine serves my Mac too, while the Mac with no server paired stays exactly what it is today.

### Background

The user's vision, 2026-11-07: Apogee is a private, self-hosted harness that feels like Claude on every device the person owns. A strong machine of theirs (a large, finely tuned model on a lot of memory) serves their own desktops and phones. The amended rule ([CLAUDE.md → The GUI is a machine-mode client](../../../assistant/darwin/CLAUDE.md)) lets the app reach the CLI one of two ways: over stdio locally, or over an authenticated connection to the person's own `apogee serve`. This item builds the second way into the Mac app, as an add-on beside the first. iPhone and iPad will only ever have the second way (8a onward).

It delivers:
- a transport seam in Infrastructure, with the child process as the default and unchanged, and a connection to a paired server as the other choice;
- Settings → **Connections**, where the person pairs with a server by its code or its QR code, picks which connection is active for the whole app or for one window, and forgets a server;
- an indicator that shows the active connection everywhere work happens.

With no server paired, the app is exactly today's app.

### Evidence

Re-measured at HEAD `4f92316d`, 2026-11-07.

**W8 — the app's only transport is the child process.**

1. **Every capability is a spawned `apogee`.** `ProcessSpawner` says so: "Every child the app starts comes through here -- a session, a read, a command, a job, a version probe" (`source/infrastructure/process/ProcessSpawner.swift:9-10`). It is Foundation's `Process` (`:50`). The only other spawn in the app is `LoginShell`'s `posix_spawn` of the person's login shell, "the way `ShellPath` asks it, and nothing else", to learn its `PATH` (`source/infrastructure/process/LoginShell.swift:4-13`, `:122-154`). Of 184 Swift files, none opens a network connection: `source/` has no `URLSession`, no `import Network` and no `socket(`.
2. **The seam is already one protocol wide.** `ChildSpawner.spawn(_:) -> RunningChild` (`source/infrastructure/process/RunningChild.swift:95-98`, "a test may hand in its own") is the one door. A session spawns through it (`business/session/MachineSession.swift:54-59`). A read or a command runs through `capture`, an extension on the same protocol (`infrastructure/process/CapturedRun.swift:17-25`; `business/reads/ReadRunner.swift:42-50`). Jobs go through it (`business/jobs/SupervisedJob.swift:37-41`), and so does discovery's `version` probe (`data/discovery/BinaryDiscovery.swift:65-69`). A remote transport that implements `RunningChild` reaches every place without any of them changing.
3. **The rest of the seam is local by construction.** `ChildCommand.executable` is "an absolute file URL" (`ChildCommand.swift:10-12`). `RunningChild` answers with a `processIdentifier` and signals to a process group (`RunningChild.swift:61-82`). An exit is either `exited` or `signalled` (`:5-10`), so there is no way to say "the connection dropped". Besides the person's override, discovery looks only in `~/.local/bin`, for `apogee`, `apogee-dev` and `apogee-test` (`BinaryDiscovery.swift:30-37`).
4. **One composition hands every place one binary.** `AppViewModel` builds every place over `ReadRunner()`, `JobStarter` and `ApogeeClient` (`presentation/app/ViewModels/AppViewModel.swift:80-176`), each of which defaults to `ProcessSpawner()` (`business/reads/ReadRunner.swift:20`, `business/jobs/SupervisedJob.swift:204-209`, `business/session/ApogeeClient.swift:44`). It then hands the library's `binaryURL` to 19 places, 18 through `connect(binary:)` and MCP through `use(binary:)` (`AppViewModel.swift:294-316`). The app has one window, on purpose: "One window: the app holds one live session, and a second window would mean a second" (`presentation/app/Views/ApogeeApp.swift:52-54`, `Window(model.title, id: "main")`). Settings has five panes and none of them is about connections (`presentation/settings/ViewModels/SettingsViewModel.swift:17-22`). The design canvas has no connection board (`lib/design/spec/html/`: no pairing or server screen among its boards).
5. **The server cannot carry the app's protocol yet.** `serve`'s route table (`lib/src/cli/source/presentation/httpserver/mux.cpp:28`, `kRoutes`, 97 rows, re-counted at HEAD) holds `/health`, seven inference rows over six paths (`/v1/chat/completions`, `/v1/completions`, `/v1/models`, `/v1/model/status`, `/v1/sessions`, `/v1/sessions/{id}`) and 89 `/v1/admin` rows over 62 paths. None of them carries the machine mode's event stream, a read's document by argv, or a permission answer. The inference plane is unauthenticated (`reference/cli/http-api.md:25`), serves one inference at a time (`:33`), and its admin plane has one per-install bearer token (`:242-249`). The CLI's 59a, 59b and 59d close that gap. This item is the app's half. The sandbox probe (`lib/src/cli/build/macos-arm64/source/apogee`, `apogee 0.2.0 (4f92316d, 2026-11-07, macos-arm64)`, `APOGEE_HOME` a scratch home) printed `channel: release` from `apogee version`: the channel is baked into every binary (`release`, `dev` or `test`; `lib/src/cli/source/main.cpp:16-26`), so a server can say which channel it is on.

## Acceptance criteria

- [ ] **Given** no server is paired, **when** the app launches, discovers, chats and reads, **then** it behaves exactly as before: no connection is attempted and no Local Network question is asked.
- [ ] The full suite is green.
- [ ] **Given** Settings → Connections is open, **when** the person looks at it, **then** it lists This Mac and any paired servers.
- [ ] **Given** a server's code and address or its QR code, **when** the person uses Pair…, **then** it shows the server's name and the pin's first groups, and stores the token and pin in the Keychain only.
- [ ] **Given** a server is active, **when** a session runs, **then** it streams, asks and answers permission cards, and is cancelled over the connection.
- [ ] **Given** a server is active, **when** the connection drops briefly, **then** the session reattaches with nothing lost.
- [ ] **Given** a server is active, **when** a place not marked "on this Mac only for now" runs its reads and commands, **then** they answer from the server's CLI.
- [ ] **Given** a connection is active, **when** the person looks at the window, **then** the window, its title and the session header say which connection is active.
- [ ] **Given** the main window is on one connection, **when** the person chooses "New Window on <server>", **then** a second window opens over that server, while the main window keeps its own.
- [ ] **Given** a server is active, **when** the server is lost, **then** the app says so and never falls back to this Mac.
- [ ] **Given** a dev-channel build, **when** it is asked to pair with a release server, **then** it will not; **and given** the release app, **when** it is asked to pair with a dev one, **then** it will not.
- [ ] Optional and said: a live pass against `apogee-dev serve` on another of the user's machines.

## Constraints

- **The anti-goal: standalone never needs a server** (the user's call, 2026-11-07; SPEC → Non-goals, No server requirement). This item fails if anything it adds makes the Mac app need a server, a network or an account to do what it does today. With no server paired:
  - discovery, the pipes, every read and every recorded fixture behave exactly as today;
  - no connection is opened and no network discovery is browsed;
  - the system's Local Network question is never asked.
- **Never-listens / serving = server** (the CLI's *Interactive turns never open a listening socket*; GUI SPEC → No HTTP path for local work, clarified 2026-11-07). The app makes outbound connections only and never listens. It never connects to a server on its own machine: a Mac with Apogee installed keeps its pipes. The remote door is the person's own `apogee serve` on another machine.
- **One protocol, two transports** (the amended machine-mode-client rule). A line arriving over the connection is decoded by the same `EventDecoder`, with the same tolerance for unknown events, fields and lines and the same honesty, as a line arriving over a pipe. Nothing above the seam can tell which transport carried it, except through the one value it is handed: which connection is active.
- **Additive protocol** (the CLI's [ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The app tolerates a server older or newer than itself the way it tolerates a binary. A server without 59b's or 59d's capability gets a refusal in the server's words, never a guess.
- **Parity** (the CLI's [ADR 0002](../../../adrs/cli/mode-parity.md), the GUI's [ADR 0003](../../../adrs/gui/parity.md)). A place behaves the same over either transport, because each transport carries the same CLI. A place that cannot yet work against a server says so in words (7c finishes them) and never silently runs locally instead.
- **Secrets are unleakable** (the CLI's *Secrets are unleakable by construction*; the GUI rule *Secrets never pass through the GUI*). The provider-key rule does not change.
  - The one new secret is the device's own token from 59a's pairing, kept with the server's pin. Both are held in the system Keychain, for this device only, and never in `UserDefaults`, a log, a fixture, the protocol stream, a URL or the screen. The token travels only as 59a's `Authorization: Bearer` header, over TLS held to that pin.
  - No provider key travels over the connection. A key is added at the server itself (`PUT /v1/admin/auth` is "Served to **loopback peers only**", `reference/cli/http-api.md:1444-1449`).
- **The layers** (GUI [ADR 0001](../../../adrs/gui/layer-enforcement.md), [ADR 0002](../../../adrs/darwin/presentation-mvvm.md)):
  - The connection and the Keychain live in Infrastructure, which may import the system's non-UI frameworks.
  - The paired servers' records live in Data, and pairing, routing and the active choice live in Business.
  - Presentation keeps its MVVM shape and is handed one value. No UI framework is used below Presentation, which 8a relies on.
- **The dev channel** ([CLAUDE.md → Development runs on the dev channel](../../../assistant/darwin/CLAUDE.md#development-runs-on-the-dev-channel); M18 [`app-dev-channel.md`](../macos/maintenance/app-dev-channel.md)). A development build's paired servers, credentials and active choice are its own, kept under its own bundle identifier's defaults and Keychain items, which M18 gives it. A dev-channel app connects only to a dev-channel server (`apogee-dev serve`), and the release app only to a release one.

## Technical approach

Planned paths, all under `lib/src/darwin/` (the design spec excepted, at its own path).

| File | Change |
|---|---|
| `source/infrastructure/connection/` (new, `InfrastructureConnection`): `ServerConnection` | One authenticated, encrypted channel to a paired server. A session is 59b's WebSocket at `GET /v1/machine`; a read or a command is what 59d defines. Both run over TLS whose certificate is held to the pin 59a's pairing kept, with the device token as the bearer header. |
| `source/infrastructure/connection/`: `RemoteChild: RunningChild` | A session's or a read's run carried over that channel. `lines` and `write` are the run's stream; each line the app writes carries 59b's `line_id`, so a resend after a reconnect is never acted on twice. `interrupt` is the protocol's `cancel` line (59b) or 59d's interrupt for a read; `closeInput` is 59b's `net_end`; `terminate` and `kill` are 59d's stop for a read. There is no process identifier and no group, and `stderrTail` is what the server says. A drop is not an end: 59b keeps the session running on the server, so `RemoteChild` reconnects and attaches with `net_connect` and its cursor (`after`: the last `seq` it holds). Only when it cannot reconnect does the run end `disconnected`. |
| `source/infrastructure/connection/`: `RemoteSpawner: ChildSpawner` | Ignores `executable` and refuses `environment`'s `PATH` games. Treats `workingDirectory` as 59b's `cwd`, a folder under one that `serve --workspace` names on the server. A `ConnectionTransport` protocol lets a test hand in a scripted fake with no socket. |
| `source/infrastructure/process/RunningChild.swift` | `ChildExit.Reason` gains `disconnected`, so a dropped connection is never drawn as a crash or a success. `processIdentifier` becomes optional; `SupervisedJob` already holds one as `Int32?` (`SupervisedJob.swift:20`). |
| `source/infrastructure/keychain/` (new, `InfrastructureKeychain`) | The device token and the server's pin, stored, read and deleted through the Security framework, for this device only. |
| `source/data/connections/` (new, `DataConnections`): `PairedServer` | Name, addresses and port, its channel and when it was paired, with no secret in it. |
| `source/data/connections/`: `ActiveConnection` | This Mac, or a server, kept in `Preferences` app-wide. |
| `source/data/connections/`: `PairingCode` | 59a's two forms decoded: the typed `XXXXX-XXXXX` code with the address and port the person enters, and the QR code's JSON payload (`{"apogee_pair":1,"addresses":[…],"port":N,"pin":"sha256/…","secret":"…"}`). iPhone and iPad reuse it. |
| `source/business/connections/` (new, `BusinessConnections`): `Connections` | Pair (59a's exchange, its proof bound to the pin the device saw), test, forget, choose. |
| `source/business/connections/`: `RoutingSpawner: ChildSpawner` | A file URL goes to `ProcessSpawner`, and an `apogee-server://<id>` handle goes to that server's `RemoteSpawner`. A handle that names no paired server fails closed, `couldNotStart`. |
| `source/business/connections/`: `ServerDiscovery` | The server's `version` and `__machine-schema` over 59d, with the channel check, standing in for `BinaryDiscovery` when the active connection is a server. |
| `source/presentation/app/`: `AppViewModel` | Composes the places over `RoutingSpawner` and hands them the active connection's handle through the existing `connect(binary:)`/`use(binary:)` fan-out (`AppViewModel.swift:294-316`). |
| `source/presentation/app/`: "New Window on <server>" | Opens a second composition over that server, which retires `ApogeeApp`'s one-window rule (`ApogeeApp.swift:52-54`) for a server window only. `Termination` (`presentation/app/ViewModels/Termination.swift:17`), which `AppDelegate` asks at quit, waits on every window's local children. A remote session is left with 59b's `net_leave` and runs on at the server. |
| `source/presentation/settings/` | `Pane.connections` with `Views/ConnectionsPane` (the paired list, this Mac first, Pair…, Test, Forget, Use) and `Views/PairSheet` (the code and address, or a QR code; the server's name and the pin's first groups shown, to compare with the server's start line, before the person agrees). |
| `source/presentation/session/` | A question answered on another device closes its card on 59b's `net_answered`, naming that device. A launch that 59b refuses over the network (`bypass`, a second live session before 59c) is drawn in the server's words. |
| `source/presentation/shell/` | The active-connection indicator in the window's toolbar, the window title (`Apogee — <server>`), the unreachable state, and the place-not-yet-remote state that 7c retires. |
| `source/presentation/shell/ViewModels/Parity.swift` | The new commands from 59a, 59b and 59d are classified (ADR 0003). `serve` stays carved out (`Parity.swift:83`), and 7c adds the remote column. |
| `Apogee.xcodeproj` | The new targets, and `NSLocalNetworkUsageDescription`, asked only at the first connection. |
| `lib/design/spec` | The Connections pane, the pair sheet, the indicator and the unreachable state drawn first, per the design's law (`spec/CLAUDE.md` → What a change touches). |

**Reference.**
- The CLI's half, which this item builds against:
  - [59a](../../cli/v0.2.3/paired-devices.md): pairing and the device credential.
  - [59b](../../cli/v0.2.3/machine-mode-over-the-network.md): the session stream.
  - [59d](../../cli/v0.2.3/reads-and-commands-over-the-network.md): reads and commands.
- Adjacent CLI work:
  - [59c](../../cli/v0.2.3/many-sessions-at-once.md): until it lands, a server runs one live network session at a time, and a second new one is refused by name while attaching to the live one works (59b's default).
  - [59e](../../cli/v0.2.3/tasks-outlive-the-connection.md): tasks that outlive the connection.
- The siblings that build on this seam:
  - [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md): the Mac offers its own files and shell.
  - [7c](every-place-against-a-server.md): every place works against a server.
  - [8a](../v0.2.5/app-builds-for-ios-and-ipados.md): the same code on iPhone and iPad, where this transport is the only one.
- Precedents:
  - The discovery and override (1b, M6), which a server's `version` check mirrors.
  - The capability read at launch (4k), which a server's schema read mirrors.
  - M18's dev channel.

## Testing

All hermetic: scripted fakes, recorded documents, no binary, no socket, no window.

- **`tests/infrastructure/connection/`** — `RemoteChild` over a scripted transport, covering lines, write, interrupt, stop, a drop and reattach after its cursor with `line_id` resends, a wrong pin refused, and a disconnect.
- **`tests/data/connections/`** — the new `DataConnections` module.
- **`tests/business/connections/`** — the routing table, the fail-closed handle, the channel check, the own-machine refusal, and no transport built while nothing is paired.
- **`tests/presentation/settings/ConnectionsTests.swift`** — the Connections pane.
- **`tests/presentation/app/`** — the fan-out on a switch, and two compositions.
- **The recorded fixture.** `tests/data/protocol/Fixtures/mock-actor.jsonl`, replayed over the fake connection, must decode exactly as over the fake pipe.
- **No-server cases.** With no server paired, a test fails if any transport is constructed. The existing discovery, session and read suites pass unchanged. The recorded fixture decodes the same over both fake transports.
- **The routing table is pinned,** including a handle for an unknown server failing closed and never running a local binary.
- **Pairing refusals are pinned:** own machine and the wrong channel.
- **The Keychain fake** shows the token never reaches `Preferences`, a log line, a URL or a document.
- **A drop mid-turn** reattaches after its cursor and decodes the replay with no event lost or doubled.
- **Layering and parity.** The layering check stays green with the new targets (no UI framework below Presentation, no upward edge), and the parity suite classifies every new command.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Who owns the wire shape, the encryption, the pairing exchange and the device token? | `(consumed decision)` | 59a, 59b and 59d; this item implements what they state. Where the device keeps its token and pin, and how it checks the pin, is this item's (59a's open calls hand it here). |
| Where does a remote session work (server tools by default, the Mac's own files offered)? | `(consumed decision)` | The user's call C, built by 7b. Here, a remote session works in a server folder only. |
| How is adding a provider key handled while a server is active? | `(consumed decision)` | 7c's, which classifies it as not remote ("a key is added at the server itself"). Until 7c, Backends & keys says "on this Mac only for now" while a server is active. |
| How does the Mac pair with a server? | `[default]` | The Mac pairs by typing the code with the server's address, comparing the pin's first groups with the server's start line, or by reading the QR code from an image (a screenshot, a dropped file, or Continuity Camera) through the Vision framework in Infrastructure. Both forms decode through `PairingCode`, which the phones' live camera scan (8b, 9a) reuses. |
| Does M18 ship before this item is built? | `[default]` | Yes: M18 ships before this item is built. It is ungated in Maintenance, and without it a development build's paired servers and Keychain items would be the installed app's. If it has not shipped when 7a is taken, the builder takes M18 first. |
| Where is the active connection chosen, and how does a second window keep its own? | `[default]` | The active connection is chosen app-wide in Settings and from the indicator. "New Window on <server>" adds a window over another connection, keeping its route and open session in memory rather than in the app-wide preferences, so the main window's are never overwritten. |
| What happens when the connection is lost? | `[default]` | A lost connection never falls back to this Mac, and never moves to another server. `RemoteChild` reconnects and reattaches after its cursor first (59b keeps the session running). If that fails, the window says the server is unreachable and offers Try again and Use this Mac, and a run in flight ends `disconnected` while its session runs on at the server, to be reattached by Try again. 59e and 7c keep autonomous tasks alive past it. |
| How is `devices` (59a) classified in the parity table? | `[default]` | Carved out beside `serve` (`Parity.swift:83`): a pairing code is minted only on the server's own terminal (59a), and the app never serves. |
| What happens to a server address that is loopback, or this Mac's own install? | `[default]` | A server address that is loopback, or whose identity is this Mac's own install, is refused at pairing, with the SPEC's reason. |
| Which server channels may each app pair with? | `[default]` | A dev-channel app pairs only with a server that reports `channel: dev`, and the release app only with one that reports `channel: release`. Any other channel, `test` included, is refused, and the refusal names the channel. |
| What does a place whose work is this Mac's own do while a server is active? | `[default]` | Until 7c, a place whose work is this Mac's own (media folders, save and open panels, the Library's spec-file editor, the monitor's memory by process id, adding a key) says "on this Mac only for now" while a server is active. |
| How are the token and pin kept, and what if a build cannot read them back? | `[default]` | The token and pin are Keychain items for this device only, never synced. If an ad-hoc-signed build cannot read them back after an update without a system prompt, the build measures that and says so in its record, and pairing again is the remedy. |

No **[user]** call blocks this item.

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item: (1) Apogee is a private, self-hosted harness for the person's own devices; the server admits the person and their devices only, with no accounts, billing or metering. (2) Remote mode is an add-on and never the only way the app runs; the Mac app over pipes stays complete and the default. (3) The SPEC amendment is approved and written: the app reaches the CLI over stdio, or over an authenticated connection to the person's own `apogee serve`. (4) iPhone and iPad are server clients only; the Mac stays standalone. (5) Where the work happens (call C): server tools by default, and a connected desktop offers its own files and shell; when the Mac points at a server and is asked to change a project on the Mac, the Mac's files are the ones touched (7b builds that). (6) Successive versions, with v0.2.5 the target where macOS, iPadOS and iOS (the iPhone and the foldable iPhone) fully work; Windows and Android follow v0.2.5 (a recorded default). | the user |
| 2026-11-07 | In v0.2.4 (track 7): it needs the server to speak the machine mode (59b) and the reads (59d), both in v0.2.3. It comes before 8a, which needs this seam to have anything to drive on iPhone and iPad. | the spec |
| 2026-11-07 | The seam is `ChildSpawner`, not a new protocol above it. Every place already reaches the CLI through `spawn`/`capture` (W8.2), so a remote `RunningChild` keeps Business and Presentation unchanged in shape. Their one new fact is that a run can end `disconnected`. | the spec |

## Out of scope

- The Mac's files and shell offered to a server session (7b).
- The places this item marks "on this Mac only for now" (7c).
- Tasks that outlive the connection (CLI 59e, 7c).
- iPhone and iPad (8a onward).
- Finding servers on the network automatically (59a leaves Bonjour out too).
- Windows and Android, which follow v0.2.5.
- Any account, billing or metering (the user's call: paid features are a someday).

## Links

- **Blocked by:** [CLI 59b](../../cli/v0.2.3/machine-mode-over-the-network.md), [CLI 59d](../../cli/v0.2.3/reads-and-commands-over-the-network.md) (CLI [59a](../../cli/v0.2.3/paired-devices.md) through them) · **Blocks:** [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md), [7c](every-place-against-a-server.md), [7d](apogeekit-swift-package.md), [8a](../v0.2.5/app-builds-for-ios-and-ipados.md)
- **Relates to:** CLI [59c](../../cli/v0.2.3/many-sessions-at-once.md) and [59e](../../cli/v0.2.3/tasks-outlive-the-connection.md); M18 [`app-dev-channel.md`](../macos/maintenance/app-dev-channel.md); 8b and 9a (the phones' live camera scan reuses `PairingCode`); the precedents 1b, M6 and 4k; the CLI's [ADR 0006](../../../adrs/cli/backwards-compatibility.md) and [ADR 0002](../../../adrs/cli/mode-parity.md); the GUI's [ADR 0001](../../../adrs/gui/layer-enforcement.md) and [ADR 0003](../../../adrs/gui/parity.md); darwin's [ADR 0002](../../../adrs/darwin/presentation-mvvm.md); [CLAUDE.md → The GUI is a machine-mode client](../../../assistant/darwin/CLAUDE.md)
