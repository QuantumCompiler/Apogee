# APP-7d · ApogeeKit: the client core as a Swift package

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Apple apps |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | ApogeeKit: the lower layers (Infrastructure, Data, Business) as a root `Package.swift`, and the layering check |
| **Labels** | `swift-package` `app-integration` `layering` `architecture` |
| **Epic** | Track 7 · apps as clients (7d–7e) |
| **Blocked by** | [7a](app-connection-local-or-server.md) |
| **Blocks** | [7e](maintaining-apogeekit.md), [CLI 60d](../../cli/v0.2.4/building-an-app-on-apogee.md) |
| **Reporter** | The user's goal and the apps-as-clients spike (2026-11-14) |
| **Created** | 2026-11-14 |

---

## Summary

There is no package another app can depend on: the Mac app's client core exists only as Xcode framework targets, and the layering check forbids a Swift package outright. This item carves the Mac app's lower layers and [7a](app-connection-local-or-server.md)'s connection into **ApogeeKit**, a SwiftPM package other apps embed, moves the Mac app onto it as its one copy, teaches the layering check to read both maps, and adds a sample command-line client that builds outside the app.

## Description

### User story

**As** an app developer using Apogee as my app's AI back end (the first being Taylor'd Portfolio),
**I want** a Swift package, factored from the Mac app's client core, that carries Apogee's protocol, transports, sessions, reads and jobs,
**so that** my app can swap its CLI seam for Apogee's without copying Apogee's decoder, and my app and the Mac app run one implementation, tested by Apogee.

### Background

The user's goal, 2026-11-14: other apps use Apogee as their AI back end, the first being Taylor'd Portfolio (QuantumCompiler/Taylor-d-Portfolio), the user's SwiftUI job hunter for macOS. The user's call the same day: Apogee ships a **Swift package other apps embed**, factored from the Mac app's client core, and **the Mac app depends on it too**, so there is one implementation, tested by Apogee ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4, *Revised 2026-11-14*).

Taylor'd Portfolio's engine already drives a CLI from its Infrastructure layer: `ClaudeProcessClient` runs `claude -p <prompt> --output-format json` behind a `TextGenerating` port (read from its repo, 2026-11-14). A package is what lets such an app swap that seam for Apogee's without copying Apogee's decoder. This item carves the Mac app's lower layers into that package, **ApogeeKit**, and moves the Mac app onto it.

It delivers:

- **ApogeeKit**, a SwiftPM package of the lower layers and [7a](app-connection-local-or-server.md)'s connection, building and testing on its own with `swift build` and `swift test`;
- **the Mac app on it**: the moved modules' framework targets and test bundles leave `Apogee.xcodeproj`, and the app links the package's products, so there is one copy of every file;
- **the layering check and the module map reading both**, holding the package's modules to the same four-layer law;
- **a sample client**, a small command-line Swift program in its own package that depends on ApogeeKit and proves it builds outside the app;
- **the typed calls** an embedder needs for the CLI's new app-facing features (pairing, capabilities, structured output, an app's own tools), each built where its CLI half has shipped.

### Evidence

**W6 — there is no package to depend on.** Re-measured 2026-11-14 at HEAD `231fb049`. Paths are under `lib/src/darwin/` unless they say otherwise.

