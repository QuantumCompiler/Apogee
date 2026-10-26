# Apogee GUI — Milestones

The **record of finished work** — completed items move here out of [`backlog/`](../backlog/README.md) (the work queue) so the backlog holds only what's left. This is history and reference: what shipped and how it was built. The release-level board is in [ROADMAP.md](ROADMAP.md); the architecture reference is in [DEVELOPER.md](DEVELOPER.md).

Milestones are grouped by feature area and lettered. Each entry gives the user-facing **Goal**, a checklist of what was built, and a **Notes** line for trade-offs.

When a backlog item ships, **fold** it into the matching milestone here (extend the checklist, or add a dated `###` subsection for a substantial addition) rather than starting a new one, unless it's a genuinely new area — and delete its backlog document per the docs flow.

---

## Milestone A — The macOS application

**Goal.** Apogee as a native Mac app (track 1, opened 2026-10-20 from that day's GUI spike, the user's calls: **Swift and SwiftUI**, macOS first, **unsigned ad-hoc** for now): an Xcode project under `lib/src/darwin/` that drives the `apogee` binary over the stdio machine mode and reimplements nothing, held to the four layers ([ADR 0001](../adrs/layer-enforcement.md)) and Presentation's MVVM shape ([ADR 0002](../adrs/presentation-mvvm.md)) by a check with teeth from its first commit. Its tests are hermetic: scripted fakes, no network, no `apogee` binary, no window.

### 2026-10-20 — `darwin-scaffold` (backlog item 1a): a buildable four-layer app from day one

