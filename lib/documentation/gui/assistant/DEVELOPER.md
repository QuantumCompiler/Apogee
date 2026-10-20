# Apogee GUI — Developer Reference

The package-by-package architecture reference for the GUI applications: every directory, file, interface, and build/test command. The contributor entry point is [CLAUDE.md](CLAUDE.md); the product spec is [SPEC.md](SPEC.md). The CLI's own reference — including the machine-mode protocol the GUI is built against — is [`cli/assistant/DEVELOPER.md`](../../cli/assistant/DEVELOPER.md) and [`cli/reference/machine-mode.md`](../../cli/reference/machine-mode.md).

---

## Layout

Each application is self-contained, owning its build, with `source/` and `tests/` at its first level. Each is four layers — Infrastructure, Data, Business, Presentation — with dependencies pointing down, regardless of its language and framework ([ADR 0001](../adrs/layer-enforcement.md)): `source/<layer>/<package>/`, tests mirroring. Presentation modules keep the MVVM shape — `Views/`, `ViewModels/`, module-specific extras — with shared typography, colors and common components in the one `styles` module ([ADR 0002](../adrs/presentation-mvvm.md)).

```
lib/src/darwin/                 the macOS application (1a, 2026-10-20)
├── Apogee.xcodeproj/           the Xcode project -- the module map
│   ├── project.pbxproj         every target, its folder, dependencies, links and embeds
│   └── xcshareddata/xcschemes/Apogee.xcscheme
│                               the shared scheme: builds the app, runs every test bundle
├── scripts/build.sh            the one build entry from outside (cicd.sh calls it)
├── source/
│   ├── main/                   the application target `Apogee`: the entry point alone
│   ├── infrastructure/         CLAUDE.md (the layer's card) · process/
│   ├── data/                   CLAUDE.md · protocol/
│   ├── business/               CLAUDE.md · session/
│   └── presentation/           CLAUDE.md · styles/ · app/ (Views/, ViewModels/)
├── tests/                      mirrors source/'s modules, one unit-test bundle each
│   └── architecture/layering/  the layering check
└── build/<target>/             output (ignored): xcode/ derived data, Apogee.app
lib/src/linux/                  the Linux application -- not yet started
lib/src/windows/                the Windows application -- not yet started
```

### `lib/src/darwin/` — the macOS application