1. **The client core exists only as Xcode framework targets.** There is no `Package.swift` anywhere in the repository (outside build output), and `Apogee.xcodeproj/project.pbxproj` holds 56 native targets and no package reference. That is a recorded user's call, 2026-10-20: "An Xcode project, not a Swift package", superseding the SwiftPM default 1a was groomed with ([MILESTONES.md](../../../assistant/darwin/MILESTONES.md):43). The contributor guide leaves the question open: "What is shared between the three apps (one machine-mode client core, or three held to the same layer contract) stays open until a second app exists" ([CLAUDE.md](../../../assistant/darwin/CLAUDE.md):69). A second consumer now exists.
2. **The layering check forbids a package outright.** Its rules say "nothing in the project reaches outside it -- no Swift package" (`tests/architecture/layering/LayeringCheck.swift:17-18`). `outside()` (`:160-178`) fails any package reference (`:162-164`) and any dependency on a package product (`:173-175`). The guide's enforcement sentence says the same ([CLAUDE.md](../../../assistant/darwin/CLAUDE.md):54). So the app cannot consume a package, even its own, until the check learns to read one.
3. **The lower layers are already a clean client core.** 8 modules, 56 files, 8,318 lines: `infrastructure/process` (9 files), `data/protocol` (7), `data/discovery` (2), `data/preferences` (1), `business/jobs` (5), `business/media` (7), `business/reads` (19) and `business/session` (6). They import Foundation, Synchronization, Darwin (in `ProcessMemory.swift` and `LoginShell.swift` only) and each other. None imports SwiftUI, AppKit or UIKit. Only two files start a process: `ProcessSpawner.swift` (`Process()`, `:50`) and `LoginShell.swift` (`posix_spawn` of the login shell).
4. **They build and test as a package with the tree's layout kept.** The probe copied the eight modules' `source/` and `tests/` folders into the session's scratchpad, with a `Package.swift` whose targets point at the same `source/<layer>/<package>` and `tests/<layer>/<package>` folders and keep their names. Nothing in the tree was touched. Under Swift 6.4 (Xcode 27.1), `swift build --build-tests` finished warning-free in Swift 6 mode in 9.3 s. `swift test --skip RealBinaryTests` ran 299 tests in 8 bundles, and 293 passed. All 6 failures are in two reads test files that reach **up** into the Tasks screen's fixtures: `tests/business/reads/TaskReadsTests.swift:10-11` and `ListRowsAndConfigVerbsTests.swift:12-14` read `../../presentation/tasks/Fixtures/`. A first attempt, with the files flattened into SwiftPM's default `Sources/` and `Tests/` layout, failed far more. The tests find their fixtures by `#filePath` relative to the repository's `tests/<layer>/<package>/` (for example `tests/business/media/ProjectsTests.swift:82`, which reaches `business/reads` fixtures too), so the package must keep the tree.
5. **Two things tie the core to this app.**
   - `BusinessSession` imports `DataPreferences` for one value. `ApogeeClient` holds the app's `Preferences` (`business/session/ApogeeClient.swift:3`, `:23`) only to read `binaryOverride` when it locates the binary (`:68`). `Preferences` is the app's own settings over `UserDefaults` (`data/preferences/Preferences.swift:4`, `:134-138`: the route, the sidebar, the open session, the media folders, the theme). That is app-specific, so it stays out of the package, and this edge has to go.
   - The convenience initializers name the Mac: `ProcessSpawner()` and `homeDirectoryForCurrentUser` at `business/reads/ReadRunner.swift:20`, `business/jobs/SupervisedJob.swift:212`, `:217`, `:235`, `business/session/ApogeeClient.swift:44-45` and `data/discovery/BinaryDiscovery.swift:21`. GUI [8a](../v0.2.5/app-builds-for-ios-and-ipados.md)'s probe found that these sites and four `infrastructure/process` files are exactly what keeps the lower layers off iOS (its W9.2).
6. **The protocol is already client-agnostic.** `ClientIdentity`'s name defaults to `"Apogee"` (`ApogeeClient.swift:13`), so an embedder that forgot to name itself would be logged as Apogee. Probed on the branch binary (`apogee 0.2.0 (b37b0a9f)`) in a scratch `APOGEE_HOME` under the scratchpad with a `mock` backend: a `hello` line naming `kit-sample 0.0.1`, then a `user` line, on `chat -m mock --output-format stream-json --input-format stream-json`, streamed the session event and `{"finish_reason":"stop","model":"mock","text":"mock response",…,"type":"result"}`. The CLI's log recorded `INFO [chat] hello from kit-sample 0.0.1, wants []`. No CLI change is needed for another app to drive the stdio door.
7. **Two values are process-wide.** `ChildRegistry.app` (`business/jobs/ChildRegistry.swift:62`) is what the monitor shows and what quit stops. `ShellPath.app` (`infrastructure/process/ShellPath.swift:93`) is the login-shell `PATH` asked once at launch. A static library linked into more than one of the app's frameworks would give each framework its own copy of both.

## Acceptance criteria

- [ ] **Given** the repository root, **when** `swift build` and `swift test` run there, **then** ApogeeKit builds and passes alone, warning-free in Swift 6; and the iOS Simulator build of the package succeeds with the process files compiled out.
- [ ] **Given** the move is done, **when** `Apogee.xcodeproj` is read, **then** it holds no copy of the package's modules: its framework targets are `DataPreferences`, the Presentation modules and 7b's host, and it references one package, ApogeeKit, locally. The app builds, signs, tests and packages as before, with the suite's count not lower than before the move.
- [ ] **Given** the layering check reads both maps, **when** a violation is planted (a Presentation or `DataPreferences` target in the package, an upward edge, a UI import, a remote package or a package dependency, `unsafeFlags`, a folder built twice, a listening call), **then** each fails naming its file or target.
- [ ] **Given** `lib/src/darwin/samples/apogee-kit-sample`, **when** it builds, **then** it builds against the kit by path, importing only its modules and Foundation; **and when** it is run with `--binary` set to the branch CLI and `--backend mock` in a throwaway `APOGEE_HOME`, **then** it prints the version line, the `models list` rows and the streamed `mock response`, and the CLI's log says `hello from apogee-kit-sample`.
- [ ] **Given** any of 59f, 59g, 59h and 60c whose CLI half has shipped, **when** its typed call's hermetic tests run, **then** they pass; **and when** the call is made against an older binary, **then** it says the date it needs.
- [ ] `reference/darwin/apogeekit.md` documents the products, the modules, the platforms, `ApogeeKit.schema`, the git URL and the local path. CLAUDE.md, DEVELOPER.md, the layer cards, `changed.sh` and MILESTONES are updated.
- [ ] Optional live pass, said when skipped: the user adds ApogeeKit by git URL at this branch to a scratch Swift app, and drives `apogee-dev` over stdio with no server running.