**Why.** Nothing of the GUI existed in the build -- `cicd.sh` built `APPS=(cli)` and `changed.sh` knew only `cli` -- while the toolchain wall of September was gone: Xcode 27.0 licensed, Swift 6.4 answering, SwiftUI parsing (the spike's measurements). ADR 0001 said its enforcement lands with the first application's build, so the law's teeth had to arrive with the first file, not later.

- [x] **The project** (`lib/src/darwin/Apogee.xcodeproj`: Swift 6 language mode, macOS 27 and later, `arm64` only). **The project is the module map**: each module a framework target building one folder, `source/<layer>/<package>/`, as a file-system synchronized group -- a file added to the folder is in the target with no project edit -- and named `<Layer><Package>`, so where a module lives is its declaration and its dependencies and links are its edges. Each is mirrored by an unhosted unit-test bundle at `tests/<layer>/<package>/`, and the shared scheme `Apogee` (`xcshareddata/xcschemes/`) builds the app and runs every bundle.
  - Infrastructure `process` (`InfrastructureProcess`): `ChildCommand`, a child to spawn -- an absolute executable, never a `PATH` lookup a `.app` cannot make -- with its environment laid over the parent's.
  - Data `protocol` (`DataProtocol`): `MachineProtocol` -- protocol version 1, a stream announcing another refused rather than misread, and the `stream-json` format word.
  - Business `session` (`BusinessSession`): `ChatLaunch`, the child a driven chat starts as -- `chat --output-format stream-json --input-format stream-json --tools`, tools on as the CLI's interactive default.
  - Presentation `styles` (`PresentationStyles`): the shared vocabulary -- the design's Graphite Light and Dark colors (`lib/design/spec/json/tokens.json`, copied, never read) following the system's appearance, its type scale in the system fonts, spacing, radii and window sizes, and the first common component, `EmptyState`.
  - Presentation `app` (`PresentationApp`): the SwiftUI `App` with one window, `Views/` (`ApogeeApp`, `WelcomeView`) over `ViewModels/` (`AppViewModel`, SwiftUI-free: the name, and the version the bundle carries, or "Development build" for a build from Xcode).
  - **The application target `Apogee`** builds `source/main/`: `ApogeeMain`, the entry point that starts `ApogeeApp`, and nothing else -- the composition root, as the CLI's `source/main.cpp` is. It links `PresentationApp` and embeds exactly the frameworks that reaches (`PresentationApp`, `PresentationStyles`), signed on copy. Its `Info.plist` is generated from the target's settings: `com.quantumcompiler.apogee`, the developer-tools category, `LSMinimumSystemVersion` 27.0.
- [x] **The layering check** (`tests/architecture/layering`, a Swift Testing suite): reads `project.pbxproj` as the property list it is -- every target's kind, folder, dependencies, links, embeds and build settings -- and the shared scheme's testables, and the sources, and fails, each violation naming its targets:
  - an empty map (it refuses to pass vacuously); a target building other than one folder whole (no membership exceptions); a module off `source/<layer>/<package>/` or misnamed;
  - an upward edge; anything from outside the project -- a Swift package, a package product, a search path, `OTHER_LDFLAGS` or `OTHER_SWIFT_FLAGS` (the app links nothing of the CLI's);
  - an `import` of a project module the target does not declare;
  - a UI framework (SwiftUI, AppKit, UIKit, Cocoa, Charts, WidgetKit) below Presentation, or in any `ViewModels/` file;
  - a Presentation module without `Views/` and `ViewModels/` holding its files, and a `styles` that depends on a sibling;
  - an application that is not the one `Apogee` at `source/main/` holding one file, or that misses a framework it needs at run time -- the app would not launch -- or embeds one nothing needs;
  - a module without its mirroring test bundle, and a test bundle the shared scheme does not run;
  - a layer card (`source/<layer>/CLAUDE.md`, the CLI's A5 discipline carried over) whose **Modules** line does not name exactly its layer's modules -- the CLI's `harness.layer_context` analog.
  - Each rule is pinned on a planted miniature of the project, and one dependency planted into the real `project.pbxproj`'s objects fails as an upward edge by name. On its first run, against the scaffold's first layout, the check caught the scaffold's own layer cards naming types on their **Modules** lines.
- [x] **The one build entry** (`scripts/build.sh build|test|open|clean`): `xcodebuild` over the shared scheme, its derived data under `build/<target>/xcode/`. `build` makes the Release app with the version stamped from `lib/release/VERSION` (`MARKETING_VERSION`), signed ad hoc (`CODE_SIGN_IDENTITY "-"` in the project), copied to `build/<target>/Apogee.app` and verified with `codesign --verify --deep --strict`; `test` runs the scheme's tests in Debug, and on a failure prints each failed test with its own messages from the result bundle -- `-quiet` names a failure but not why, and on a runner the log is all there is. macos-arm64 on an Apple-silicon Mac only, refused anywhere else; a toolchain line first.
- [x] **The repo's wiring**:
  - `lib/scripts/cicd.sh`: `APPS=(cli darwin)`, each application with its own targets (darwin: macos-arm64) and either build entry -- a CMake project or an app's `scripts/build.sh`, which it calls and knows nothing behind. `--app NAME` (repeatable, `all`) picks the applications, every one by default; `--test` builds and runs darwin's suite, `--unit-tests` its suite alone. On another host darwin's target defers to CI, and `--no-defer` fails it loudly; darwin asked for a Linux or Windows target is said, never an error.
  - The CLI pipeline names its deliverable: `ci.yml`'s four `cicd.sh` calls, `pr-ci.sh`, `linux-unit-tests.sh` and the CLI's Makefile pass `--app cli`, so the CLI's macOS jobs never build the app. No job, name, matrix or gate changed.
  - `lib/scripts/changed.sh gui-darwin`: `lib/src/darwin/`, `cicd.sh`, `changed.sh` and `lib/release/VERSION`. Tab completion learns `--app`. The repository ignores Xcode's per-user `xcuserdata/`.

Decisions taken while building, for veto:
- **An Xcode project, not a Swift package** (the user's call, mid-build, superseding the SwiftPM default 1a was groomed with -- the scaffold was first built as a package named `Apogee`, then rebuilt): one `Apogee.xcodeproj` holding every module as a framework target, Xcode building and signing the `.app` itself, so no bundling script exists. The project file was written once by a throwaway generator, for stable Xcode-shaped identifiers, and is maintained as any project file is from here.
- **Dynamic frameworks, embedded by the application**, rather than static libraries: no final link has to repeat a module's transitive links, and the unhosted test bundles load each framework from the build products. The cost is the embed list, which the check holds to exactly what the app reaches.
- **The entry point is its own target at `source/main/`**, so the application's scene, views and view-models live in a framework and test headlessly; a hosted test bundle would launch the app.
- **Unhosted test bundles, Swift Testing**, and **no root files in a Presentation module** -- its files sit in `Views/`, `ViewModels/` or a directory of their own -- the check's stricter reading of ADR 0002's shape.
- **The version is stamped by the release build only**: `scripts/build.sh` passes `lib/release/VERSION` as `MARKETING_VERSION`; the project carries none, so a build from Xcode says "Development build" rather than a number nobody keeps in step.
- **`cicd.sh --app`, every application by default, and the CLI pipeline's calls pinned to `--app cli`.** A developer's `cicd.sh --test` must cover the app, and the CLI's macOS jobs (Xcode 15 on `macos-14`) must never build it; the only way to have both is for each pipeline to name its deliverable. It touches `ci.yml`'s run lines and nothing else of it -- no job, name, matrix or gate -- so the required checks are unaffected.
- **`lib/release/VERSION` is an input of `gui-darwin`**, because the app reports it: a release bump runs the app's pipeline, where it runs no CLI pipeline.
- **System fonts at the design's sizes**, not the design's IBM Plex (no bundled fonts yet), and the colors following the system's light and dark appearance with no theme setting (the 1c default, honored early).
- **Every module has a mirroring test bundle**, the CLI's ADR 0004 carried over and held by the check.

**Verified.** `lib/scripts/cicd.sh --app darwin --clean --test`: the Release app built, signed ad hoc and verified, embedding `PresentationApp` and `PresentationStyles`; the suite's 31 tests in 7 suites pass, with no compiler warning. A test broken on purpose in a scratch copy was reported by name with its expectation. Bare `lib/scripts/cicd.sh --test` builds and tests both applications: the CLI's 3550 tests passed with `--app` in place (two skipped, as always: the libass caption burn and pdftotext); a second run after the docs pass failed only `cli.mlx_lifecycle`, the flake v0.1.9 recorded ("check did not report the entry's window" -- a race in that test's script, found and fixed later the same day: the CLI's MILESTONES, 50a's entry), which passed alone three times out of three, and the darwin half was then run on its own. `changed.sh gui-darwin` answers `false` on a docs-only range (`9a4d00c..9e45f52`) and `true` on a darwin-only tree (a commit object no branch points at), where `changed.sh cli` answers `false`. With a fake `uname` reporting Linux x86_64, `cicd.sh --app darwin` says darwin has no Linux build, defers `--platform macos-arm64` to CI, and fails it under `--no-defer`. The built app launched on the dev host with its embedded frameworks and stayed up, silent on stderr.

**Not run: looking at the window.** That the window draws the name and version as designed is the user's one manual check (`lib/src/darwin/scripts/build.sh open`, or Run in Xcode).

### 2026-10-20 — `machine-mode-client-core` (backlog item 1b): the CLI as a Swift type

**Why.** The GUI spike had driven the shipped binary end to end with a ~40-line probe, and measured the wall this item owned: a `.app` inherits no shell `PATH`, and `machine-schema.json` is not installed as a file. The core turns the probe into the layers' own packages, so the window (1c) consumes a session and never a pipe.

- [x] **Infrastructure `process`**: a child held by its three pipes and nothing else (`RunningChild`, `ProcessSpawner` over Foundation's `Process`):
  - stdout framed into lines by `LineFramer` -- a line cut across reads comes out whole, UTF-8 decoded only once whole, bytes that are not UTF-8 become U+FFFD (the CLI's own mending);
  - stderr captured into an 8 KiB `BoundedTail`, never shown, its last line what a failure says;
  - a write to a child that stopped reading is an error, never a SIGPIPE that ends the app (`F_SETNOSIGPIPE` on the pipe);
  - `terminate` and `kill` left to the caller's escalation; the exit awaited with its reason (`exited` or `signalled`);
  - `capture`: a one-shot run whole, its input closed, stopped past its time (SIGTERM, SIGKILL two seconds later) or its stdout limit.
- [x] **Data `protocol`**, the protocol as types:
  - `ProtocolEvent` -- every event of machine-mode.md as a typed case (session and its capabilities, thinking, deltas, memory, tool status, notice, todo, the answer, result with usage absent when absent, question with permission's tool, target, outbound and detail, error); a task run's `task_*` events kept whole; `unknown(type:)` for a type it does not know, or a known one missing what its case needs; `unreadable(line:)` for a line that is not one JSON object with a type. **Every event keeps all its fields**, read and unread, so nothing the binary says is dropped and decoding never fails.
  - `DriverLine` -- `hello`, `user`, `answer`, `attach`, `cancel`, one JSON object each, `type` first, the same bytes every time.
  - `ReadDocument` -- a read's one JSON document kept whole, with a list's entries, total and problems; `JSONValue` keeps any JSON exactly.
  - **The fixture** (`tests/data/protocol/Fixtures/mock-actor.jsonl`): the real v0.1.9 binary's stdout for the mock-actor conversation -- a question, a permission-gated `write_file`, reasoning and a streamed answer -- recorded in a throwaway `APOGEE_HOME` on the CLI's scripted mock backend by `scripts/capture-fixture.sh`, which refreshes it deliberately and reproduced it byte for byte. The decoder is held to it: every line typed, the deltas the result's text.
- [x] **Data `preferences`**: the GUI's own settings, never the CLI's config -- the binary override, in the app's defaults domain (`com.quantumcompiler.apogee`) behind a `KeyValueStore`, so tests write nothing outside themselves.
- [x] **Data `discovery`** (`BinaryDiscovery`): the override, then the release channel (`~/.local/bin/apogee`), then the dev and test channels beside it; each candidate verified by running `apogee version` (ten seconds) and reading its `apogee x.y.z` line; one that is missing, not executable, silent, failing or not Apogee skipped with the reason; none found the typed `notFound`, every place said, with `installHint` (the installer's one-liner).
- [x] **Business `session`**:
  - `MachineSession`, an actor: spawns the child, writes `hello` first, delivers every line as a `ProtocolEvent` on an `AsyncStream`, in order, `.ended(SessionEnd)` last -- `finished`, `failed`, `stopped`, `couldNotStart` or `unsupportedProtocol(n)` -- with the stderr tail and a one-line `message` in the child's own last words. `send(user:)`, `answer(_:)`, `attach(path:)` and `cancel()` write the protocol's lines; `close()` closes the input so the child saves and exits; `stop(grace:)` escalates only as far as it must: a turn in flight cancelled, the input closed, SIGTERM after the grace, SIGKILL two seconds later. A session announcing a protocol version other than 1 is refused -- nothing of it delivered, the child stopped -- rather than misread.
  - `TurnTracker`: where the session stands, from its events alone -- opening, idle, running a turn, asking (answers still owed), over -- numbered from the session's `next_turn`.
  - `ApogeeClient`, the front door: `locateBinary()` with the stored override, `startChat(binary:)` with `--tools` and the user's home directory as its folder.
- [x] **Business `reads`** (`ReadRunner`): one read with `--output-format json` added, captured and bounded (sixty seconds), its outcome a decoded document, a failure with the CLI's words, stdout that is not a document, a timeout, or a start that failed.
- [x] **The tests**, 87 in the app's suite now, hermetic:
  - the real spawner over `/bin/sh`, `/bin/cat` and friends (framing, the tail, a write to a dead child, terminate, the environment and folder, a loud stderr, the captured run's limits);
  - the tolerance matrix and the recorded stream (`EventDecoderTests`), the lines byte-exact (`DriverLineTests`);
  - the discovery table over fake binaries in a home of their own;
  - the session against **a scripted fake `apogee`** (`FakeApogee`, a Python program speaking the protocol by scenario and logging what it reads): the whole conversation in order with the hello, the answer and the question's phases; a cancel mid-stream ending in a cancelled result and the same child answering the next turn; a dying child's stderr tail riding the failure; unknown types, unknown fields and a non-JSON line flowing through; protocol 2 refused; a child ignoring end of input and SIGTERM killed after the grace; a binary that will not start;
  - the reads over fake read scripts;
  - **the real binary**: discovery finding the install on this machine, and the core driving it through the mock-actor conversation in a throwaway `APOGEE_HOME` -- both answered, `todo.md` written, a clean end -- skipped and said where `~/.local/bin` holds no apogee.

Decisions taken while building, for veto:
- **Events keep their fields; the union's shape is a struct of `kind` and `fields`**, not an enum carrying raw JSON in every case, so tolerance is one rule: whatever a case reads, `fields` has everything.
- **A known type missing what its case needs is `unknown`, not guessed**, and a non-JSON line is `unreadable` -- both delivered, never dropped (the window renders them as the quiet row).
- **`AsyncStream`, not a delegate**: the window's view-model iterates it.
- **Discovery's places are the install channels' own** (`~/.local/bin/apogee`, `-dev`, `-test`); a custom `PREFIX` is what the override is for. A channel not installed is reported skipped as "not installed", so the absence names every place looked.
- **A chat starts in the user's home directory** unless given a folder: a `.app` starts in `/`, which is no place for a chat's tools to work.
- **The child inherits the app's environment as it is**, with only the session's own variables laid over it. The cost, named: launched from the Finder, the app's `PATH` is launchd's, so a tool the CLI looks for on a shell's `PATH` (`ffmpeg`, `gh`, `pdftotext`) may not be found by a session the app starts, and the CLI says so in its own words. Widening the child's `PATH` is a later, deliberate call.
- **`stop` waits five seconds before SIGTERM**, and two more before SIGKILL; a read waits sixty seconds, a version probe ten.
- **The fake `apogee` is Python** (`/usr/bin/env python3`), as the CLI's own drivers are: the session tests need `python3` on the machine, which the dev host and GitHub's macOS images have.

**Verified.** `lib/scripts/cicd.sh --app darwin --clean --test`: the Release app built and signed, and all 87 tests pass with no compiler warning, the layering check holding the three new modules; the suite ran green three more times in a row. The real-binary pass ran against the installed v0.1.9 and passed. `scripts/capture-fixture.sh` reproduced the recorded fixture byte for byte. Bare `lib/scripts/cicd.sh --test`: the CLI's suite passed but for `cli.mlx_lifecycle` -- the same recorded flake as 1a's second run ("check did not report the entry's window"), which passed alone twice; nothing of the CLI changed in this item. (Found the same day to be a race in that test's script, not the window -- a `perl | grep -q` pipe under pipefail -- and fixed: the CLI's MILESTONES, 50a's entry.)

### 2026-10-20 — `session-window` (backlog item 1c): a conversation you can watch and steer

**Why.** The client core could drive a session; nothing showed one. This is the first real surface -- the reason the app exists -- and the protocol already carried everything it needed: streamed answers, the thinking phase, tool lines, questions of both kinds, results, honest errors, and a `cancel` that ends a turn the protocol's way.

- [x] **Presentation `session`** (`PresentationSession`), MVVM-shaped:
  - **`Transcript`** (`ViewModels/`), a pure reducer: each `SessionEvent` and each of the user's acts (`userSent`, `answered`, `cancelling`) folded into rows -- the user's message; a turn's **thinking section** (its reasoning and its tool and helper lines, open while live, closed to "thought · N steps" once the answer starts, a new one after each card so the order is what happened); the answer, **exactly its deltas joined** (a result with no deltas before it still shows its text), with its usage when the provider gave one; notices and what memory recalled, quiet; the plan drawn in place; question and permission cards; the error in the CLI's words; "stopped" for a cancelled turn; **a quiet row for an event this client does not know**, a task event or a line that is not JSON; and the session's end, with its one-line message and the stderr tail behind it. The status -- connecting, ready, thinking, working, answering, waiting for you, stopping, ended -- comes from events alone: "stopping" holds from the `cancel` until the turn's own result.
  - **`SessionViewModel`** (`ViewModels/`, SwiftUI-free): locates the binary through `ApogeeClient` and starts the session, folds its events, and sends the user's acts back -- the turn, each answer, `cancel` -- through **one ordered outbox**, so lines leave in the order they were made. No binary: `missing`, with the install hint and every place looked. The input is held while a turn runs or a question stands; Stop is offered only while a turn is in flight. `shutDown` stops the session by escalation.
  - **The views** (`Views/`): `SessionWindow` -- starting, not found (the hint, selectable, the places, Try again), or the conversation: a header with the model and the state, the transcript in the design's 860-point column following its end, and the input bar (Send, ⌘⏎ and ⏎; Stop, ⌘. and Esc). `QuestionCardView`: each question with its options and, every time, a field for the user's own words; one Answer (⏎); answered, it collapses to "asked: … → …". `PermissionCardView`: the tool and its whole target, the whole URL for an outbound one, and Allow once (⏎), Allow for this session, Always allow, Deny -- `yes`, `session`, `always`, `no`; answered, its line ("allowed once", "denied by you"). The error card, the ended card (Details, Start a new session), the plan, the quiet rows. A `Card` component in the module's own `Components/`.
- [x] **The app** shows it: `AppViewModel` composes the session's view-model over `ApogeeClient` (the client's version the bundle's, or "development"), and `ApogeeApp`'s one window opens on it, starts it at launch and stops it when the window closes. The welcome placeholder is gone.
- [x] **Business** grew two protocols the window is written against -- `ChatSession` (what `MachineSession` is to a renderer) and `ChatStarting` (what `ApogeeClient` is) -- so a test hands in scripted sessions; and `SessionEnd.stderrText`.
- [x] **The tests** (103 in the suite), no window and no process:
  - `TranscriptTests`: **the recorded mock-actor stream** folded with the user's send and both answers, to the seven rows a person should see, each state pinned to its event; the deltas joined; a result standing alone; a cancel mid-stream (stopping until the result, the partial answer kept, "stopped"); an error's words exact; unknown, task and unreadable rows; a failed session's message and details; a binary that would not start; the plan in place and the recalled notice; a question during a cancel.
  - `SessionViewModelTests` over a scripted session that records every line it is asked to write: not found; a turn sent as typed and the input held; two answers in order; Stop only in flight; an ended session and a new one. And **end to end over a real `MachineSession`** fed by `ReplayChild` -- an in-memory child replaying the recorded stream, pausing for the user's turn and each answer -- every line the window wrote checked **byte for byte**: the hello, the turn, `{"type":"answer","text":"todo.md"}`, `{"type":"answer","text":"yes"}`; the answer "I wrote todo.md with one item: buy milk."; a clean end.

Decisions taken while building, for veto:
- **Esc stops the turn**, always -- the item's rule -- and the permission card's Deny is its own button. The design has Esc deny a standing permission card instead; a stop during a question fails the turn, where a deny lets it carry on, so this is the call to revisit if Esc-denies is wanted.
- **The answer is its exact text, not rendered Markdown**: the transcript's truth is the deltas joined, and rendering is a later, deliberate step (now in ROADMAP's unspecced ideas).
- **No digit shortcuts on the question card** (the design's 1–N): with the free-answer field focused they would take the user's typing. ⏎ answers.
- **A multi-select answer is the picked labels joined with ", "**, one `answer` line per question; the user's own words win over picked options.
- **The session starts at launch, in the home folder** (1b's default), and **closing the window stops it** by escalation.
- **No UI to set the binary override yet**: the not-found screen gives the hint and Try again; the override is the `binaryOverride` default until a chooser lands (in ROADMAP's unspecced ideas).
- **The session's `Card` stays in its module's `Components/`** -- one module uses it; it moves to `styles` when a second does (ADR 0002).
- **Coverage instrumentation turned off, a 1a defect fixed on the way**: Xcode 27 resolved `ENABLE_CODE_COVERAGE = YES` for every build through the scheme, so even the Release `Apogee.app` and its frameworks carried coverage counters (`__llvm_prf`) and wrote a `default.profraw` into whatever folder it ran from -- found when the live probe left one beside it. The project now sets `ENABLE_CODE_COVERAGE`, `CLANG_ENABLE_CODE_COVERAGE` and `CLANG_COVERAGE_MAPPING` to `NO` and the shared scheme's test action `codeCoverageEnabled = NO`; the Release app and every framework were checked with `otool` and carry none.
- **A defaults-domain bug fixed on the way**: `Preferences.standard()` had asked for a suite named for the app's own bundle identifier, which `UserDefaults` refuses and keeps nothing in -- caught by the live probe below; it uses the app's standard defaults (domain `com.quantumcompiler.apogee` in the app).

**Verified.** Bare `lib/scripts/cicd.sh --test`: the CLI's 3550 tests and the app's 103 all pass; Release and Debug builds with no Swift warning; the layering check holds the new module, its view-models free of SwiftUI. **A live probe of the built app**: launched with `APOGEE_HOME` pointing at a scratch home on the CLI's mock backend (never `~/.apogee`), the app found the installed `~/.local/bin/apogee` itself and spawned `apogee chat --output-format stream-json --input-format stream-json --tools` in the home folder, with the sandbox inherited; quitting the app ended the child; nothing on stderr once the defaults fix was in.

**Not run: driving the window by hand.** Typing a turn, answering the cards and pressing Stop in the real window against the installed binary is the user's run, recorded as such (`lib/src/darwin/scripts/build.sh open`, or Run in Xcode). The tests drive everything the window does through its view-model; what they cannot see is the drawing.

### 2026-10-20 — `darwin-pipeline` (backlog item 1d): built, tested and packaged beside the CLI

**Why.** The repo's law for a sibling application -- its own `changed.sh` entry, its own pipeline beside the CLI's -- was recorded but never exercised. 1a wired the local half; this is the CI half, so every pull request proves the app still builds, passes its suite and signs.

- [x] **`.github/workflows/darwin.yml`**, the app's own pipeline, on pull requests into `stable` and on request (no `closed`: a merge releases nothing of the app):
  - **`what changed (darwin)`** asks `changed.sh gui-darwin` against the latest release, on the pull request's whole test merge -- the CLI pipeline's question, for the app's inputs; on request everything runs.
  - **`build darwin`**: when the app changed, on GitHub's `xcode-27` image, `lib/scripts/cicd.sh --app darwin --platform macos-arm64 --test --no-defer` -- the one build path a developer runs -- then `scripts/build.sh package`, and the zip kept as the artifact `apogee-gui-darwin`. When it did not, the job runs on a Linux runner with every step skipped and passes under its required name.
- [x] **`scripts/build.sh package`**: the built app and a README zipped as `build/<target>/apogee-gui-darwin.zip` (`Apogee-<version>/`), the signature verified before, and again on the app unpacked from the zip. The README says what the app is, the CLI it needs with the installer's one-liner, and the ad-hoc signature's cost plainly: macOS will not open it with a double-click the first time -- right-click, Open.
- [x] **`changed.sh gui-darwin`** gains the workflow file, so a change to how the app is built is exercised by building it.
- [x] **The CLI's docs and `ci.yml`'s header** no longer call `ci.yml` the only workflow: it is the only one that releases (M8's decision, unchanged), with the app's beside it.

Decisions taken while building, for veto:
- **Job names `what changed (darwin)` and `build darwin`**: the CLI pipeline's style, never a CLI job's name -- a required check is named by its job alone, and two workflows' `what changed` would be one ambiguous check.
- **One job builds, tests and packages**, rather than a build job and a package job handing the bundle between runners: one call into `cicd.sh`, the artifact made where the app was built.
- **The build job's runner is the gate's answer** -- `xcode-27` when the app changed, `ubuntu-24.04` when it did not -- so an unchanged app costs no Mac runner. The CLI pipeline runs its no-op jobs on their own runners; this departs from that for cost, keeping the same required-name guarantee.
- **The runner is `xcode-27`**, GitHub's macOS 27 image with Xcode 27 -- the only image with the macOS 27 SDK the app targets -- and it is a **preview** image (since 2026-07-16): queueing may be slower, and should it change or move to general availability under another label, this one line moves with it.
- **The rehearsal is the same two commands on the dev host**, as the item specified; `make pr-ci` stays the CLI pipeline's.

**Verified.** The job rehearsed on the dev host, command for command: the gate's question against the latest release (v0.1.9, `81b0da7`) answers `gui-darwin=true` for this branch, `true` with `cli=false` for a darwin-only commit (1b's), and `false` for a docs-only range; `cicd.sh --app darwin --platform macos-arm64 --test --no-defer` built the Release app and passed all 103 tests; `build.sh package` made a 428 KB zip holding `Apogee-0.1.9/Apogee.app`, its eight frameworks and the README, the signature verified once unpacked. The workflow parses as YAML. Bare `lib/scripts/cicd.sh --test`: the CLI's 3550 tests and the app's 103 all pass.

**Not run, so the user's:**
- **The first pull request's run.** The workflow has never run on GitHub: a pull request touching the app should start `build darwin` on `xcode-27` and leave `apogee-gui-darwin` on the run; one that does not should pass `build darwin` with its steps skipped. No push was made.
- **The required checks.** Once that run passes, and before the merge: `lib/scripts/required-checks.py --pr <N>` shows `what changed (darwin)` and `build darwin` joining the "Stable: CI must pass" ruleset, and `--apply` sets them -- a repository setting, the user's to change.
- **The artifact launched on a Mac** -- downloaded from the run and opened (right-click, Open) -- the item's manual check.

### 2026-10-20 — a Debug build that would not link, fixed

**What happened.** A Debug build in Xcode failed linking `PresentationApp`: `Preferences.standard()` and `ProcessSpawner`'s symbols undefined, "referenced from: default argument 1/2 of `ApogeeClient.init(…)` in `AppViewModel.o`". Two causes, both fixed:
- **The code** (1b's): `ApogeeClient.init` defaulted `spawner: = ProcessSpawner()` and `preferences: = .standard()`. Swift compiles a default argument into its caller, so `PresentationApp` -- which links `BusinessSession`, not `InfrastructureProcess` or `DataPreferences` -- referenced both, and linked only when Swift's auto-linking happened to add them (it did in `scripts/build.sh`'s clean builds, which is why the suite never saw it). Now `ApogeeClient.init(identity:)` supplies those defaults inside `BusinessSession`, and the full initializer takes every argument; `ReadRunner` the same. `PresentationApp`'s binary references neither module now. The rule went into CLAUDE.md -> Code Style.
- **The environment**: SweetPad's build server had written bare `.swiftmodule` files, from the 12:10 source, into Xcode's shared `Build/Products/Debug`, where they shadow the frameworks' modules -- so every later Xcode build compiled `AppViewModel` against that stale interface. The 36 stale files were moved aside (to the session's scratchpad, not deleted) and the same Debug build, in the same derived data, succeeded. DEVELOPER.md -> Build & test says how to keep it from recurring.

**Verified.** The failing Debug build reproduced in the user's own derived data, then succeeded there after both fixes; `scripts/build.sh test`: 103 tests pass, no warning.


### 2026-10-21 — module schemes and previews (ADR 0004): every module alone, every view seen

**Why.** The user's call of 2026-10-21: each Presentation module gets its own scheme, and the views it creates can be previewed with `#Preview`. Until then:
- the one shared scheme, `Apogee`, built and tested everything;
- no view of the 122 had a preview;
- every screen in track 2 was drawn by a render harness written by hand in a scratch folder and never kept.

[ADR 0004](../adrs/module-schemes-and-previews.md) records the rule, and this change adopted it whole.

- [x] **19 shared schemes**, one per Presentation module, `styles` and `app` included (`xcshareddata/xcschemes/Presentation<Package>.xcscheme`). Each builds its framework and runs its own test bundle alone, so Cmd-U in it tests that module. `Apogee` stays the one full build: `scripts/build.sh`, CI and the check go through it.
- [x] **274 named `#Preview`s over the 122 views**, every one inside `#if DEBUG`, each named for the state it shows:
  - not read, read, failed, running;
  - a question or a permission waiting;
  - a play narrated or refused;
  - a session ended, failed or paused;
  - a sheet empty and filled.
- [x] **Each module's `Previews/`:**
  - `PreviewSupport.swift`, the fakes:
    - `PreviewRunner` answers each argv by its longest matching prefix;
    - `PreviewJob` and `PreviewJobs` say recorded updates and end as a child does;
    - the session's `PreviewChat` and `PreviewChats` play a recorded stream as the binary paces it: up to its `session`, then to each `question`, ending as a child that exited or failed;
  - `PreviewDocuments.swift`, the recorded fixtures copied byte for byte;
  - `<Module>Previews.swift`, the view-models driven through their public API.

  Each module keeps its own copy, as each test bundle keeps its own scripted support. The media places write their records to a folder under the temporary directory. No preview starts `apogee`, reads `~/.apogee` or the person's folders, or reaches the network.
- [x] **The layering check holds it** (`ModuleMap` now reads every shared scheme). Each rule is pinned on a planted violation:
  - every Presentation module has a scheme named for it, building that target and running that bundle;
  - every top-level view in a module's sources is constructed by a `#Preview` in its own file;
  - previews and `Previews/` files sit wholly inside `#if DEBUG`;
  - `PreviewProvider` appears nowhere;
  - no preview names a real starter or runner (`ApogeeClient`, `ReadRunner`, `JobStarter`, `ProcessSpawner`).
- [x] **Docs:**
  - CLAUDE.md's checklist (a new view has its preview, a new Presentation module its scheme) and its map;
  - the Presentation card's "Seen on its own" and "Changing this layer";
  - DEVELOPER.md's tree, the layering check, "Adding a module" and "Previewing a view".

**Decisions made while building** (2026-10-21, each recorded for veto):
- **Declared dependencies, never reached around.** Where a preview must make another layer's type (a `ReadDocument`, a `ChildExit`, a `ShellViewModel`'s `Preferences`), its target declares that module. Twelve edges joined eight targets, each downward and allowed by ADR 0001; the ADR's Landed note lists them. Swift would have let the types be made through an initializer's parameter with no import. That leaves the link to auto-linking, the cause of 2026-10-20's Debug link failure.
- **A state with no recording has no preview**, rather than an invented one. The ADR's Landed note lists them: compose's failure, a few failed list reads, a website permission, a card with several questions.
- **Documents beyond the two fixture folders,** each cited in a comment beside it:
  - those the module's own tests hold inline;
  - the CLI's pinned `write_file` diff for the permission card's change, from `fs_test.cpp`, since no recorded session carries a diff.

  A trimmed recording counts as a slice: review's empty targets, the store without its voice model.
- **One stream's values were adapted:** compose's rendering preview says scene 2 of 3 to fit its test's three-scene project, from a recorded two-scene stream.

**Verified.**
- `check-previews` (the check's preview rules, scripted) finds nothing across the 19 modules. Each module's scheme builds alone in Debug with no warning.
- Headless runs of the previews' setups against the Debug frameworks reached the states they are named for:
  - a play running and narrated;
  - a question waiting;
  - a session ended, failed and paused;
  - a run training;
  - an ingest running;
  - a refused promotion;
  - the media places' records.

  Offscreen renders drew the library's, the session's, and the tasks', knowledge and training places'.
- `scripts/build.sh test`: 336 cases (331 before, plus five layering tests), no Swift warning. `thisProjectHoldsTheLaw` holds the real project to every new rule.
- Bare `lib/scripts/cicd.sh --test`: exit 0, the CLI's 3,597 tests and the app's 336 cases passing.

### 2026-10-22 — `children-get-the-shell-path` (maintenance item M6): the app's children get the person's shell PATH

**Why.** The user's run, 2026-10-22: a session refused `no backend serves 'claude' (configured backends: GPT-OSS-20B, … Qwen3.8-27B)`, though `claude` worked in their terminal. Launched from the Dock, the app handed every `apogee` child launchd's `PATH` (`/usr/bin:/bin:/usr/sbin:/sbin`). Every vendor CLI their cloud backends run was out of reach:
- `claude` and `codex` in `~/.local/bin`;
- `gemini` in `/opt/homebrew/bin`;
- `ollama` in `/usr/local/bin`.

That was the cost 1b recorded and deferred: "Widening the child's `PATH` is a later, deliberate call." The user read the refusal as a backend rename not mapping. It wasn't; the CLI's M26, shipped the same day, now says why.

- [x] **`ShellPath`, in `InfrastructureProcess`, asks the person's login shell once per launch.**
  - The command is `$SHELL -ilc '/usr/bin/printenv APOGEE_PATH_MARKER; /usr/bin/printenv PATH; /usr/bin/printenv APOGEE_PATH_MARKER'`, with `/bin/zsh` when `$SHELL` is unset. It runs through `LoginShell`, by `posix_spawn` in a session and process group of its own, in `HOME`, its input and errors on `/dev/null`.
  - The marker's value is only in the environment, never in the command text, so a profile that echoes commands can't print it early. Only `PATH` is read, between the marker's first two printings.
  - An answer needs both markers, a non-empty `PATH` and exit status 0. Output is capped at 1 MiB.
  - It has a 5-second deadline. Past it, the shell's whole process group is killed.
- [x] **The fallback** is `~/.local/bin:/opt/homebrew/bin:/usr/local/bin` ahead of the `PATH` the app inherited, `~` expanded and each directory once, in order. It is used for a shell that hangs, fails, isn't there or prints no `PATH`, and before the answer arrives. The reason is said: "didn't answer within 5 seconds", "exited with status 1", "isn't there", "didn't print its PATH", "hasn't answered yet".
- [x] **Every child gets it.** `ProcessSpawner` lays the current `PATH` under each child's own variables (`ChildCommand.environment(over:path:)`) without waiting, so a command's own `PATH` still wins. `capture` goes through `spawn`, and it is the source's only `Process()`.
  - The app's composition (`AppViewModel`) starts the ask before any child. The composition the tests use never starts it.
  - `PresentationApp` now depends on `InfrastructureProcess`, a downward edge, added by hand with no re-sort.
- [x] **Settings' General pane says which `PATH` the app's sessions use,** read only: one directory per line, then whether it came from the shell or the fallback and why. Two new previews (from the shell; the fallback past its deadline). The existing "not read yet" preview covers "Asking your login shell…". Its read runs on its own `.task`, so the deadline never delays the pane's other reads.

**Decisions made while building** (2026-10-22, each recorded for veto):
- **The open calls' defaults, taken:**
  - the interactive login shell (`-ilc`), as editors such as VS Code resolve it, so `.zprofile` and `.zshrc` are both read, with a 5-second deadline;
  - the read-only Settings row.
- **The fallback's directories go ahead of the inherited `PATH`,** in a terminal's order (Homebrew and `~/.local/bin` before `/usr/bin`). Its base is the inherited `PATH`, launchd's from the Dock and the terminal's when started from one.
- **`posix_spawn`, not `Process`.** `Process` can't give the shell a session and process group of its own. A new session also keeps an interactive shell from taking the terminal of an app started from one. Signals are reset, and no file descriptor of the app's leaks in.
- **A successful ask leaves the profile's background jobs alone;** only a shell past its deadline has its group killed. The run counts as finished once the shell has exited and its output closed or both markers arrived, so a background job holding the output open doesn't stall it.
- **The road not taken stays recorded:** the CLI searching well-known directories itself would guess, and miss nvm's or a version manager's directories.

**Verified.**
- **17 new cases:**
  - `ShellPathTests` (13):
    - the user's case, a profile printing around the markers with a decoy marker-like string;
    - a child through the real `ProcessSpawner` carrying the answer under its own variables;
    - a command's own `PATH` winning;
    - a shell that never answers cut at the deadline, it and its background child gone, children given the fallback;
    - a shell that exits non-zero, no shell at all, and one that exits 0 printing nothing;
    - input closed;
    - a child before the answer given the fallback, and one after the answer;
    - asked once, and never begun meaning never asked;
    - the fallback's shape, and the defaults.
  - `ChildCommandTests` +1, `SettingsViewModelTests` +2 (the shell and fallback wording), `AppViewModelTests` +1 (Settings given the resolver; building the view-models never starts the shell).
- **Live check,** under `env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin`: the real `/bin/zsh` answered in 0.83 s with 17 directories. Under launchd's `PATH`, `command -v` found none of `claude`, `codex`, `gemini` or `ollama`. Through `ProcessSpawner()` it found all four. Nothing was run but the lookups.
- **Runs:** `InfrastructureProcessTests` passed 34 of 34, six runs. The `PresentationSettings` and `PresentationApp` schemes passed 10 of 10 and 4 of 4. `build.sh test` passed 400 of 400, three runs. One early flake, the never-answers test missing a 0.5-second deadline under load, was fixed by a 2-second test deadline, with its pid written first.
- `check-previews` finds nothing.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,848 tests and the macOS app's 400 cases passed, with no Swift warning.

**Not done:**
- **csh and tcsh** refuse `-ilc` (their `-l` must stand alone), so those users get the fallback.
- **The design canvas** (`lib/design/spec`) doesn't yet show General's new row.
- **The app run from the Dock against the real binary** (`check` finding all four CLIs) is the user's run: it needs the real `apogee` against `~/.apogee`. The live harness check stands in for it.

### 2026-10-22 — `resumed-chat-shows-its-turns` (maintenance item M7): a resumed chat shows its earlier turns

**Why.** The user: "When we reload a chat, I want the previous turns to show." A reopened conversation drew only `resumed: 5 earlier turns of this conversation aren't shown here -- apogee keeps them, and the model still has them`. The transcript said the turns existed because the CLI handed a driver no read of them. The CLI's M27, shipped the same day, added that read: `chats show <id> --output-format json`.

- [x] **`SavedChat`** (`business/reads`): `chats show`'s document typed, every field optional, unknown fields tolerated.
- [x] **A resuming session reads its turns.** On `start()` (first open, Try again, Start again), a session with a conversation id starts `chats show <id>` and the child together. Before drawing the child's first event, it waits for the read and hands the turns to the transcript.
  - Live events wait in their stream meanwhile, and the input stays on "Starting" until the session event opens it. So whichever comes first, the earlier turns stand above the live ones, and the notice is never drawn when the read succeeds.
  - A session the app paused and carried on, or restarted for its settings, reads nothing.
  - `Transcript.earlier` folds only into an empty transcript.
  - The Sessions library passes its runner to every session, and a new session it later identifies reads its turns back when started again.
- [x] **The earlier turns are drawn as live ones:**
  - the user's text, the answer, and tools as folded activity lines (`[tool] read_file {"path":…} -- returned 30 bytes`);
  - a compaction as a notice where it happened ("history compacted; what came before is this summary:");
  - an unanswered turn as "no answer kept: the turn was cancelled or failed", the CLI text view's words;
  - attachments as quiet notices, one heading the turns ("attached to the chat: …") and one after the turn each rode.

  A divider, "earlier in this conversation" with an up-arrow, stands where the live session takes over, and live turns number on from the session's `nextTurn`.
- [x] **An older binary keeps the notice.** Where the read is refused or prints no chat, the notice reads exactly as before.
- [x] **ADR 0003:** `chats show` is recorded on the `chats` row's note (Sessions, plus the session window) and pinned by a test, since the table classifies top-level words. The parity fixture's refresh against the branch's binary left it unchanged (29 words).
- [x] **Recorded from the branch's binary** over a sandbox home in scratch, the home written `/Users/someone/.apogee`. A driven `chat --tools` was run over stream-json, hello first as the app runs it, with two mock backends and an unreachable one. It made every shape asked for and one more:
  - a compaction, a plain turn, a `read_file` tool call, an attachment, and a failed turn (`answered: false`);
  - `chats show`'s JSON and text (`chats-show.json`, `.txt`), `chats list`, the resumed session's opening (`resumed-chat.jsonl`, `next_turn` 6, one live turn 6);
  - a pre-M27 build's refusal of the read (`chats-show-refused.err`).

  The previews embed them byte for byte.

**Decisions made while building** (2026-10-22, each recorded for veto):
- **The open calls' defaults, taken:** every earlier turn is drawn as a live one, with no paging, tools folded and a compaction a notice where it happened.
- **The read is held before the first live event,** rather than spliced in later. The live path is unchanged, and no row already drawn is moved or removed. The wait has no deadline of its own beyond the read's 60-second limit; `chats show` answers in milliseconds.
- **The divider stands where the live session takes over,** as the item's seam put it, with an up-arrow so it reads as pointing at the turns above.
- **Answers are plain text, live and earlier alike.** The app draws no markdown in a live answer, and earlier answers share that row, so `**bold**` shows literally in both. Rendering markdown would change every live answer and is its own item.
- **Attachments are quiet notices,** not chips in the user's card, which would have changed the user row everywhere.
- **Two new row kinds,** `.unanswered` and `.divider`. The history does not say whether an unanswered turn was cancelled or failed, so it is neither the "stopped" row nor the failed-turn card.

**Verified.**
- 17 new cases:

  | Suite | Cases |
  |---|---|
  | `SavedChatTests` (6) | `recorded`, `numberedAsTheSessionNumbers`, `textFormAgrees`, `theRead`, `tolerant`, `notAChat` |
  | `TranscriptTests` (+3) | `earlierThenLive` (the reducer over the recorded read and stream, live turn 6, no notice), `olderBinaryNotice`, `earlierOnlyIntoAnEmptyTranscript` |
  | `ResumedSessionTests` (6) | `readFirst`, `sessionFirst` (status held at connecting with no rows until the read answers), `olderBinary`, `unreadable`, `pausedAndCarriedOn` (reads once), `newThenStartedAgain` |
  | `SessionLibraryTests` (+1) | `openedChatReadsItsTurns` |
  | `ParityTests` (+1) | `chatsShowPlaced` |
- **Module schemes:** `PresentationSession` passed 42 of 42, `PresentationSessions` 25 of 25, `PresentationShell` 19 of 19, and `BusinessReadsTests` with the layering check 64 of 64.
- **`check-previews`** finds nothing.
- **A live harness over the real binaries** drew the earlier turns, the divider and live turn 7 on the M27 build, and the notice on the older one.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,859 tests and the macOS app's 417 cases passed, with no Swift warning.

**Not done:** markdown in answers, live or earlier.

### 2026-10-22 — `markdown-in-chat-answers` (maintenance item M8): chat answers render their markdown

**Why.** The app drew every chat answer as plain text, live and resumed. The answer row was a `Text(answer.text)` (`session/Views/TranscriptRow.swift:25–31`), and SwiftUI parses markdown only from a string literal, never from a `String` value. So `**bold**`, inline code, lists and code fences showed raw. Nothing in the app rendered markdown. The design draws answers formatted (`lib/design/spec/html/main.html:300–345`): prose, inline code, and a code block under a caption with Copy. M7, which draws resumed turns through the same row, named the gap.

- [x] **`MarkdownText`, a shared component in `styles`, over a Foundation-only model** (`Components/MarkdownDocument.swift`). The model runs `AttributedString(markdown:)` with its full syntax and groups the runs into a tree by their `PresentationIntent` components; that grouping is its only parsing of its own. The blocks it yields:
  - paragraph, heading (`#` h1, `##` h2, deeper h3), and list with its items, keeping written numbers;
  - quote, code (its language or "code" as the caption), rule, and table with column alignments, short rows padded;
  - runs in no block (HTML) kept as written.

  Inline: emphasis, strong, code, strikethrough, link and image.
- [x] **Drawn in the design's terms:**
  - **Inline code:** mono on `Palette.control`, rounded through a `TextRenderer`, used only on text with code or links, so a plain answer is pixel-identical to before in both themes.
  - **Code blocks:** bordered on `Palette.code`, under the language caption and a **copy** button that says "copied".
  - **Links and images:** links in the text colour, underlined in `line.strong`. An image is drawn as its alt text and address, never fetched.
  - **Selection** works within each block.
- [x] **Links follow the system only for `http`, `https` and `mailto`.** Every other scheme, `file:` included, is drawn but not opened. A small bubble above a link shows its address on hover, adding "not opened" where it isn't followed.
- [x] **Streaming.** The answer row draws `MarkdownText(answer.text, streaming: !answer.finished)`. The parse runs in an `Equatable` child, so once per redraw, not per delta, and once for a finished answer.
  - **At the stream's edge:** an unclosed fence draws as a code block in progress, never its fence line raw, and an unclosed `**` or backtick shows literally until it closes.
  - **Two edge rules, applied only while streaming:** a last line of markers alone waits for the rest of its line (else `Text\n-` flashes as a heading), and a last paragraph opening with `|` waits for its delimiter row.
- [x] **Recorded from the branch's binary,** over a sandbox home in scratch, the home written `/Users/someone`.
  - A driven `chat`, launched as the app's `ChatLaunch` launches it, with the scripted mock answering the design's asyncio answer in markdown. The answer carries every shape, plus an ordered list, a quote, a rule, and `mailto:` and `file:` links. It streamed as 150 deltas of 8 bytes (`markdown-answer.jsonl`).
  - Its `chats show` (`chats-show-markdown.json`).
  - The script asserts the deltas join to the scripted text.
- [x] **Previews:** `styles` gained its `Previews/`, with 5 previews of the component plus one more (a table's header waiting for its delimiter row). The session has 3 new row previews: a full answer, a resumed one, and one mid-stream with an open fence.

**Decisions made while building** (2026-10-22, each recorded for veto):
- **The open calls' defaults, taken:** no syntax colouring; the user's own words unrendered; the answer re-parsed per redraw, with no caching.
- **No caching, by measure.** One parse took 0.41–0.49 ms for the recorded 1.2 KB answer, 4.3–4.5 ms at 12 KB and 18.6–18.9 ms at 48 KB (Release). 1,500 re-parses of the 12 KB answer averaged 2.15 ms, a quarter of a 60 Hz frame. Foundation's own parse is about 90% of each figure, and caching blocks would mean splitting the source before Foundation reads it, the hand-rolled parser the item ruled out.
- **A single newline stays a line break,** unlike CommonMark, as the CLI's terminal renderer keeps it, so a plain answer reads as written.
- **The design's line heights are not applied,** matching the rest of the app. Its 10 and 14 point gaps map to `Spacing.m` and `Spacing.s`.
- **Two files, not one,** so the model being SwiftUI-free is structural.

**Verified.**
- 12 new cases:
  - `MarkdownTests` (11):
    - `recordedAnswer`, `readBack` (M7's read-back answers render too);
    - `streamedInPieces`: each of the 150 recorded prefixes keeps blocks that never change once settled, and no closed marker raw;
    - `openFence`, `edge`, `plainAnswers`, `structure`;
    - `linkSchemes`, `linkClicks`, `imageNeverFetched`, `copyCode`. File links and images are checked through an injected opener, with network requests recorded beside a control fetch.
  - `TranscriptTests` +1: `markdownLiveAndReadBack`.
- Three mutants were each caught by at least one test: no held-back line, no table wait, and a soft break as a space.
- The `PresentationStyles` scheme passed 16 of 16, `PresentationSession` 43 of 43, and `build.sh test` 429 of 429. The layering check passes, and `check-previews` finds nothing. The project file is unchanged.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,859 tests and the macOS app's 429 cases passed, with no Swift warning.

**Not done:**
- Hover with a real pointer is the user's live pass. SwiftUI ignores synthetic mouse events, and the bubble's placement was checked on a scratch copy instead.
- Syntax colouring: the design's `code.*` tokens are not in `Palette`.
- Markdown in Review, Knowledge and Tasks, which can reuse the component.

### 2026-10-22 — `code-syntax-colouring` (maintenance item M9): code blocks coloured by syntax

**Why.** M8 drew a chat answer's fenced code in one colour (`MarkdownText`'s `Text(verbatim: code.text)`), leaving out the design's five code colours by its own open call. The design colours code with `code.keyword`, `code.string`, `code.comment`, `code.number` and `code.function` (`json/tokens.json`; the prototype's `.k .s .c .n .f`, `main.html:18`). It colours a diff's added lines `status.accent` and its context `text-tertiary` (`main.html:414–424`). The app's `Palette` carried none of the five. Every pair clears 4.5:1 on `surface.code` in each theme; the lowest is `code.string` in light, at 4.81:1.

- [x] **The five code colours join `Palette`** as `ColorToken`s with the design's light, dark and contrast values, in `Palette.tokens`, pinned to the design. `ColorToken` gained an internal `nsColor` for the code block's build.
- [x] **`CodeLexer`** (`styles/Components/CodeLexer.swift`, Foundation only) is one left-to-right pass over the code's UTF-8 bytes, producing spans (kind and range). Each language is one row of a table: keywords, line and block comment markers (nesting in Swift, Rust and Kotlin), string forms, definition words, and whether a call names a function. It finds:
  - **keywords,** with the literals (`None`, `true`, `nil`). A word after `.` is a member and never a keyword (`promise.catch(`), except Rust's `.await` and Swift's `.self`, `.init` and `.Type`.
  - **comments.** In shell and YAML, `#` starts one only where a word could start, so `$#` and `a#b` are not comments.
  - **strings:**
    - escapes and triple quotes;
    - raw strings (Swift `#"…"#`, Rust `r#"…"#`, C++ `R"x(…)x"`, Go backticks, C# `@""`);
    - JavaScript/TypeScript templates, tagged ones too, their interpolations read to the matching bracket;
    - characters, so a Rust lifetime `'a` is no string;
    - here-documents in shell and Ruby, and SQL/YAML doubled quotes;
    - prefixes (`rb''`, `u8""`).
  - **numbers:** ints, floats, hex, octal and binary, exponents, underscores, suffixes, and versions like `1.2.3`. Never inside a word, and a shell `$1` is no number.
  - **function names:** the word after `def`, `func`, `fn`, `function`, `fun` (SQL's `function`, `procedure`) on its line, past qualifiers such as `def self.name`; a word directly before `(`; Rust's `name!(` macros.
- [x] **Languages:**
  - python (`py`, `python3`, `py3`), swift;
  - javascript (`js`, `jsx`, `mjs`, `cjs`), typescript (`ts`, `tsx`, `mts`, `cts`);
  - c (`h`), cpp (`c++`, `cc`, `cxx`, `hpp`, `hxx`, `hh`), objc (`objective-c`, `obj-c`, `objc++`, `mm`, …);
  - rust (`rs`), go (`golang`), java, kotlin (`kt`, `kts`), csharp (`cs`, `c#`), ruby (`rb`);
  - shell (`sh`, `bash`, `zsh`), sql, json, yaml (`yml`), toml;
  - diff (`patch`).

  The language is the info string's first word, in any case (`python:src/x.py` too). Any other, or none, is uncoloured as before.
- [x] **A diff colours by line:**
  - `+` `Palette.accent`, `-` `Palette.error`;
  - `@@`, `+++`, `---` and the tool's lines (`diff --git`, `index`, `\ No newline`) `textTertiary`;
  - context `textTertiary`, as the design draws it.
- [x] **Drawn once per change.** The code `Text` sits in a private child redrawn only when the code changes, so hover and copy never re-lex. Each span's colour is set over its range in one `NSAttributedString`, five times faster than appending styled runs (6.8 ms against 34 ms at 48 KB). Copy still takes `code.text` alone. Selection's plain-text flavour equals the code.
- [x] **Streaming:** the lexer runs where M8's parse runs, and only the growing block re-lexes. A token is decided only from what precedes it and the rest of its own line, so a finished line never changes colour, and an open string or block comment colours to the edge.

**Decisions made while building** (2026-10-22, each recorded for veto):
- **The open calls' defaults, taken:**
  - the languages, plus the aliases above;
  - no language guessed;
  - a diff coloured by line;
  - inline code uncoloured.
- **A diff's context is `textTertiary`,** as the design's figure draws it. The permission card's diff uses `textSecondary`; switching is one line in `MarkdownText.token(for:)`.
- **`+++` and `---` are known by prefix,** as the permission card knows them, so a removed line whose own text starts with `--` reads as a header.
- **The lexer follows the design's lead:**
  - **Swift argument labels:** a keyword used as one (`sleep(for: …)`, `default: 0`) stays plain. The recording showed `for:` coloured as a keyword without the rule.
  - **Built-in types** that are not reserved words (`str`, `Int`, `i32`, Go's `int`) stay plain, as the design leaves `str` plain. In C, Java and C# they are keywords.
  - **Quotes:** one opens a string only where a word could start, so the apostrophe in `don't` opens nothing, and a one-line string stops at its line's end.
- **Selection's rich-text flavour now carries the colours.** The plain-text flavour equals the code. Stripping colour from the rich text would mean taking over AppKit's copy; it is left as is.

**Verified.**
- **Measured** (optimised build):

  | Block | Lexing | Building the coloured text |
  |---|---|---|
  | 12 KB | 0.19 ms | 1.7 ms |
  | 48 KB | 0.62 ms | 6.8 ms |
  | 200 KB | 2.5 ms | 28 ms |

  A 48 KB block arriving in 8-byte pieces lexes in 0.31 ms on average, 0.67 ms at worst, per redraw. The recorded answer's 2,977 bytes add 0.014 ms to its 0.358 ms parse.
- **Recorded** as M8's was, from the branch's binary over a sandbox home, driven as the app launches a chat, against the scripted mock:
  - `code-answer.jsonl`, 373 deltas: fences in Python, Swift, TypeScript, Rust, bash and JSON, the design's patch as a diff, an unnamed fence of program output, and an Elixir fence for one the lexer does not know;
  - its `chats show` (`chats-show-code.json`).

  The previews embed them byte for byte.
- **20 new cases:**
  - `CodeLexerTests` (17), among them:
    - `designsAnswer`: M8's recorded Python coloured exactly as `main.html` marks it;
    - `everyCut`: every sample cut at every character;
    - `streamedInPieces`: all 373 recorded prefixes;
    - `drawnInItsColours`: each exact token colour found in rendered pixels;
    - `copyTakesThePlainCode`.
  - `StylesTests` +2:
    - `codeTokensMatchTheDesign`;
    - `codeContrast`: every code colour and the three diff colours at least 4.5:1 on `surface.code` in each theme, the WCAG ratio computed in the test.
  - `TranscriptTests` +1: `codeLiveAndReadBack`.
- **Mutation:** 8 of 8 mutants were caught.
- **Runs:** the `PresentationStyles` scheme passed 35 of 35, `PresentationSession` 44 of 44, and `build.sh test` 449 of 449, the layering check's 25 among them. `check-previews` finds nothing.
- **Previews:**
  - code coloured by language, in Graphite Light, Graphite Dark and High Contrast;
  - a diff;
  - a fence open mid-stream with a string open at its edge;
  - the session's code answer, live and read back.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,859 tests and the macOS app's 449 cases passed, with no Swift warning.

**Not done:**
- Selection's rich-text colours, recorded above for the user's call.
- No live pass of the app against the installed binary. The renders were offscreen.

### 2026-10-26 — `session-label-follows-its-chat` (maintenance item M10): a session's window says the chat and the model it is running

**Why.** The user, 2026-10-22, with a screenshot: "I am running a local model and it is not displaying it correctly. It just says claude, instead of the local model name." Their window was headed `claude`, and its inspector named the chat `20261022-214642-9a66`, 0 turns, model and backend `claude`. Meanwhile a Gemma conversation's eighth turn streamed in it. The saved records said `9a66` was `claude` with 0 turns, and the conversation was `20261022-214648-d3e2`, on `Gemma4-26B-Instruct-mixed_4_8`. Both defects reproduced on today's source as failing tests first:
- **The label followed only the opening event.** `Transcript` set `model` from the `session` event alone and ignored each answer's `result.model`. A switch made from the settings sheet (since 4b a held `set` line) left the header and the inspector on the launch model all session (`SessionLabelTests`: `labelFollowsTheAnswer`, `headerFollowsTheSwitch`, `olderBinaryLabelsTheSame`).
- **A new session was pinned to the wrong chat.** Its "listed before" snapshot came from the cached list, not a read after the left session had closed, and the guess took any chat that appeared, empty ones included.
  - **On a binary before the CLI's M28,** which saves an unsent chat with 0 turns, a new session opened from an unsent one was named for the empty chat (`73de` in the recording).
  - **Rename and Delete followed it:** they ran `chats title 73de` and `chats delete 73de`, and a relaunch reopened it (`SessionIdentityTests`, six of seven failing first).

- [x] **The label follows the CLI's word.** A `result` naming a model moves `model`, and the header and the inspector's Model read it. A `set` notice is never read for a name.
- [x] **So do the meter and the monitor.**
  - **The context meter** re-reads the window (`models info <model>`) when a `result` names a different model, since neither `result` nor its `usage` carries one. A failed read shows unknown, a late read for a model no longer answering is let go, and an answer from the same model reads nothing again.
  - **The monitor:** `MachineSession` renames the session's registry entry "Session: <model>" on such a result.
- [x] **A session that names its chat is identified by it at once.** The `session` event's `chatID` (the CLI's M28) is decoded, an unknown field tolerated as before, and `preferences.openChat` records it.
  - **Named versus written.** On M28 a new chat is written only once a turn is answered, so `--resume` of an unsent session's id would be refused. `SessionViewModel` keeps `isSaved` and `savedChatID`, learned from the list, not inferred. Relaunching, reading back, Rename, Delete and the inspector's Saved line use the saved id, and a chat named but unwritten starts afresh on a pause, suite switch or Start again.
- [x] **Without `chat_id`, the guess is made right.** Its snapshot is a fresh `chats list` read in `SessionLibraryViewModel.replace(with:starter:)`, inside the close loop, as 4a left room for. If another opening arrives while the read waits, the loop closes it and reads again, and `make()` runs synchronously after the loop, so only one child is ever live. `identifyNewSession` passes over chats with 0 turns.

**Decisions made while building** (2026-10-26, each recorded for veto):
- **The defaults, taken:**
  - between a switch and the next answer, the header (and the meter) keep the model that last answered, and the CLI's notice stands in the transcript;
  - the inspector's Backend reads the session's own chat row, refreshed after each turn. After the switched turn it says `Gemma4-26B-Instruct`.
- **A `--private` session ignores any `chat_id`,** keeping 4i's `whereItCannot`.
- **The 0-turn skip has one exception:** the older private fallback's promised delete, so an unsent `--no-recall` chat on a pre-52d binary is still deleted.
- **The snapshot is read only for a launch that may need a guess:** new, execute, the older private fallback. A failed read makes no guess. ⌘N and launch now read `chats list` twice.
- **A relaunch reopens only a listed chat,** so an unsent one opens a new session.

**Verified.**
- **Recorded** from the branch's binary over a sandbox home, with mocks `claude` (the default) and `Gemma4-26B-Instruct`, each session driven as the app launches one, the home written `/Users/someone`:
  - `switched-to-local.jsonl`, `new-unsent.jsonl` and `private-unsent.jsonl`;
  - the lists around them;
  - each model's `models info`, the local one over a stand-in GGUF header as 4i made them (8192 tokens).

  **The older shapes are partly hand-made.** The installed binary is M28's own commit, so `switched-to-local-older-handmade.jsonl` and `new-unsent-older-handmade.jsonl` are its recordings with `chat_id` alone removed. The older lists are real, holding the empty chat a pre-M28 binary wrote during M28's probe (`20261025-050011-73de`, 0 turns).
- **24 new cases:**
  - `SessionChatTests` (2);
  - `SessionLabelTests` (5);
  - `SessionNamedChatTests` (3);
  - `SessionIdentityTests` (9), among them `olderBinaryGuessesRight`, `olderBinaryActsOnTheRightChat`, `emptyChatPassedOver` and `namedAtOnce`;
  - `SessionMeterTests` (3);
  - `SessionRegistryLabelTests` (2).
- **Repeat runs** over 10 iterations: `PresentationSession` 840 of 840, `PresentationSessions` 830 of 830 and `BusinessSessionTests` 310 of 310. `check-previews` finds nothing in session or sessions.
- **Mutation:** removing the fresh snapshot, the 0-turn skip, the private delete's exception, the window re-read, its late-read check or failed-read fallback, or the rename each fails its own test.
- **Three new previews:** the header after a switch has answered, the same switch not yet answered, and the inspector on the identified chat.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 834 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- The user's own empty chats from before M28 (`9a66`, `7e1e`) stay listed until they delete them.

---

## Milestone B — The design's screens

**Goal.** The whole of the design (`lib/design/spec`: the 29-screen prototype, its layout and contract boards) built around the shipped session window (track 1's design-completion split, grown 2026-10-20 from that day's spike, the user's call): every place the sidebar names, its states, and the overlays, each drawn from the commands and events the CLI already offers -- and where a CLI growth was missing when the item was specced, on the contract board's stated fallback, upgraded once the growth shipped (track 52, the CLI's Milestone AO). Hermetic tests throughout: scripted reads and streams, no binary, no window.

### 2026-10-20 — `app-shell` (backlog item 1e): the window around every screen

**Why.** The shipped app was one session window; the design is an application around it -- a sidebar of places, a key for every destination, overlays, and a window law. Everything screen-shaped in the split gated on this frame, so it came first: the places, the route, the keys, the overlay and toast scaffolding, with an honest placeholder wherever a screen was not yet built.

- [x] **`PresentationShell`** (`source/presentation/shell/`, a new module): `ViewModels/` -- `Destination`, the design's seven places in its order (Sessions, Models, Suites, Tasks, Code graph, Video, Voice) with their titles, symbols and keys ⌘1–⌘7, and for an unbuilt one a `Placeholder` (what will live there, and the command that does it today); `Overlay` (the system monitor); `KeyMap` -- `KeyChord`s written as the Mac's menus write them (⌃⌥⇧⌘), the shell's `KeyBinding`s, and the design's whole keyboard list (`main.html`'s `set.keys`, twenty lines, verbatim) as `KeyMap.reference`; `ShellViewModel` (SwiftUI-free) -- the `route`, the `overlay`, the sidebar hidden or not, the measured width, the toast, `perform(ShellCommand)`, and `ShellLayout`, the window law as a value: below 900 points the sidebar is a 56-point column of icons and side panes close; hidden is hidden at any width. `Views/` -- `ShellView` (the sidebar beside the place shown, the overlay as a sheet, the toast at the bottom centre for five seconds), `Sidebar` (the session list's slot on top, the places row, Settings and the monitor below; icons only when narrow), `ShellCommands` (a Go menu with every place and ⇧⌘L, and the View menu's Hide Sidebar ⌃⌘S and System Monitor ⌥⌘M), `PlaceholderScreen`, `ScreenHeader` (a screen's 52-point header, for every screen to come), `SessionListPlaceholder`, `SettingsPlaceholder`.
- [x] **The shell knows places, not screens**: `ShellView` takes each place's screen and the sidebar's session list from the composition, so `PresentationApp` mounts a screen and the shell never links it. The shipped session window is the Sessions place's screen.
- [x] **The application** (`PresentationApp`): one `Window` scene (no New Window: a second window would mean a second live session, and it frees ⌘N for New session), floored at 960×640 (`.windowResizability(.contentMinSize)`), with the shell's commands, and a `Settings` scene -- ⌘, its own -- holding the settings placeholder. `AppViewModel` composes the shell over the app's preferences beside the session.
- [x] **Navigation is state**: the route and whether the sidebar is hidden are kept in the app's preferences (`Preferences.route`, `Preferences.sidebarHidden`, in its own defaults domain), so a relaunch opens where the window was left; a remembered place this build does not have opens Sessions. The window's frame is SwiftUI's own restoration.
- [x] **`styles` grown by promotion**: the design's sidebar, inspector, selection, strong-line and three model-kind tokens (`model.local`, `model.api`, `model.subscription`) from `tokens.json`, and the layout sizes the shell draws with (sidebar 256 and 56, header 52, a place 40×34, media content 820).

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The keys are the design's list.** The shell binds every place (⌘1–⌘7), Models library (⇧⌘L), the monitor (⌥⌘M) and the sidebar (⌃⌘S); ⌘, is the Settings scene's own; the keys a screen owns (⌘N, ⇧⌘N, ⇧⌘M, ⌥⌘I, ⇧⌘F) arrive with that screen, and ⌘. was already the session's Stop. System conflicts, resolved: ⌥⌘M is macOS's Minimize All (the Window menu's Option alternate of ⌘M) -- the design's key kept, the app's View menu item takes it; ⌃⌘S is the platform's own Hide Sidebar key; ⌘N would have been WindowGroup's New Window, which the single `Window` scene removes.
- **Compositions live under Video**, as the prototype draws them (its second tab, `mediaTab`), so the places are the design's seven, not eight.
- **Where the window's state lives**: the app's preferences (`UserDefaults`, its own domain), not a file under Application Support -- the design's decision 4 is that the app's state is the app's and `~/.apogee` stays the CLI's, which holds.
- **The window keeps the platform's title bar** (the design's window controls are "the platform's"), the sidebar's buttons below it.
- **The window law, both halves**: the floor holds the window at 960×640, and the collapse is keyed to the measured width, so it engages wherever the system lays the window out narrower than 900 (a split-screen tile, a small display) -- the threshold pinned in the view-model's matrix.
- **The places row fits its seven** in the 256-point sidebar rather than scrolling sideways.
- **Settings is a `Settings` scene**, the design's own native mapping (BuildLayout: "a Settings scene with its own sidebar").

**Verified.** `scripts/build.sh test`: every test case passes (117 in the gate's run), the layering check holding the new module and its edges (`PresentationApp` → `PresentationShell` → `PresentationStyles`, `DataPreferences`; the app embedding `PresentationShell`); the Release app builds, signs and verifies with nine frameworks, with no Swift warning. The new tests: the route matrix over every place with restoration across a fresh view-model, an unknown remembered route, each command, overlays, the window law's threshold (899.5, 900, hidden at any width), the toast's lifetime and newest-wins dismissal, the key map against the design's list (seven places on ⌘1–⌘7 in order, no chord twice, every shell key listed by the design), the placeholders' honesty, and the app's composition. **Drawn offscreen**: the shell rendered through `ImageRenderer` against the Debug frameworks -- the full layout at 1100×700 (light, Models' placeholder, a toast) and the narrow one at 860×640 (dark, icons only, Code graph's placeholder) -- matched the design's frame. Bare `lib/scripts/cicd.sh --test`: the CLI's 3593 tests pass (two skipped, as always: the libass caption burn and pdftotext) and the app's suite passes.

**Not run: looking at the live window.** The dev host's screen was locked through the run, so the built app could be launched but not seen; the window by hand -- the places, the keys, the narrow collapse by resizing -- is the user's check (`lib/src/darwin/scripts/build.sh open`).

### 2026-10-20 — `session-library` (backlog item 1f): the saved sessions, one live at a time

**Why.** The Sessions place was one window on one new session; the design's is a library -- the saved conversations by date, opened, renamed and deleted -- with an inspector beside the open one. Every part of it was ready in the CLI (`chats list`, `chats title`, `chats delete`, `chat --resume`, `models status`); what the app had to bring was the design's two laws: one live chat child, and read back rather than mirror.

- [x] **`PresentationSessions`** (`source/presentation/sessions/`, a new module): `SessionLibraryViewModel` (SwiftUI-free) -- the list as `chats list` last read it, filtered by a part of a name and grouped as the design draws it (Today, Yesterday, Previous 7 days, Earlier; an undated row is Earlier), each row's time as the list shows it (the time today and yesterday, the weekday within the week, the date before that); the open session; `open(.chat(id))` and `newSession()`; `rename` and `delete` as the commands a person types, each read back and said in the toast; the questions it asks first (`Pending`: stop a running turn, delete); and the `Inspector` -- the session's row, the model it named, its backend and kind when the list says, the last turn's tokens (absent is "not reported", never zero), and the roles from `models status`. The views: `SessionList` in the sidebar's slot (search, collapsible date groups, a row's glyph, Rename… and Delete… on its menu, `RenameSheet`), `SessionsScreen` (the open session, the inspector pane at 360 points, the confirmations and the failure alert), `SessionCommands` (New Session ⌘N in place of New, Search Sessions ⇧⌘F, Show Inspector ⌥⌘I).
- [x] **One live chat child.** Opening another session ends the open one's child first -- a turn running in it is asked about ("Stop the turn and switch"), never cut silently -- so exactly one child lives at a time. Leaving the place pauses an idle session: its child ends and its model is freed, the conversation stays on screen with no ending card; mid-turn, the turn finishes first and the session pauses when it ends. Coming back resumes it with `--resume <id>` and reads the list again. The shell tells the library each change of place (`ShellViewModel.routeChanged`); at launch with another place in view no child starts until Sessions is shown.
- [x] **Read back, don't mirror**: the list is read on launch, on coming into view, after every turn's end and after every rename or delete; a failure is the CLI's last stderr line and its exit, word for word. **A new session's id**: the CLI names a chat when its first turn saves it, and the stream does not carry the id, so the library takes the one id that appeared in the list since the session opened -- two at once name none, rather than guess. The open session's id is kept (`Preferences.openChat`), so a relaunch resumes the session left open; one deleted or never saved opens a new one. The inspector's state is kept too (`Preferences.inspectorOpen`).
- [x] **The model glyph** (the design's G8): one neutral glyph per row until the list names the backend (the CLI's 52a, shipped the same day: `chats list` rows carry `backend` and `model`) -- then the backend's kind from `models list`'s `backend_type`: on this machine (`llamacpp`, `mlx`, `ollama-cli`), cloud API, billed (`anthropic`, `openai`, `google`), subscription (`claude-cli`, `codex-cli`, `gemini-cli`); the mock backend or a type this build does not know stays neutral. Read only when some row names a backend.
- [x] **The client core grew** for it: `ChatLaunch.resume` (`--resume <id>`, after the flags); `ChatStarting.openChat(binary:launch:)`, with `openChat(binary:)` the new chat it always was; `LocatedStarter`, so discovery runs once and each session after opens on what it found; `CommandRunning` in `BusinessReads` -- `read` and `command`, the second running `apogee <arguments>` exactly as typed, its `CommandOutcome` (`done`, `failed`, `timedOut`, `couldNotStart`) and a `failure` sentence for it and for a read; typed reads `ChatSummary` (a row without an id skipped, an unparsable stamp unknown), `Field` (a `label: value` record) and `ModelKind`.
- [x] **The session window grew**: `SessionViewModel` takes a launch, knows its `chatID` (the one it resumed, or the one the library identified), tells an `observer` every event, and can `pause()` -- the child ended, the conversation kept, `Transcript.Status.paused` -- and `resume()`, carrying on with `--resume`, or starting afresh when nothing of it was saved. A resumed conversation says its earlier turns exist ("resumed: 3 earlier turns of this conversation aren't shown here -- apogee keeps them, and the model still has them"). The ended card's button is **Start again** (it resumes a saved conversation). `SessionWindow` takes a header accessory -- the inspector's toggle.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The session opened at launch** is the one open when the app last ran (the design's startup "open the last session", read with its decision 4's "open sessions" as the app's own state), else a new one -- not whichever conversation the CLI touched last.
- **Switching mid-turn asks**, so "exactly one child" holds without cutting a turn silently; leaving the *place* mid-turn needs no question, since nothing else opens.
- **A new session is identified by the one id that appears**; two at once (a CLI session saving meanwhile) name none -- the session then resumes nothing if paused, and starts afresh.
- **`openai` counts as a billed API**: the type cannot tell a local OpenAI-compatible server from the vendor's, and saying "billed" of a free server is the safer error than the reverse.
- **List order and grouping** are the prototype's: newest first, the four date groups, each collapsible; a search opens every group.
- **⇧⌘F shows the sidebar** if it was hidden, since it focuses the sidebar's field.

**Found: a gap the board does not list.** No read hands a driver a saved conversation's turns: `chats info` has no `--output-format json` and prints no messages, so a resumed session shows only what is said from the resume on (and says so). A CLI growth -- `chats info <id> --output-format json` carrying the messages -- would let the window show them; it is not built here (the GUI builds no capability of its own) and is the user's to queue.

**Verified.** `scripts/build.sh test`: every test passes (142 cases), the layering check holding the new module (`PresentationSessions` → `PresentationShell`, `PresentationSession`, `BusinessSession`, `BusinessReads` and Data below; the app embedding it and `BusinessReads`), no Swift warning. New: the library's matrix over a scripted runner and scripted sessions -- launch resuming the last open session, launch out of view starting nothing, one child at a time (the open one's `stop` before the next opens), the mid-turn question and both answers, leaving idle (paused at once) and mid-turn (paused at the turn's end), coming back resuming with `--resume`, a new session identified and the ambiguous case naming none, rename and delete as argv with read-back and their failures in the CLI's words, the date groups and search, the glyph fallback and its upgrade, the inspector's fields, a missing binary, a failed read; the session's pause and resume; the resumed notice; the command runner over a shell script (argv as typed, a refusal's last line and exit); the typed reads with and without 52a's fields; `--resume` in the launch. **Against the real binary**: the library driven through the installed v0.1.9 in a throwaway `APOGEE_HOME` on the mock backend (never `~/.apogee`) -- the list read, a session started, its first turn sent and the new chat identified from the list, paused (its child gone), resumed with `--resume` and a second turn read back as 2 turns, renamed, deleted, and a new session in its place. **Drawn offscreen** through an AppKit window (`NSHostingView`, cached to a bitmap -- scroll views draw this way, where `ImageRenderer` leaves them blank): the Sessions place at 1360×760 with five rows across the four groups, the glyphs of three kinds and one neutral, the open session and the inspector, matched the design's layout. Bare `lib/scripts/cicd.sh --test`: the CLI's 3593 tests pass (the two usual skips) and the app's 142 cases pass.

**Not run: the live window by hand** (the screen was locked through the run): clicking rows, the context menu, the confirmations and ⌘N/⇧⌘F/⌥⌘I in the real window are the user's check.

### 2026-10-20 — `session-states` (backlog item 1g): every state of a session, and the input's reach

**Why.** The session window rendered a conversation; the design gives it its whole vocabulary -- a new session's start, a private one, a failed turn you can send again, the excerpts an answer drew on, the chats a turn recalled, a file change shown before it is allowed -- and an input that reaches files, the model's settings and the session's own acts. The CLI's track 52 had shipped the same day (52a's `sources`, recall names and permission diffs; 52d's `detach`, `set`, `compact` and `--private`), so each affordance is built on the board's fallback *and* its upgrade, the build's own `capabilities.accepts` deciding which.

- [x] **The protocol grew** (`DataProtocol`, additively): `ProtocolEvent.Kind.sources` with `Source` (file, pages, lines or times, score, retriever, and `place` -- `pages 41–42`, `line 12`, `0:30–1:05`); `ProtocolEvent.recalled`, a `memory` event's named chats (`RecalledChat`), empty from a build before 52a; `SessionInfo.isPrivate`; `DriverLine.detach`, `.set(RunSettings)` and `.compact`, byte-exact, `set`'s settings in the order the CLI applies them (the model first); `RunSettings` with `changes(to:)` (what differs -- clearing is no change a session can make) and `merging`.
- [x] **The client core grew** (`BusinessSession`): `ChatLaunch.settings` (`-m`, `-s`, `--think`, `--effort` -- `auto` effort being the default, and temperature having no launch flag), `privately` (`--private`) and `noRecall` (`--no-recall`); `MachineSession.detach`, `set`, `compact` and the same on `ChatSession`; `Mentions` -- the `@` being typed at the draft's end (after a space or at the start; an address's `@` is none) and the session folder's entries that complete it, folders marked, hidden ones only when asked.
- [x] **The states**, from events (`PresentationSession`): **new** -- "What are we working on?", the model's tag, Attach a folder, Choose a model, Private session instead, and the keys' hint; **private** -- what is kept said exactly: on a build with `--private` "Nothing of this session is written: no transcript, no recall in later sessions.", on the fallback "Never recalled in later sessions, and deleted when you close it. This apogee writes it until then." (`PrivateMode`); **a failed turn** -- the CLI's words, and **Try again**, which sends the last turn as typed and leaves the draft alone; **recalled** -- "recalled 1 earlier chat and 2 decisions" with each chat a link that opens it (52a; a count alone before it); **sources** -- numbered chips, `[1] report.pdf · pages 41–42`, as the answer cites them; **a file change** on the permission card -- `+5 −1` and Show changes over the unified diff (52a's `detail` on `write_file`/`edit_file`; the path alone before it, the board's G1 fallback); streaming, questions and the ended session as 1c built them.
- [x] **The input's reach**: **attach** -- the paperclip (a file, folder, image, recording or video through the file chooser), a drop on the window, or `/attach`: the protocol's `attach` line, a chip above the input, the outcome in the CLI's notices; **detach** -- a chip's ✕ and `/detach`, only where the build takes `detach` (else the chip offers none, G2's fallback); **`@`** -- the session folder's paths offered as typed, ⏎ completing; **the context meter** in the header -- the last finished turn's `usage.input_tokens` against the model's window (`models info <model>`: a stored set's typed `data.window.tokens` from the CLI's 52e, else the leading count of its `window` field, "8192 tokens (the default)"), warning at 80% in the design's colour, "unknown" with no window and "not reported" with no usage, never guessed (`ContextMeter`); **settings mid-session** -- the sheet (⇧⌘M, `/model`, Choose a model): model (free, or a configured backend), thinking, effort, system prompt, and temperature where the build takes `set`; applied as the `set` line (the CLI says each "-- from the next turn"), or on an older build by restarting the child with the new flags and `--resume`, which the app says ("restarting with model claude, effort high -- this apogee changes settings by restarting the session; the conversation carries on, and a local model loads again"), the board's G5 fallback; **slash commands** -- the app's own acts, each onto a line or command that exists (`SlashCommand`: `/new`, `/private`, `/model`, `/attach`, `/detach`, `/compact`, `/rename`, `/delete`, `/inspector`, `/stop`), offered only where they can run now (`/compact` and `/detach` on a 52d build, `/stop` mid-turn, `/rename` and `/delete` for a saved session), ⏎ running the first.
- [x] **The library's side** (`PresentationSessions`): **New Private Session** (⇧⌘N, the sidebar's eye button, `/private`): `--private` when the last session announced 52d's lines -- nothing to delete, never listed -- else `--no-recall`, the chat identified as a new one is, kept out of the list, and deleted (`chats delete`) when it closes -- switched from, replaced, or the window closed; an "Open now" row in the sidebar while it is open. The models list is read at launch for the sheet's backends, and `models info` of the session's model when it names it, for the meter. The window's acts reach the library (`SessionAction`): rename (the sheet now in the Sessions place), delete, the inspector, opening a recalled chat. A list row's Rename… and Delete… bring the Sessions place into view, where they ask.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **Which build is which** is read from the session's own `capabilities.accepts` (`detach`, `set`, `compact` arrived together in 52d, as `--private` did), never from a version number; before any session has spoken, a private session takes the fallback, which is the honest one.
- **The private fallback is said as what it is** -- written until it closes -- not "never written".
- **The slash set** is the prototype's acts that map onto something that exists; `/help` and the rest of the CLI's terminal commands are not offered.
- **⇧⌘M opens the session settings** (the design's "switch model"), the model field first.
- **The window for the meter** comes from `models info` of the model the session names, as the board says -- 52e's typed window where the document carries one, else the count that leads its `window` line (presentation arithmetic over the CLI's words; anything else is unknown). An API model states none, so its meter says unknown -- the design's own "not reported" state.
- **Try again resends the last turn** rather than retrying the CLI's request: machine mode has no retry line, and a resent turn is what a person does.

**Verified.** `scripts/build.sh test`: 163 cases pass, no Swift warning. New: the growths read and written (sources with each place form and an excerpt with no file skipped, recall names and their absence, `private`, the three lines byte-exact, the settings' difference and merge); the launch's flags; `Mentions` over a temporary folder; the session's states over scripted sessions announcing an old and a 52d build -- attach and its chip, detach and compact only on 52d, settings by `set` and by restart with `--resume` and the said line, Try again, the meter's arithmetic and its unknowns, the slash list's availability and each act, the private words, `@` completion, the recalled and sources rows; the library's private sessions on both builds (`--private` with nothing deleted; `--no-recall`, unlisted, deleted on close) with the meter's window and the sheet's backends, the window read from both document shapes and its unknowns, and the window's acts. **Drawn offscreen**: the new session, the private one (dark), a busy session -- recalled chat, numbered sources, the answer with its tokens, a permission card's `+5 −1` diff, the slash list, two attachment chips, the meter at 83% in the warning colour -- and the settings sheet, each as the design draws it. Bare `lib/scripts/cicd.sh --test`: the CLI's 3593 tests pass (the two usual skips) and the app's 163 cases pass.

**Not run: the live window by hand** (the screen was locked): the file chooser, dropping a file, ⏎ on a suggestion and the sheet in the real window are the user's check; and **against a 52d build**: the installed binary is v0.1.9, so the 52d paths ran against scripted sessions announcing 52d's `accepts`, while the fallbacks are what the installed binary exercises.

### 2026-10-20 — `models-library` (backlog item 1h): the models library and the roles

**Why.** Models was a placeholder; the design's place is the library of everything the CLI runs -- local weights by format, pipelines, voice models, cloud APIs and subscriptions -- with each model's details, the changes a person makes to them, and the roles that say which model does which job. All of it was ready in the CLI (`models list`, `models info`, `models status`, `models delete | rename | repair`, `config set-default*`); two measured caveats shaped the details (the GUI spike): `models info` takes a backend or one stored set, not a bare model name, and its document is display fields -- with the typed `data` (window, cache, bytes, lineage) only since the CLI's 52e, shipped the same day.

- [x] **`PresentationModels`** (`source/presentation/models/`, a new module): `ModelsViewModel` (SwiftUI-free) -- the tabs (All, On this Mac, Cloud & subscriptions, Downloads, Roles, the design's), the rows as `models list` last read them under the CLI's own groups (`ModelGroup`: SafeTensors, GGUF, MLX, Pipelines, Voices, Ollama, Other, then each provider, local first), a model's details as `models info <backend>` reads them, `delete` (asked first, then `models delete <name> --yes`, with `--and-backends` only when chosen -- the CLI's `--yes` never implies it), `rename` (`models rename <name without its level> <new>`), `repair` ("Check its files": its own words in the toast), the roles from `models status` moved through `config set-default` / `set-default-<role>`, each change read back. The views: `ModelsScreen` (the header's tabs, the confirmations, the rename sheet, the failure alert), `ModelTable` (name with kind and roles, format and quantization, on disk, status), `ModelDetails` (the 380-point pane: Use in a new session, More -- Assign a role…, Rename…, Check its files, Show in Finder, Delete… -- the note, the typed facts, the record, the lineage), `RolesView`.
- [x] **The details, honestly**: the record's fields in the CLI's words always; where the document carries 52e's typed `data` -- a stored set's -- **Facts from its files** too: format, on disk, the window (its tokens, `the default` or `context_size`, the trained window), the cache at that window, **memory** (weights plus cache, only when the files state both; else "not known: the files state no cache"), the handle, and the **lineage** step by step back (`quantized from …`, `pulled from …`, an inferred or vanished step said so). A backend's document has no `data`, so its record stands alone -- the board's fallback, which is also the CLI's answer.
- [x] **Typed reads** (`BusinessReads/Models.swift`): `ModelRow` (`models list`'s fields; `kind` from the backend's type; `renameName`, the name `models rename` takes -- `qwen3-8b-Q4KM` is `qwen3-8b`), `ModelInfo` (`fields`, and `Facts` -- `Window`, `Cache`, `Step` -- each `nil` where the format's reader states nothing; `memory`), `RoleAssignment` (the CLI's words, the backend it names, whether it is set or falls back, its verb).
- [x] **Wired in**: the Models place mounts the screen (the shell's placeholder for it gone); Use in a new session opens Sessions on a new session with `-m <backend>` (`SessionLibraryViewModel.newSession(on:)`); the library's binary (`binaryURL`) is the place's, read on launch and again whenever Models comes into view. The Downloads tab is an honest placeholder until the downloads item; `PlaceholderView` is public in the shell for it.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **Grouping is the CLI's own** (`section` and `group` on every row since 36), local groups in its order then the providers, rather than "configured, then stored-unconfigured": `models list` lists what the config runs, and the CLI's grouping is the one the terminal shows.
- **Details are read by the row's backend name**, which `models info` takes for every row it lists; a set's handle appears in its typed facts.
- **Delete offers both answers** -- the files alone, or the files and the backends that use them -- as two buttons, since the confirmation is the person's only chance to choose `--and-backends`.
- **Rename and Delete are offered for local rows only**; a cloud backend is renamed or removed in Settings' backends (its config), not here.
- **"Check its files" is `models repair`** (it re-verifies against the record and fixes only a damaged SafeTensors set in place); the whole-install `check` is Settings' (1p).
- **No "unset" for a role**: no verb clears one; the picker sets, and an unset role says how it falls back in the CLI's words.

**Verified.** `scripts/build.sh test`: 170 cases pass, no Swift warning, the layering check holding `PresentationModels`. New: the library over a scripted `models list` with every kind (GGUF, MLX with its window prose, a pipeline with components and bytes, an API, a subscription) -- groups, tabs, counts, no kind guessed for a pipeline; details before 52e (the record alone, a path for Show in Finder) and with it (the typed facts, memory, the window's words, the lineage, an inferred step); memory unknown when no cache is stated; delete asked, then `--yes`, `--and-backends` only when chosen, read back and said; rename by the name without its level, repair's own words, a refusal in the CLI's words; the roles' words, backends and fallbacks, each verb (`set-default` for chat), nothing run when already set; read on coming into view, a vanished selection closing, the missing-binary and failed-read states. **Drawn offscreen**: the library at 1200×720 -- GGUF, Pipelines, Voices, Anthropic and Claude Code groups, the kinds and roles, a pipeline's and a voice model's bytes -- with a GGUF set's details (the typed facts with memory 9.86 GB at its window, the record in the CLI's words, a three-step lineage with a vanished and an inferred step), and the roles in dark, each as the design draws them. Bare `lib/scripts/cicd.sh --test`: the CLI's 3593 tests pass (the two usual skips) and the app's 170 cases pass.

**Not run by hand**: deleting, renaming and checking a real stored model -- the user's store, never touched by the run -- and the live window (the screen was locked).

### 2026-10-20 — `downloads-jobs` (backlog item 1i): the one supervisor of every long run, and downloads over it

**Why.** The design's long runs -- a download, a clip, a composition, a speech -- each take minutes, each can be stopped, and each reports progress; held per screen, they would be four copies of process code in Presentation. This item built the one supervisor they share, and the first screen on it: Models › Downloads. Progress was stderr text until the CLI's 52b (`--output-format stream-json` on `models pull`, `video generate`, `video compose`, `voice say`), shipped the same day -- so both: the stream where the build announces it, the board's G9 fallback where not.

- [x] **`BusinessJobs`** (`source/business/jobs/`, a new module): `SupervisedJob` (an actor) spawns `apogee <arguments>` in a working folder with its input closed, yields `JobUpdate`s -- each stdout line as a 52b `JobEvent` when the run streams, else as a line the verb printed for people (`said`) -- samples stderr's last line every 250 ms as the busy line (`busy`: shown, never decided on), and ends with `JobEnd` (the exit, the stderr tail, whether it was stopped). **Stop is Ctrl-C's**: an interrupt (SIGINT), terminated after a grace if the child will not go, killed two seconds later. `JobEvent` reads the stream tolerantly -- `job`, `stage`, `bytes` (a total only when known), `ready`, `step`, `frames`, `assembling`, `segment`, `scene`, `notice`, `document`, `error`, and anything else kept whole (`other(type:fields:)`: a task's lifecycle, a turn's events). `JobProgress` folds the updates into what a screen shows -- the stages by index, the bytes and their fraction, the notices, the busy line, the documents, a media run's step, segment, scene and device, and the status (`running`, `stopped` with the started stage marked stopped, `failed` in the stream's own words or stderr's last line, `done`) -- and `resumeCommand`, the command a notice names for carrying on ("the one command that resumes it", in the CLI's words). `JobStarting`/`JobRunning` are what a screen is written against; `JobStarter` is the app's.
- [x] **Infrastructure**: `RunningChild.interrupt()` -- SIGINT, as Ctrl-C at a terminal.
- [x] **Downloads** (`PresentationModels`: `DownloadsViewModel`, `DownloadsView`): the form -- a reference (`owner/repo`, `owner/repo:file.gguf`, `name:tag`), and "Full weights, then convert and register them" with the levels to register (`--safetensors --register-with Q4_K_M,…` in the CLI's order; `--register` alone with none) -- its command shown as typed; each download a card: its stages (started, done, failed, stopped), the bytes as a bar where the total is known and an indeterminate one where it is not, the file moving and its index, the step in a few words (the stream's stage; without it the busy line's words for the step -- downloading, converting, quantizing, registering, checking -- and "working" for anything else, the doc's degrade), Stop, **Run …** for the command the CLI named to carry a stopped or failed chain on (its whole command on the button's help), **Run again** otherwise, Dismiss, and the log (the notices and the stderr tail). A finished pull reads the library back. Only the four streaming verbs get `--output-format stream-json`; a resume command (`models convert`, `models quantize`) runs as typed.
- [x] **Which build streams** is the last session's announcement: `capabilities.events` containing `job` (the session library's `streamsJobs`); until a session has spoken, the fallback.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The fallback recognizes the busy line's step words only** (the doc's default), case-blind, and says "working" otherwise; nothing decides on them.
- **Resume runs the command the CLI names** in its notice, exactly: it is the command a person would copy from the terminal, and the app shows it before it runs. Where none is named, "Run again" runs the same pull, whose finished steps the CLI skips.
- **No `--yes`** on a pull: the unrunnable-architecture warning stops it as the CLI decides, and its words say so; the person can type the flag.
- **The busy line is sampled from the stderr tail** (every 250 ms) rather than streaming stderr: the tail is already kept, and the line is display-only.

**Verified.** `scripts/build.sh test`: every case passes (190 in the gate's run), no Swift warning, the layering check holding `BusinessJobs` (Business over Infrastructure and Data; `PresentationModels` and `PresentationApp` reaching it, the app embedding it). New: the stream's events typed and the unknown kept; a streamed pull's fold (stages, bytes and fraction, a new stage's bytes reset, documents, done); stopped and failed (the stage marked, the stream's words kept over stderr's, the resume command read from a notice, stderr's last line when the stream said nothing); a real child through `JobStarter` -- prose lines and a busy line without the stream, Stop as an interrupt a script traps (exit 130 after its `error` line), a binary that will not start; an interrupt's signal on a real `sleep`; the downloads view-model -- the form's argv, a streamed pull with its library read-back, the fallback's step words and degrade, Stop then Run again then a named resume run as typed (no stream flag on `models quantize`), nothing without a binary. **Drawn offscreen**: the Downloads tab with the form (levels and the command), a running pull at 3.2 GB of 9.8 GB on its second file, and a stopped one with Run models convert…, as the design draws them. Bare `lib/scripts/cicd.sh --test`: its first run failed one of this item's own new tests -- the Stop test interrupted its fake run after a fixed 300 ms, before the script had set its trap under the suite's load (the end came as SIGINT, not the script's exit 130); the test now stops the run once its first line is out, three runs of the suite were green, and the gate's second run passed: the CLI's 3593 tests (the two usual skips) and the app's 182 cases.

**Not run: a real download** -- the run pulls no model (the standing rule); a pull through the app against the installed binary is the user's run.

### 2026-10-20 — `tasks-screens` (backlog item 1j): tasks, from the ledger's own events

**Why.** Tasks was a placeholder; the CLI's whole task surface was ready -- `task run … --output-format stream-json` streams the ledger's transitions one for one, `task list` and `task status` read the rest, and `halt`, `cancel`, `resume` are the controls. The design binds one law on it: tasks never ask, so whatever a tool would ask for is granted up front, and the sheet says so.

- [x] **`PresentationTasks`** (`source/presentation/tasks/`, a new module): `TasksViewModel` (SwiftUI-free) -- the ledger's tasks (`task list`, read on launch and on coming into view, never polled), a task's view (`task status <id>`), the runs the app started followed live through the jobs core (`task run …` and `task resume <id> --output-format stream-json`, in the folder the sheet chose), `halt` (`task halt <id>`: stop after this round) and `cancel` (`task cancel <id>`: stop now), each read back, and time so far on the app's own clock over the ledger's stamps. `TaskRunState` folds a live run's stream: the history `task_started` carries first, then each event's `transition` -- the ledger's sequence one for one -- the plan, each round (`kind`, `outcome`, checks passed, what it denied, its tokens), the checks, the grants a round let through, the answer streaming in the turn now, notices and a turn's error, and `task_finished`'s status and reason. `NewTaskForm` assembles the run exactly as typed: the goal, `--require` per line, `--require-file` per line, `--rounds` clamped to 1–32, `--tools` with `--allow write_file --allow edit_file` and `--allow run_command` only when granted, `-m`, and `--output-format stream-json`. The views: `TasksScreen` (the 300-point list -- a live run listed before the ledger's next read catches it -- and the detail: status, Stop after this round, Stop now, Resume, the plan, the checks, the rounds, the grants, the activity), `NewTaskSheet` (the goal, the checks, the budget, the grants, the model, the folder, "Tasks never ask. A tool that would ask is denied unless it is allowed here, and every use is recorded.", and a warning when no check is given), New task… ⇧⌘T.
- [x] **Typed reads** (`BusinessReads/Tasks.swift`): `TaskSummary` and `TaskView` -- the view `task status` prints and `task_started`/`task_finished` carry: goal, status, reason, plan, rounds used and budget, `Check`s, `Round`s, the conversation, the folder, the grants, the stamps, the process.
- [x] **The jobs core carries it**: a task's stream reaches the screen as `JobEvent.other(type:fields:)` kept whole, so the core needed no task-specific code.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **Grants default off**: the sheet's tools are on (reading needs no grant), writing and commands off until allowed -- the conservative side of the design's "grants up front".
- **The sheet does not offer `--on-question answer:…`**: a declared answer is kept in the ledger and shown by `task status`, and a sheet field for it invites a credential; a question ends the task, as the CLI's default says.
- **Recipes are not in the sheet** (the doc's scope note).
- **The live run's view leads** while it runs; the read takes over once it ends, read back with the list.

**Verified.** `scripts/build.sh test`: every case passes, no Swift warning, the layering check holding `PresentationTasks`. New: the sheet's argv (bare, everything, rounds clamped, no `--tools` without grants); **a recorded real run** -- the installed v0.1.9 on the mock backend, `task run "say hello" --require mock --rounds 2 --output-format stream-json`, its 30 lines kept as a fixture (`tests/presentation/tasks/Fixtures/`, paths made neutral) -- folded to its ledger (history first, `plan_recorded`, three rounds plan/execute/correct, one check passing, `exhausted` with the CLI's reason); a run started from the sheet in its folder, followed live, listed before the read, read back at its end; halt, cancel and resume as their verbs; a task from elsewhere read on focus from the recorded `task status`. **Drawn offscreen**: the Tasks place with two ledger tasks and the live run's plan, check, rounds and controls. Bare `lib/scripts/cicd.sh --test`: the CLI's 3593 tests pass (the two usual skips) and the app's 187 cases pass.

**Not run by hand**: a real task run through the app's window -- the user's run (a task writes files where it is told to).

### 2026-10-20 — `monitor-suites` (backlog item 1k): the system monitor and the suites, honestly

**Why.** Two system-shaped surfaces were placeholders: the monitor overlay (⌥⌘M) and the Suites place. The monitor's read was ready (`system --output-format json`); the suites' members were readable (`config get`, `models status` -- verified by the spike); but "will it fit?" has no read in the CLI (the board's G10, marked Later), so the app works it out from what `models info` and `system` state, unknown wherever they state nothing.

- [x] **`PresentationMonitor`** (`source/presentation/monitor/`, a new module): `MonitorViewModel` (SwiftUI-free) reads `system` every two seconds **while open and only then** (`opened()` starts the poll, `closed()` cancels it; the view's appearance drives both), and each of the app's own children's resident memory -- the open session's child, each running download and task -- by process id from the system (`ProcessMemory.resident`, `proc_pidinfo`), never by parsing `ps`. `MonitorView`: CPU with its utilization, memory used of total, the GPU and what models may use, the store's volume and what the store holds, and the children by name. The shell now takes an overlay's content from the composition (`ShellView`'s `overlay:`), as it takes screens; the monitor's placeholder is gone.
- [x] **`PresentationSuites`** (`source/presentation/suites/`, a new module): `SuitesViewModel` -- the suites (`config get suites`, then `config get suites.<name>` per suite: `role: backend`, a pinned `(window N)`), the default (`models status`'s `suite` field), each member's memory -- **none** for a cloud API or subscription (by its backend type), **known** from 52e's typed `bytes` and `cache` where a document carries them, else the record's `size` and `cache` lines in MiB, else **unknown** -- and the verdict against `system`'s model budget: fits, does not fit, unknown (naming the members with no size), or no budget stated; a backend serving two roles is held once. "Use for new sessions" is `config set-default-suite <name>` (and `off`), read back. `SuitesScreen`: the 280-point list with each suite's verdict, the members table (role, model, weights, cache, window), and the line saying the verdict is worked out here and `chat --suite` reaches its own.
- [x] **The cores grew**: `ProcessMemory` in `InfrastructureProcess`; `processIdentifier` on `MachineSession`/`ChatSession`, `SupervisedJob`/`JobRunning`, and the session, download and task view-models' `children`; `SystemReading` and `Suite` in `BusinessReads`.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **No sidebar system strip**: the design's strip shows the same read, and polling it while the sidebar is open would break the monitor's own row ("every 2 s while the monitor is open"); the sidebar keeps the monitor's button.
- **The GPU's utilization is not shown**: `system` reports none (the budget only), and none is guessed.
- **The record's sizes are read in MiB only** (`size: 4800 MiB`, `cache: 4608 MiB at f16`): any other unit, or no line, is unknown -- presentation arithmetic over the CLI's words, retired if G10's read lands.
- **Suite editing stays the CLI's** (`config add-suite`, `set-suite`): the doc's scope is the existing verbs; the place names the command.

**Verified.** `scripts/build.sh test`: every case passes, no Swift warning, the layering check holding both modules. New: the monitor's poll lifecycle (nothing read while closed, several reads on a 40 ms interval while open, none after closing), the children's memory through an injected reader, a size said as a number ("0 bytes", never "Zero KB"), the missing-binary state; `ProcessMemory` on this test's own process and a gone pid; the suites read through `config get` and `models status`, the fit -- does not fit over the budget, a cloud member holding nothing, an MLX member unknown, a backend in two roles held once and fitting -- 52e's typed facts taken first, MiB the only unit read, `set-default-suite` and `off` read back, no suites and no binary said. **Drawn offscreen**: the monitor in dark over the machine's real `system` document (the M3 Max's CPU, memory, GPU budget and disk) with two children's memory, and the Suites place -- a suite that fits (15.74 GB of 107.52 GB, the members' weights, cache and a pinned window) and one whose MLX member states no size, said unknown. Bare `lib/scripts/cicd.sh --test`: the CLI's 3593 tests pass (the two usual skips) and the app's 196 cases pass.

**Not run by hand**: the monitor over a real machine in the live window (the screen was locked); a suite's verdict against the user's real store.

### 2026-10-20 — `graph-screen` (backlog item 1l): the code graph, from its documents

**Why.** The Code graph place was a placeholder while every read it needed was ready (`graph stats | query | explain`, each with its JSON document) and the layout was the design's own app-side row. Documents in, views out: the screen draws what the reads say, laid out by arithmetic in its view-model, never by embedding the CLI's HTML export.

- [x] **`PresentationGraph`** (`source/presentation/graph/`, a new module): `GraphViewModel` -- the graphs (the binary's own completion of a graph's name, `__complete graph stats ""`: no read lists them), the chosen graph's counts (`graph stats`), a search (`graph query <question> --graph <g>`: the entities named and the neighbourhood around them), a node explained (`graph explain <name> --graph <g>`: degree, where it is written, its neighbours by relation), a walk to a neighbour (explained; asked about first when the picture does not hold it), **Rebuild** (`graph update <g>` for a sourced graph, `graph build <g>` otherwise) and **New graph…** (`graph build <name> --source <folder>`: no model, no key, no network) as supervised jobs, read again after. `GraphLayout`: a seeded Fruchterman–Reingold layout over the query's nodes and edges -- 300 passes, start positions from SplitMix64, scaled into the unit square inside an inset -- the same graph giving the same picture whatever order its nodes came in. The views: `GraphScreen` (the graph picker and counts, Rebuild, the search), `GraphCanvas` (edges on a `Canvas`, each node a button at its place, the question's own nodes outlined, the selection's edges in the accent), `ExplainCard` (the 300-point pane: the node, its description, its degree, where it is stated, its relations -- "called by", "calls", "defines", "defined in" -- each neighbour a step to walk, a capped relation saying "n of total"), `NewGraphSheet`.
- [x] **Typed reads** (`BusinessReads/Graph.swift`): `GraphNode` (name, type, description, file and line, the query's hop, `short`), `GraphEdge`, `GraphQuery` (edges kept only between nodes the document listed: the caps honoured, never an unbounded walk), `GraphExplain` (with each `Relation`'s heading), `GraphStats`.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The graph list is the binary's completion** (`__complete`, the shells' protocol, answered from live state per the CLI's ADR 0007): `config get graphs` refuses the key, and no read lists the named graphs -- a gap the board does not carry, for the user to queue (a `graph list --output-format json`).
- **The layout is seeded Fruchterman–Reingold** (the doc's default), deterministic under test, over the query's capped node set.
- **A search is the way in**: the place opens on the graph's counts and a search, rather than drawing a whole graph the documents never bound.
- **Communities and the prose graph** stay out (the doc's scope note).

**Verified.** `scripts/build.sh test`: every case passes, no Swift warning, the layering check holding `PresentationGraph`. New, over **the real binary's documents** -- a four-file Python tree built into a named graph in a throwaway `APOGEE_HOME` (`graph build demo --source …`: 16 nodes, 21 edges, no model), its `stats`, `query`, `explain`, `neighbors` and `show` documents kept as fixtures (`tests/presentation/graph/Fixtures/`, paths made neutral): the graphs from the completion (a description after a tab dropped), the counts, a search's eight entities with the edges between them laid out, `Runner` explained (4 in, 1 out; called by, defined in, defines; stated at `ingest/runner.py:4`), a walk inside the picture (no new query) and outside it (asked about, then explained), Rebuild as `graph update` read back, a new graph as `graph build --source`, no graph said; the layout -- deterministic whatever the node order, a different seed a different picture, inside its inset, no two nodes on top of each other. **Drawn offscreen** over the same fixtures: the graph picker and counts (16 nodes, 21 edges, 4 files), the query's eight nodes laid out with the question's own outlined and `Runner`'s edges in the accent, and its explain card -- called by `main`, defined in `runner.py`, defines `__init__`, `_fetch`, `run_batch` -- as the design draws them. Bare `lib/scripts/cicd.sh --test`: the CLI's 3593 tests pass (the two usual skips) and the app's 202 cases pass.

**Not run by hand**: a graph of a real repository in the live window.

### 2026-10-20 — `video-clips` (backlog item 1m): the clips library and the generate flow

**Why.** Video was a placeholder; the CLI generates and iterates clips (`video generate` with `--from`, `--extend`, `--draft`, `--final`, `--lora`, `--no-lora`, 49c–49e) and writes each clip's record beside it. The design makes the library the records themselves -- `~/Movies/Apogee` and the folders the person adds, nothing written over -- the runs supervised, and the runtime's install the app's question, never its assumption.

- [x] **`BusinessMedia`** (`source/business/media/`, a new module): `ClipRecord` (the `video.generation` document: status, output, sidecar, model, prompt, seed image and mode, seed and its source, the settings, codec, the run's seconds, the parent's move and clip, the preset, the LoRAs, the error) and `SpeechRecord` (the `voice.speech` document, its sentences laid onto the file's own clock: each segment's offset plus the sentence's; `sentence(at:)`); `MediaLibrary` -- `scan`, a shallow walk of the folders for `*.json` records of an object, newest first, each whose output is still there, a record whose file is gone or that is not JSON skipped and said; `freePath` (a taken name gets a number, its record's name counted too: nothing is written over); `stem` (a file name from a prompt's first words); `chain` (a clip's parents, back to the first); `installCommand` (the `train setup --with …` a refusal names). `ClipForm` assembles `video generate` exactly as typed: a new clip (a prompt or a seed image), `--from` a record (the seed held) or `--extend` a clip, `-o`, `--image`, `-m`, `--seed`, `--duration`, `--resolution`, `--steps` (none under a preset), `--fps`, `--draft`/`--final`, each `--lora`, or `--no-lora`.
- [x] **`PresentationVideo`** (`source/presentation/video/`, a new module): `VideoViewModel` -- the library (read on coming into view and after every run), the folders (`~/Movies/Apogee` and those added, kept in `Preferences.videoFolders`), a clip's page, the generate sheet (its command and its free output shown before it runs), the run supervised through the jobs core (`--output-format stream-json` on a 52b build: the step of steps; else the narration's last line, shown), Stop as Ctrl-C ("nothing was written"), and **the install consent**: a run refused because the video runtime is not installed names `apogee train setup --with video`; the app asks -- the CLI's own words, what installing does, the command -- and only on Install runs it, then the refused run again; Not now installs nothing. The views: `VideoScreen` (Clips | Compositions, the design's tabs; compositions are the next item's), `ClipsView` (the 264-point list, the run row, records skipped and why, Add a folder…), `ClipPage` (the player, the prompt, Run again…, Continue it…, Make the final for a draft, Show in Finder, the record's facts, "made from"), `ClipSheet`, `ConsentSheet`.
- [x] **Preferences** keep the media folders added (`videoFolders`, `voiceFolders`: paths one a line).
- [x] **`styles`' `MediaPlayer`**: a clip plays through `AVPlayerView` itself. Drawing the clip page offscreen found that SwiftUI's `VideoPlayer` links only its SwiftUI shim (`_AVKit_SwiftUI`), never AVKit -- whose `AVPlayerView` the shim subclasses -- so the class was missing at run time and the page crashed its process ("failed to demangle superclass of VideoPlayerView"); in the app, the first clip page would have crashed. Naming the view links AVKit into `styles`, every media module plays through it, and a styles test holds `AVPlayerView` loadable.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The records scan is a shallow walk for `*.json` sidecars, on focus and after runs** (the doc's default): no file-system watching yet.
- **The library's folder for new clips is `~/Movies/Apogee`**, created when the first clip is made; added folders are read, never written.
- **The consent is asked when a run is refused for its runtime**, from the refusal's own words naming the command -- the app does not probe the runtime ahead of time.
- **A clip's player is the system's** (`VideoPlayer`), under the design's controls only as far as the system's go.

**Verified.** `scripts/build.sh test`: every case passes, no Swift warning, the layering check holding `BusinessMedia` and `PresentationVideo`. New: the scan over fixture folders (newest first, a chain linked, a gone clip and a broken record skipped and said, a speech record not counted as a clip); free names (numbered past a taken file and a taken record); every documented flag combination argv-exact (new, everything, a preset dropping `--steps`, `--no-lora`, `--from` with `--final`, `--extend` with a new prompt); a speech's sentence clock; a clip generated into a free name, supervised, read back and selected; a 52b run's step and Stop; the consent -- refused, asked, nothing installed before yes, installed, the refused run again; run again and continue from a record; folders added and remembered. **Drawn offscreen**: the Video place over three fixture clips (a draft, its final made from it, another), the final's page with the player, its facts and "made from", and the Run again sheet with its `--from … --final` command and free output -- after the player fix, with no AVKit linked by the drawing harness, which is the proof the app loads it. Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips: the libass caption burn, pdftotext) and the app's 213 cases. Two earlier runs failed on the CLI side alone and were not this item's: a PTY timing case under load (green three times alone), then sixteen e2e lifecycle cases whose temporary homes a concurrent run of the suite in the same build tree removed mid-test.

**Not run: a real clip.** No model is pulled or run by the run (the standing rule); generating through the app against the installed binary and its pipeline is the user's run.

### 2026-10-20 — `compose-screens` (backlog item 1n): a brief to a video, edited by instruction

**Why.** Compositions were a placeholder under Video. The composer and its whole edit vocabulary are the CLI's (`video compose` with `--from`, `--edit`, `--redo-scene`, `--voice`, `-m`, `--music`, `--no-captions`, 50c/50d); the project document beside the output, saved after every step, is the progress; and two fallbacks stood until the CLI's 52c (shipped the same day): a new brief planned and rendered in one run with Stop (G12), and an edit's diff by comparing the documents (G13).

- [x] **`PresentationCompose`** (`source/presentation/compose/`, a new module): `ComposeViewModel` -- the compositions (the `video.project` records in the video library's folders, listed even before their video exists), a composition's page, **the project document as the progress** (while a composition renders, the document beside it re-read every second -- its scenes spoken and rendered as the composer saved them, never an accounting of the app's), Stop (every finished scene stands), **Resume** and **Render it** (the composition's last command again, which renders only what the project lacks; for one the app did not start, its brief and output), the new sheet (brief, your stills and clips, music, captions, voice, pipeline; **Plan first** where the build plans alone -- 52c's `--dry-run` on a brief -- else one run with Stop, said), and **edit by instruction** (the instruction and the scenes to render again on fresh seeds; **Preview the changes** runs the edit's `--dry-run --output-format json` and draws its diff; **Render** runs the same command without it). `ComposeView`: the 264-point list (status, scenes made, EDIT n), the page (the video, the brief, the run's scene and step, Stop, Resume, Edit by instruction…, Show in Finder, the diff, the scene cards, the captions said honestly -- burned in, or kept beside it when this ffmpeg can't), `ComposeSheet`.
- [x] **`BusinessMedia` grew**: `ProjectRecord` (status, brief, title, output, the scenes -- narration, visual, seconds, asset, seed, the voice and clip files, and since 52c `from`, `changes`, `moved` -- captions, parent, edits, `removed`, error), `ProjectRecord.beside(_:)`, `MediaLibrary.projects`, `MediaLibrary.diff` (**52c's own comparison** where the edited project carries it -- kept, changed with what, new, removed, moved -- else **the documents compared scene by scene in order, said as a comparison**: before 52c the scenes do not name their source), and `ComposeForm` (`video compose` exactly as typed: a new brief with its options, or `--from`, `--edit`, each `--redo-scene` in order; `--dry-run` when asked).

**Decisions made while building** (2026-10-20, each recorded for veto):
- **52c's presence is read from the session's schema date** (2026-10-20 or later: the whole of track 52), the same release that brought 52b's stream.
- **An edit is always previewable**: `--dry-run` with `--from` predates 52c, so every build previews; only the diff's source differs.
- **The project document is polled once a second while a run is live**, and only then -- no file-system watching (the media scan's own default).
- **A scene card's fields** are the prototype's that the document carries: narration, what it shows (or its asset), spoken, rendered, seed.
- **`PresentationCompose` and `PresentationTasks` link `InfrastructureProcess` and import it**, as `PresentationMonitor` does: both read `BoundedTail.lastLine` off a job's end, which only the transitive autolink resolved, so nothing ordered the link after that framework's -- a person's build of the app failed to link `PresentationCompose` on `lastLine` (a clean build passed), and a declared link closes the race. A stricter guard was tried and left: Swift's `MemberImportVisibility` flags the same gap at compile time, at the cost of an explicit import in some dozen files.

**Verified.** `scripts/build.sh test`: every case passes, no Swift warning, the layering check holding `PresentationCompose`. New: the project document read (made scenes, captions not burned); 52c's diff from the document (kept, changed with what, new, removed) and the compared fallback (said as compared); every command argv-exact (new with every option and `--dry-run`, an edit with its redo scenes in order, nothing without an instruction or a scene); a stopped composition listed before its video exists; a composition composed with its scenes read as the document moved, stopped with its finished scenes standing; plan first as one run before 52c and as a dry run then the same command after; an edit previewed from its dry run's JSON with its diff, then rendered. **Drawn offscreen**: the Compositions tab over three fixture projects -- a finished one, its edit (EDIT 1, its page with the player and 52c's changes: kept, changed with what, new, removed) and a stopped one (Resume, "2 of 4 scenes made -- the finished scenes stand", the scene cards spoken and rendered or not) -- and both sheets with their commands: the new brief with its voice, and the edit with its instruction, a scene to render again and a free output name beside the taken one. Drawing the stopped page first with a made-up `stopped` status hid Resume; the composer writes `cancelled` (the CLI's `compose.cpp`), which the page reads and words as stopped. Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips) and the app's 221 cases.

**Not run: a real composition** -- no model is run (the standing rule); the user's run.

### 2026-10-20 — `voice-screen` (backlog item 1o): speeches, the say flow, the timings

**Why.** Voice was a placeholder while every part was ready: `voice say … --output-format json` returns the `voice.speech` document with per-sentence timings (50a), the voices come from the voice model's own `models info`, the library is the records in `~/Music/Apogee` and the folders the person adds, and the install is the app's question.

- [x] **`PresentationVoice`** (`source/presentation/voice/`, a new module): `VoiceViewModel` -- the speeches (`MediaLibrary.speeches` over `~/Music/Apogee` and `Preferences.voiceFolders`, read on coming into view and after each run), **the voices as the model lists them** (the store's voice row in `models list`, then its `models info` `voices` field, one language a line -- never a list of the app's), the say sheet (the text, a voice from the model's own by language, the speed; `voice say <text> -o <free name> [--voice …] [--speed …]`, shown before it runs), the run supervised (52b's segment of segments where the build streams it), the install consent (`train setup --with voice`, asked from the refusal's own words, then the refused run again), and **playback by the record's timings**: `position` is the player's clock, `currentSentence` the sentence the document's offsets and lengths say is being spoken, `start(of:)` where a click seeks. `VoiceScreen`: the list, the speech page (Play/Pause on space, the time, the sentences lit as they are spoken and each a seek, Show in Finder), `SaySheet`, the consent sheet.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **Player affordances** are the prototype's that the record supports: play and pause, a sentence clicked to seek; no waveform scrubbing (the document carries no samples).
- **The clock is the system player's** (`AVAudioPlayer.currentTime`, read every 50 ms); the highlight is the document's arithmetic over it, never a re-measure.
- **With no voice model in the store**, the sheet says so and names the pull that brings one.
- **A speech's length** is the record's `seconds` (which `voice say` always writes), else where its last sentence ends -- never a zero and a dangling separator for an older or partial record (found drawing a record without it).
- **Every place is built**: the app's place `switch` lost its placeholder arm (now dead, and a compiler warning); `Destination.placeholder` stays, nil for every place, as the shell's way to stand in a place to come.

**Verified.** `scripts/build.sh test`: every case passes, no Swift warning, the layering check holding `PresentationVoice`. New: the voices from the model's `models info` grouped by language; say as the real command into a free name, read back and selected; the timing cursor on a scripted clock (the first sentence, the second after its offset, nothing past the end) and seeking to a sentence's start; the consent and the refused run again. **Drawn offscreen**: the Voice place over two fixture speeches (voice and length in the list), a speech's page with the clock at 2.4 s lighting the second sentence by the document's timings, the voice and model line, and the say sheet -- the voice picker from the model's `models info`, the speed, the free name beside a taken one, and the command as typed. Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips) and the app's 225 cases (the result bundle's count).

**The app's case counts corrected.** The counts this group's entries recorded (1f to 1n) were read off the test log, which repeats some cases' lines -- each was 6 to 8 high; they now read the result bundle's totals, which equal the suite's `@Test` declarations at each commit.

**Not run: real synthesis** -- no model is run (the standing rule); the user's run.

### 2026-10-20 — `settings-panes` (backlog item 1p): Settings, five panes, and the theme switch

**Why.** Settings was a placeholder scene. The design's window has its panes over verbs the CLI already has -- backends and providers, keys, health with remedies, the gate's levels, the media default -- and an Appearance setting whose three themes supersede 1a's system-following default (the design's call). Two fallbacks stood until the CLI's 52e (shipped the same day): plain config keys read but not set (G15), and no way to forget recall (G16).

- [x] **`PresentationSettings`** (`source/presentation/settings/`, a new module): `SettingsViewModel` -- **General**: the command-line tool's path and version, "Use another apogee…" (the discovery override, from the next launch) and the design's keyboard list; **Appearance**: `ThemeSettings` -- Follow the system, Graphite Light, Graphite Dark, High contrast, and the text size (Small, Medium, Large, Extra large: `tokens.json`'s factors) -- kept in the app's preferences; **Backends & keys**: the configured backends (`models list`), the keys stored (`auth list`: names only), **Add key…** -- the key typed into a secure field, sent as `auth add <provider> --stdin` on the child's standard input, never an argument, and dropped: no property, preference or log keeps it -- and Remove (`auth clear`), the providers on this machine (`providers scan`), health (`check --output-format json`, its document kept even when a row fails) with each failing row's remedy, and **check --fix** and **bootstrap** as supervised jobs; **Video & voice**: the default pipeline (`config set-default-video`, or `off`) and where media goes; **Permissions**: each tool's gate level (`config get permissions`, moved by `config set-permission`), the websites `fetch_url` reaches without asking (`add-allowed-host` / `delete-allowed-host`, their own words the confirmation: this build has no read of the list), the web search address and recall read through `config get` (unset is the default, said so) and set with `config set tools.search.url` / `memory.recall` where the build has 52e -- read-only with the honest hint before it (G15) -- and **Forget every recalled summary** (`chats recall --forget`) only where it exists (G16: hidden before). Each change read back. `SettingsView`: the 220-point pane list and the panes.
- [x] **`styles` grew the theme switch** (ADR 0002's module owns it): `ColorToken` carries the High Contrast value beside Light and Dark (`tokens.json`'s three themes); `Appearance` holds the theme and the text scale (behind a `Mutex`), every token resolving by it -- the system's light or dark when following -- and `Appearance.colorScheme` the window's scheme; `TypeScale.scaled` multiplies every step by the text size, rounded to the half point, and the `Font.apogee*` steps scale with it. The app applies the setting to the window and the Settings scene and draws them again when it changes.
- [x] **The client core grew**: `capture(…, input:)` writes a child's standard input then closes it; `CommandRunning.command(binary:_:input:)` (the app's sends the input; a scripted runner's default ignores it) and `report(binary:_:)` -- a read whose document stands whatever the exit (`check` exits non-zero when a row fails).
- [x] **Gone**: the shell's settings and session-list placeholders.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The panes are the item's five**; the design's Models & roles is the Models place's Roles tab, and its Web access and Memory rows sit in Permissions.
- **The theme is applied by redrawing the window** (the view tree rebuilt when the theme or size changes): a view's own transient state -- an open group in the sidebar -- resets with it.
- **The allowed websites are written blind**: no read lists them in this build (`config get tools.allowed_hosts` refuses the key), so each change shows the CLI's own sentence -- a gap for the user to queue (a read of the list).
- **The binary override takes effect at the next launch**, said so, rather than re-locating mid-session.
- **A key provider's name** is its company's spelling (Anthropic, OpenAI, Google); any other provider reads as the CLI says it.
- **The health pane's two jobs stack** (check --fix, then bootstrap): side by side they truncated at the window's 860-point minimum, found drawing it.

**Verified.** `scripts/build.sh test`: every case passes, no Swift warning, the layering check holding `PresentationSettings`. New: every pane's reads (backends, keyed providers by name, providers, health with a remedy and its summary from a failing check, the gate's levels, unset keys as defaults); **the secret's path** -- `auth add anthropic --stdin` run, the key in no argument, written once to the child's input, held by no property of the view-model (a mirror of it searched) and no preference; each change as its verb, read back; 52e's setters and forget used only where the build has them; check --fix as a supervised job; the theme and size remembered; the styles' theme switch (each theme picks its value, the system's by appearance, the scheme, the text size's half-point rounding); a command's input reaching a real child's stdin (counted, never echoed); a report's document kept on a failing exit. **Drawn offscreen**: all five panes at 900 x 640 over fixture reads -- General (the path, the version, the keyboard list), Appearance, Backends & keys (two backends, the stored key marked billed, OpenAI and Google unset, two providers found, a failing check with its remedy and the two jobs), Video & voice, Permissions (six tools' levels, the allowed-sites field, the web search address and recall read as defaults with the hint) -- and Appearance under High contrast at Extra large, the whole pane in the theme's colors and the bigger steps. Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips) and the app's 236 cases.

**Not run by hand**: adding a real key -- the user's keychain is never touched by the run -- and bootstrap, which installs runtimes over the network: the user's buttons.

### 2026-10-20 — `design-board-sync` (backlog item 1q): the contract board catches up

**Why.** The GUI spike checked the design's contract board (`lib/design/spec/html/BuildContract.dc.html`) against the shipped repo and found the board itself behind: its macOS line said 14 where the shipped decision is 27, two of its four "verify" rows were really gaps the board did not list, and the other two had passed. By the design's own law (`lib/design/spec/CLAUDE.md`), a board change lands in the repo file and the canvas together.

- [x] **macOS 27 stands**: decision 8 reads "macOS 27 or later, on Apple silicon", and the "Calls for you" line on the lowest version reads answered -- the same correction, not a revisit (1a's shipped decision).
- [x] **The verify rows settled** as the probes measured them: *Made-from history* -- `models info`'s fields carry the lineage, as prose: **ready**; *Suites and their members* -- `models status` names the default suite, `config get suites` and `suites.<name>` its members: **ready**; *Memory a model needs at a window* -- the fields are display strings and a SafeTensors set states no window, cache or bytes: **gap G17**; *Forget all session summaries* -- no command forgets them: **gap G16**. The counts follow: 31 ready, 13 the app's own, 17 gaps, 0 assumed ("each checked by probe").
- [x] **The ledger gains G16 and G17**, in the board's own voice, each with its fallback and its owner: G16 *Recall can't be forgotten* (`chats recall --forget`; until then, hide Forget; owned by the CLI's 52e); G17 *A model's details are display strings* (`models info <set>`'s typed `data`; until then, the record's prose, memory at a window unknown; owned by the CLI's 52e).
- [x] **The canvas**: read first -- its `project/BuildContract.dc.html` was byte-identical to the repo's (nobody had edited it since its last publish) -- then the corrected file alone published to its canvas path (the Apogee Desktop canvas, version 32; the other boards and `canvas.json` kept: the layout did not change) and read back identical to the repo's (sha-256 `b70d7f2c…`).

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The board's "Calls for you" line on macOS is marked answered** with the version that stands: it is the same correction as decision 8, and leaving "Recommended: 14" beside it would contradict the board.
- **G16 and G17 are listed as gaps with 52e named as their owner** even though 52e shipped the same day: the item's scope leaves flipping a gap to landed to each CLI ship's own docs pass.
- **The Layout board's "SwiftUI · macOS 14" column heading** (`BuildLayout.dc.html`) is left as it is: outside this item's three corrections ("nothing else on the board moves"); recorded for the user.

**Verified.** The design's own health check (`lib/design/spec/CLAUDE.md`, the node run of every start screen for 30 ticks): 29 start screens, no problems. The board's counts checked against its chips: one of each kind in its legend, then 31 ready, 13 the app's own, 17 gaps and none assumed; the ledger G1 to G17 matches the gaps the map names; its markup balanced (334 `div`s open and closed). The board's text after the edit, read back: the decision line, the call, the four rows' statuses, G16 and G17 with their fallbacks. Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips) and the app's 236 cases.

### 2026-10-20 — `stored-weights-by-handle` (maintenance item M1): every stored set its own row

**Why.** The user ran the built app, and the Models library showed none of the 16 SafeTensors sets and 2 MLX models in their store. The root cause was measured, not guessed: the store's metadata was mirrored into a scratch `APOGEE_HOME`, the installed 0.1.9 listed it with `models list --output-format json`, and the screen was drawn over that document.
- **The CLI's side.** It writes its table's placeholder, `(not configured)`, as the backend of every stored row no backend points at. The handle is in `model`.
- **The GUI's side.** It keyed and named rows by `backend`, so all 18 rows shared one identity. One row drew, then blank space, and the MLX group drew empty.
- **The same name reached every verb.** Info, delete and repair asked the CLI for `(not configured)`, which it refuses. Voice asked `models info "(not configured)"` for its voices, and Settings offered the placeholder as a pipeline.
- **Why tests missed it.** The fixtures behind 1h, 1o and 1p held stored rows hand-written with made-up backend names.

- [x] **`ModelRow` names stored weights by their handle.**
  - `isStored`: the CLI's two markers together, the backend `(not configured)` and the type `-`.
  - `name`: a configured row's backend, or stored weights' handle. `id` is `name`.
  - `renameName`: for stored weights, the handle's directory.
  - `ModelRow.folded(in:)`: the list's own count.
- [x] **The library.**
  - Rows are drawn by name, and details and every verb go by `name`: `models info`, `delete` and `repair <handle>`, `models rename <directory>`.
  - A stored row's status: "not configured" for a GGUF or MLX model a backend could point at; "stored" for a SafeTensors set, pipeline or voice model, which no backend runs; the CLI's own state where it has one ("cannot load").
  - Stored weights get no "Use in a new session" and are no role target.
  - The fold appears in the CLI's sentence, with **Show them** (`models list --all`, held while the place is open) and a way to fold them again.
- [x] **Voice** finds the store's voice model by its handle. **Settings** offers the pipelines' handles and lists configured backends only.
- [x] **The recorded fixtures**: `tests/business/reads/Fixtures/models-stored-{list,list-all,info-set,info-voice}.json`, with paths made neutral.
  - Recorded from the installed binary over a scratch store built to the CLI's own completeness rules, with sparse weights.
  - The store holds two SafeTensors sets, an MLX model converted from a third (which folds), a pipeline, a voice model and a configured mock backend.
  - The Models, Voice and Settings tests read these, and their hand-written stored rows take the CLI's real shape.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **A stored row is recognized by the CLI's two markers together** (the open call's default), pinned by the recorded fixture, so a CLI rewording fails a test rather than the screen. An explicit field (`"configured": false`) would be a CLI item, if wanted.
- **Show them holds while the place is open** (the open call's default): coming back into view folds them again.
- **The status says what stored weights are.** This refines the item's "not configured": no backend ever runs a SafeTensors set, a pipeline or a voice model, so "not configured" would invite a step that does not exist. They read "stored", and a GGUF or MLX model reads "not configured".
- **The fixture's configured row is the mock backend:** no weights and no network. A stored GGUF would need a parsable header, and every stored kind takes the same placeholder (`models.cpp`), so the SafeTensors, MLX, pipeline and voice rows cover the rule.

**Verified.**
- `scripts/build.sh test`: 240 cases (the result bundle's count), no Swift warning. New tests cover:
  - the recorded list: six rows with six names, the five stored ones by handle, a fold of 1, and `--all` listing the consumed set;
  - the library's groups and statuses over it and the roles' targets; the fold said, `--all` read on asking and dropped on coming back;
  - info, delete, repair and rename argv-exact for stored weights, with the placeholder never used as a name;
  - Voice's voice model and voices from the recorded `models info`;
  - Settings' pipelines by handle, with stored weights listed as no backend.
- **Drawn offscreen** over the user's store as mirrored (18 stored rows): all 18 drawn under SafeTensors and MLX, each named by its handle, with "2 snapshots consumed by conversions are folded · Show them" beneath and a set's details read by its handle.
- Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips) and the app's 240 cases.

**Not run:** the app on the user's real store. The screen was drawn offscreen over its mirrored metadata; running it for real is the user's step.

## Milestone C — CLI parity

**Goal.** The GUI does everything the CLI and the TUI do: the user's mandate from their first hands-on run, 2026-10-20, recorded as the SPEC's **Full parity** principle. Track 2 gives it teeth and fills the gaps:
- the parity law and its table (2a);
- the console, which runs any verb (2b);
- the families with no place yet: knowledge and collections, training, agents, MCP and symphonies, execute sessions, and review (2c–2g);
- the model store's own verbs (2h, 2i).

Every screen stays a machine-mode client of the binary, tested hermetically.

### 2026-10-20 — `parity-law-and-table` (backlog item 2a): every command has a place

**Why.** Nothing held the GUI to the CLI's whole surface. The audit against the TUI's own table (`tui_parity.cpp`, the CLI's ADR 0010) found 8 command families with no GUI place and no recorded carve-outs. The TUI never drifted because its law is mechanical, and the GUI had no analog.

- [x] **ADR 0003** (`lib/documentation/gui/adrs/parity.md`): every top-level command the CLI offers a person has one classified place in the GUI: a screen named by its module, the console, a gap named by its queued item, a carve-out with its reason, or plumbing. It is in the house shape and indexed.
- [x] **`ParityTable`** (`source/presentation/shell/ViewModels/Parity.swift`), the GUI's `tui_parity`, as one SwiftUI-free declaration, beside the shell's places.
  - **Today's truth:** 15 screens (chat, chats, models, config, version, tui, system, check, providers, graph, task, auth, video, voice, bootstrap); 9 gaps by item (knowledge and embed 2c; train and datasets 2d; agents, mcp and symphonies 2e; execute 2f; review 2g); the console's 2 (`analyze`, `complete`); 3 carve-outs; 3 plumbing.
  - **The carve-outs**, each with the GUI's own reason: `serve` (a daemon the app would orphan), `reset` (it deletes the data directory the running app stands on), `uninstall` (the app never uninstalls the binary it runs).
  - `ParityTable.refusal(of:)` is the console's refusal; `violations(words:)` is the law over a word list.
- [x] **The fixture**: `tests/presentation/shell/Fixtures/command-words.txt` holds the 29 top-level words `apogee __complete ""` answers, sorted. They are recorded from the installed `apogee 0.1.9 (31b170c)` in a throwaway home by `scripts/refresh-parity-fixture.sh`, beside `capture-fixture.sh`.
- [x] **The checklist** (CLAUDE.md, Implementing a Feature) carries the rule: a CLI command added or removed is classified in the same change.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The word list is a committed fixture refreshed by a script** (the open call's default). No test asks a binary, which keeps it hermetic, like the protocol fixture. A live pass is the refresh script, run against a new CLI.
- **The table lives in `PresentationShell`, and its check in the shell's own tests.** The doc suggested `tests/architecture/`, but the table names each command's place as `Destination` names the places, the console (2b) reads its refusals from it, and every module's tests mirror the module (ADR 0001).
- **`tui` is a screen: the window itself.** The app is the full-screen shell's counterpart, as the TUI's own table names the shell. It is not a fourth carve-out.
- **A gap's item is held to the backlog index.** A queued row whose item no longer has an index row fails the suite. Shipping 2c–2g therefore forces their rows to name the screens that now draw them, and the table cannot keep claiming a gap is queued.
- **Plumbing is classified but exempt from the vanished check**, since completion never lists it.

**Verified.**
- `scripts/build.sh test`: 246 cases, no Swift warning. New:
  - every fixture word is placed and nothing classified has vanished;
  - a planted `frobnicate` fails by name, a removed `voice` fails as vanished, and an empty list never passes;
  - the census is pinned;
  - every screen's module has its folder, and every gap's item is an index row;
  - the console's refusals give the carve-outs' reasons and nothing else.
- Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips) and the app's 246 cases.

### 2026-10-20 — `gui-console` (backlog item 2b): every verb reachable, today

**Why.** The parity law (2a) names a place for every command, and the console is the place that makes reach true now. A family's screen improves the experience, while the console guarantees reach as screens accrete, as the CLI's exec line (37h) did for the TUI.

- [x] **`PresentationConsole`** (`source/presentation/console/`, a new module), a shell overlay on **⇧⌘K**. It joins the View menu, gets a terminal button beside the monitor's in the sidebar, and a line in the keyboard list.
  - **`ConsoleViewModel`:**
    - **The line:** split as a shell splits it (quotes group, a backslash escapes; an open quote runs nothing), with a leading `apogee` dropped.
    - **Completion:** the binary's own, `apogee __complete <words> <the word being typed>` in one bounded run. One candidate completes the word, and several are offered.
    - **A run:** the jobs core's, with stdout streamed, the busy line live, the stderr tail kept, and the ending said ("Done.", "Exited with status 1.", "Stopped."). Stop is Ctrl-C's, and one run goes at a time.
    - **Refusals:** a carved-out command is refused with `ParityTable`'s reason and never started.
    - **History:** recalled with ↑ and ↓.
  - **`ConsoleView`:** the transcript, whose lines keep their width so a table's columns hold, scrolling sideways; the busy line and Stop; the candidates as buttons; the input, where Tab completes and Return runs.
- [x] **The shell:** `Overlay.console`, `ShellCommand.toggleConsole`, and ⇧⌘K among `KeyMap`'s bindings and the design's keyboard list.
- [x] **The design (canvas versions 35 and 36):**
  - the prototype's console overlay and its `console` start;
  - the board `ScreenConsole`, slotted after the monitor's with the row's note grown;
  - the contract board's row, "The console: any command, as typed", ready, which makes 40 ready parts;
  - the Layout board's key row;
  - the keyboard list's ⇧⌘K.

  The design's health check passes all 33 start screens. Every changed file was read back from the canvas identical to the repo's.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The console is an overlay sheet on ⇧⌘K** (the open call's default: a global sheet on a key, keyboard-first). ⇧⌘K was free in the design's list.
- **Completion runs through `CommandRunning`'s one-shot command**, a bounded run per Tab, rather than growing the jobs core. Completion is no long job and needs no supervision.
- **Stderr is kept at the end and shown live as the busy line.** The jobs core reads stderr as a bounded tail, and the console prints it whole when the run ends, in the CLI's words.
- **The board's transcript is the binary's own output**: `models list` over a scratch store, and the completion `apogee __complete models ""` answers. The design never invents a CLI word.

**Verified.**
- `scripts/build.sh test`: 252 cases, no Swift warning. New:
  - the word-splitting matrix (quotes, escapes, the word being typed, an open quote);
  - completion argv-exact (`__complete|models|li`, a leading `apogee` dropped, an empty word after a space), with one candidate completing;
  - a run argv-exact on the jobs core, with stdout, the busy line, stderr and "Done.";
  - a failure's status and stderr, one run at a time, and Stop;
  - the three carve-outs refused by their reasons with nothing started;
  - an empty line, an open quote, no binary, and the history;
  - the keyboard list at 21.
- **Drawn offscreen over the real binary in a scratch home:** a real `models list` run through the jobs core (every table's columns intact), `reset` refused in the table's words, and the ten candidates `__complete models ""` returns.
- Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips) and the app's 252 cases.

### 2026-10-20 — `knowledge-collections` (backlog item 2c): the record layer's screens

**Why.** Two whole families had no GUI place, the organizational records (`knowledge`) and the embed store (`embed`). The graph screen also lacked its prose half: 1l shipped code graphs only. The TUI has had all three since 37c.

- [x] **The Knowledge place** (⌘8, the eighth place; `PresentationKnowledge`, `source/presentation/knowledge/`, a new module), with tabs **Records** and **Collections**.
  - **`RecordsViewModel`:**
    - the records `knowledge list` reads, newest first, narrowed by branch;
    - a question answered by `knowledge query <q> --status <branch|""> --json`, each score on the retriever's own scale and the retriever named;
    - Link (`knowledge link`), Move to (`knowledge status`) and Delete (`knowledge delete`, confirmed here, since the CLI asks nothing on a pipe), each read back;
    - Export (`knowledge export --format markdown|json [--anonymize] -o <file>`, the file chosen in a save panel);
    - **Capture** (the clerk's one model call: `knowledge capture <text> [--status] [--discipline] [--link]`) and **Reindex**, both supervised.
  - **`CollectionsViewModel`:**
    - the collections `embed list` reads, and a collection's `embed info` (chunks, the text index's health, its vectors or "a text index only", its sources);
    - a search, `embed query`, shown as the CLI prints it (it has no document form);
    - **Ingest** (`embed ingest <collection> <path> [--retriever] [--graph]`), supervised;
    - deletes confirmed: a whole collection with `--yes` only after the person's word, since the CLI only describes the delete without it; one source as the CLI takes it.
  - **`KnowledgeScreen`:** the two tabs with their sheets, the capture and the ingest each showing its command as typed.
- [x] **The Graph place's prose half.**
  - A collection's graph (completion already listed them) says "Build its graph" until built, then shows its **report**: `graph report --output-format json`, read as `GraphReport` (the summary, communities with their members, decisions, the busiest entities).
  - **Find communities** runs `graph communities` supervised.
  - A node's explanation shows its **communities** and attached **decisions**.
- [x] **The reads:**
  - `Knowledge.swift`: `KnowledgeRecord`, `KnowledgeQuery`, `CollectionRow`, `CollectionInfo`;
  - `Graph.swift` grew `GraphCommunity`, `GraphDecision` and `GraphReport`, `GraphExplain`'s `communities` and `decisions`, and `GraphStats`' `kind`, `communities` and `isProse`.
- [x] **The parity table:** `knowledge` and `embed` are screens of `PresentationKnowledge`, and `graph`'s note names the prose half (17 screens, 7 gaps).
- [x] **The design (canvas version 37):**
  - the prototype's Knowledge place (⌘8, its records with a question answered, its collections with a search) and the graph's "Collection · notes" view;
  - the boards `ScreenGraphProse`, `ScreenKnowledge` and `ScreenCollections`;
  - the contract board's KNOWLEDGE rows and the prose-graph row (43 ready);
  - the Layout board's ⌘1 – ⌘8.

  Every string on the boards is the binary's own, from the recorded documents. The health check passes all 36 start screens.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **One place, two tabs:** Knowledge holds Records and Collections, on ⌘8. A single place keeps the eight-key sidebar, where two would have needed ⌘9.
- **A records question runs as a command, its stdout the document.** `knowledge query` takes `--json` and refuses `--output-format`, which the read helper always appends. The live drawing found this: the scripted test had passed it.
- **A collection's search is the CLI's text** (no `--json` exists), shown in a code block, never re-scored.
- **The prose half lives in the Graph place**, which already lists collection graphs, rather than in Knowledge. Building a collection's graph is the same `graph build` its Rebuild runs.
- **Capture and reindex are supervised jobs.** Capture is one model call and can take a while.

**Verified.**
- `scripts/build.sh test`: 266 cases, no Swift warning. They read documents recorded from the installed binary over a scratch home (`tests/business/reads/Fixtures/knowledge-*`, `collection*`, `graph-prose-*`, paths made neutral). That home held records captured by a scripted mock clerk, a lexical collection of three notes, and its prose graph built by a scripted extractor, its community summarised and a decision record captured into it. New tests cover:
  - the records, the query's scores and retriever, the collections, the prose graph's stats, report and explain;
  - every verb argv-exact: query, link, move (and nothing for the same branch), delete, export, capture with overrides, ingest with its options, `--yes` only on a whole collection;
  - capture and ingest supervised, read back at their end;
  - the report read for a built collection graph, Find communities, and an unbuilt graph's honest line;
  - the keys and places at eight.
- **Drawn offscreen over the real binary in the scratch home:** a question answered with its scores and a record open; a collection's health, sources and a search as printed; the notes graph's report, then Heat pump's communities, decision and relations.
- Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips) and the app's 266 cases.

### 2026-10-20 — `training-studio` (backlog item 2d): runs, regimes and datasets

**Why.** The whole training track had no GUI place: the kits and regimes, a run's gate and promotion, the datasets. The TUI has had Train and Datasets since 37f.

- [x] **The Training place** (⌘9, the ninth place; `PresentationTraining`, `source/presentation/training/`, a new module), with tabs Runs · Regimes · Datasets.
  - `TrainingViewModel` reads `train status`, `train versions`, `train regime list`, `datasets kits`, `datasets list`, and `models list --all` for the students (the stored SafeTensors sets by handle) and the backends.
  - **Runs:**
    - the runs (running first, each once), each with its trainer, loss and the gate's verdict, said plainly ("failed the gate (46%) -- promote refuses it unless forced");
    - **New run…** (`train run <handle> --dataset <d> [--method] [--iters] [--batch-size] [--num-layers] [--grad-checkpoint] [--mask-prompt]`), launched only from the sheet;
    - **Gate it** (`train eval <run>`);
    - **Promote…** (`train promote <run> --as <backend> [--target] [--quantize] [--force]`, the sheet saying the gate's verdict first);
    - the promoted versions per backend, with **Roll back…** (`train rollback`, confirmed).
  - **Regimes:** each one's kits in order, and **Run this regime** (`train regime run <name>`).
  - **Datasets:**
    - the datasets and the kits;
    - **New dataset…** (`datasets create <name> --from template|sessions|empty`);
    - **Synthesize…** (`datasets synth <name> --teacher --kit [--count] [--topic]`, a cloud teacher said to be billed);
    - **Prepare a file…** (`datasets prepare <path>`);
    - **Delete…** (`datasets delete <name> --yes`, after the person's word).
  - Every long act is supervised on the jobs core with Stop, its busy line in the CLI's words.
- [x] **The reads** (`Training.swift`): `TrainingRun`, `TrainingStatus`, `VersionLedger`, `TrainingRegime`, `TrainingKit`, `TrainingDataset`.
- [x] **The parity table:** `train` and `datasets` are screens of `PresentationTraining` (19 screens, 5 gaps).
- [x] **The design (canvas version 38):**
  - the prototype's Training place (⌘9: a gated run, a run's busy line, the regimes, the datasets and kits);
  - the boards `ScreenTraining` and `ScreenTrainingDatasets`;
  - the contract board's TRAINING rows (46 ready) and **gap G21**, "A run's progress is text", with its fallback;
  - the Layout board's ⌘1 – ⌘9.

  The health check passes all 38 start screens.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **The trainer's stream does not reach a driver.** The item assumed it did ("rendered as received, no fallback needed"), but `train run` takes no `--output-format`, and its iterations reach stderr as text (`[train] iter 2/4 · loss 0.6200 · …`). The run's busy line is shown verbatim and nothing is decided from it. The machine face is recorded as **G21**, for later CLI growth (52b's shape for the pulls and the media runs). This is a premise correction recorded for the user.
- **One place, three tabs, ⌘9.** Training is its own place rather than a Models tab: its acts (runs, gates, promotions) are hours long and its own.
- **Students are the stored SafeTensors sets by handle.** `train run` takes a handle, checked against the binary.
- **Gate and regime runs take the CLI's defaults** (the run's own suite, the regime's own tuning). Their flags stay the console's until a sheet wants them.

**Verified.**
- `scripts/build.sh test`: 273 cases, no Swift warning. Most tests read documents recorded from the installed binary over a scratch home (`train-status`, `train-regimes`, `train-regime`, `datasets-kits|list|info`, `train-run.err`): a run on `--trainer mock` gated on a kit, the seeded regimes and kits, and a dataset from the template. A promoted version needs a real conversion, so its ledger is the CLI's own shape, written out. The tests cover:
  - the run with its loss, gate verdict and score;
  - the regimes, kits and datasets, and the version ledger (GGUF or MLX, active, pruned);
  - every act argv-exact: run (with every option), gate, promote (`--force` only when chosen), regime, create, synthesize, delete (`--yes` only after the confirm), rollback;
  - a run supervised with the trainer's busy line;
  - nothing started until the sheet's launch;
  - the places and keys at nine.
- **Drawn offscreen over the real binary in the scratch home:** the runs with the gated run open, the regimes, the datasets, and the promote sheet with the gate's verdict.
- Bare `lib/scripts/cicd.sh --test`: green -- the CLI's 3593 tests (the two usual skips) and the app's 273 cases.

**Not run:** real training. No venv was created and no model was trained, by the standing rule. Hours-long runs are the user's.

### 2026-10-20 — `agents-mcp-symphonies` (backlog item 2e): the library's resources

**Why.** Three resource families had no GUI place: agents, MCP servers and symphonies. The TUI has had them since 37d and 37g.

- [x] **The Library place** (⌘0, the tenth place; `PresentationLibrary`, `source/presentation/library/`, a new module), with tabs Agents · Symphonies · MCP servers.
  - **`AgentsViewModel`:**
    - `agents list`, the shipped agents and the person's own;
    - **New agent…** (`agents create <name> [--description] [--model] [--tools] [--no-schema]`, byte for byte the scaffold's own create);
    - **Delete…** (`agents delete --purge` or `--keep-files`, as chosen; the CLI asks nothing on a pipe);
    - the person's own prompt and schema files **edited in place**, at the paths `agents list` names under the data directory, which comes from `apogee config path`.
  - **`SymphoniesViewModel`:**
    - the catalog (`symphonies list`, its three sources shown);
    - **Play** (`symphonies play <name> --input <text> --output-format json`, supervised: the stage lines as it goes, each stage's answer and the output from the play's document);
    - the person's own spec file edited in place and then **checked by the binary**: `symphonies show` either refuses the file in its words or lists its problems, and **Put it back as it was** restores it;
    - **Copy** (`symphonies create <name> --from`) and **Delete…** (a config entry's `symphonies delete`).
  - **`MCPViewModel`:**
    - `mcp list`, each server's state and tools (its environment said only as set or not), read when the place opens because listing connects to every server;
    - the presets, each **registered** byte-identically (`mcp create <name> --from <preset>`) or showing its refusal in the CLI's words;
    - **Enable/Disable**, and **Try it** (`mcp test <server> <tool> <json>`, its answer as printed).
  - `LibraryScreen` and `DefinitionEditor` (the editor sheet with the binary's word under it).
- [x] **The reads** (`Library.swift`): `AgentEntry`, `MCPServer`, `MCPPreset`, `Symphony` (with `Stage`, `isOwnFile`), `SymphonyPlay`.
- [x] **The parity table:** `agents`, `mcp` and `symphonies` are screens of `PresentationLibrary` (22 screens; 2 gaps, `execute` 2f and `review` 2g).
- [x] **The design (canvas version 39):**
  - the prototype's Library place on ⌘0;
  - the boards `ScreenLibrary`, `ScreenLibrarySymphonies` and `ScreenLibraryMCP`;
  - the contract board's LIBRARY rows (49 ready) and **gap G22**, "An agent's files are checked only when it runs";
  - the Layout board's ⌘1 – ⌘9, ⌘0.

  The health check passes all 41 start screens.

**Decisions made while building** (2026-10-20, each recorded for veto):
- **One place, three tabs, on ⌘0.** The design's law gives every place a key, and ⌘1–⌘9 were taken, so the tenth place takes ⌘0. One tabbed Library holds the three families, where three places would have had no keys.
- **A play inside the live session is 2f's.** `/play` is an `execute` session's turn (machine-mode.md), and the session window drives `chat`, where it would be plain text. So 2e's plays run on their own, supervised and narrated. The in-session play lands with 2f's execute door.
- **In-app editing covers the person's own files only:** an agent's prompt and schema, and a `file` symphony's spec. A shipped starter or a config entry is edited at a prompt (`symphonies edit`, the $EDITOR's), as the page says. The CLI's own edit of a starter writes a config entry that stands in for it, and the app does not reimplement that.
- **No GUI-side parser.** A symphony's save is checked by `symphonies show`. Nothing in this build checks an agent's files short of running it, so a save is read back and said as such, and the gap is recorded as **G22**.
- **MCP servers are listed when the place opens, never at launch:** listing starts every server.

**Verified.**
- `scripts/build.sh test`: 282 cases, no Swift warning. They read documents recorded from the installed binary over a scratch home (`agents-list`, `mcp-list`, `mcp-presets`, `symphonies-list`, `symphony-show`, `symphony-play`): the shipped agents and one scaffolded, `code-exec` registered and connected, the starters with a spec file and a config entry, and a play on a mock backend. New tests cover:
  - every read;
  - create, delete (purge and keep), a shipped agent's files read-only, an own file written where the CLI reads it and said honestly;
  - a play argv-exact and supervised, with its document read;
  - an own spec saved, refused verbatim by a scripted `symphonies show`, and put back;
  - MCP: nothing at launch, a preset registered byte-identically, disable, a tool tried as typed.
- **Drawn offscreen over the real binary in the scratch home:** an agent with its files; `critique-revise` played on the mock, with each stage's answer and the output; the connected `code` server with its tools, `code-exec`'s description and `self`'s refusal.
- Bare `lib/scripts/cicd.sh --test` passed in 7m26s: the CLI's 3,593 tests, none failing, and the app's 282 cases, none failing and none skipped, with no warning.

### 2026-10-21 — `execute-sessions-plays` (backlog item 2f): execute sessions, plays and recipes

**Why.** The session window spoke plain chat. The CLI's session surface is wider, and the parity audit named what was missing:
- **execute sessions:** under a suite, bare on one model, or in a project, with `--orchestrate` offering the symphonies as tools;
- **plays:** `/play` in a session;
- **task recipes:** `task run --recipe`.

- [x] **The execute door** (`PresentationSessions`, `Views/ExecuteDoor.swift`): ⌥⌘N, the sidebar's hammer, or New Execute Session… in the File menu.
  - **The sheet:** the suite (a configured one, or left to the config: its default suite, else bare), the model (`-m`), the folder it opens in, and orchestrate, with the command shown as it will run.
  - **The suites are read when the door opens:** `config get suites` and `config get models.default_suite`.
  - **Orchestrate** is offered only where a suite is there to offer: one named, or the default. Bare, the CLI refuses it.
- [x] **The launch** (`BusinessSession`, `ChatLaunch`):
  - `ChatLaunch.Execute` (`--suite`, `--orchestrate`) makes the child `apogee execute`, with the same two stream-json directions;
  - `ChatLaunch.folder` is where the child works;
  - `ApogeeClient` opens a launch in its folder.
  - A session paused and resumed reopens as it opened (`execute --resume <id> --suite …`), in its folder.
  - A project's context file and the coder persona are said by the session's own `notice`s.
- [x] **Plays** (`PresentationSession`):
  - `/play` is offered in an execute session only (the CLI's execute-only rows), with its description in the CLI's words.
  - After `/play `, the symphony catalog is offered from `symphonies list`, read when the door opens a session. A name completes the line; the input follows, and the line is sent as typed.
  - **Narration** (`ViewModels/Play.swift`, `PlayNarration`): a play's `tool_status` lines are kept as the CLI wrote them, and read only to say which stage it is on. The activity's summary is `playing summarize-verify · stage 2 of 2`, then `played … · 2 stages`, and each stage line carries its `1/2` mark. The model's own play (`play — the model chose …`) is named by that line; a typed one by its `/play`.
  - **The header's banner:** `execute · suite coding · orchestrating · in ingest`.
- [x] **Recipes** (`BusinessReads`, `Recipes.swift`, `TaskRecipe`; `PresentationTasks`):
  - The new-task sheet's Recipe picker lists `task recipes`. The chosen recipe is read whole (`task recipes show`): what it does, its input with the CLI's description of it, and its suggested grants as unticked toggles.
  - **The launch** is `task run --recipe <name> --input …`, plus the person's own checks, `--rounds` only when moved off the recipe's, and an `--allow` per ticked grant.
  - **The expansion** is shown before it runs: the hand-typed twin, with the goal rendered as the CLI renders it, the recipe's checks before any typed, `--tools`, its rounds, its agent and the grants given. It is quoted as a shell takes it (`NewTaskForm.typed`).
- [x] **The parity table:**
  - `execute` is a screen of `PresentationSessions`: 23 screens, 1 gap (`review`, 2g);
  - `task`'s note now covers recipes;
  - `analyze` stays the console's.
- [x] **The design (canvas version 40):**
  - the prototype's execute chat with a play narrated, `/play` and its catalog in the slash menu, the door's sheet, and the new-task sheet's recipe picker with its expansion;
  - the boards `ScreenSessionPlay`, `ScreenSessionExecute` and `ScreenNewTaskRecipe` (44);
  - the contract board's rows: 52 ready and 21 gaps, with **gap G23**, "An execute session's suite and its orchestration, in its session event";
  - ⌥⌘N on the Layout board and in Settings › Keyboard.

  The health check passes all 44 start screens.

**Decisions made while building** (2026-10-21, each recorded for veto):
- **The door is a sheet in Sessions on ⌥⌘N, not a place.** An execute session is a session: the one live child, listed and paused like any other.
- **There is no bare choice while a default suite is set.** `execute` refuses `--suite off`, so the door offers each suite and "left to the config", and names the default where one is set.
- **The banner says what the launch said, and no count.** The `session` event names the model only. So the suite comes from the door's flags (or the config's default, read), and orchestration is said without the `orchestrating N` the terminal prints. Recorded as **G23**.
- **A saved execute conversation reopened from the list resumes as chat.** `chats list` does not say how a conversation was opened. The door's flags last while it is the open session, paused and resumed with them.
- **`/play` is an execute session's alone,** as the CLI's slash rows are. In a chat, `/play` is a prompt like any text, and the app offers nothing after it.
- **A recipe launches as `--recipe`** (the ledger keeps its provenance), and its hand-typed twin is drawn from the recipe's own document by the CLI's expansion rules: shown, never run.
  - Suggested grants start unticked.
  - The sheet's write and command toggles are hidden for a recipe: its tools are its own.

**Verified.**
- `scripts/build.sh test`: 303 cases, no Swift warning. They read streams recorded from the installed binary over a scratch home with mock members (`execute-play`, `execute-orchestrate`, `execute-play-refused`, `execute-project`) and the recipe documents (`task-recipes`, `task-recipe-show`). New tests cover:
  - the launch per rung (left to the config, `--suite`, `--orchestrate`, bare `-m`, resumed) and its folder, a real child started there;
  - the door end to end: suites read, launch argv, orchestrate never sent without a suite, the banner in each case, the catalog handed to `/play`;
  - `/play` offered, completed and sent as typed in execute and absent in chat;
  - narration over each recorded stream: a typed play, the model's own, a refused one (the CLI's error, no narration made up), a project's notices, a chain's stage line;
  - recipes read, the launch argv (input, grants only ticked, rounds only moved), the expansion byte for byte. Its goal is the very text `task run --recipe digest-folder --input docs` wrote to its ledger, read back from the binary.
- **Drawn offscreen over the real binary in a scratch home:**
  - the door's sheet;
  - an execute session opened by the door in a project, with `/play echo2 hello` played through the app's own client: the persona and project notices, the two stages, and the answer;
  - the `/play ` catalog;
  - the new-task sheet with `digest-folder`, its input and grant, and the expansion.
- Bare `lib/scripts/cicd.sh --test` passed in 7m19s: the CLI's 3,593 tests, none failing, and the app's 303 cases, none failing and none skipped, with no warning.

### 2026-10-21 — `review-screen` (backlog item 2g): merge requests, targets and skills

**Why.** The review agent (the CLI's track 48) had no GUI place. The TUI classified it as covered by the runner and left a Review view for later; the parity mandate (2026-10-20) makes the GUI's view now.

- [x] **The Review place** (`PresentationReview`, `source/presentation/review/`, a new module): the eleventh place, on ⇧⌘R, with tabs Reviews · Targets and sweeps · Skills.
  - **`ReviewViewModel`, a review:**
    - `RunForm` gives the reference, the lenses (`--skills`) and the model (`-m`).
    - **A review is a dry run first:** `review mr <ref> --dry-run --output-format json`, supervised on the jobs core with the CLI's narration (`[review] hunting with security`) as its busy line. Its `review.run` document is read from stdout and drawn: the merge request, head and mode, the lenses and plan, each finding (severity, kind, `file:line`, the claim, the suggested fix, confidence, the lenses that found it, inline or summary-only), the counts in the CLI's order, the batch's fate and the notes.
    - **Posting only on the person's click on the shown batch:**
      - **Post…** is offered only on a dry run with findings, and asks first, saying what goes (`postWords`).
      - Only that yes passes `--yes`, on the same review's argv.
      - The posted document is drawn after.
    - **Silence is a complete review:** "nothing to say: silence is a complete review" is drawn as one, never as a failure, and no Post is offered.
    - **A forge's refusal** (missing `gh`, logged out) is shown in the CLI's last words, its fix with it. No document is made up.
  - **Targets and sweeps:**
    - **Targets:** `review targets` is listed, with each target's grant ("posts when swept" or "documents only"). **Add target…** builds `review targets add <name> --repo|--owner … [--forge] [--host] [--skills] [--post] [--drafts] [-m]` as typed. **Remove…** asks first (`review targets delete`). Each change is read back.
    - **A sweep** is a dry run (`review sweep --dry-run [--target …] [--since …] [-m]`) unless consented. **Sweep and post…** asks, naming the targets it would post on: those whose own `post: true` grants it, or every one it reaches with `--allow-post`, chosen in the sheet.
    - **The `review.sweep` document is drawn:** the totals; per target its grant; per project its watermark before and after, or that a first sweep starts at now; each review; any error; and where the CLI kept the document.
  - **Skills:** `review skills list` shows the seven lenses and the person's own, with their source and what each applies to (or every file). One is read whole with `review skills show`: its prompt, read only.
  - `ReviewScreen`, `RunPage`, `FindingCard`, `TargetsView`, `SweepPage`, `SkillsView`, `TargetSheet`, `SweepSheet`, `RunningLine`.
- [x] **The reads** (`Review.swift`): `ReviewFinding`, `ReviewRun` (with `Posted`, `isSilent`, `inlineFindings`, `overCap`), `ReviewTarget`, `ReviewSweep` (with `Target`, `Project`), `ReviewSkill`.
- [x] **The shell:** `Destination.review`, the eleventh place on ⇧⌘R (`arrow.triangle.pull`), and the key on the keyboard list.
- [x] **The parity table:** `review` is a screen of `PresentationReview`. That makes 24 screens and **no gap**: every CLI command now has its place.
- [x] **The design (canvas version 41):**
  - the prototype's Review place, with its place-bar icon and ⇧⌘R;
  - the boards `ScreenReview`, `ScreenReviewPost`, `ScreenReviewTargets` and `ScreenReviewSkills` (48);
  - the contract board's REVIEW rows: 58 ready and 22 gaps, with **gap G24**, "Post the batch a dry run found";
  - the Layout board's ⇧⌘R row.

  The health check passes all 48 start screens. Every skill's description and prompt opening on the boards is the binary's own.

**Decisions made while building** (2026-10-21, each recorded for veto):
- **Review is the eleventh place, on ⇧⌘R.** The design gives every place a key, and ⌘1–⌘0 are spent. A review is no library resource, so it isn't a Library tab.
- **The post runs the review again.** No verb posts a dry run's kept batch, so the consent says the post reviews again and posts what that run finds; the posted document is drawn after. Recorded as **G24**.
- **The app never sends an unconsented, non-dry run.** A pipe's "nobody to ask -- run with --yes to post" refusal is read as the CLI says it, never provoked.
- **A sweep that posts names where it posts** before it runs. `--allow-post` is a choice in the sheet, never a default.
- **Skills are read only** (editing them is out of scope). This session's reviews are held in memory: the CLI keeps no review history but a sweep's documents, so the Reviews tab shows the last run.
- **`gh` is found the way the CLI finds it, on the child's PATH.** An app opened from the Finder runs with launchd's PATH, so a `gh` installed by Homebrew can be "not on PATH" for it. The CLI's own refusal says so verbatim, and the app does not search for `gh` itself.

**Verified.**
- `scripts/build.sh test`: 314 cases, no Swift warning, including the layering check over the new module. They read documents recorded from the installed binary over a scratch home, with a fake `gh` first on PATH (the CLI's own review e2e's way) and a mock reviewer:
  - `review-run-{dry,refused,posted,silent}`, `review-targets`, `review-sweep`, `review-skills`, `review-skill-show`;
  - the forge's refusal with no `gh` (`review-no-gh.err`).

  New tests cover:
  - every read;
  - a dry run argv-exact, supervised, its finding drawn;
  - the consent pin: nothing posts without the yes, cancel runs nothing, `--yes` appears once, on the consented run, and the posted document is read;
  - silence offering no Post;
  - the forge's refusal verbatim;
  - targets added (repository, and owner with forge, host, post and drafts), removal asked then run, read back;
  - a dry sweep's argv and document, the posting targets named, the consented `--allow-post` sweep;
  - the skills listed and one shown.
- **Drawn offscreen over the real binary and the fake `gh`:**
  - a dry review of `acme/widget#12`, its finding and Post…;
  - the targets, and a sweep that reviewed nothing new past its kept watermark;
  - the security lens whole.
- Bare `lib/scripts/cicd.sh --test` passed in 7m25s: the CLI's 3,593 tests, none failing, and the app's 314 cases, none failing and none skipped, with no warning.

### 2026-10-21 — `model-store-screen` (backlog item 2h): backends and stored weights, apart

**Why.** On 2026-10-20 the user asked for the store in the GUI: "There's no actual way to look at like the safe tensors and then convert them and everything else like you can in the CLI." The Models place still mixed backends and stored weights in one table. Only `models delete`, `rename` and `repair` reached the store.

- [x] **The tabs are Backends · Stored weights · Jobs · Roles** (`ModelsViewModel.Tab`). Downloads became Jobs, which 2i grows.
- [x] **Backends lists only what can be used:** local backends, then cloud and subscription ones. A pipeline and a voice model move to Stored weights. A local backend has:
  - **Show its weights**, which opens its set under Stored weights;
  - **Remove the backend…**, which asks first and runs `config delete-backend`, the weights kept;
  - **Delete with its weights…**, which is `models delete --yes`, with `--and-backends` only when chosen.
- [x] **Stored weights** (`StoreViewModel`, `StoreView`):
  - **Every set by its handle, grouped by model folder.** Each row shows its format, precision, size and state. The state is "weights only · convert to run", "used by a conversion", "not registered", "backend `<name>`", "pipeline", "voice model", or the CLI's own (`cannot load: …`).
  - **Each row's made-from line** ("quantized from …"), and each group's count with its size where every set states one.
  - **The fold** keeps M1's sentence and its Show them (`models list --all`, whose difference from the plain list marks what a conversion consumed).
  - **"Register the N not registered…"** shows `config scan`'s own report, word for word, then runs `config scan --register`.
- [x] **A registered set is found through the CLI's own documents.** `models list` names no handle on a backend's row, so a backend's `models info` gives its `model_path`, and `models info <that path>` answers with the set's handle and typed facts. Nothing is parsed from a path.
- [x] **A set's details:**
  - the typed size, window and cache, then the record's own lines;
  - **made from**, each step linked where the store lists the parent, inferred and gone said;
  - **made from it**, as the CLI's `made from it` lines name them, each linked;
  - the design's note on why it doesn't run yet;
  - the model's **Rename…** (`models rename`, its backends following) and **Delete all of it…**;
  - `apogee models info <handle>`.
- [x] **The actions by format:**
  - GGUF or MLX not registered: **Register…**;
  - registered: **Use in a new session** and **Remove the backend…**;
  - a pipeline: **New clip with it** and **Make it the default** (`config set-default-video`);
  - a voice model: **Open Voice**;
  - every set: **Show in Finder** and **Delete…**.

  Convert, quantize and check its files are 2i's.
- [x] **Register…** builds `config add-backend <name> --type llamacpp|mlx --model-path <file> [--mmproj-path] [--context-size] [--cache-type]` and shows it as typed:
  - the name is the one `config scan` lists the path under, the CLI's own naming;
  - the window and the cache are left out unless changed (q8_0 is the CLI's default), and an MLX backend takes no cache type;
  - a projector the record names is carried.
- [x] **Sizes in the CLI's own units:** whole KiB and MiB, and GiB to a tenth, each truncated as `format_progress_size` truncates them.
- [x] **The design (canvas version 42):** the drift is settled on both sides; see the decisions. The health check passes all 48 start screens.

**Decisions made while building** (2026-10-21, each recorded for veto):
- **Stored weights has its own view-model, `StoreViewModel`,** beside `ModelsViewModel`, which narrows to the backends. M1's handle-keyed verbs on `ModelsViewModel` stay, and are tested.
- **A registered set's handle is the CLI's answer for its path,** two reads per local backend. A backend outside the store gets no set.
- **Each set's `models info` is read after the table draws** (the item's default), one at a time.
  - It was not measured on the person's own store: a probe there would read real user state.
  - On the scratch store, the reads take well under a second.
- **The drift settled.**
  - **The app took the design's words:** the states "pipeline" and "voice model", the format names, each row's made-from line, each group's count and size, the kicker (`GGUF · Q4_K_M`), the design's notes, and "New clip with it".
  - **The design took two of the app's:** the summary reads "in the model store", since the app can't assume the store is `~/.apogee/models`; and a registered set offers "Use in a new session", the item's own words.
  - **A set's facts are the record's own lines**, after the typed ones, rather than the prototype's composed list.
- **Convert, quantize and Check the files are left to 2i.** The design draws them, and 2i builds them on the jobs core.

**Verified.**
- `scripts/build.sh test`: 323 cases, no Swift warning. They read documents recorded from the installed binary over a scratch store, built from sparse copies of M1's sets and tiny GGUF headers with records (the CLI's own e2e technique; nothing converted or quantized). The store holds:
  - a pulled Llama SafeTensors set, converted to an F16 GGUF (not registered) and quantized to a Q4_K_M one (registered);
  - a Gemma SafeTensors set, and the MLX model converted from it (not registered);
  - a voice model.

  New tests cover:
  - the sets by handle, the registered one through its backend's documents;
  - the states, the fold, `--all`'s consumed marks;
  - made from and made from it, linked;
  - Register…'s name from `config scan` and its flags, left out unless changed (none for MLX), run as typed and read back;
  - register all, asked with the scan's report;
  - deletes with and without `--and-backends`, a backend's removal asked first;
  - rename by the model folder;
  - the sizes;
  - Backends holding only backends, M1's tests carried over.
- **Drawn offscreen over the real binary on that store:** Stored weights with the registered Q4_K_M open (its made-from lines, sizes, actions); the register sheet for the F16 with the CLI's name; Backends with Show its weights.
- Bare `lib/scripts/cicd.sh --test` passed in 7m23s: the CLI's 3,593 tests, none failing, and the app's 323 cases, none failing and none skipped, with no warning.

### 2026-10-21 — `convert-quantize-jobs` (backlog item 2i): the store's long runs as jobs

**Why.** This is the second half of the user's ask of 2026-10-20 ("look at like the safe tensors and then convert them and everything else like you can in the CLI"). 2h gave every stored set a place, but no path reached `models convert` or `models quantize`, and Check its files held the Models place through a re-hash with no Stop.

- [x] **The convert sheet** (`ConvertSheet` over `ConvertForm`) opens from a SafeTensors set's **Convert to GGUF…** and **Convert to MLX…**. It is one command, the CLI's own chain:
  - the precision `-t`: GGUF f16 (the default, left out), bf16, f32, q8_0, auto; MLX 4bit (the default), 3bit, 6bit, 8bit, mxfp4;
  - **Then quantize to** (GGUF) or **Also make, one conversion each** (MLX), as `--register-with` in the CLI's order;
  - **Register what's made** (`--register`), on by default and forced on by a level;
  - **Names begin with** (`--base-name`), and MLX's **Model type** (`--model-type`);
  - the steps listed, each with an estimate labelled "about" (bits per weight over the set's stated bytes), and the names it registers by the CLI's naming (`<base>-F16`, `<base>-Q4KM`, `<base>-4bit`);
  - the command, shown as typed.
- [x] **The quantize sheet** (`QuantizeSheet` over `QuantizeForm`) opens from an unquantized GGUF's **Quantize…**. A GGUF is unquantized by the precision its row states, else by the level its file is named with.
  - The level is Q4_K_M by default (left out), with estimates.
  - **Register it**, and its name: `<base>-<level>`, following the level until typed.
  - `models quantize` registers nothing, so the job then runs `config add-backend <name> --type llamacpp --model-path <the new file>`. The new file comes from the read-back's `models info` of the set that landed. A refusal leaves the GGUF made and is said in the CLI's words.
- [x] **Check the files** is `models repair <handle>` as a job, from every set under Stored weights and from a backend's More menu. `ModelsViewModel.repair` hands the check to the jobs core and no longer holds the place.
- [x] **The Jobs tab** grows from 1i's Downloads (`DownloadsViewModel`), and holds pulls, conversions, quantizations and checks. Until G18:
  - each step ticks as the sets it makes land in `models list --all`, read again as the verb speaks (at most every two seconds) and at its end;
  - a registration ticks when the run ends well;
  - the running step shows how long it has run, with no percent;
  - the CLI's busy line and text show as said, and decide nothing;
  - Stop is the interrupt: the sets already made stay;
  - a resume command the CLI names runs as typed (1i).
- [x] **At the end:** the CLI's own last words (a check's result is its last line, G20's fallback), the store read back, and **Show the weights**, which opens the first set it made under Stored weights.
- [x] **The design:** no change. The sheets and the Jobs card take the prototype's words (the boards `ScreenModelsConvert` and `ScreenModelsDownloads`, "Models · jobs"), so the canvas stays at version 42. The health check passes all 48 start screens.

**Decisions made while building** (2026-10-21, each recorded for veto):
- **The sheets build the CLI's own chain:** one `models convert … --register-with …` (the item's decision). Only quantize's registration is the app's second command, because the verb has none.
- **Ticks are counted, never matched.** The k-th set to land ticks the k-th step that makes one, in the order the CLI makes them; nothing is read from a set's name to decide which step it was.
- **Estimates come from bits per weight over the stated bytes, said "about"** (the item's default). Where no size is stated, there is no estimate.
- **The register names on the convert sheet** are the CLI's documented naming (`models_pull.h`: the level without its underscores), shown and never passed. The CLI names them itself.
- **Check the files leaves the one-shot path for a job** (the item's default). Its result is the exit and the last line.
- **The quantize sheet offers Q2_K to Q8_0,** as the design does. The CLI's `-t F16` is a console line.

**Verified.**
- `scripts/build.sh test`: 331 cases, no Swift warning. New tests cover:
  - the convert form's argv matrix (GGUF and MLX, precision, levels, register, base name, model type), each flag pinned to the binary's own `--help` text, recorded as `models-convert-help.txt` and `models-quantize-help.txt`;
  - the steps, names and estimates;
  - the quantize form, with and without its registration;
  - the base name rule;
  - supervised runs over scripted jobs and read-backs:
    - a conversion's steps ticking as sets land, and Show the weights;
    - Stop keeping what landed, and the CLI's resume command run as typed;
    - a quantization registering from the read-back, and a refused registration said;
    - a check passing and failing;
  - Stored weights opening the sheets by format and handing the jobs on.

  Nothing converts: the standing rule.
- **Drawn offscreen over the real binary on 2h's scratch store:** the convert sheet for the Llama SafeTensors set, and the quantize sheet for its F16. No job was started on real weights; a real conversion through the app is the user's run.
- Bare `lib/scripts/cicd.sh --test` passed in 7m28s: the CLI's 3,593 tests, none failing, and the app's 331 cases, none failing and none skipped, with no warning.

### 2026-10-21 — `store-commands-the-cli-takes` (maintenance item M2): every store command one the binary takes

**Why.** The user's run of the built app on 2026-10-21 met the binary's refusals from the Models place:
- an MLX conversion of 8bit then 4bit: `with --mlx, --register-with names every precision to convert -- drop --type, or name it in the list`;
- a rename of the MLX backend `GPT-OSS-20B-mixed_4_8`: `no model named 'GPT-OSS-20B-mixed_4_8' -- name it as its backends share it …, or by its stored directory`.

The convert sheet wrote `-t 8bit --register-with 4bit`, where since the CLI's M16 `--register-with` names every MLX precision. The same form sent a GGUF precision other than f16 with Register, which the CLI refuses because registering and quantizing start from the F16. And the Backends tab's Rename… always ran `models rename`, which takes a GGUF family or a stored folder, never a backend's own name. 2i had checked each flag against `--help`, which names flags and not the rules between them, and its tests pinned the refused commands.

- [x] **The convert sheet builds only what the CLI takes** (`ConvertForm`).
  - With `--mlx` and further precisions, `--register-with <written>,<rest…>` and no `-t`, in the order the CLI converts and names them.
  - A GGUF at another precision is made on its own. Choosing one clears Register and the levels, and the sheet says why where the levels were. Turning Register on, or choosing a level, puts f16 back.
- [x] **Rename is two acts** (`ModelsViewModel.Renaming`). **Rename the backend…**, on every backend, runs `config rename-backend`: the backend and every reference to it, the store left alone. **Rename the model…** runs `models rename`, offered only where the binary completes the row's family name: `__complete models rename ""`, read with the list. The sheet says which act it is.
- [x] **The bug is a fixture.** `scripts/refresh-store-commands-fixture.sh` records the binary's verdict on every command the convert and quantize sheets can build: 92, each run in a throwaway home of its own against a set that does not exist, so nothing converts. It also records the rename's listing and completion from a sandbox holding a GGUF family and an MLX model with a mixed recipe, as the user's is. `StoreCommandsTests` fails on a sheet command the binary refuses, and on one it was never asked, naming the line to add.
- [x] **Previews:** both rename acts and the completion as recorded (ADR 0004).

**Decisions made while building** (2026-10-21, each recorded for veto):
- **The GGUF sheet ties precision to Register and levels**, the item's default: the sheet never offers a command the CLI refuses.
- **Rename the backend is offered on every backend, cloud ones included,** the item's default: `config rename-backend` takes any.
- **The verdicts are recorded by a script** beside the parity fixture's, the item's default.
- **A family is named from the level its listing states, and confirmed by the binary's list.** A GGUF whose listing states no level, because its header cannot be read, is renamed as a backend only. That is conservative, and Stored weights still renames its folder. The recorded sandbox's GGUF is such a placeholder, so the family case is pinned on the listing `ModelsViewModelTests` holds, with the binary's verdict on the command recorded.
- **A 2i test raced its own reads.** `storeOpensTheSheets` waited for the sets and then checked what each set's `models info` says, which is read after them. The first full run failed it. It now waits for the reads it checks.
- **The schemes' `codeCoverageEnabled = "NO"` was put back** in `Apogee.xcscheme` and `PresentationReview.xcscheme`, the user's call, after Xcode re-saved them without it.

**Verified.**
- The `PresentationModels` scheme's own tests: 36 of 36.
- The recorded verdicts: the 89 commands the sheets build or the renames use are taken. The three refusals are the user's two and the latent GGUF one, kept as recorded evidence.
- `check-previews` finds nothing.
- Bare `lib/scripts/cicd.sh --test`: the CLI's 3,610 tests passed. The app's first run failed the 2i race above, at 340 of 341. Once that test waited for its reads, `cicd.sh --app darwin --test` passed 341 of 341, with no Swift warning, the CLI's tree unchanged in between.

**Not done:** a convert button on a set no converter takes, such as a diffusion model or a draft head. That is M3's, once the CLI's M20 states which conversions a set takes.

### 2026-10-21 — `convert-offers-what-the-set-takes` (maintenance item M3): Convert follows what the CLI says a set takes

**Why.** The same run as M2: Stored weights offered **Convert to GGUF…** and **Convert to MLX…** on every SafeTensors set. That included the user's `Wan-AI--Wan2.2-TI2V-5B`, a diffusion transformer no converter takes, whose MLX conversion ran three seconds and failed in mlx-lm's words, and their four Assistant draft heads. The app rightly decides nothing about weights. The CLI's M20 now states the fact as `models info`'s `data.converts_to`.

- [x] **`ModelInfo`'s `Facts` reads `convertsTo`.** It is `nil` where the document does not say: another format, or a binary before M20.
- [x] **Stored weights offers the conversions the set takes** (`StoreViewModel.converts(_:mlx:)`), and `askToConvert` opens no sheet around the view. A set that takes none shows no convert action and no advice to convert. Its facts carry the CLI's own `format:` line saying why. Against a binary before M20, both are offered, and the CLI's refusal stays the answer.
- [x] **Recorded:** `models list` (plain and `--all`) and `models info` for each of the three, from the binary that ships M20, over a sandbox holding the user's three kinds of set: the Wan transformer, the 12B draft head and a Llama snapshot. The sandbox's home is written `/Users/someone/.apogee`, as 2h's store fixtures are.
- [x] **Previews:** a diffusion model and a draft head, each with no convert action, over those documents (ADR 0004).

**Decisions made while building** (2026-10-21, each recorded for veto):
- **No convert buttons, rather than greyed-out ones,** for a set that takes no conversion, the item's default. The CLI's `format:` line in its facts says why.
- **The SafeTensors advice ("convert them to GGUF … or to MLX") is withheld** for such a set too, since it would say the opposite of the facts beside it.
- **The draft head's preview says its base by its width**, as the recording does: the sandbox held no base model, and the preview is named for what it shows.

**Verified.**
- The `PresentationModels` scheme's own tests: 37 of 37. The new case covers no conversion and no sheet for the Wan set and the draft head, the CLI's words in the facts, both for the Llama snapshot, and both again over 2h's older document.
- `check-previews` finds nothing.
- Bare `lib/scripts/cicd.sh --test`: green. The CLI's 3,614 tests passed, and the app's 342 cases, with no Swift warning.

**For the user:** the installed `apogee` predates M20, so until it is reinstalled (`make install`) the app offers both conversions as before, and the CLI's own refusal answers.

## Milestone D — MLX kinds in the app

**Goal.** The app follows the CLI's MLX-for-every-kind track (its 53e–53m, from the 2026-10-21 spike the user asked for). That track makes MLX directories of six kinds (`chat`, `vision`, `embedding`, `drafter`, `video`, `voice`), and adds an MLX engine for clips and voice, and drafters for Gemma 4. The app shows what each set is and offers only what runs it, and its sheets say which engine and converter a run uses. ADR 0003's parity holds: what the CLI does, the app does.

### 2026-10-22 — `mlx-kinds-in-stored-weights` (backlog item 3a): each MLX set says its kind

**Why.** Stored weights read every MLX set as a chat model's weights:
- **Register…** opened the same `config add-backend --type mlx` form for any of them.
- **Use in a new session** was offered on any MLX backend.
- The convert sheet said nothing of which converter would run.

Since the CLI's 53e, each set's record says its kind, and `models info` and `models list` state it.

- [x] **The reads.**
  - `ModelRow.mlxKind` and `ModelInfo.Facts.kind` carry the kind through the business `MLXKind` type: the six words, and any other word kept as written. `ModelRow.kind` already names where a backend runs, hence `mlxKind`.
  - `ModelInfo.Facts` also gains `mlxConversion` (`towers`, `converter`, `text_only`) and `vectorWidth`.
  - A document with no kind, from a binary before the CLI's 53e, reads as before: every MLX set is a chat model's.
- [x] **The kind is said beside the format** (`MLX · vision`) in the table and the details' header. `StoreViewModel.actions(_:)` lists what each set offers:
  - `chat` and `vision` register as before, and a vision set says it reads images;
  - `embedding` registers as an `mlx` entry (`config add-backend --type mlx`, as `config scan --register` does), and the registered set offers **Use for embeddings** (`config set-default-embedding`);
  - `drafter` draws **Pair with its base…** disabled, with a tooltip and an advice line giving the CLI's `config set-backend <base> --draft <path>`. Pairing is 3b's (`Action.pairWithBase`);
  - `video` offers **New clip with it** and **Make it the default** (53l takes an MLX video model as the default);
  - `voice` offers **Open Voice**;
  - an unknown kind offers only **Delete…**, plus **Remove the backend…** if one was registered by hand.

  Register all counts only chat, vision and embedding, as the binary does. The Backends tab offers **Use in a new session** only on an entry that chats.
- [x] **The convert sheet follows the converter the CLI names** (`mlx_conversion.converter`).
  - A towered set says its towers are kept through mlx-vlm and offers **Text only** (`--text-only`), which it passes whenever `text_only` says only mlx-lm's build is offered.
  - Draft heads, Wan sets, voice models and any converter word the app does not know get none of `--register`, `--register-with`, `--text-only` or `--model-type`, all of which the CLI refuses there.
  - Defaults follow the CLI's: bf16 for a drafter or a voice model, 4-bit otherwise. Wan offers only 4-bit and 8-bit.
  - A voice-format set is offered **Convert to MLX…** where `converts_to` says `mlx`. M3 had kept conversions to SafeTensors sets.
- [x] **Recorded from the binary that ships the CLI's 53e–53m**, over three sandbox homes in scratch (the home written `/Users/someone/.apogee`; nothing read `~/.apogee`):
  - one MLX directory of each kind, plus one recorded `music`, before and after `config scan --register`;
  - a Python environment holding only the files the CLI reads to choose a converter.

  That gave 26 documents in `tests/business/reads/Fixtures/` (`store-list-kinds*`, `store-info-kind-*`, `store-info-kinds-*`, `store-list-routes`, `store-info-route-*`, `config-scan-kinds*`, `models-status-kinds-registered`).
  - `store-route-verdicts.txt` holds the binary's refusals of the four flags on a draft head, a Wan set and a voice model.
  - `store-command-verdicts.txt` gains 71 commands, all taken. Its 92 earlier verdicts came back unchanged.
- [x] **Previews:** 22 new previews (20 in Stored weights, 2 in Backends) over those documents, 25 of them embedded byte for byte (ADR 0004). The layer cards name `MLXKind`.

**Decisions made while building** (2026-10-22, each recorded for veto):
- **The open call's default, taken:** the kind is a word beside the format, `MLX · chat` included when the binary says so. Nothing the CLI lists is hidden.
- **Registering an embedding set does not also point the embedding role.** **Use for embeddings** is its own act, since chaining them would silently move an existing role and drop collections built with the old one back to text-only search.
- **Roles needed no change.** The role verbs take any backend, and Roles already lists every backend for every role.
- **A drafter's pairing is drawn but disabled** until 3b, saying the CLI's command, rather than hidden.
- **An unknown kind is read strictly:** only its details and its delete, with no "Check the files" or "Show in Finder".
- **`wan_layout` is not parsed.** The CLI's own `diffusion:` line in the facts already says it.

**Verified.**
- 17 new cases: `rowsCarryTheirKind`, `kindsAndWhatRunsThem` and `factsOfTheKinds`, then:

  | Suite | Cases |
  |---|---|
  | `StoreKinds` | `eachKindSaid`, `eachKindsActions`, `embeddingRegistersForTheRole`, `visionReadsImages`, `drafterPairsLater`, `videoAndVoice`, `olderBinaryReadsAsToday` |
  | `ConvertRoutes` | `toweredKeepsItsTowers`, `onlyTextOnly`, `registeringRoutes`, `noRegistrationWhereNoBackendRuns`, `voiceConvertsToMLX` |
  | `StoreCommands` | `routesNoBackendRuns` |
  | `ModelsViewModel` | `embeddingEntryForTheRole` |

  `convertCommandsTaken` now covers every converter and Text only.
- The `PresentationModels` scheme passed 51 of 51, and `BusinessReadsTests` 34 of 34.
- `check-previews` finds nothing. The layering check's 25 cases pass.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,839 tests and the macOS app's 359 cases passed, with no Swift warning.

**Not done:**
- Sessions, Suites, Settings and Training still list every backend whatever its kind, so an embedding entry can be offered there as a chat backend. The CLI refuses the chat.
- The Backends table does not show the kind; the document scoped it to Stored weights.

### 2026-10-22 — `mlx-engines-and-drafters` (backlog item 3b): engines and drafters in the app

**Why.** Three of the CLI's items changed how something runs rather than what is stored:
- a drafter pairs with its MLX base (53i);
- `video generate` gains an MLX engine (53l);
- `voice say` gains one too (53m).

The app had no draft field on a backend. Its clip and say sheets built their commands with no engine, and its records read none. Since 53m, once the user's Kokoro is converted, a bare `voice say` is refused because two voice models stand, and the Voice page ran exactly that.

- [x] **Drafters.**
  - **Pair with its base…** works on a drafter in Stored weights. Its sheet offers only the backends the binary's own completion offers for it. It runs `config set-backend <base> --draft <drafter path>` and reads the result back. With no base registered, it says so and offers nothing.
  - A paired drafter says whose draft it is and offers **Unpair from <backend>**.
  - A backend's details have a Drafter section in the CLI's own words, with **Unpair** (`--draft ""`).
  - The draft is read from the backend's `models info` `draft:` line (`BackendDraft`). The Backends tab re-reads when shown, so a pairing made in Stored weights appears there.
- [x] **The clip sheet.**
  - Its model field is a picker: the default, each stored video model, or another.
  - A line says which engine the run uses, from the chosen model's kind.
  - `-m` and `--engine` are passed only where one folder holds both a pipeline and its MLX conversion, since the binary refuses `-m <folder>` then.
  - The default is read with `config get models.default_video`. With none set, the sheet says the CLI will refuse.
- [x] **The say sheet.**
  - When the store holds more than one voice model, a picker appears, PyTorch first. The command passes `-m <handle>`, and `--engine` where Kokoro sits beside its MLX build.
  - With one voice model the command is the bare one, as before.
- [x] **The pages.** A clip's and a speech's page say the record's engine and its MLX versions when the record states one. A record with no `engine` is PyTorch and reads exactly as before (`MediaEngine`, read additively by `ClipRecord` and `SpeechRecord`). An MLX clip's first fact is labelled "Model", not "Pipeline".
- [x] **Recorded from the branch's binary, nothing hand-written,** over sandbox homes in scratch, the home written `/Users/someone/.apogee`:
  - `drafts-*`: the list, the drafter's and the backend's infos before and after pairing, the three completions, and the pair and unpair output.
  - `media-*`: the list, both voice models' infos, and the default video set and unset.
  - Four real records (`clip-torch`, `clip-mlx`, `speech-torch`, `speech-mlx`), written by the binary over the CLI's own stub runtimes and a stub ffmpeg, as `cli.video_lifecycle` sets them up.
  - `engine-command-verdicts.txt`: 21 commands, 13 taken and 8 refused as evidence. The refusals include the bare `voice say` naming both models, `-m <folder>` over two models, and the wrong `--engine`.
  - `drafter-pairing-verdicts.txt`: 5 commands. Pairing by path, pairing by handle and the clear are taken. A 3840-wide base and an mlx-lm `chat` base are refused.
  - The 163 store-command verdicts came back unchanged.
- [x] **Previews.** 18 new previews, and 3a's drafter preview renamed. The embedded documents are byte-identical to the fixtures. `PresentationVideo` now depends on `BusinessReads` and `DataProtocol`, added to the project by hand with no re-sort. Its runner is a settable `reader`, as `DownloadsViewModel`'s is.

**Decisions made while building** (2026-10-22, each recorded for veto):
- **The open call's default, taken:** the engine is shown, not chosen, except where one folder holds both. Then the sheet offers the choice, PyTorch first, and names the set's handle with `-m`.
- **The pairing passes the drafter's path,** as the CLI's own `models info` suggests. Completion only chooses which bases to offer, and both forms are recorded as taken.
- **Unpair is offered from the drafter too,** beyond the document. It runs the same command, held to the same verdicts.
- **The clip sheet's picker shows on older binaries too,** wherever the store lists a video model. The commands for the same picks are unchanged.

**Verified.**
- 24 new cases:

  | Suite | Cases |
  |---|---|
  | `Engines` | 6 |
  | `VideoEngines` | 8 |
  | `VoiceEngines` | 4 |
  | `DrafterPairing` | 6 |

  3a's `drafterPairsLater` became `drafterPairs`.
- The bundles touched all passed:

  | Bundle | Passed |
  |---|---|
  | `BusinessMediaTests` | 15 of 15 |
  | `PresentationModelsTests` | 57 of 57 |
  | `PresentationVideoTests` | 13 of 13 |
  | `PresentationVoiceTests` | 8 of 8 |
  | `PresentationAppTests` | 3 of 3 |
  | the layering check | 25 of 25 |

- `check-previews` finds nothing in models, video, voice or app.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,839 tests and the macOS app's 383 cases passed, with no Swift warning.

**Not done:**
- `video compose`, and the engine on Run again, are untouched. The CLI replays a record's engine itself.
- The Backends table does not mark a paired backend, and a drafter cannot yet be paired from the backend's side.
- There is no preview of a pairing that can no longer draft, since no recording of that state exists.

### 2026-10-26 — `chat-choices-follow-kind` (maintenance item M4): only backends that chat are offered to chat

**Why.** Since the CLI's 53h, `config scan --register` registers an `embedding`-kind MLX directory as an `mlx` entry, which never chats: its chat, stream and complete refuse before anything starts. An `mlx` entry over a drafter, video or voice directory chats no better. Since 53e each such row in `models list` carries its `kind`, and 3a's Use in a new session already asked it. Every other place that picked a backend to chat with or to train still offered them all. Over 3a's recorded registered kinds, each such place failed first:
- **the session settings sheet's Configured picker** (also opened by `/model` and ⇧⌘M) and **the execute door's Model**, both offering `["(not configured)", "EmbeddingGemma-300M-bf16", "Gemma4-E4B-4bit", "Qwen3-8B-4bit"]`;
- **Training's Synthesize Teacher,** which opened on the embedding entry, first in the list;
- **adapter promote's Judge and Onto,** writing `--judge EmbeddingGemma-300M-bf16`;
- **Models › Roles' chat row,** the app's only choice of a default chat backend. The CLI takes `config set-default` on an embedding entry, and the chat role then points at a backend whose every chat is refused.

- [x] **One answer to "does this row chat":** `ModelRow.chats` (`business/reads/Models.swift`), from the row's MLX kind. `chat` and `vision` chat. `embedding`, `drafter`, `video`, `voice` and any kind word the app doesn't know don't. A row with no kind chats as before. `ModelRow.chatBackends(_:)` returns the configured backends that chat, in list order.
- [x] **Every chat and train picker asks it.** The settings sheet, the execute door, Synthesize's Teacher, adapter promote's Judge and Onto, and the Roles chat row (`ModelsViewModel.choices(for:)`) all ask it. A chat role already on an entry that can't chat stays in its own picker, shown as it stands.
- [x] **The places that show backends keep every entry:** the Backends tab, Stored weights, Roles' other five rows (the embedding role still offering the embedding entry), and Settings › Backends & keys.

**Decisions made while building** (2026-10-26, each recorded for veto):
- **The default, taken:** a picker doesn't explain an entry it leaves out; its place is the Backends tab.
- **The Roles chat row narrows, while Roles still shows every entry.** The two criteria read as offering and showing: the chat role's picker is a place that offers a backend to chat with.
- **Sessions' two pickers drop the stored weights' placeholder `(not configured)`,** which they offered whenever the store held unregistered sets, over lists from before the CLI's 53e too. It is no backend, and every other picker already left it out. That is the one place an older binary's lists don't get exactly what they got before.
- **A kind word the app doesn't know doesn't chat,** as 3a reads it.
- **Kept, by what the verb does:**
  - **Training's other inputs:** the run's Student, a stored set; merge mode's free-text name; the regimes, pipelines and cycle, which offer no backend.
  - **Suites and the suite chooser,** which offer suites the CLI resolves.
  - **Pair with its base…,** from the binary's completion.
  - **The clip, voice and compose model pickers,** which offer their own kinds.
  - **The free-text model fields.**
  - **The console's completion.**

  Settings never had a default-chat picker; the document's row for it was the Roles chat row.

**Verified.**
- **Recorded** from the branch's binary over a sandbox home with tiny fake MLX directories, each recording repeated byte-identical, the home written `/Users/someone`:
  - `store-list-kinds-by-hand.json`: `mlx` entries over a drafter, a video model, a voice model and an unknown kind, written into the config by hand, since `config add-backend` refuses all four even with `--force`;
  - `store-list-chat-on-embedding.json` and `models-status-chat-on-embedding.json`: the chat role pointed at the embedding entry.
- **21 new cases:**
  - `RowsThatChatTests` (4), `SessionChatChoicesTests` (5), `TrainingChatChoicesTests` (6) and `RolesChatChoicesTests` (4);
  - `BackendsKeepEveryEntryTests` and `SettingsBackendsKeepEveryEntryTests`, one each;
  - each picker's older-binary case among them.
- **Repeat runs:**
  - over 10 iterations: `PresentationModels` 840 of 840, `PresentationSessions` 710 of 710, the training choices 60 of 60;
  - `PresentationSession` 76 of 76, `PresentationSettings` 25 of 25 and `BusinessReadsTests` 65 of 65.

  The layering check passed 25 of 25, and `check-previews` finds nothing in session, sessions, training, models or settings.
- **Six new previews:** the settings sheet, the execute door, Synthesize and adapter promote over the registered kinds; Roles' narrowed chat row; and a chat role already on the embedding entry.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 801 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- **A CLI gap:** `config set-default <embedding entry>` exits 0 and makes it the chat role, while a voice entry is refused ("cannot take a role").

### 2026-10-26 — `backends-say-kind-and-drafter` (maintenance item M5): the Backends table says each entry's kind and drafter

**Why.** 3a said an MLX set's kind in Stored weights. 3b paired a drafter from the drafter's side and showed a backend's drafter in its details, with Unpair. Its record left three gaps on the Backends side, each reproduced as a failing test first:
- **No kind or drafter in the table.** A person scanning their backends couldn't see which ones embed or draft.
- **No pairing from the base.** Someone looking at the model they wanted to speed up had to find its drafter in Stored weights.
- **No "cannot draft" state.** It was never recorded, so a drafter deleted, or a base converted again as mlx-lm's `chat` build, showed only as the CLI's refusal.

Probed on the branch's binary over sandbox homes with tiny fake MLX directories (a vision base, the drafters that pair with it, and a 12B base with its own):
- **`models list` doesn't say a pairing:** it is byte-identical before and after. The base's `models info` states it in its `draft:` and `drafter:` lines.
- **`__complete config set-backend <base> --draft ""`** answers exactly the drafters that pair: the E4B's assistant for the E4B, the 12B's own for the 12B, nothing for mlx-lm's Qwen3, and nothing once the drafter is deleted.
- **With the drafter deleted:**
  - the draft line reads `<path> -- cannot draft beside it: there is no drafter at <path> -- 'apogee models convert <a draft head> --mlx' makes one`;
  - the list row's state becomes `draft`;
  - `check` warns with the remedy `apogee config set-backend <base> --draft ""`;
  - a chat refuses in the same words.
- **A base converted again as mlx-lm's build** refuses with "…is mlx-lm's build (kind chat, recorded)…".

- [x] **The table says an `mlx` entry's kind,** `MLX · vision` over its precision, as Stored weights says it. Every other row is unchanged.
- [x] **A paired base is marked.** A small drafter glyph after its name carries the tooltip "Drafter: <its handle, or its path once the store no longer lists it> -- <the CLI's words>". The glyph turns the warning colour when the drafter can't draft. Each `mlx` entry's `models info` is read with the list, as 3b's `BackendDraft` reads it, and only when the list states kinds.
- [x] **Pair a drafter… from the base's details** (`PairDrafterSheet`), on a `vision` entry with none paired. It offers exactly what the binary's completion offers, then runs `config set-backend <base> --draft <drafter path>` and reads back. 3b's `BackendDraft` and Unpair are reused.
- [x] **A pairing that can no longer draft is drawn in the CLI's words,** from its "cannot draft beside it:" phrase (`BackendDraft.refusal`), as a warning section in the details.

**Decisions made while building** (2026-10-26, each recorded for veto):
- **The default, taken:** a small drafter glyph with the drafter's name in its tooltip, rather than a column of its own.
- **The kind cell is two lines** in the existing 110-pt column, the precision truncated with its full text in the tooltip.
- **An older binary's table costs no extra reads.** Drafters are read only when the list states kinds, and a failed read leaves that backend unmarked.
- **Pair a drafter… is offered only with none paired.** Replacing a pairing means Unpair first, since no verdict exists for replacing one.
- **The sheet's "none offered" line is the app's own,** since the completion prints nothing when no drafter pairs.

**Verified.**
- **Recorded** from the branch's binary over sandbox homes, the home written `/Users/someone`, a second run byte-identical: 18 documents in `tests/business/reads/Fixtures/`.
  - **The pairing:** `base-pair-*`, the list, each base's completion and `models info` paired and not.
  - **The drafter deleted:** `draft-gone-*`, its list, info, `check` and chat refusal.
  - **The base converted again:** `draft-rebuilt-*`, its list, info, `check` and chat refusal.

  The rebuilt homes needed a placeholder Python environment with an `mlx_lm` marker, so the CLI's checks reach the draft rung. `drafter-pairing-verdicts.txt` gains two lines:
  - the 12B pairing, taken;
  - the E4B paired with the 12B's drafter, refused: "…drafts for a 3840-wide gemma4 model… is a 2560-wide gemma4 model -- a drafter runs only beside a base of its family and width".
- **9 new cases** in `BackendsKindAndDrafterTests`:
  - `tableSaysEachKind` and `pairedBaseMarked`;
  - `pairFromTheBase`, `pairFromTheBaseHeldToVerdicts`, `pairsAndReadsBack` and `noDrafterOffered`;
  - `drafterGone` and `baseRebuiltAsChat`;
  - `olderBinaryKeepsTodaysTable`, which held before and after.
- **Repeat runs:** `PresentationModels` passed 930 of 930 over 10 iterations. The layering check passed 25 of 25, and `check-previews` finds nothing in models.
- **9 new previews:**
  - the table with kinds and a paired mark;
  - the base's details with Pair a drafter…;
  - the sheet's offered drafters, and none offered;
  - the deleted drafter, in the table and the details;
  - the rebuilt base;
  - the glyph;
  - an older binary's table.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 810 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- Use in a new session is still offered on a base whose drafter can't draft, as before.
- **CLI gaps:**
  - `models list` names no pairing, costing a `models info` per `mlx` entry on each refresh;
  - the completion gives no reason when nothing pairs;
  - `chat -m <backend>` refuses with every backend's problem, not only the named one's.

---

## Milestone E — The audit's GUI findings

**Goal.** The app does what it says. The CLI/GUI audit of 2026-10-22 (the user's call: a very thorough bug and feature-disparity audit over both applications, every issue a v0.2.0 item in its queue) ran 14 dimensions with probes against the branch binary and a three-lens adversarial panel per finding. 68 of the 111 surviving findings were the GUI's: 37 bugs, 27 disparities and 4 doc drift, 11 of them high. Track 4 groups them into twelve items:
- the session's lifecycle and honesty (4a, 4b);
- the children's supervision (4c);
- rendering (4d);
- the store's jobs (4e) and the media studio (4f);
- the console and every argv (4g);
- the parity screens' correctness (4h);
- the verbs still unreached (4i, 4j, 4k);
- the docs and the contract board (4l).

The CLI's 43 are its track 55 ([CLI Milestone AR](../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings)). Every fix stays a machine-mode client of the binary, tested hermetically.

### 2026-10-25 — `private-sessions-survive` (backlog item 4a): private sessions survive navigation, stop and quit

**Why.** The audit found three high-severity breaks in the session lifecycle; three hunters found the first independently. Each still reproduced on today's source, as a failing test first:
- **Leaving Sessions wiped a private conversation.** `leftView` paused the live session unconditionally. A `--private` session has no chat id to resume by, so the return started an empty one; the CLI refuses `--private --resume`.
- **⌘Q never finished the shutdown.** The app had no delegate, only the window's `onDisappear` task, which nothing waited on. The stop escalation and the private fallback's `chats delete` never ran. A window reopened inside the 5-second grace started a second child beside the first.
- **A stop during a launch did nothing.** `session` was set only after `locateBinary` returned, so a stop, pause or switch in that window missed it, and the child spawned moments later ran unsupervised.

- [x] **A private conversation stays live when its place is left.** `isLostIfEnded` names a session that can't come back: `--private`, no chat id, a turn sent, its child not ended. `leftView` never pauses one, so it stays the one live child. Switching away from it, or opening a new session, asks first, in the dialog "Stop the turn and switch?" uses: "This private conversation can't be reopened", with **End it and switch** or **Keep it running**. Ordinary sessions still pause on leave.
- [x] **Quit waits for the sessions.** `AppDelegate` (`app/Views/AppDelegate.swift`, through `@NSApplicationDelegateAdaptor`, now the owner of `AppViewModel`) asks `Termination` (`app/ViewModels/Termination.swift`, AppKit-free). It answers `.terminateLater` while a child is live or launching, or a private session's delete is still to run.
  - `SessionLibraryViewModel.quit()` stops new starts, ends the open session (joining a close already in flight), runs the delete, and only then replies.
  - The child is waited for at most `SessionViewModel.grace` (5 s) on an injectable clock, then given up. A run-loop backstop at 15 s replies even when the main actor can't run.
  - Each step is written to the system log (subsystem `com.quantumcompiler.apogee`, category `quit`).
- [x] **A reopened window waits for the close in flight.** `start(inView:)` joins it first, so no second child starts during the grace.
- [x] **A launch can be called off.**
  - Each launch has a number. A stop or pause calls it off, and a newer launch supersedes it.
  - After `locateBinary` returns, a launch opens its child only if it is still current. That open is synchronous on the main actor, so no stop lands between the check and the spawn: the late child is never made.
  - A pause during a launch now pauses. `shutDown()` is final and joinable, so a settings restart can't relaunch after a stop.
  - `switchTo`, delete and start go through one `replace()`, which closes what is open before making the next.

**Decisions made while building** (2026-10-25, each recorded for veto):
- **The defaults, taken:** a private session stays live when its place is left, and a switch away asks; quit waits at most the 5-second grace per session.
- **A child still alive at 5 s** gets its escalation's SIGTERM, but the SIGKILL step at 7 s doesn't run once the app has exited. A bound of grace + 2 s would guarantee the kill.
- **An empty private session** (no turn sent) has nothing to lose: it pauses on leave, and a switch asks nothing.
- **A private conversation with a turn running** asks the private question, whose message says the turn stops too.
- **Closing the window and ⌘Q end a private conversation without asking.** The criterion's "never without the person's say" is navigation's.
- **The `--no-recall` fallback's private session** is resumable by its chat id, so it keeps pause-on-leave.
- **Nothing starts while the window is closed or the app is quitting.** That also stops ⌘N with the window closed from starting a child no window shows.
- **The backstop** isn't in the document. A scratch AppKit probe showed `.terminateLater` services the main actor only when the quit comes from the event loop (⌘Q, the Dock, a logout). Asked from inside a main-queue job, the shutdown could never run and quit would hang. Nothing in the app quits that way today.

**Verified.**
- 28 new cases: `SessionLaunchTests` (4), `SessionLifecycleTests` (17) and `TerminationTests` (7). Among them:
  - `leaveAndReturn`, `leaveMidTurn` and `switchAsks`: the private conversation kept;
  - `stopWhileLocating`, `pauseWhileLocating`, `switchWhileLocating`, `returnAndOpenAtOnce`, `twoSwitchesAtOnce`, `restartWhileLocating` and `stopDuringRestart`: no child left;
  - `reopenWaitsForTheShutdown`;
  - quit's order, recorded by a shared ledger (the private session's stop, then `chats delete`), and a child that won't die given up when a scripted clock reaches 5 s;
  - a lifecycle matrix checking at most one live child after every step.
- The module schemes: `PresentationSession` 48 of 48, `PresentationSessions` 42 of 42 and `PresentationApp` 11 of 11, each also over 10–15 repeated runs. The layering check passed 25 of 25, and `check-previews` finds nothing in session, sessions or app. One new preview: a private conversation's end waiting on the person's word.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 477 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app: running it would write the person's preferences and reach real discovery. Scratch AppKit probes covered the delegate's mechanism.
- `newSession(on:)` leaves its chosen model pending when the question is dismissed, so the next ⌘N uses it. That predates this item, but the private question makes it easier to hit.
- Supervised jobs at quit are 4c's.

### 2026-10-25 — `session-lines-honest` (backlog item 4b): the session's lines and transcript stay honest

**Why.** The audit found eight places where the app's view of a session drifted from the session. Each reproduced on today's source as a failing test first:
- **A `set` or `detach` sent mid-turn was shown applied, and a pending question ate it.** The CLI drops every non-answer line while a card waits (52d; `machine-mode.md` says so since the CLI's 55b). On the branch binary, a `set` sent under a question produced no notice and left no `params` in the saved chat, while the sheet and chips claimed it.
- **Pause/resume relaunched on the launch's settings.** A `set model` never reached `launch.settings`, so `--resume <id> -m <old>` won over the switch.
- **A card orphaned by a cancelled or failed turn stayed answerable,** and its answer went out as a stray line.
- **Temperature could never be set at launch,** though `chat` and `execute` both take `-t/--temperature`.
- **Live events matched earlier rows by turn number alone.** The CLI's 55b reconciled the counts, but they still collide across children: after a stopped turn the live numbers run 1, 2, 3 while the saved chat counts [1, 2] and resumes at `next_turn` 3. A binary before 55b resumes a countless chat at 1 over earlier rows 1 and 2.
- **A resumed chat's standing attachments had no chips,** so `/detach` stayed disabled though the CLI accepts it.
- **`chats show`'s warning was lost.** The read kept the document only, so a chat with a dropped message looked unbroken. The resume `notice` events were already drawn.
- **A rename of the open session was undone by its next turn** on a binary before 55b.

- [x] **Lines wait for a moment the CLI takes them.** `set`, `attach`, `detach` and `compact` are held (`HeldLine`) while a turn runs or a card stands, and go in order once the status is ready (or connecting) with no card open, before anything the person sends next. Settings and chips change only when the line is sent. The sheet starts from the intended settings, and a strip under the input says what is waiting.
- [x] **Relaunch carries what the session runs.** Every `set` sent updates `launch.settings`. A value the CLI refuses by name (`set model: no backend named 'nosuch'`) is put back, so a relaunch never carries it either.
- [x] **A dead card closes.** Cards close on the turn's result or error, on the child's end, and on pause or reopen, and are drawn "not answered: the turn ended first". `answered(_:with:)` says whether it took the answer, and only a taken one is written.
- [x] **`-t` at launch.** `ChatLaunch` passes the temperature, and the sheet always offers it; on a binary without `set`, applying it restarts the session with `-t`.
- [x] **Rows know their range.** `TranscriptItem.range` is 0 for the turns read back, then 1, 2, … for each child, and live events match on (range, turn). No live usage or result lands on an earlier row, and a result-only live answer is never dropped.
- [x] **A resumed chat's attachments have chips,** seeded from the names `chats show` lists. The branch binary took `detach` by that name.
- [x] **The read's warnings are drawn.** `SavedChat.warnings` decodes the document's `warnings` (`nil` when the key is absent). A `.warning` row heads the earlier turns in the CLI's words. Only a document with no `warnings` key falls back to the read's stderr, which `ReadRunner` now keeps on success (`ReadWithStderr`, `CommandRunning.readKeepingStderr`).
- [x] **A rename sticks.**
  - **A binary that keeps outside renames:** one announcing schema `2026-10-25` or later. There Rename runs `chats title` in place and reads back. On the branch binary such a rename survived the session's next turn.
  - **An older binary:** the open session is paused, renamed and resumed. The installed `2026-10-24` build put its own title back on its next turn. Mid-turn, the rename waits for the turn to end ("Renaming when this turn ends"); if the child ends first, `chats title` alone does it.

**Decisions made while building** (2026-10-25, each recorded for veto):
- **The defaults, taken:** lines queue rather than apply and roll back; Rename is conservative, in place only on a binary that keeps outside renames.
- **The schema date tells the two apart.** 55b landed before 55h moved the date to `2026-10-25`, so a development build between them announces `2026-10-24` and takes the pause path, which still renames correctly.
- **A refused value is put back**, or a relaunch would carry `-m nosuch`, which the binary accepts at launch while every turn fails.
- **Ranges are per child, not only earlier and live,** since on the branch binary the collision is between children.
- **Cards also close** when the child ends and on pause or reopen.
- **`attach` and `compact` are held too,** and connecting counts as between turns.
- **Chips reset on a fresh start,** so a dead session's chips don't carry over.
- **A read's warning has its own row,** in the warning colour, rather than a quiet notice.
- **Temperature is written as the `set` line writes it** (`1`, not `1.0`).

**Verified.**
- **Recorded** from the branch's binary over sandbox homes, against the scripted mock, the home written `/Users/someone`:
  - **Session streams** (`tests/data/protocol/Fixtures/`): `question-then-set`, `set-between-turns`, `set-refused`, `set-model`, `question-cancelled`, `permission-cancelled`, `resumed-switched`, `stopped-turn`, `resumed-after-stop`, `resumed-warnings`, `resumed-detach` and `rename-outside`.
  - **Reads** (`tests/business/reads/Fixtures/`): `chats-show-warnings.json`, `chats-show-after-stop.json`, `chats-show-attached.json` and `chats-list-renamed.json`.
  - **The older shapes,** from the installed binary over a sandbox, the saved chats hand-edited to drop a field or a count:
    - `chats-show-warnings-older.json` and its `.err`;
    - `chats-show-countless.json` and `chats-list-renamed-older.json`;
    - `resumed-warnings-older.jsonl`, `resumed-countless.jsonl` and `rename-outside-older.jsonl`.

  The previews embed them byte for byte.
- 25 new cases:
  - `SessionLinesTests` (10), among them `theCLIDropsASetMidQuestion`, `setHeldUntilTheTurnEnds`, `relaunchCarriesTheSwitch`, `refusedValuePutBack`, `deadCardTakesNoAnswer`, `resumedChips` and `olderWarningsFromStderr`;
  - `TranscriptHonestyTests` (6): `deadCardCloses`, `deadPermissionCloses`, `endedChildClosesItsCard`, `earlierRowsApart`, `usageOnTheLiveRow` and `childrenApart`;
  - `SessionRenameTests` (5), among them `whatEachBinaryKeeps`, `renamedInPlace` and `pausedRenamedResumed`;
  - `ChatLaunchTests` +1 (`temperatureAtLaunch`, the old "no launch flag" expectation updated), `SavedChatTests` +2 and `ReadRunnerTests` +1.
- The module schemes: `PresentationSession` 64 of 64 and `PresentationSessions` 47 of 47; `BusinessReads` and `BusinessSession` 68 of 68. The layering check passed 25 of 25, and `check-previews` finds nothing in session or sessions.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 502 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- On a binary without `set`, a settings change made mid-turn still restarts at once: that is a restart, not a line.

### 2026-10-25 — `children-end-cleanly` (backlog item 4c): children end cleanly and are seen

**Why.** The app supervises every `apogee` child through `infrastructure/process` and `business/jobs`. The audit found five defects in that machinery; all five still reproduced on today's source, 7 cases failing first:
- **A grandchild holding stdout wedged a stopped job.** The reader waited for stdout to close, and only stderr had the 2-second failsafe. Once the child had exited, a grandchild ignoring SIGINT was never terminated, and SIGKILL went to the child's pid alone. The job's card ran on, Stop apparently ignored.
- **`FoundationChild` leaked.** Both pipe handlers and the failsafe captured it strongly, and the timeout path never cleared a handler.
- **A failing binary override fell back silently.** Discovery returned the override's failure reason, and the session library and the session both dropped it, so the app ran another binary than the one pointed at.
- **A profile that printed its environment poisoned `PATH`.** `ShellPath` took the text between the marker's first two occurrences, and `env`, `export -p` or `set` printed the marker's variable first.
- **The monitor missed the heaviest children.** It listed the session, downloads and tasks only: no video, compose, voice, training, graph, knowledge, symphony, review or console run.

- [x] **A stopped job always ends.**
  - Foundation's `Process` already starts each child at the head of its own process group, and what it starts joins that group. A scratch program showed it, and `ProcessGroupTests/ownGroup` pins it, so no `posix_spawn` rewrite was needed.
  - `interrupt()`, `terminate()` and a new `kill()` now all signal the group, whether or not the child itself still runs, falling back to the pid only if no group exists. `groupRunning` says whether anything of the group is left.
  - Stdout has a failsafe 2 s after the child's exit, as stderr has.
  - `SupervisedJob.stop` escalates (interrupt, terminate after the grace, kill 2 s later) until the job has ended and nothing of its group is left. Its card still ends within the failsafe after the run exits.
  - `MachineSession` keeps its steps, now reaching its group, and ends within 2 s of the child's exit. It doesn't wait for leftovers in its group, which would stretch 4a's 5-second quit.
- [x] **No child outlives its process.** Every handler captures weakly, and both are cleared at end of stream, at the failsafe, on a failed start and on release.
- [x] **The binary in use is the one said.** `DiscoveryOutcome.skippedOverride` picks out a passed-over override.
  - The library says it once per launch, as a toast: "Not using <path>: <reason> -- running <binary> instead". A session that locates for itself reports it through `onLocated`.
  - Settings › General shows "Not using <path>: <reason>. This launch runs the one above." in the warning colour. "From the next launch: …" now appears only for an override chosen since launch.
- [x] **The marker stands alone.** `ShellPath` counts the marker only as a whole line, with the `PATH` on the line between two of them. The command asked now starts with `/bin/echo;`, so the marker begins a line even after a profile's output with no trailing newline. Checked against the real `/bin/zsh` with a profile running `env`, `export -p` and `set` in a temporary home.
- [x] **Every child is seen: `ChildRegistry`** (`business/jobs`). It is thread-safe, one for the app (`.app`), each entry carrying an id, kind (session or job), label, pid and that child's own Stop.
  - **Supervisors register, never screens.** A `SupervisedJob` registers when it spawns and leaves just before its end is announced. `MachineSession` registers as "Session: starting" and is renamed "Session: <model>" once the session says its model.
  - **Every job registers,** because every job-running place goes through the composition's one `JobStarter`: Downloads and the store jobs, Tasks, the console, Graph, Records, Collections, Training, Symphonies, Review, Settings' fix, Video, Compose and Voice. Jobs a screen keeps only in a local, such as installs and previews, are seen too.
  - **What reads it.** `AppViewModel.liveChildren` reads the registry. `DownloadsViewModel.children`, `TasksViewModel.children` and `SessionViewModel.processIdentifier` are gone. A row's label is the command words before the first option.
  - **At quit,** `Termination` stops the running jobs beside the session: a 3-second grace, SIGTERM, then SIGKILL at 5 s, inside 4a's bound, each said in the quit log.

**Decisions made while building** (2026-10-25, each recorded for veto):
- **The default, taken in substance:** children run in their own process group and Stop reaches it. Foundation already makes the group, so the spawner stayed as it was, and the signals were what changed.
- **Stop keeps escalating until the group is empty,** while the card ends within the failsafe.
- **The session doesn't wait for its group's leftovers,** to keep 4a's quit bound.
- **The monitor's labels became command words** (`apogee models pull org/model`) rather than "Download: …" and "Task: <goal>".
- **Quit also ends the running jobs.** The document left this optional.
- **New project edges:**
  - `BusinessSession` and `BusinessSessionTests` now depend on `BusinessJobs`, so the session can register.
  - `PresentationAppTests` gained `BusinessJobs`, `BusinessReads`, `DataProtocol` and `InfrastructureProcess`.

**Verified.**
- 24 new test functions:

  | Suite | Tests |
  |---|---|
  | `ProcessGroupTests` | 5 |
  | `ShellPathTests` | +1, over `env`, `export -p` and `set` |
  | `JobGroupTests` | 1 |
  | `ChildRegistryTests` | 4 |
  | `MachineSessionRegistryTests` | 2 |
  | `OverrideSkippedTests` | 3 |
  | `SettingsViewModelTests` | +2 |
  | `ChildrenSeenTests` | 4 |
  | `TerminationTests` | +2 |

  `ChildrenSeenTests` runs every job-running place and the session over a fake `apogee` script in a temporary home.
- **Repeat runs:**
  - the process-related bundles: 12, then 15 iterations, all green;
  - the whole plan: four runs of 10 iterations each. The one failure was `GraphViewModelTests/rebuild()`, which fails the same way on an untouched copy of HEAD, 4 of 10 iterations: the test reads the toast before the view-model has said it.
- The module schemes: `PresentationApp` 17, `PresentationSettings` 12, `PresentationMonitor` 2, `PresentationSessions` 50, `PresentationSession` 64, `PresentationModels` 57 and `PresentationTasks` 10, all passing. The layering check passes, and `check-previews` finds nothing in settings, monitor, app, sessions, session, models or tasks.
- Two new previews:
  - General with your apogee passed over, the installed one running;
  - the monitor with media and training runs beside the session.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 526 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- `GraphViewModelTests/rebuild()`'s flake predates this item and is left for an item that touches the graph (4k).

### 2026-10-25 — `markdown-code-edges` (backlog item 4d): Markdown and code keep every line the model wrote

**Why.** M8 and M9 render chat answers as Markdown and colour code. The audit found five places where the rendered answer lost text the model wrote. Each reproduced on today's source, 10 of the 48 styles tests failing first:
- **Reference-style definitions vanished.** Foundation reads `[1]: https://…` as link metadata, so `Sources:` followed by two definitions drew only `Sources:`, and while streaming the line flickered `[2`, `[2]:`, then nothing. It also resolved `[text][1]` into a link and `[^1]` into one pointing at "note". The CLI prints them all as text: its `link()` accepts only `](` (`markdown/inline.cpp:171`, `:187`).
- **Short answers were held back whole.** Digits, space and pipe were construct markers, so `128 * 8 = 1024`, `42` and `2 + 2 = 4` streamed as nothing.
- **A pipe-led paragraph hid for the whole stream,** and a completed numeric table row lagged a delta.
- **A finished answer ending at a bare fence opener drew that line as nothing.** The fallback ran only when parsing threw, and `` ```python `` parses to zero runs.
- **`CodeLexer` read a shift as a here-document,** so `echo $((a<<b))` and Ruby's `x<<Const` coloured the rest of the block as a string.

- [x] **Definitions kept, as the CLI prints them.** Before parsing, a noncharacter (U+FDD0) goes between `]` and `:` on any line that could be a definition, inside quotes and list items too. Foundation then reads the line as text, linking its address as a bare URL as the CLI does (`bare_url`, `inline.cpp:284`). The mark is removed from every run afterwards. Footnote definitions and reference uses (`[text][1]`) stay as written.
- [x] **Hold-back is bounded**, streaming only:
  1. **A marker line is held.** The last line is held when, after its indentation and any `>` or list marker followed by a space, what remains is one of:
     - nothing;
     - only backticks or only tildes;
     - only `#`s;
     - only `-*_=+` and blanks;
     - a 1–9 digit number and `.` or `)`;
     - only `|`, `:`, `-` and blanks with at least one `|`.
  2. **A possible table header is held.** When the last block is a top-level paragraph ending in a `|`-led line whose line before is not `|`-led, that line alone waits. Any next line, or a blank one, ends the hold.

  Nothing else is ever held. A real table still never flickers: its header waits under the second rule and its delimiter under the first, and when the delimiter's newline arrives the table draws whole. A lone `|` starting a new row waits too.
- [x] **A finished answer never draws a non-empty line as nothing.** An answer ending at a fence-shaped line is parsed with a probe line (U+FDD1) appended, which shows whether the fence opened with nothing under it. If it did, that line is drawn as written. Any other parse yielding nothing for non-blank text draws the text as written.
- [x] **A shift is a shift.** `isShift` decides from what precedes `<<` on its line alone, so a streamed line never changes colour. `<<` is a shift:
  - directly after a letter, digit, `_`, non-ASCII byte, `)`, `]`, `}` or quote, with no space;
  - after another `<`;
  - inside a `((` its line opened and hasn't closed.

  Real here-documents (`cat <<EOF`, `<<-EOF`, `<<~EOF`, `<<'EOF'`, Ruby's `<<~SQL`) still colour as strings, and `<<<word` here-strings no longer do.

**Decisions made while building** (2026-10-25, each recorded for veto):
- **The default, taken:** definitions render as plain text lines, never resolved, and a reference use stays as written, as the CLI does it.
- **Footnote definitions are kept the same way,** since Foundation dropped them too.
- **The fence fallback is wider than the finding:** prose then a bare opener (`Intro` then `` ```python ``) is drawn too, and a finished lone `#` draws as written.
- **A `|`-led line straight under prose waits,** where M8 drew it as prose and reshaped it when the table formed. An answer that is only one `|`-led line appears when it finishes.
- **`1.` is still held, as M8's test requires, and a bare `1` is not,** so a delta ending between the digit and the dot can show `1` for one redraw.
- **`cat<<EOF` with no space is no longer coloured as a here-document,** a deliberate miss under "prefer false negatives".
- **`[1]:https://…` with no space:** Foundation links the address and the CLI doesn't. The text is kept either way; the autolink difference predates this item.

**Verified.**
- **Recorded** from the branch's binary over a sandbox home, driven with the app's `chat` arguments and hello against the scripted mock in 8-byte chunks, the home written `/Users/someone`:
  - `edges-answer.jsonl`, 812 bytes in 102 deltas: a short arithmetic line, a table straight under prose, a here-document and shifts in bash and Ruby, a reference use with two definitions, two lines of pipe-led prose, and a final `42`;
  - its `chats show`, `chats-show-edges.json`.

  The deltas, the result and the read-back all equal the scripted answer. The CLI's own rendering of the same answer, replayed from a terminal session, is pinned as the oracle's literals.
- 13 new cases:
  - `MarkdownTests` +8: `probeMatrix`, `linkDefinitionsAsTheCLIPrintsThem`, `definitionsStream`, `onlyMarkersWait`, `pipeRuleBounded` (two tables cut at every character), `finishedAtAFenceOpener`, `edgesAnswer` and `edgesStreamedInPieces`;
  - `CodeLexerTests` +5: `shiftsAreNotHeredocs`, `realHeredocs`, `edgesAnswer`, `everyCutOfTheEdges` and `edgesStreamedInPieces`.

  M8's and M9's tests stay green, `everyCut` and `streamedInPieces` among them.
- **Mutation:** all 12 mutants were caught. One survived at first, and a lone-`#` case was added for it.
- **Measured** (Release, median ms, the old and new code compiled alike and run interleaved):

  | Input | Before | After |
  |---|---|---|
  | M8's answer, 1.2 KB | 0.40 | 0.40 |
  | M8's answer ×40, 48 KB | 19.1–19.2 | 19.1–19.2 |
  | 1,500 streaming re-parses, 12 KB (mean) | 2.38 | 2.38 |
  | M9's code answer | 0.356 | 0.372 |
  | the edge answer ×56, 48 KB (112 definitions) | 17.2 | 19.0 |
  | the edge answer in 8-byte pieces, mean / worst | 0.126 / 0.24 | 0.132 / 0.28 |

  - **M9's code answer** pays 0.016 ms for the probe line, since it ends at a fence.
  - **The edge answer's 10%** is Foundation now parsing the definitions as visible text; the marking costs 0.15 ms.
  - **Shell and Ruby with shifts** lex in 0.38 ms at 48 KB. They were faster before only because the false here-document made the rest of the block one string.
- The module schemes: `PresentationStyles` 48 of 48 (was 35) and `PresentationSession` 64 of 64. The layering check passed 25 of 25, and `check-previews` finds nothing in styles or session. Six new previews draw the edge answer streaming and finished.
- **A flake the gate hit, fixed.** The first full gate failed on `GraphViewModelTests/rebuild()`, which fails about 4 runs in 10 on an untouched HEAD (found by 4c's repeat runs). The view-model clears `isBuilding` before the read-back that follows a build, and says its toast after it, so the test's wait for `!isBuilding` could end before the toast. The test now waits for both; `GraphViewModelTests` then passed 30 of 30 iterations. The view-model is unchanged.
- `lib/scripts/cicd.sh --test`, run again with nothing else: green. The CLI's 3,992 tests and the macOS app's 539 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- Three edges past this item still draw nothing:
  - an empty fenced block in the middle of an answer;
  - `Text` then a lone `#`, finished;
  - a fence left open inside a list item or quote at an answer's end.

### 2026-10-25 — `downloads-and-store-jobs` (backlog item 4e): store jobs keep their identity

**Why.** The Models place's Jobs and Stored weights tabs (2h, 2i) run pulls, conversions and quantizations as supervised jobs. The audit found four defects, and all four reproduced on today's source as failing tests first:
- **A quantization's registration could bind the wrong GGUF.** It took the first GGUF in a store-wide before/after difference, so with jobs running side by side `config add-backend Llama3.2-1B-Instruct-Q4KM` was given Gemma's file. A failed first read became an empty snapshot, after which every GGUF counted as landed.
- **A finished chain offered a resume.** The CLI prints `from here on, if this stops, resume with: …` after the pull stage even on success (`model_chain.cpp:68`), and a standalone convert ends with a suggested quantize. The app offered either as a resume on a done job.
- **Run again and the named resume re-entered store jobs as plain pulls:** no steps, no registration, no before-snapshot, and an elapsed time that ran forever.
- **Delete swallowed the CLI's words.** The stranded-backend warning and its two doors (CLI M14) were dropped whenever a `done` string was passed, and the confirm dialog named no backend.

Probed on the branch's binary over a sandbox:
- **`models quantize` takes no `--output-format`.** Its plain output names the stored file on a line of its own (`already quantized:` and the path when asked again). The tiny F16 files it quantized were built as the CLI's own tests build them.
- **`models delete <handle>` without `--yes` prints a plan** and exits 0, removing nothing and never prompting. When a backend points in, the plan says `backend 'X' points into this and will stop working until it is repointed.` and the `--and-backends` door.

- [x] **A job registers what it made.** It takes the first line of its own output that is a path to a `.gguf`, asks `models info <that path>` for the set, and accepts it only as a GGUF in the job's own model folder. Then it registers that document's `model_path`. A failed first read fails the job in the read's words, with nothing run (`JobEnd.notStarted`).
- [x] **Steps tick on the job's own work.** The store read-back counts a set in the job's own model folder, or a backend its own run registered: one its output names as `registered <name>` or `already registered: <name>`, or one the app registered after a quantization. Another job's backend never counts.
- [x] **Resume only on stops.** `JobProgress.resumeCommand` answers only for a stopped or failed job.
- [x] **Re-runs keep their identity.** A store job's again and resume keep its kind, steps, registration and model folder, followed by the store follower. They carry the first run's before-snapshot and what it made, so finished steps stay ticked. Pulls record `endedAt` too.
- [x] **Delete says what it will do and what it did, on both tabs.** `DeletePlan` and `DeleteNarration` (`models/ViewModels/DeletePlan.swift`) are shared by Stored weights and Backends' **Delete with its weights…**:
  - **Before:** asking runs the dry plan, and the confirm shows its lines.
  - **The button:** the "and the backends that run from it" choice appears only when the plan names a backend, and `--and-backends` is passed only then.
  - **After:** the CLI's output stays in `DeleteNarrationCard` above the table until dismissed, on every path.

**Decisions made while building** (2026-10-25, each recorded for veto):
- **The default, taken:** the confirm's affected backends come from the CLI's own plan, the delete run without `--yes`, so the app computes no references itself.
- **The narration card shows only when the CLI said more than what it removed.** A plain delete keeps its toast alone.
- **A delete that would strand only a drafter or the default video** offers no second button. The plan's words and the CLI's door afterwards still show.
- **A backend counts only on the run's own words or the app's own registration,** never on the sheet's predicted names, keeping 2i's rule that nothing is predicted.
- **The Backends tab's delete and the convert step's ticking** weren't in the document's files. They are the same defects at a neighbouring seam, closed in this change.
- **A set the CLI found already quantized registers too,** found while fixing the first defect (`alreadyQuantizedRegisters`).

**Verified.**
- **Recorded** from the branch's binary over sandbox homes, the home written `/Users/someone`, re-recorded byte-identical: 36 documents in `tests/business/reads/Fixtures/` (13 `quantize-*`, 23 `delete-*`), plus `backends-status.json` and `backends-complete-rename.txt`.
  - The failed quantization is real: the store was made read-only, so llama.cpp failed.
  - The delete answers by backend name equal those by handle byte for byte, so the `delete-*` set stands for both tabs.
  - **Hand-made:** `tests/business/jobs/Fixtures/pull-chain-done-handmade.jsonl`, transcribed from the events the CLI's `model_chain_test.cpp` pins, since a real chain needs the network and real weights.
- 18 new cases:
  - `JobResumeTests` (3);
  - `JobIdentityTests` (8), among them `concurrentQuantizesRegisterTheirOwn`, `failedFirstReadFailsTheStep`, `doneChainOffersNoResume`, `againOnAFailedQuantizeRegisters`, `resumeKeepsTheStoreJob` and `convertTicksOnlyOnItsOwn`;
  - `StoreDeleteTests` (3), `BackendsDeleteTests` (3) and `StoreJobsTests/quantizeNamesNothing`.

  `StoreJobsTests/quantizeJob` now runs over the recorded quantization.
- **Repeat runs:**
  - the touched suites over 20 iterations, 960 of 960;
  - the whole plan over 10 iterations, twice. One run had one failure, `MachineSessionTests/conversation()`, about 1 in 5,500, in code this item didn't touch (it passed 100 of 100 alone). The other was all green.
- The module schemes: `PresentationModels` 72 of 72 and `BusinessJobsTests` 14 of 14. The layering check passes, and `check-previews` finds nothing in models.
- 12 new previews:
  - a finished chain with no resume;
  - a failed quantization run again, now registering;
  - each tab's confirm, with and without a backend named;
  - each tab after a delete, the backend kept with its doors or removed;
  - the narration card alone.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 557 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- No preview of a failed store read, since none is recorded.
- The design canvas doesn't draw the delete's plan in the confirm or the narration card.
- `MachineSessionTests/conversation()` failed once under a 10-iteration load, a timing race this item didn't touch.

### 2026-10-25 — `media-studio-finished` (backlog item 4f): the media studio, finished

**Why.** Video, Compose and Voice share the media machinery. The audit found seven gaps:
- **The consented runtime install ran with no UI.** It was a local job that nothing drew, `isRunning` ignored it, a failure was never said, and a second generate re-raised the sheet.
- **Compose could not name a voice model.** Once the store holds two, the CLI refuses every composition, Plan included: `the store holds 2 voice models -- name one with --model:`.
- **Compose had no install consent,** only an OK alert.
- **Plan first toasted "Composed <name>.mp4".**
- **The edit preview's dry run was unsupervised.** Stop couldn't reach it, nothing bounded it, and it could run beside a render.
- **Compose dropped the scan's skipped projects.**
- **Captions' burn ability was learned only after a render,** though `check` states it (52e), and Health showed no `skipped` row or its remedy.

Six reproduced on today's source as failing tests first. The seventh mostly did: the retry after a successful install already worked.

Probed on the branch's binary over a sandbox with a stub Python whose pip never runs:
- **Compose without its runtime** says on stderr `torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video`. Under `stream-json` the stream holds only the `job` line. Once video is in, it refuses the same way for `--with voice`.
- **`--voice-model` takes a store handle, id, model or path.** With it, two voice models plan.
- **`check` on this Mac:** both ffmpeg rows `ok`. Behind a wrapper hiding libass's filters, `ffmpeg captions` is `skipped` with the remedy `brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full`.

- [x] **Installs are supervised jobs,** drawn by one shared card.
  - **The card:** `styles/Components/InstallCard.swift` draws running with Stop, then installed, stopped or failed with Dismiss. `InstallConsentSheet.swift` is Video's consent sheet and Voice's inline one, promoted to `styles` (ADR 0002).
  - **Its words** come from `RuntimeInstall` (`business/jobs`), the fold of the install's progress.
  - **The job is kept and counts as a run,** so a second generate is refused and said. `stop()` reaches it, a failure is a toast in the CLI's words, the refused run is retried only on success, and `dismissInstall()` is the card's Dismiss.
  - **On the monitor and at quit** with no wiring, through 4c's registry.
- [x] **Compose names its voice model.** `ComposeForm.voiceModel` adds `--voice-model`. The store's voice models come from one read, `MediaModel.voiceModels(in:)` in `business/media/StoreMedia.swift`, shared by Voice and Compose. The picker appears only with two or more, PyTorch first.
- [x] **Compose asks to install,** as its siblings do. `InstallConsent` (`business/media`) reads the failure and stderr together, and a run retried after video's install that is refused for voice asks again. Other refusals stay an alert.
- [x] **A plan says "planned":** "Planned <name>: nothing is rendered yet -- Render it, on its page, renders the plan".
- [x] **The edit preview is a job.** Stop reaches it, a 5-minute limit is shown in the sheet and said when reached, and it never runs beside a render, nor a render beside it.
- [x] **Skipped projects are listed** under the compositions with their reasons, and the Voice place now draws its skipped records too.
- [x] **Captions are said up front.** `CaptionsAbility` (`business/media`) reads `check`'s `ffmpeg captions` row when the compose sheet opens. The sheet says whether captions burn in or are kept as `.srt`, with the remedy. Settings' `healthRows` (fail, warn, then `skipped` rows carrying a remedy) are what Health draws.

**Decisions made while building** (2026-10-25, each recorded for veto):
- **The default, taken:** the voice-model picker appears only with two or more voice models.
- **One install at a time.** While a `train setup` or `bootstrap` runs anywhere in the app, a refusal isn't offered another; each place asks the children registry.
- **A free output name skips a taken `<name>.srt` while captions are on,** since the CLI's 55c refuses to write over one.
- **Compose reads the store and `check` only when its sheet opens,** so nothing extra runs at launch.
- **A finished install card stays until dismissed.**
- **The skipped-captions `check` was recorded through a wrapper** that hides libass from the real ffmpeg, rather than hand-edited, so its row and remedy are the binary's own words.
- **A refusal with no Python environment at all** (`Run 'apogee train setup' first`) names no set, so it stays an alert with no consent.

**Verified.**
- **Recorded** from the branch's binary over sandbox homes, the home written `/Users/someone`: 23 documents under `tests/business/media/Fixtures/` and `tests/business/reads/Fixtures/`.
  - The refusals with their `.err`, the planned dry run and its project, and the store lists.
  - Both `check` documents.
  - The installs failed and done, against a stub interpreter, so nothing was downloaded or installed.

  **Hand-made:**
  - a `check` with a skipped row and no remedy, shaped as `check.cpp`'s "could not ask ffmpeg" branch;
  - the older `check` with no captions row;
  - the cut-short and moved project documents.
- 43 new cases:

  | Suite | Cases |
  |---|---|
  | `VideoInstallTests` | 8 |
  | `VoiceInstallTests` | 7 |
  | `ComposeStudioTests` (voice model, consent, plan, preview, library, captions) | 17 |
  | `SettingsHealthTests` | 2 |
  | `RuntimeInstallTests` | 4 |
  | `MediaStudioTests` | 5 |
- **Repeat runs:** the Video, Voice, Compose, Settings and Styles schemes at 10 iterations, and Video, Voice and Compose at 30. The whole plan passed two 10-iteration runs, after a race in `drawnAndElsewhere` was fixed by deciding Dismiss and the retry from the install's own fold.
- **New project edges:** `BusinessMedia` → `BusinessReads` and `PresentationCompose` → `BusinessReads`, with their layer cards. The layering check passed 25 of 25, and `check-previews` finds nothing in video, compose, voice, settings or styles.
- **20 new previews:**
  - the install card's four states;
  - each place's install running, failed and done;
  - compose's picker, captions both ways, consent, installing, the planned run, the preview with Stop, and skipped projects;
  - Health with skipped rows.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 600 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- The planned toast is the shell's, so the compose preview shows its words under the page.
- **Two CLI gaps found by the probes, for the CLI's queue:**
  - a runtime refusal under `stream-json` emits no `error` event;
  - compose's two-voice-models refusal names `--model`, where compose's flag is `--voice-model`.

### 2026-10-25 — `console-and-argv-safety` (backlog item 4g): the console and every argv are safe

**Why.** ADR 0003 makes the console (2b) the app's catch-all, and every screen assembles argv for the binary. The audit found five ways a command ran that shouldn't, or ran the wrong command. All five reproduced on today's source as failing tests first:
- **A root option let a carve-out through.** The gate checked the first word only, and the binary takes root options before the command. Of 270 typed lines, 264 started a job: only the six bare carve-outs were refused, and `--release serve`, `--dev reset -y` and every carve-out behind any of 14 root prefixes started.
- **The console had none of the TUI exec line's door and editor refusals.** All 180 door and `$EDITOR` lines started: `chat` ended silently on its closed stdin, and `agents edit` ran `vi` with no terminal.
- **A person's text was read as an option.** `video compose "-m cats brief"` took the brief as `-m`, and `voice say "--draft"` answered "text is required". A brief of `--dry-run` made Compose report the run as planned.
- **A bare `apogee` answered "A quote is left open."**
- **Completion corrupted a quoted or escaped word:** `models info "My Mo"` + Tab became `models info "M"My Model" `, and `My\ Mo` ran with `MMy Model`.

Probed on the branch's binary over a sandbox:
- **Root options:** `-h/--help`, `-V/--version`, `--release`, `--dev` and `--test` take no value; `--config PATH` and `--custom PATH` take one, which may itself start with a dash. They are accepted only before the command.
- **`--` stops the binary's verb dispatch.**
- **Every free-text site takes `--`.** With the text after `--`, each verb took dash-leading text as text; without it, each failed: `voice say`, `video compose`, `video generate`, `task run`, `graph query`, `graph explain`, `chats title`, `knowledge capture`, `knowledge query` and `embed query`.
- **`--output-format` before the `--` is honoured.**

- [x] **The console refuses by the command path.** `ConsoleViewModel.commandPath` skips every dash-leading word before the command, and the value after `--config` or `--custom`, as the TUI's `first_verb` does. It then matches `ParityTable.refusal(of:)`.
- [x] **One refusal table, the TUI exec line's eight, in its order.** Two-word entries are grown in `ParityTable.refusals`, with no second list in the console:

  | Words | Reason |
  |---|---|
  | `chat` | a conversation is a session in the app -- New session (⌘N), or reopen one from Sessions |
  | `execute` | an execute session is a session in the app -- New execute session (⌥⌘N) |
  | `tui` | the app is the full-screen shell's counterpart already -- run it at a prompt for the terminal's own |
  | `agents edit`, `symphonies edit` | opens your `$EDITOR`, which needs a terminal the console doesn't have -- run it at a prompt |
  | `serve`, `reset`, `uninstall` | their carve-out reasons |

  The parity law also fails a refusal of a command the binary no longer has, or one with no reason. ADR 0003 records the set.
- [x] **No user text is ever an option.** `Argv` (`business/reads/Argv.swift`) puts text last after `--` and inserts added flags before it. `ReadRunner`'s `read`, `readKeepingStderr` and `report` put `--output-format json` before the `--`.
  - **Sites changed:**
    - the clip prompt, a new composition's brief and its resume, the speech text;
    - the task goal (run and expansion);
    - the graph query and explain;
    - the chat title;
    - the knowledge capture and query, and the collection search.
  - **Planning** is read from the options only, never from the text.
  - **`ChildRegistry`'s label** keeps the text after `--`, so 4c's monitor rows read as before.
- [x] **Bare `apogee`:** "Nothing to run: type a command after apogee -- Tab offers them, and --help lists them all." Nothing starts, and the line stays in the input.
- [x] **Completion edits what was typed.** The split records where the word being typed starts, as typed. Tab replaces from there and re-quotes the candidate in double quotes, escaping `"` and `\`.

**Decisions made while building** (2026-10-25, each recorded for veto):
- **The default, taken:** the console mirrors the TUI exec line's refusal set exactly. The `edit` reasons don't point at the Library's editor, since that redirect waits on 2e's editor being offered as one.
- **Every dash-leading word before the command is skipped,** as the TUI does, so `-h serve` and `-- serve` are refused conservatively.
  - `--config serve` still runs: there `serve` is a config path, and the binary runs nothing (`neighboursRun`).
  - `agents -- edit x` isn't refused, since the binary won't dispatch past `--`.
- **The text sites beyond the document's list** (knowledge capture and query, collection search, graph explain) take `--` too: a node name can start with a dash, as Objective-C's `-[Foo bar]` does.
- **Left as they were:**
  - option values, which the binary takes whole even when dash-leading;
  - typed names in create, rename and targets, which the CLI validates;
  - `mcp test`'s JSON, which never starts with a dash;
  - `config set` values;
  - `knowledge link`'s identifier.
- **`Argv` lives in `BusinessReads`,** which every caller already imports, so no project edge was added.
- **The console's empty state still names only the three carve-outs,** as the design board draws it (`main.html:1926`).

**Verified.**
- 25 new cases:
  - `ConsoleSafetyTests` (5): the refusal matrix behind every root option, `bareApogee`, `completionEditsWhatWasTyped` and `candidatesQuoted`;
  - `ParityTests` +2 (`refusalSet`, `refusalLaw`), `ReadFormatBeforeTextTests` (2), `ArgvTests` (2) and `JobGroupTests` +1;
  - the argv of each text site: `MediaTextAfterDashesTests` (2), `ComposeTextTests` (3), `VideoTextTests`, `VoiceTextTests`, `TaskTextTests` (2), `GraphTextTests`, `KnowledgeTextTests` (2) and `ChatTitleTextTests`.

  30 existing cases that pinned the old argv order were updated one by one.
- **`engine-command-verdicts.txt` re-recorded** from the branch's binary, after the 3b store rebuilt in a sandbox first reproduced the old file byte for byte. All 21 verdicts are unchanged in the new order. The compose, generate and say streams recorded both ways are identical but for their seeds and times, so they stand. The parity fixture needs no refresh: the classification didn't change.
- The module schemes, all passing: Console 11, Shell 21, Compose 23, Video 22, Voice 16, Tasks 12, Graph 9, Sessions 51 and Knowledge 10. The layering check passed 25 of 25, and `check-previews` finds nothing in any module touched.
- Two new previews: a door refused after a root option, and an editor refused.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 625 cases passed, with no Swift warning. The two layer cards' console and reads lines were then brought up to the new refusals and `Argv`, and the layering check passed alone.

**Not done:**
- No live pass of the app.
- The second word of a two-word refusal isn't held to the binary, since the parity fixture records top-level words only.
- Completing inside an unclosed quote (`"My M` + Tab) still offers nothing, as before.

### 2026-10-26 — `library-tasks-review-correctness` (backlog item 4h): Library, Tasks, Knowledge and Review do what their buttons say

**Why.** The parity wave's screens (2c–2g) shipped fast, and the audit found nine correctness defects across them:
- **Tasks' Resume ran in the home folder,** so the CLI refused it for every task started elsewhere (`… resumes only there -- cd there first`).
- **Resume was offered on an exhausted task,** which the CLI refuses ("spent its round budget … start a new one"). It was never offered on a task killed mid-round: `interrupted` wasn't parsed, so such a task showed Halt and Cancel as if running.
- **A Library save wrote back the copy taken when Save was pressed,** reverting what was typed meanwhile while the verdict said "Saved."
- **A fresh install's Records tab showed a failure,** since `knowledge list` exits 1 by design on a store that never captured.
- **A play's document landed on whichever symphony was selected when it finished.** So did a collections search.
- **Library and Review refreshes kept the old list on a failed read,** with nothing said.
- **Review and Training raised a missing binary as an alert on every visit.**
- **MCP Register and Create dataset selected the new name even when the create was refused.**
- **The Add-target sheet had no lenses control,** though its form carries `--skills`.

Eight reproduced on today's source as failing tests first. The ninth, a sheet with no control, was confirmed by reading the view.

Probed on the branch's binary over sandbox homes with the mock backend:
- **A task killed mid-round** reads `"status":"running","interrupted":true,"process":null`. `task list` carries no `interrupted`.
- **The refusals:** resume outside the task's folder, and resume of an exhausted task.
- **On a fresh store,** `knowledge list` exits 1 with `no knowledge collection named 'knowledge' -- capture a record first with 'apogee knowledge capture'`, while `embed list` exits 0 with an empty list.

- [x] **Resume runs in the task's folder, for exactly the states the CLI resumes.**
  - `TaskView.interrupted` and `isResumable`: halted, cancelled, stalled, failed, or killed mid-round.
  - `isRunning` leaves an interrupted task out, so a killed task shows Resume and no stops, with a line saying Resume reruns the round.
  - Resume says its folder ("in <folder>, where it was started"), and once the app's own run ends the read takes over.
- [x] **No save loses typing.** `DefinitionEditor.saving` makes the agents' and symphonies' editor read-only with a progress line, and the save's result lands on the editor as it stands then. Save and "Put it back" wait for it.
- [x] **A fresh install's Records are empty, not failed.** `RecordsViewModel.neverCaptured` matches exit 1 and the CLI's whole last line, for any collection name. Every other failure, a timeout or a killed read stays a failure.
- [x] **Async results land where they started.** A play's document and progress carry their symphony's name, and another symphony's page says "<name> is playing -- one play at a time". The collections search re-checks its selection.
- [x] **Failed reads are said.** Each list has its own listing state: `LibraryListing`, `ReviewViewModel.Listing`, and Training's six reads (runs, promoted versions, regimes, kits, datasets and the stored models its sheets pick from).
  - **Where it's said:** in the CLI's words, where the list is drawn.
  - **What's kept:** earlier rows, as Records and Tasks keep them.
- [x] **A missing binary is an empty state** in Review, Training and the Library, each tab saying it once, its add and new buttons disabled.
- [x] **Selection follows only a successful create.** `change` returns Bool, and MCP Register and Create dataset select on success alone.
- [x] **Targets pin their lenses.** One `LensesMenu` serves the review form and Add target, feeding `TargetForm.skills`.

**Decisions made while building** (2026-10-26, each recorded for veto):
- **The default, taken:** the editor is read-only while a save runs, then re-enabled with the saved text.
- **A killed task offers no Stop.** The CLI would take `task cancel` on one, but the document offers Resume.
- **Resume's states are the CLI help's list, written out.** A status a later CLI adds isn't offered until it's added here.
- **A missing folder isn't checked before a resume,** to keep the tests hermetic. A missing one fails to start, and the run says so.
- **Closing the editor mid-save leaves it closed.** Text that changed during a save anyway is kept and said unsaved.
- **Choosing another symphony keeps the last play's result,** so going back shows it. A failed play's alert names its symphony.
- **Training's failed reads, and the Library's missing binary,** weren't in the document's lists. They are the same discipline at neighbouring seams.

**Verified.**
- **Recorded** from the branch's binary over sandbox homes, the home written `/Users/someone`; none hand-made:
  - **Task views:** `task-status-interrupted.json`, `task-status-exhausted.json` and `task-list-interrupted.json`.
  - **Fresh stores:** `knowledge-list-fresh.err` and `collections-list-fresh.json`.
  - **Refused creates** (`mcp create`, the command Register runs, since this binary has no `config add-mcp-server`): `mcp-create-taken.err` and `mcp-create-unseeded.err`. Also `datasets-create-taken.err`.
  - **A config missing a comma:** six refusals, `*-bad-config.err`, for agents, symphonies, MCP, review targets, regimes and the store.

  Timeouts, a killed read and status variants are scripted inline. `train status`, `train versions`, `datasets kits` and `datasets list` exited 0 over everything tried.
- 24 new cases: `TaskResumeTests` (5), `LibraryCorrectnessTests` (7), `KnowledgeCorrectnessTests` (4), `ReviewCorrectnessTests` (4) and `TrainingCorrectnessTests` (4). `TasksViewModelTests/controls` was updated: it pinned the old resume.
- The module schemes, each over 10 iterations: Tasks 17, Library 12, Knowledge 14, Review 12 and Training 8. The layering check passed 25 of 25, and `check-previews` finds nothing in tasks, library, knowledge, review or training.
- **26 new previews:**
  - an interrupted task with Resume, and an exhausted one without;
  - the editor during a save;
  - Records on a fresh install;
  - each read failure;
  - each tab with no apogee;
  - another symphony playing;
  - the target sheet's lenses.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 649 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- A killed task's row in the task list still reads running, since `task list` carries no `interrupted` (a CLI gap); its page knows.

### 2026-10-26 — `training-and-suites-parity` (backlog item 4i): training and suites reach the CLI's v0.2.0 surface

**Why.** The full-parity law (the SPEC; ADR 0003) puts every CLI capability on a screen. The CLI's track 53 and the TUI's suite actions shipped after the Training and Suites screens, and the audit found four gaps. Each reproduced on today's source as a failing test first:
- **Adapters were out of reach.** The promote sheet had no `--adapter`, so a ledger holding adapter versions was unpromotable from the screen. The version reader dropped `adapter_path`, and no screen listed or edited a backend's `adapters`.
- **The Datasets tab had no `datasets pull`.**
- **The Suites fit double-counted a shared base.** It added weights per backend, so on recorded data the app said "does not fit: 120.4 GB of 107.52 GB" where the CLI says fits.
- **No chat ran under a suite of its choosing.** There was no `--suite` at launch, no switch and no `/suite`, so a plain chat ran under the global default only.

Probed on the branch's binary over sandbox homes, with a mock trainer and stand-in GGUF headers:
- **`train promote` has its adapter mode** (`--adapter`, `--suite`, `--judge`, `--trainer`, `--keep-fused`) and refuses six ways, from a plain promote into an adapter ledger to `--suite` without `--adapter`.
- **Adapter reads:** `config get backends.<name>.adapters` prints `path[:scale]` per line and no pin. The pin is only in `check`'s Adapters rows. `set-backend --adapters` takes a comma list, and `""` clears it.
- **`datasets pull`** takes no `--output-format`.
- **The CLI states a suite's verdict:** `models status --suite <name> --output-format json` carries `footprint`, a line per member and a total ending `fits`, `fits as far as known`, `it does not fit`, `budget not known here` or `no member holds memory`.
- **The protocol carries no suite.** A `set` naming one is refused whole, and a `/suite` user line is a prompt.
- **A resumed chat runs under its last suite,** so "none" is always `--suite off`.

- [x] **Adapters on the screens.** The promote sheet has an adapter mode, and each mode sends only the flags the CLI takes in it. A refused promote offers "Change the promotion…", which reopens the sheet in the mode the refusal names, the refusal shown. The sheet says what the backend's ledger holds, from `train versions`. Models lists a backend's adapters with scale and pin (`business/reads/Adapters.swift`, `models/ViewModels/BackendAdapters.swift`) and edits them as one list through `set-backend --adapters` (`Views/AdaptersSheet.swift`), read back after.
- [x] **Datasets pull** is a Pull… sheet run as a supervised job. When it ends, **Prepare it** runs `datasets prepare` on the folder the CLI named.
- [x] **The fit is the CLI's.** The Suites screen shows `models status --suite`'s verdict and member lines. Its own estimate, labelled "estimated here", is only the fallback: a weights file counted once, adapters priced by their files.
- [x] **A chat under a suite.**
  - New chats take `--suite` at launch.
  - `/suite`, the session header's suite name and "Choose a suite" open `SuiteSheet`: each suite with the CLI's verdict, and None.
  - An open session switches by stopping and resuming with `--resume` and `--suite`, said in the transcript. A switch made mid-turn waits for the turn.
  - A suite the CLI refuses is put back in its words, the session going on under the old one.
  - Suites gains **Open a chat under it** and **Switch the session to it**.
- [x] **The parity rows** for `chat`, `models`, `config`, `train` and `datasets` name suites, the CLI's footprint, adapters and pull.

**Decisions made while building** (2026-10-26, each recorded for veto):
- **The default, taken:** a live session's suite switch is a pause and resume with `--suite`, said in the transcript; the protocol has no suite on `set`.
- **A switch drops `-m` on the relaunch,** or the model flag would beat the suite's chat member.
- **A refused switch relaunches under the previous suite** on its own.
- **An execute session is offered named suites only,** since `execute` refuses `--suite off`.
- **A private chat with a turn can't switch,** since it can't be resumed.
- **`--force` goes only from a button labelled "anyway",** on a suite the CLI says doesn't fit.

**Verified.**
- **Recorded** from the branch's binary over sandbox homes, the home written `/Users/someone`: 47 documents under `tests/business/reads/Fixtures/` and 7 streams under `tests/data/protocol/Fixtures/`.
  - The adapter reads and `check` rows.
  - The six promote refusals.
  - `models status --suite`'s footprints, a shared base among them.
  - Sessions launched and switched under a suite.

  The stand-in bases are sparse GGUF headers with size records, as the CLI's own tests make them. **Hand-made:** `datasets-pull-handmade.txt`, from `datasets.cpp`'s output lines, since a real pull needs the network.
- **46 new cases:**
  - reads +5, `ChatLaunch` +2;
  - Suites 5, among them `SharedBaseFitTests/sharedBaseAdmitted`;
  - Shell 1 (`trainingAndSuitesRows`);
  - Session 12, `SuiteSessionTests` among them;
  - Sessions 4, Training 9, Models 7, App 1.

  Four existing cases that pinned the old reads or slash list were updated.
- **The module schemes** each passed ten iterations (Session 760 of 760). The layering check passed 25 of 25, and `check-previews` finds nothing in training, models, suites, session, sessions or shell.
- **26 new previews:**
  - the adapter promote sheet and a refused promote reopened;
  - a backend's adapters;
  - datasets pull;
  - a shared-base suite fitting;
  - the suite chooser in a session.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 695 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- `/suite --warm` isn't offered.
- The fallback estimate ignores drafters and the parts of the shared-base key the app can't see.
- **CLI gaps the probes found:**
  - nothing reports which suite a resumed chat runs under;
  - the footprint is text inside a field, not typed JSON;
  - launch refusals and admission lines reach stderr only;
  - `datasets pull` has no stream output.

### 2026-10-26 — `pipeline-and-cycle-screens` (backlog item 4j): the Training place sees pipelines and the cycle

**Why.** The Training place was blind to two of `train`'s four run families. Nothing ran `train pipeline run|resume|status`, and nothing ran `train cycle run|status|halt|resume`. `TrainingStatus` decoded only the pipelines' count and `cycleActive`, shown nowhere. A pipeline ran only inside a regime, with no stage view and no resume. The item waited on the CLI's 55h, since neither family took `--output-format`; 55h shipped 2026-10-25 with their documents and streams.

Probed on the branch's binary over a sandbox with the mock trainer: two named pipelines (one stage; two, the second regressing the first's suite) and the cycle over a queue directory, its breaker at 1.
- **No verb lists the named specs.** Only `__complete train pipeline run --pipeline ""` names them.
- **`train pipeline status`** is `{"data":[…],"object":"list"}`, with rows `{id, kind, spec_name, status, stages, passed, started_at[, completed_at]}`, status `running`, `complete` or `aborted`.
- **`train pipeline status <id>`** is the manifest. Its stages count from 0, the stream's from 1, and a stage still training records `cumulative_score` 0.0.
- **The streams:** `job` (schema `2026-10-25`), `notice`s, `stage {count, index, label, pipeline_run_id, run_id?, cumulative_score?, state}`, `iteration {iteration, iterations, loss, lr, stage, throughput}`, `pass {state, text}`, the `document`, and `error`.
- **Exit codes:**
  - a pipeline: aborted 2, complete 0, refused 1, Ctrl-C 130;
  - a cycle pass: passed or skipped 0, failed 2, refused 1.
- **`train cycle status` with no history** exits 1 (`no cycle history yet`). `train status` carries `cycle: null` until there is one.
- **Halt and resume** answer in prose, all exit 0.

- [x] **The Pipelines tab** lists the named specs, each with Run…, and every pipeline run newest first.
  - **A run's page** shows each stage's word, its run id, its cumulative score once gated, and the iteration while it trains. It is live from the stream while the run is here, then read back from `train pipeline status <id>`.
  - **Resume…** appears on an aborted or failed run and names the stage it starts from.
- [x] **The Cycle tab** shows the backend, the anchor, the state, the failures in a row and the passes newest first, with gate reasons and notes.
  - **Run one pass…** asks in the TUI's words; Halt… and Resume… ask too.
  - **Scheduling stays the person's own** (`launchd` or cron), and the tab says nothing in the app schedules it.
  - **With `cycle: null`** it draws the empty state, never asking the read that exits 1.
  - **A pass running here** is drawn live: its narration and its pipeline's stages.
- [x] **Every run is supervised** through the jobs core: on the monitor (`apogee train pipeline run`, `apogee train cycle run`), with Stop. `JobEvent.stage` gains `pipelineRunID`, `runID` and `cumulativeScore`, and `iteration` and `pass` are typed. `JobProgress.lastSaid` is the header's line.
- [x] **The reads** decode the pipeline list and manifest, the cycle history and `train status`'s `cycle`, `active_pipeline`, `latest_pipeline`, `cycle_active` and `pipelines`, tolerant of unknown fields (`business/reads/Training.swift`).
- [x] **The parity row** for `train` names Runs, Pipelines, Cycle and Regimes, the pipeline and cycle verbs, and "its schedule the person's own, launchd or cron".

**Decisions made while building** (2026-10-26, each recorded for veto):
- **The default, taken:** the Cycle tab offers run-once, halt and resume only, and says scheduling is the person's.
- **Two new tabs** (Runs · Pipelines · Cycle · Regimes · Datasets) rather than sections inside Runs.
- **The spec names come from completion,** since no read lists them, as Graph's do.
- **All five acts ask first:** a pipeline's run and resume, and the cycle's run, halt and resume.
- **Run and resume pass the CLI's defaults only.** `--continue-on-fail`, `--judge` and `--trainer` stay the console's.
- **Resume only for `aborted` or `failed`,** so a manifest a crash left `running` isn't resumed from the app.
- **A score is shown only once its stage is gated** (fusing, passed, failed), hiding the stale score a resumed stage repeats. A cycle score of 0 goes unsaid, as the CLI's table prints `-`.
- **Halt isn't offered while a pass holds the cycle,** and Run one pass is held back while the cycle is halted or held, the reason said. Halt stays available with no history, which the CLI accepts.
- **Pipeline and cycle runs share the place's one job slot.**
- **The confirm's words live in the view-model,** with `isDestructive` choosing the button's role.

**Verified.**
- **Recorded** from the branch's binary over the sandbox, the home written `/Users/someone`; none hand-made:
  - 26 reads in `tests/business/reads/Fixtures/` (`train-pipeline-*`, `train-cycle-*`, `train-status-*`);
  - 14 streams in `tests/business/jobs/Fixtures/`.

  The mid-run documents (a running manifest, `active: true`, the locked refusal) were caught by not reading a run's stream while the reads ran, the pipe's backpressure holding it mid-stage.
- 36 new cases:
  - `PipelineCycleReadsTests` (6);
  - `TrainingStreamTests` (12), every line of every stream typed;
  - `PipelineCycleTests` (16), among them `watchedStageByStage`, `resumed`, `cycleNoHistory`, `scheduleStaysYours`, `runOnceConfirmed` and `haltAndResume`;
  - `ParityTests/pipelinesAndCycleRow` and `ChildrenSeenTests/pipelineAndCycleSeen`.
- **Repeat runs:** Training 33 of 33 over two 30-iteration runs, after three tests were made to wait for the read-back rather than the job's end. App and Shell passed over 10 iterations; the reads, jobs and layering bundles 114 of 114 over 10.
- **Mutation:** five mutants planted in the view-model were each caught.
- The layering check passed 25 of 25, and `check-previews` finds nothing in training or shell.
- **Previews:**
  - no pipelines, and pipelines listed;
  - one running stage by stage, and one failed at a stage with Resume;
  - the cycle with no history, active and halted;
  - the run-once confirm.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 731 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- No design board draws the two tabs.
- `TrainingStatus.running` decodes run objects where the CLI sends ids, so it stays empty. That's harmless, since `recent_runs` carries the running run.
- **CLI gaps the probes found:**
  - **No read lists the named pipeline specs** or their stage plan before a run.
  - **A halt during a pass is lost.** `train cycle halt` taken while a pass holds the lock answers "cycle halted", then the pass's save overwrites it: loaded at `cycle.cpp:403`, written at the end.
  - **A failed pass under `stream-json` writes nothing to stderr,** though `machine-mode.md` says failures are said there too.
  - **A resume's stream and manifest carry the previous attempt's score** on the stage that trains again.

### 2026-10-26 — `settings-sessions-graph-parity` (backlog item 4k): Settings, Sessions, Graph and Library reach their remaining verbs

**Why.** Under the full-parity law, the audit found seven places where a classified screen didn't reach a verb its command owns. Each was console-reachable, the law's floor and not its bar:
- **Saved chats** could not be compacted, inspected or have their recall reviewed from Sessions.
- **Settings could not register one scanned provider** from its row.
- **No surface showed a backend's `server_tools`,** its recorded consent for a vendor to run tools under the person's key (the CLI's 47a).
- **Graph's export, report `--out`, dedupe and delete** had no button.
- **Library could add an MCP server but never remove one.**
- **Knowledge never showed a record's archived conversation** (`knowledge info --raw`, text only).
- **Track-52 capabilities were learned only from a chat session's `session` event,** and a session starts only when Sessions is in view. A launch restored onto another place ran a capable binary on fallbacks, and Settings asserted "this apogee has no config set" without having asked.

Each reproduced on today's source as a failing test first. Two more turned up:
- **"Forget every recalled summary" never worked when recall held anything:** it ran `chats recall --forget` without `--yes`, which the CLI refuses on a pipe.
- **No CLI verb registers one provider.** `register_provider` runs in-process, reached only by the TUI's `r`.

Probed on the branch's binary over sandboxes:
- **`__machine-schema`** answers with an empty home, writes nothing and refuses `--output-format`. It is a JSON Schema 2020-12 document whose `x-apogee` holds `{protocol_version: 1, schema: "2026-10-25"}`.
  - **Its `$defs`:** 31 `event_<type>` and 8 `line_<type>` (answer, attach, cancel, compact, detach, hello, set, user).
  - **The installed 0.1.9** answers `2026-10-24`.
- **Text-only answers:** `chats info` prints `label: value` lines, and `chats recall` a count and a path, never the summaries. `graph dedupe` has `--dry-run`, text only. `knowledge info --raw` marks its part `-- Raw conversation --`.
- **`config get backends.<name>.server_tools`** prints one tool per line, empty when unset. The installed binary refuses: `no such config key`.
- **`graph delete`** asks nothing and keeps the graph's config entry.
- **An MCP server is removed by `config delete-mcp-server <name>`.**

- [x] **Capabilities are known from launch.**
  - **The read:** the library runs `__machine-schema` (`data/protocol/MachineSchema.swift`) right after locating the binary, before any session starts. It runs again whenever the binary's file changes (`data/discovery/BinaryFingerprint.swift`: its number, size or modification date), checked on every place change and every Settings refresh.
  - **The flags:**
    - `streamsJobs` is `event_job`;
    - `hasTrack52` a schema dated 2026-10-20 or later;
    - `keepsOutsideRenames` 2026-10-25 or later;
    - compact and detach their lines.
  - **Per session,** detection stays as a refresh.
  - **Settings** says "this apogee has no config set" only after the binary has said so, and otherwise "hasn't said what it can do".
- [x] **Saved chats:** a row's menu and the inspector offer Details… (`chats info`) and Compact… (asked, then `--yes`, read back).
  - **The open session's own chat** compacts through its session's `compact` line, so the CLI never rewrites a file under a running session.
  - **The inspector's Recall section** shows `chats recall` as printed, with "Forget every summary…" asked first and run with `--yes`.
  - **Compact and recall** are offered only on a track-52 binary.
- [x] **One provider registers from its row.** A row the scan calls `not registered` gets Register…, asked in the TUI's words; Ollama asks for its model. It runs `config add-backend <id> --type <type>`, the entry `register_provider` writes.
- [x] **Consent is visible.** Each anthropic, openai or google backend in Settings shows its consented tools through `config get` (`business/reads/ServerTools.swift`). An older binary's refusal is shown in its words, never as "none".
- [x] **Graph's More menu:**
  - **Export** (HTML, GraphML, Mermaid) and **Save the report**, each through an `NSSavePanel` in the view, offering the CLI's own file name.
  - **Dedupe…**, the dry run first, Merge offered only when it names groups, the merge supervised.
  - **Delete the graph…**, asked first.
- [x] **MCP Remove…** asks first, runs `config delete-mcp-server`, and reads back.
- [x] **A record's raw conversation:** a record with an archive (`raw_ref`, now decoded) offers its conversation from `knowledge info --raw`, run as a command and cut at the CLI's marker (`RawConversation`).
- [x] **The parity rows** for `chats`, `config`, `providers`, `graph`, `knowledge` and `mcp` name what their screens now reach; `__machine-schema` stays plumbing.

**Decisions made while building** (2026-10-26, each recorded for veto):
- **The default, taken:** exports and the report write through a save panel to a path the person picks, never a default location.
- **The startup schema read supersedes per-session detection** as the source of the flags (the document's decision). It lives where the binary is found, `SessionLibraryViewModel.start`, and `AppViewModel` wires the re-checks.
- **Register composes `config add-backend`** because the CLI has no single-provider verb. Unlike the TUI's `r`, it neither sets `models.default` when none is set nor fetches the roster.
- **Settings › Backends hides a roster's rows** (`<backend>:<id>`, listed there since M13), keeping the `server_tools` reads to config entries.
- **The graph's acts** (export, report, delete and the dry run) are commands bounded at 60 s. The merge is supervised.

**Verified.**
- **Recorded** from the branch's binary over sandboxes, the home written `/Users/someone`:
  - `machine-schema.json`;
  - 32 documents in `tests/business/reads/Fixtures/`: chats, providers, `server_tools`, graph export, report, dedupe and delete, MCP delete, and knowledge `--raw`;
  - **the older shapes,** from the installed binary over a sandbox: `machine-schema-older.json` and two `server_tools` refusals.

  **Hand-made:**
  - `machine-schema-pre52-handmade.json`, the 2026-10-24 schema cut back to v0.1.7's `protocol.cpp` line types, since no pre-52 binary is at hand;
  - two dedupe answers written to `graph.cpp`'s lines, since the mock embedder can't make two entities agree.
- **49 new cases:**
  - `RemainingVerbsRowTests` (7);
  - `CapabilitiesTests` (4), `CapabilitiesFromLaunchTests` (3), `MachineSchemaTests` (3) and `BinaryFingerprintTests` (1);
  - `SavedChatActsTests` (7) and `RecallForgetTests` with `RecallSeenTests` (3);
  - `SettingsParityTests` (7), `GraphActsTests` (6), `MCPRemoveTests` (2), `RawConversationTests` (3) and `RemainingVerbsReadsTests` (3).
- **Repeat runs:** each touched scheme and bundle, ten iterations green (Sessions 660 of 660, Shell 400 of 400). The layering check passed 25 of 25, and `check-previews` finds nothing in sessions, settings, app, graph, library, knowledge or shell.
- **21 new previews:**
  - a saved chat's info and recall;
  - the compact confirm;
  - a provider row's Register;
  - a backend's `server_tools`;
  - Settings on a capable binary restored onto another place;
  - graph export, dedupe and delete;
  - MCP remove;
  - a raw record.
- `lib/scripts/cicd.sh --test`, run with nothing else: green. The CLI's 3,992 tests and the macOS app's 780 cases passed, with no Swift warning.

**Not done:**
- No live pass of the app.
- `server_tools` shows in Settings only, not the Models place.
- The dedupe's merged state has no preview: `PresentationGraph` doesn't reach `InfrastructureProcess`, which a finished preview job needs.
- **CLI gaps the probes found:**
  - no verb registers one provider;
  - no verb lists recall's summaries;
  - `chats info`, `chats recall`, `graph dedupe` and `knowledge info --raw` have no JSON;
  - `graph delete` keeps the graph's config entry, so the picker still lists it;
  - `config add-backend` never sets a default backend.

### 2026-10-26 — `docs-and-board-catch-up` (backlog item 4l): the GUI docs and the contract board catch up

**Why.** The audit found the GUI's planning docs and the design's contract board describing an earlier state:
- **The contract board** (`lib/design/spec/html/BuildContract.dc.html`) marked only G9 and G17 closed. The gaps the CLI's track 52 shipped still read as open with their "Until then" fallbacks. The header said "22 gaps open" and "Checked against apogee 0.1.9", and the first call recommended landing G1, G8, G9, G11 and G12 before v1, though they had shipped. Flipping each gap was left to "each CLI ship's own docs pass" (1q), and that pass never happened.
- **The ROADMAP** kept "The answer as Markdown … a deliberate later step" as an idea, though M8 and M9 shipped it.
- **The SPEC had three stale gates:**
  - its scope's TODO waited on the first spike's feature cut, which shipped as 1a–1q;
  - parity was held mechanically "once 2a lands", which it did on 2026-10-20;
  - its success-criteria TODO cited open, drive and cancel as examples, which 1c already shipped.
- **CLAUDE.md** named Milestones A and B as shipped, missing C and D.

- [x] **The board, each gap probed** on the branch's binary (`apogee 0.2.0`, `f72f05f6`) over a sandbox, the scripted mock and fixture-made video and voice sets.
  - **16 of its 24 gaps are closed:** G1–G9 and G11–G17. Each probe's command and evidence is in the item's run: a permission `question` carrying its diff, `detach` and `set` answered as notices, `chat --private` writing nothing, `chats list` rows carrying `backend` and `model`, compose's dry run writing a plan alone, `check`'s ffmpeg rows, and so on.
  - **Open:** G10 and G18–G24. G18–G20 are the store verbs taking no `--output-format`; G21 is `train run` with no stream (55h gave `train pipeline` and `train cycle` theirs); G22 is no `agents show`; G23 is no suite on the `session` event; G24 is no `review mr --post`.
  - **The closed gaps** are dimmed in the board's way, each naming the CLI item that landed it (52a–52e) and what the app does now, in place of its "Until then". Every number and its original ask stay.
  - **The contract map's 14 rows** that named them say what the app uses. The slash-commands row names the `set`, `detach` and `compact` lines first, no longer "a restart with flags".
  - **The header** reads "Checked against apogee 0.2.0 (f72f05f6)". The stats went from 58 / 11 / 22 / 0 to 72 / 11 / 8 / 0, and now match the chips (before, two rows carried both an app chip and a gap).
  - **The first call** reads answered, and names the four open gaps marked before v1: G18–G21.
  - **G10 is corrected, not closed.** The fit read has existed since the CLI's 27e, so "offers no read" was never true. It stays open as "A suite's fit is text", narrowed to the typed form 4i recorded, its fallback what the app does.
- [x] **The canvas**, read first: its `BuildContract.dc.html` and `canvas.json` were byte-identical to the repo's, so nobody had edited it since its last publish. The board alone was published (canvas version 46) and read back identical to the repo's (sha-256 `c5826f6b…`). `canvas.json`, the other boards, `support.js` and the `.md` files were not sent. The design's README line counts sixteen closed.
- [x] **SPEC.md** (dated 2026-10-26):
  - **The first release's cut** is stated from what shipped: the macOS application, Milestones A–E, with Linux and Windows waiting for their own work.
  - **Full parity** is held by ADR 0003 and `ParityTable` since 2a.
  - **The success criteria** are five behaviours the shipped app has, each traced to its milestone: a conversation the app's and the CLI's at once; every command placed; long work supervised; the binary's words shown; nothing lost to the app's own moves.
- [x] **ROADMAP.md** drops the shipped Markdown idea.
- [x] **CLAUDE.md** names Milestones A–E as what v0.2.0 carries.

**Decisions made while building** (2026-10-26, each recorded for veto):
- **The default, taken:** the success criteria are written from five behaviours the shipped app demonstrably has, each traceable to a milestone, as observable outcomes.
- **G10 stays open, narrowed,** rather than flipped landed: no item landed it, since its read predates the board.
- **The CLI gaps this run found aren't added to the board.** They are listed below for the CLI's queue: the board's flips were this item's scope, and its rows for the new pipeline and cycle tabs are a design change of their own.

**Verified.**
- **The design's health check:** 48 start screens, no problems.
- **The board's markup** balances (497 `div`s, 495 `span`s). It holds no `{{hole}}`. Its stats match its chips, and its ledger's 8 open gaps are the 8 the map names.
- **Served locally and opened,** the board renders with no console errors, its closed rows dimmed.
- **The GUI docs' link sweep and the index validation** pass.
- `lib/scripts/cicd.sh --test`: green on the second run, the CLI's 3,992 tests and the macOS app's 780 cases passing with no Swift warning. The first run lost `cli.busy_line` to a timeout: `apogee check` under a PTY ran past 60 s while the suite loaded the machine. The CLI is unchanged in this run, and the test passed alone three times in 11.6 s.

**Not done:**
- **The board has no rows** for the `train pipeline` and `train cycle` reads and streams the app uses since 4j.
- **The design canvas doesn't draw this run's other additions:** 4e's delete plan and narration card, 4f's install card, 4i's suite sheet and adapters, 4j's tabs, and 4k's verbs.
- **CLI gaps found across this run, for the CLI's queue:**
  - a runtime refusal under `stream-json` emits no `error` event;
  - compose's two-voice-models refusal names `--model`, where its flag is `--voice-model`;
  - `task list` carries no `interrupted`;
  - nothing says which suite a resumed chat runs under, and suite refusals and admission lines reach stderr only;
  - the footprint is text, not typed JSON;
  - `datasets pull` has no stream;
  - no read lists the named pipeline specs;
  - a `train cycle halt` taken during a pass is overwritten when the pass saves;
  - a failed pass under `stream-json` writes nothing to stderr;
  - a resume repeats the previous attempt's score;
  - no verb registers one provider;
  - no verb lists recall's summaries;
  - `chats info`, `chats recall`, `graph dedupe` and `knowledge info --raw` have no JSON;
  - `graph delete` keeps the graph's config entry;
  - `config add-backend` never sets a default;
  - `config get tools.allowed_hosts` reads nothing.