**An Xcode project, and the project is the module map** (1a; an Xcode project rather than a Swift package is the user's call, 2026-10-20). A module is a **framework target** building one folder, `source/<layer>/<package>/`, as a file-system synchronized group — a file added to the folder is compiled into the target with no project edit — named `<Layer><Package>` (`DataProtocol`), so its folder is its layer's declaration and every `import` names the layer it reaches. Its **dependencies and links are its edges**. Each module has an **unhosted unit-test bundle** at `tests/<layer>/<package>/`, `<Layer><Package>Tests`, in the shared scheme. The **application target `Apogee`** builds `source/main/` — `ApogeeMain`, which starts `PresentationApp`'s `ApogeeApp` and nothing else, the composition root as the CLI's `source/main.cpp` is — links `PresentationApp`, and embeds every framework that reaches. Project-wide: Swift 6 language mode, macOS 27.0 deployment target, `arm64` only, `CODE_SIGN_IDENTITY "-"` (ad hoc, the user's call, 2026-10-20), no hardened runtime, no package references.

| Module | Layer | Files | What it is |
|---|---|---|---|
| `InfrastructureProcess` | Infrastructure | `ChildCommand.swift` | A child process to spawn: an absolute executable (a `.app` inherits no shell `PATH`), its arguments, its environment laid over the parent's, its working directory. Describes, never runs -- the spawning arrives with the client core (1b). |
| `DataProtocol` | Data | `MachineProtocol.swift` | The machine-mode protocol this client reads: `version` 1 (`understands(protocolVersion:)` refuses another), the `stream-json` format word. The event vocabulary arrives with 1b. |
| `BusinessSession` | Business | `ChatLaunch.swift` | How a driven chat's child starts: `chat --output-format stream-json --input-format stream-json`, then `--tools` (on by default, as the CLI's interactive default). |
| `PresentationStyles` | Presentation (the floor) | `Palette.swift`, `Typography.swift`, `Metrics.swift`, `Components/EmptyState.swift` | The shared vocabulary: `ColorToken`s from the design's Graphite Light/Dark themes (`lib/design/spec/json/tokens.json`, copied) resolved against the window's appearance through a dynamic `NSColor`; `TypeScale` and `Font.apogee*` in the system fonts; `Spacing`, `Radius`, `Layout`; `EmptyState`, a centered symbol, title and message. Leans on no sibling. |
| `PresentationApp` | Presentation | `Views/ApogeeApp.swift`, `Views/WelcomeView.swift`, `ViewModels/AppViewModel.swift` | The application: `ApogeeApp` (public, one `WindowGroup`), the welcome view over `AppViewModel` -- `@MainActor @Observable`, Foundation only, the name and the bundle's version or "Development build". |
| `Apogee` (application) | -- (`source/main/`) | `ApogeeMain.swift` | The entry point: `@main`, `ApogeeApp.main()`. |

**The layering check** (`tests/architecture/layering/`): `ModuleMap` reads `project.pbxproj` with `PropertyListSerialization` (it is an old-style property list) -- each target's product type, synchronized folder (its whole path walked up the groups), target dependencies, linked and embedded products, and the build settings that would reach outside the project -- and the shared scheme's testables; `SourceTree` is the files, on disk or planted; `LayeringCheck` holds them to the rules its header lists (ADR 0001's layers and edges, nothing from outside the project, declared imports, UI frameworks in Presentation and never in `ViewModels/`, the MVVM shape, `styles` as the floor, the application's one file and exact embeds, mirrored tests, the scheme running every bundle, the layer cards). `LayeringTests` runs it on this project, plants each violation in a miniature, and plants one dependency into the real project file's objects.

**Adding a module:** the folder `source/<layer>/<package>/` (Presentation: with `Views/` and `ViewModels/`); in Xcode, a macOS framework target named `<Layer><Package>` whose synchronized folder is that directory, linking only its own layer or below; its test bundle `tests/<layer>/<package>/` (`<Layer><Package>Tests`, no host application) added to the shared scheme's tests; its name on its layer card's **Modules** line; and, when the app comes to reach it, the application target's Embed Frameworks. The layering check names whichever of these is missing.

## The contract the GUI builds against

- **The protocol:** [`machine-mode.md`](../../cli/reference/machine-mode.md) — the JSONL events, the driver's stdin lines (including `cancel`), the reads' `--output-format json` documents, task and play narration.
- **The schema:** `machine-schema.json` — printed by the binary (`apogee __machine-schema`), shipped in every CLI archive, and pinned to the doc and the implementation by `cli.machine_schema_conformance`.
- **Getting a CLI to drive:** a release install (`install.sh` / `install.ps1`), a source build (`make -C lib/src/cli install`, channels per the CLI docs), or CI's `apogee-<target>` artifacts.

## Build & test

```bash
lib/scripts/cicd.sh --test                    # every application, host-native -- the gate
lib/scripts/cicd.sh --app darwin --test       # the macOS app alone: build, bundle, suite
lib/src/darwin/scripts/build.sh test          # its suite alone (Debug)
lib/src/darwin/scripts/build.sh build         # Release Apogee.app, version stamped, signed ad hoc, verified
lib/src/darwin/scripts/build.sh open          # build, then open the app
```

Or open `lib/src/darwin/Apogee.xcodeproj` in Xcode: the shared scheme `Apogee` runs the app and the tests (Cmd-U) with the same settings; a build from Xcode carries no version and says "Development build".

`scripts/build.sh` drives `xcodebuild` over the shared scheme for `platform=macOS,arch=arm64`, its derived data in `build/<target>/xcode/`, `-quiet` so the log keeps warnings, errors and the tests' output. `build` passes `lib/release/VERSION` as `MARKETING_VERSION`, copies the Release product to `build/<target>/Apogee.app` and runs `codesign --verify --deep --strict` on it. `test` writes `build/<target>/tests.xcresult` and, on a failure, prints every failed test with its messages from it. It builds macos-arm64 on an Apple-silicon Mac only. `cicd.sh` calls it -- `build`, then `test` under `--test`; `test` alone under `--unit-tests`; `clean` under `--clean` -- and knows nothing else of how the app compiles: `APPS=(cli darwin)`, darwin built for macos-arm64 only, deferred to CI on another host (refused there under `--no-defer`), and `--app` picks applications (every one by default; the CLI pipeline passes `--app cli`). `lib/scripts/changed.sh gui-darwin` declares what the app is built from: `lib/src/darwin/`, `cicd.sh`, `changed.sh` and `lib/release/VERSION`. Its CI pipeline is track 1's 1d; the repo-wide release law is in the [CLI docs](../../cli/assistant/CLAUDE.md#release-and-install-infrastructure).