## Constraints

- **Remote use is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network, a pairing or an account. The CLI alone and the Mac app over pipes stay complete and the default (the user's call, 2026-11-07; GUI SPEC → Non-goals, No server requirement; [SPEC.md](../../../assistant/cli/SPEC.md):34). The package's local door is the child process over stdio, exactly today's. Its remote door is 7a's, and it is never built while nothing is paired. The sample client works with no server, no network and nothing paired.
- **Never-listens / serving = server** (the CLI's *Interactive turns never open a listening socket*; GUI SPEC → No HTTP path for local work). The package makes outbound connections only. No source in it may call `listen`, `accept` or `bind`, or use `NWListener`. An embedding app is held to this by the package, not by its own care.
- **Secrets are unleakable by construction** (the CLI's rule; the GUI rule *Secrets never pass through the GUI*). The package carries no provider key: the protocol has no key field by construction. Its one secret is a pairing credential (7a's device token, or [59f](../../cli/v0.2.3/paired-apps-with-scopes.md)'s `apa_` app token) and the server's pin. Both are kept in the system Keychain under the embedding app's own items. No public type holds the token, so an embedder's code cannot log it, put it in a URL or show it. It travels only as the `Authorization: Bearer` header over TLS held to the pin.
- **Additive protocol** (the CLI's [ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The package decodes as the app does today: unknown events, fields and lines are tolerated, and a feature a binary lacks is said, never guessed. The package's public interface grows by addition. The written promise to integrators is [60d](../../cli/v0.2.4/building-an-app-on-apogee.md)'s, and its enforcement is [7e](maintaining-apogeekit.md)'s.
- **Parity** (the CLI's [ADR 0002](../../../adrs/cli/mode-parity.md); the GUI's [ADR 0003](../../../adrs/gui/parity.md)). The package adds no capability: every capability stays the CLI's, reached by its words. The parity table stays the app's (`presentation/shell/ViewModels/Parity.swift`) and gains no row. An embedding app is held to its scope (59f), not to parity.
- **One client core** (the GUI's machine-mode-client rule, [CLAUDE.md](../../../assistant/darwin/CLAUDE.md)). After this item, the package is the only implementation of the protocol, the transports, the sessions, the reads and the jobs. A module lives in the package or in the project, never both.
- **The layers** (GUI [ADR 0001](../../../adrs/gui/layer-enforcement.md); darwin [ADR 0002](../../../adrs/darwin/presentation-mvvm.md) and [ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md)). The package holds Infrastructure, Data and Business only. It never holds `DataPreferences` (the app's own settings) or anything from Presentation. Its targets keep the `<Layer><Package>` names and the `source/<layer>/<package>` folders, so every `import` still says which layer it reaches, and every edge points down. The rule that a public default argument names only its own module (CLAUDE.md → Code Style, 2026-10-20) holds across the package boundary as it does across frameworks.
- **The dev channel** ([CLAUDE.md → Development runs on the dev channel](../../../assistant/darwin/CLAUDE.md#development-runs-on-the-dev-channel); M18 [Milestone A](../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel)). The package's hermetic tests start no binary. Any live run of the sample uses `apogee-dev` over a throwaway `APOGEE_HOME`, or the branch build's binary, and never the release `apogee` or `~/.apogee`.
- **Swift 6, strict concurrency, warning-free** (CLAUDE.md → Code Style), under both `swift build` and `xcodebuild`.

## Technical approach

| File | Change |
|---|---|
| `Package.swift` (new, at the repository root; see the open questions for why the root) | The ApogeeKit manifest: targets pathed into the folders their framework targets build today, test targets, one dynamic product per module plus the umbrella `ApogeeKit`, no dependencies (below) |
| `lib/src/darwin/Apogee.xcodeproj` | The moved modules' framework targets and test bundles are removed. It gains one `XCLocalSwiftPackageReference` to the repository root (`relativePath = ../../..`). Each remaining target depends on the package products it imports, and the app embeds them as it embeds frameworks today |
| `lib/src/darwin/source/business/session/ApogeeClient.swift` | `Preferences` leaves: the binary override is handed in as `@Sendable () -> URL?`, read at each `locateBinary()`; `import DataPreferences` goes; `ClientIdentity.init(name:version:)` loses its `"Apogee"` default (`:13`) |
| `lib/src/darwin/source/presentation/app/ViewModels/AppViewModel.swift` | `AppViewModel` (`presentation/app/ViewModels/AppViewModel.swift:85`) passes `{ preferences.binaryOverride }` |
| `lib/src/darwin/source/infrastructure/process/` | `ProcessSpawner`, `LoginShell` and `ProcessMemory` become `+macOS` files, unchanged inside; `ShellPath`'s run of the login shell moves to `ShellPath+macOS.swift`; `ChildCommand`, `RunningChild`, `ChildSpawner`, `CapturedRun`, `LineFramer` and `BoundedTail` stay everywhere |
| The convenience-initializer sites in W6.5 (`ReadRunner.swift`, `SupervisedJob.swift`, `ApogeeClient.swift`, `BinaryDiscovery.swift`) | The convenience initializers move to `+macOS` overloads; the injected initializers stay everywhere |
| `lib/src/darwin/tests/business/reads/Fixtures/` | Gains the task fixtures that `TaskReadsTests.swift` and `ListRowsAndConfigVerbsTests.swift` read (`task-status-uses.json` and the `task-list-*` rows), moved down from `tests/presentation/tasks/Fixtures/` |
| `lib/src/darwin/tests/presentation/tasks/` | The Tasks screen's tests read those fixtures in `tests/business/reads/Fixtures/`, a downward reach like their code's imports |
| `DataProtocol`, `BusinessConnections`, `BusinessSession`, `BusinessReads` | The typed calls for 59f, 59g, 59h and 60c, and `ApogeeKit.schema` (below) |
| `lib/src/darwin/samples/apogee-kit-sample/` (new) | The sample client: its own `Package.swift` depending on the kit by path, an executable target `apogee-kit-sample`, and 60d's worked example as a second target (below) |
| `lib/src/darwin/tests/architecture/layering/` | `ModuleMap` reads the package as well as the project, and `LayeringCheck` holds both to one law (Testing). `SourceTree` reads `Package.swift` through `swift package dump-package` (SwiftPM's own JSON of the manifest; the package has no dependencies, so the dump touches no network). Planted miniatures pin each new rule as JSON beside today's (`LayeringTests.swift`) |
| `lib/scripts/changed.sh` | `gui-darwin` also declares `Package.swift`, so a change to the manifest builds the app |
| `lib/scripts/cicd.sh --app darwin` | `--test` runs the shared scheme `Apogee`, whose testables now include the package's test targets, so the suite stays one count in one result bundle; `--build` also runs `swift build` at the root, the package alone, and `swift build` and `swift test` in the sample's folder |
| The layer cards (`source/infrastructure/CLAUDE.md`, `source/data/CLAUDE.md`, `source/business/CLAUDE.md`) | **Enforced by** names `Package.swift` beside `Apogee.xcodeproj`. **Changing this layer** says a module below Presentation is a package target, and `DataPreferences` the exception, said in prose outside the backticked **Modules** line, which the check reads as module names |
| [CLAUDE.md](../../../assistant/darwin/CLAUDE.md) | Stack & environment's **Build** (the package beside the project), the Codebase Map, the enforcement sentence at `:54` ("fails any package dependency" becomes "fails any package but ApogeeKit"), and `:69`'s open question, now answered for the Apple apps |
| [DEVELOPER.md](../../../assistant/darwin/DEVELOPER.md) | The package's layout, `swift build` and `swift test` at the root, and the sample |
| `lib/documentation/reference/darwin/apogeekit.md` (new) | The package's reference for embedders: its products and modules, platforms, `ApogeeKit.schema`, both doors and the typed calls, and consuming it by git URL or by path. 60d's guide links it |
| [MILESTONES.md](../../../assistant/darwin/MILESTONES.md) | The record, naming 2026-10-20's "An Xcode project, not a Swift package" (`:43`) as revised for the lower layers only |
| The GUI SPEC | Its Goal line gains a dated sentence: the app's client core is ApogeeKit, which other apps embed |

The docs above land in the same change. The darwin ADR and the guide's ApogeeKit section are [7e](maintaining-apogeekit.md)'s.

**`Package.swift`.**

- `name: "ApogeeKit"`, tools version 6.2, `swiftLanguageModes: [.v6]`, platforms macOS 27 and iOS 27.
- Targets, each pathed into the folder its framework target builds today and keeping its name:
  - `InfrastructureProcess` (`lib/src/darwin/source/infrastructure/process`): its process-starting files compile on macOS only (below);
  - 7a's `InfrastructureConnection` and `InfrastructureKeychain`, on every platform;
  - `DataProtocol`, `DataDiscovery` and 7a's `DataConnections`;
  - `BusinessJobs`, `BusinessReads`, `BusinessMedia`, `BusinessSession` and 7a's `BusinessConnections`.
- Test targets pathed into `lib/src/darwin/tests/<layer>/<package>`, each named `<Target>Tests`, excluding its `Fixtures/` folder (read by `#filePath`, as today).
- Products: one dynamic library product per module, which the Mac app links, and one umbrella library product, `ApogeeKit`, holding them all, for embedders.
- `dependencies: []`. There are no plugins, no binary targets, no `unsafeFlags` and no `linkerSettings`.

**What stays in `Apogee.xcodeproj`:** `DataPreferences`, the 19 Presentation modules, 7b's device-tool host (`source/business/device/`, [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md): the Mac offering its own files and shell, which is the Mac app's affordance), and the application target.

**`ApogeeClient` and the binary override.** The override is handed in as `@Sendable () -> URL?`, read at each `locateBinary()`, so a change in Settings still applies to the next locate, as `preferences.binaryOverride` does now. With `import DataPreferences` gone goes the package's last edge into app-specific code. With `ClientIdentity`'s default gone, the app names itself, and so must every embedder.

**Platform files.** The Mac-only sites become platform files, by 8a's rule (`<Name>+macOS.swift`, wholly inside `#if os(macOS)`). In `infrastructure/process/`, `ChildCommand`, `RunningChild`, `ChildSpawner`, `CapturedRun`, `LineFramer` and `BoundedTail` stay everywhere because 7a's `RemoteSpawner` implements them. No package test reads a file outside the package's test folders once the task fixtures move down.

**The typed calls**, each over its CLI half's wire shape, built in this item for each half that has shipped when it is taken (open questions):

- **Pairing an app** (59f): `BusinessConnections`' pairing (7a's exchange, its proof bound to the pin) takes an app's code as it takes a device's. The token and pin are stored under the embedder's Keychain service, and the embedder keeps a `PairedServer` handle with no secret in it.
- **Capabilities** ([59g](../../cli/v0.2.3/capability-discovery.md)): `DataProtocol` gains `CapabilityDocument`, every list and entry decoded tolerantly. It is read through `ReadRunner` as `__capabilities` locally, or 59g's route over the connection.
- **Structured output** ([59h](../../cli/v0.2.3/structured-output-on-every-door.md)): `DriverLine.user` gains its optional `schema`, and `ProtocolEvent`'s `result` keeps the verdict (`json`, `conforms`, `attempts`, `errors`). A one-shot call, `complete(_:instructions:backend:schema:as:)`, runs `complete --json-schema … --output-format json` and decodes into the caller's `Decodable`. It throws a non-conforming error that carries the validator's errors and the raw text, and never a guess.
- **An app's own tools** ([60c](../../cli/v0.2.4/app-tools-on-every-door.md)): a `ClientTool` protocol (60a's definition, plus `call(_ arguments: JSONValue) async throws -> JSONValue`). A session handed tools sends the `tools` line after `hello`, answers each `tool_request` with a `tool_result`, and cancels the call's work on `tool_cancel`. Names are checked by 60c's rule before anything is sent.
- `ApogeeKit.schema`: the machine-schema date the package speaks (60d's promise, point 3). A call whose feature needs a later date than the binary's `__machine-schema` announces (4k's `MachineSchema`) fails as unavailable, naming the date it needs.

**The sample**, `lib/src/darwin/samples/apogee-kit-sample/` (new): its own `Package.swift` depending on the kit by path (`.package(path: "../../../../..")`) and on the umbrella product only. It has an executable target, `apogee-kit-sample`, of a page or two:

- it locates the binary (`--binary PATH`, else discovery) and prints its version line;
- it runs one read (`models list`) through `ReadRunner` and prints the rows;
- it drives one chat turn on `--backend NAME` through `ApogeeClient` and `MachineSession`, under its own `ClientIdentity`, and prints the streamed answer.

It imports only the kit's modules and Foundation. [60d](../../cli/v0.2.4/building-an-app-on-apogee.md)'s worked example (`JobMatchEngine.swift` and its test) lands in this same sample package as a second target, so both consume the public products only.

**Reference.**

- [7a](app-connection-local-or-server.md), this item's gate: the connection modules the package carries, and the Keychain store an embedder's credential uses.
- The CLI's app-facing items, whose typed calls this package carries:
  - [59f](../../cli/v0.2.3/paired-apps-with-scopes.md), paired apps and their scopes;
  - [59g](../../cli/v0.2.3/capability-discovery.md), capability discovery;
  - [59h](../../cli/v0.2.3/structured-output-on-every-door.md), structured output on every door;
  - [60c](../../cli/v0.2.4/app-tools-on-every-door.md), an app's own tools;
  - [60d](../../cli/v0.2.4/building-an-app-on-apogee.md), the integrators' guide and the compatibility promise, which documents this package and puts its worked example beside the sample;
  - [60e](../../cli/v0.2.4/maintaining-the-app-integration.md), the integration's law on the harness side.
- The CLI's half of the remote door: [59a](../../cli/v0.2.3/paired-devices.md), [59b](../../cli/v0.2.3/machine-mode-over-the-network.md) and [59d](../../cli/v0.2.3/reads-and-commands-over-the-network.md).
- GUI siblings:
  - [7e](maintaining-apogeekit.md): the darwin ADR, the public-interface check and the contributor rules for this package, gated on it;
  - [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md): the Mac's device-tool host, which stays in the app;
  - [7c](every-place-against-a-server.md): every place against a server, built over the same modules;
  - [8a](../v0.2.5/app-builds-for-ios-and-ipados.md): the code built for iOS, whose platform-file rule this item uses for the lower layers first.
- Precedents:
  - 1a, the Xcode project as the module map, and the user's call that made it one ([MILESTONES.md](../../../assistant/darwin/MILESTONES.md):43);
  - the layering check's planted miniatures (`LayeringTests.swift`);
  - 4k's `MachineSchema`, the date read at launch;
  - the rule that a public default argument names only its own module (CLAUDE.md → Code Style), which is why the Mac defaults become `+macOS` overloads.
- [machine-mode.md](../../../reference/cli/machine-mode.md): the `hello` line (`:612`) an embedder names itself in.

## Testing

Tests are hermetic: scripted fakes and recorded documents, no binary, no socket, no window.

- **Every moved module's existing tests**, unchanged but for the fixture move, run under both `swift test` and the shared scheme.
- **`tests/business/session/`** — the override read through the closure at each locate, and a nameless `ClientIdentity` no longer compiling (a doc test, not a runtime one).
- **`tests/data/protocol/`** — `CapabilityDocument`, the `user` line's `schema` and the `result` verdict, over documents the branch binary printed.
- **`tests/business/session/` and `tests/business/reads/`** — the structured call (conforming, non-conforming, and an older binary's date said as unavailable), and the client-tool exchange (declare, request, result, cancel, a bad name refused before sending), over the scripted fake child.
- **`tests/presentation/app/`** — the monitor's registry is the instance `BusinessJobs` registers into (one copy, W6.7).
- **`tests/architecture/layering/`** — the new rules, each on a planted miniature.
- **The layering check reads both maps**, each rule pinned on a planted miniature:
  - the project references exactly one package, ApogeeKit, by a local reference; a remote reference or any other package fails;
  - the package declares no dependencies, plugins, binary targets, `unsafeFlags` or `linkerSettings`;
  - every package target sits at `lib/src/darwin/source/<layer>/<package>/`, is named `<Layer><Package>`, is Infrastructure, Data or Business, and is never `DataPreferences`. Its test target mirrors it under `tests/`;
  - every edge points down across both maps, and no package target depends on a project target;
  - no UI framework is imported in a package target;
  - no folder is built by both a package target and a framework target (one copy);
  - no package source names `listen(`, `accept(`, `bind(` or `NWListener` (never-listens);
  - the app embeds exactly the per-module products it needs, and never the umbrella product beside them;
  - the sample imports only the kit's modules, Foundation and Testing.
- **The package stands alone.** `swift build` and `swift test` at the root pass without the Xcode project, warning-free. No package test reads a fixture outside the package's test folders.
- **One copy at run time.** A test pins that the monitor's registry is the one `BusinessJobs` registers into.
- **The Mac app is unchanged.** `build.sh build`, `test` and `package` pass. The shared scheme's `totalTestCount`, read from its result bundle, is at least its count before the move (981 at 8a's measure; re-measured at the build). The resolved Mac build settings of the targets that stay match before and after, except for the package-product dependencies.
- **Secrets.** The Keychain fake shows a stored token never reaches a public type, a log line, a URL or a document. A `PairedServer` has no token field, and a test reflects over it.
- **The anti-goal.** With nothing paired, no transport is constructed (7a's guardrail, now in the package's tests). The sample's local run needs no server and opens no connection.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Where does `Package.swift` sit? | `[default]` | At the **repository root**. SwiftPM finds a remote package's manifest only at the root of its repository, so this is the one place a git-URL dependency works. Its targets point into `lib/src/darwin/source/` and `tests/`, so every file keeps one home and the app's tree stays the app's. `.build/` is already ignored (`.gitignore`). The costs are a root file outside `lib/src/darwin/` (the one exception to "each application is self-contained", said in CLAUDE.md), and a git consumer cloning the whole repository (about 96 MB of history). The alternatives: `lib/src/darwin/Package.swift`, which can only be consumed by local path; or a mirror repository that CI pushes to, which is a second repository to keep in step. |
| How do the Mac app and embedders consume the package? | `[default]` | The Mac app consumes the package by a **local reference** to its own checkout, never by URL or tag, so it always builds the same commit's kit. Embedders use the git URL, `https://github.com/QuantumCompiler/Apogee` (public), at a release tag. A merged release's `vX.Y.Z` tag is a SwiftPM version, and the package has no version of its own beyond the release (`lib/release/VERSION`). While developing, an embedder may use a local path. Which version requirement an integrator writes is 60d's guide's to say. |
| Which products do the Mac app and embedders link? | `[default]` | The Mac app links **one dynamic product per module**, embedded as today's frameworks are, so `ChildRegistry.app` and `ShellPath.app` exist once (W6.7). Embedders take the umbrella product `ApogeeKit` (automatic linkage). There is no umbrella module: an embedder imports the modules it uses (`import BusinessSession`, `import BusinessReads`), so its imports state the layer law too. The check refuses the app linking the umbrella product beside the per-module ones. |
| Is `BusinessMedia` in the package? | `[default]` | Yes, `BusinessMedia` is in. It is general: the CLI's video, voice and compose records and commands (`ClipForm`, `ComposeForm`, `MediaLibrary`, `StoreMedia`, `InstallConsent`, `CaptionsAbility`), importing only `BusinessReads`, `DataProtocol` and Foundation. Which folders it scans is handed in; the app's media folders stay in `DataPreferences`. |
| Are 7a's modules in the package, and where is the active connection stored? | `[default]` | All four of 7a's modules are in. Where 7a keeps the active connection in `Preferences` app-wide, that storage stays in the app: `DataConnections` holds the records and `ActiveConnection` as a value, and the app's `DataPreferences` stores it. |
| Which platforms does the package declare, and how is iOS checked? | `[default]` | The package declares macOS 27 and iOS 27. The lower layers' Mac-only sites become `+macOS` files here (8a's naming, its W9.2 list), and the iOS build of the package is checked with `xcodebuild -scheme ApogeeKit -destination 'generic/platform=iOS Simulator' build`. Running the suite on simulators, Presentation and the iOS app stay 8a's. When 8a is taken, its lower-layer bullets are already done in the package. If the builder finds the iOS split outgrows this item, the fallback is macOS only here and iOS added by 8a, said in the record. |
| Is `InfrastructureProcess` split into a protocols module and a Mac module? | `[default]` | No: `InfrastructureProcess` stays one module with its process files guarded (8a's default), not split into a protocols module and a Mac module, because every importer already reaches it and 7a's transport implements its protocols. |
| What is the package's public interface at first? | `[default]` | The moved modules' existing `public` declarations, plus this item's changes and typed calls. There is no facade redesign. A task-shaped convenience for an app like Taylor'd Portfolio's (`TextGenerating`-style) is shown in 60d's worked example over these types, not added here. |
| Which typed calls are built in this item? | `[default]` | The typed calls for 59f, 59g, 59h and 60c are built in this item for each whose CLI half has shipped when it is taken. One still pending gets its Swift call in the change that ships its CLI half (7e's per-change duty). This document's section for it is that call's spec. |
| How is an embedder's credential stored? | `[default]` | By `InfrastructureKeychain` as generic-password items for this device only, never synced, under a service the embedder names (by default its bundle identifier). So Apogee.app's items and each app's are separate, and no app can read another's. `PairedServer` carries name, addresses, port, kind and when it was paired, never the token. Removing a server deletes both items. |
| How is `CapabilityDocument` built? | `[default]` | As hand-written `Decodable` types held to the binary's printed `$defs/capability_document` by a test over a recorded schema, as `MachineSchema` is held today. There is no code generation, because the package declares no build plugins. |
| How does the layering check read the manifest? | `[default]` | Through `swift package dump-package`. It is SwiftPM's own reading, never a text scrape, and is available wherever `xcodebuild` is. If the toolchain is absent when the check runs, the check fails and names it, rather than skipping. |
| Where does the sample live, and how is it run? | `[default]` | The sample is `lib/src/darwin/samples/apogee-kit-sample/`, a package of its own outside the kit, depending by path. CI builds it and runs its tests (60d's example's hermetic test). A run of the program against a binary on the `mock` backend in a throwaway `APOGEE_HOME` is a scripted step that runs where a branch or `apogee-dev` binary is at hand, and is skipped with the reason otherwise, as the real-binary pass is today. |
| What happens to `RealBinaryTests`? | `[default]` | `RealBinaryTests` (`tests/business/session/`) moves with its module and keeps its skip-when-absent guard. It is the package's one test that may start a binary, and the hermetic suite never depends on it. |
| Who writes the darwin ADR that records this package as the one client core, the public-interface baseline check, and the contributor guide's ApogeeKit section? | `(consumed decision)` | [7e](maintaining-apogeekit.md), gated on this item. |
| Who owns the compatibility promise's wording, the guide, and the worked example's content? | `(consumed decision)` | [60d](../../cli/v0.2.4/building-an-app-on-apogee.md). This item places the example (in the sample's package) and declares `ApogeeKit.schema`, which 60d's promise names. |
| Who owns the app credential's kind, prefix, pairing verbs and scope, and whether a local stdio app is held to a scope? | `(consumed decision)` | [59f](../../cli/v0.2.3/paired-apps-with-scopes.md). Where an app keeps its token and pin, and how ApogeeKit pairs, are this item's (above). |
| Who owns the wire shapes of capabilities, structured output and client tools? | `(consumed decision)` | 59g, 59h and 60c. Their Swift calls are this item's (above). |
| Who owns the connection, pairing exchange and pin check? | `(consumed decision)` | 7a (and 59a, 59b and 59d). This item moves them into the package unchanged. |

No `[user]` call blocks this item.

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the apps-as-clients spike. The user's calls that shape this item: (1) Taylor'd Portfolio uses the full harness (models, retrieval over its documents, autonomous tasks on the server), not just model calls, so the package carries sessions, reads and jobs, not a completion call alone. (2) Apps reach Apogee through both doors: the OpenAI-compatible endpoints for plain model calls, and Apogee's own protocol (stdio locally, the paired connection remotely) for the harness; the package is the Swift form of the second door. (3) Each paired app is held to a scope the person approves; the scope is the server's to enforce (59f), and the package only carries the credential. (4) Apogee ships a Swift package other apps embed, factored from the Mac app's client core, and the Mac app depends on it too: one implementation, tested by Apogee. (5) On the OpenAI-compatible door, a client app's own tools work the standard way (Apogee returns `tool_calls`, the app runs them, the turn continues), within the app's scope. This reverses serve's recorded rule "It does not run tools the client defines" ([http-api.md](../../../reference/cli/http-api.md)), and the reference changes with [60c](../../cli/v0.2.4/app-tools-on-every-door.md). That door is plain HTTP, which an app reaches with any OpenAI client, so the package does not wrap it; its `ClientTool` is the same capability on Apogee's own protocol, by 60c's wire shape. (6) The CLI SPEC amendment is approved and written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4, *Revised 2026-11-14*). (7) Taylor'd Portfolio's own side is not specced now. (8) Remote use is an add-on: the CLI alone and the Mac app over pipes stay complete and need no server. | the user |
| 2026-11-14 | Placed in v0.2.4 (track 7) as 7d, gated on 7a only: the spike's placement, accepted by the user with "execute the spike" (CLI 59f, 59g and 59h in v0.2.3; CLI 60c, 60d and this item in v0.2.4). The package carries 7a's connection from its first version, so an embedder's remote door and the Mac's are one code path. It comes before 60d, which documents it, and 7e, which writes its law. 59f, 59g and 59h sit in v0.2.3, whose table precedes this one, so their CLI halves are normally built first. | the user |
| 2026-11-14 | This revises the user's 2026-10-20 call "An Xcode project, not a Swift package" ([MILESTONES.md](../../../assistant/darwin/MILESTONES.md):43) **for the lower layers only**, by the user's call above. The app stays an Xcode project: Presentation, `DataPreferences`, the scenes, signing and `build.sh` are unchanged in kind. | the user |
| 2026-11-14 | `DataPreferences` is not in the package. It is the app's own settings over `UserDefaults`, and an embedder has its own. `ApogeeClient`'s one use of it (the binary override) becomes a value handed in. | the spec |

## Out of scope

- Taylor'd Portfolio's own side (its engine over ApogeeKit, its settings, its move off `claude -p`), not specced now (the user's call, 2026-11-14)
- The darwin ADR, the public-interface baseline and the contributor rules (7e)
- The guide, the compatibility promise's wording and the worked example's content (60d)
- The server's side of app pairing, scopes, capabilities, structured output and client tools (59f, 59g, 59h, 60c)
- The iOS suite on simulators, Presentation on iOS and the iOS app (8a)
- The Mac's device-tool host (7b), which stays in the app
- A facade API over the existing types, an umbrella module, an XCFramework or binary distribution, and listing on a package index
- A client core for the Linux or Windows apps, which do not exist yet

## Links

- **Blocked by:** [7a](app-connection-local-or-server.md) · **Blocks:** [7e](maintaining-apogeekit.md), [CLI 60d](../../cli/v0.2.4/building-an-app-on-apogee.md)
- **Relates to:** GUI [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md), [7c](every-place-against-a-server.md), [8a](../v0.2.5/app-builds-for-ios-and-ipados.md); CLI [59a](../../cli/v0.2.3/paired-devices.md), [59b](../../cli/v0.2.3/machine-mode-over-the-network.md), [59d](../../cli/v0.2.3/reads-and-commands-over-the-network.md), [59f](../../cli/v0.2.3/paired-apps-with-scopes.md), [59g](../../cli/v0.2.3/capability-discovery.md), [59h](../../cli/v0.2.3/structured-output-on-every-door.md), [60c](../../cli/v0.2.4/app-tools-on-every-door.md), [60e](../../cli/v0.2.4/maintaining-the-app-integration.md); [machine-mode.md](../../../reference/cli/machine-mode.md); [SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4
- **Follows from:** the apps-as-clients spike (2026-11-14)
- **Revises:** the user's 2026-10-20 call "An Xcode project, not a Swift package" ([MILESTONES.md](../../../assistant/darwin/MILESTONES.md):43), for the lower layers only
