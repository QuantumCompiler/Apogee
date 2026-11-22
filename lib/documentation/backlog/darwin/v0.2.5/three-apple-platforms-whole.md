# APP-9c · v0.2.5: macOS, iPadOS and iOS whole

| Field | Value |
|---|---|
| **Type** | 🛠️ Task |
| **Project** | Apple apps |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.5 |
| **Component** | The parity table (`shell`), the app's whole and standalone passes, and the v0.2.5 release and its docs |
| **Labels** | `release` `parity` `end-to-end-tests` `anti-goal` |
| **Epic** | Track 9 · the iPhone, the foldable iPhone and the three Apple platforms whole |
| **Blocked by** | [9b](../ios/v0.2.5/foldable-iphone.md), [8c](iphone-and-ipad-distribution.md) |
| **Blocks** | — |
| **Reporter** | The Apple-platforms spike and brainstorm (2026-11-07), on the user's vision |
| **Created** | 2026-11-07 |

---

## Summary

v0.2.5 is the release where Apogee works on the Mac, the iPad and the iPhone, the foldable included, and the items before this one build its pieces, but nothing yet proves the whole. This item adds no capability: it classifies every command per platform, runs one hermetic whole pass per platform against a scripted server, proves the Mac complete with no server, and ships the release with the docs that say how to pair.

## Description

### User story

**As** a person using Apogee on their Mac, iPad and iPhone, served by their own Apogee server,
**I want** the release to show every capability on every Apple platform, with the Mac and the CLI still complete alone,
**so that** I can install v0.2.5 and pair my devices by written steps, knowing that standalone use never needs a server.

### Background

The user's vision, 2026-11-07: Apogee is a private, self-hosted harness that feels like Claude on every device the person owns, with one strong machine of theirs (for example 512 GB of memory and a finely tuned large model) serving their own desktops and phones. v0.2.5 is the release where that works on the Mac, the iPad and the iPhone, the foldable iPhone included (GUI SPEC → [One-liner](../../../assistant/gui/SPEC.md#one-liner)).

The items before this one build the pieces:
- the server's side (CLI [59a](../../cli/v0.2.3/paired-devices.md)–[59e](../../cli/v0.2.3/tasks-outlive-the-connection.md), [60a](../../cli/v0.2.4/one-tool-definition.md), [60b](../../cli/v0.2.4/device-tools.md));
- the app's connection and places ([7a](../v0.2.4/app-connection-local-or-server.md)–[7c](../v0.2.4/every-place-against-a-server.md));
- the iOS build, the iPad and distribution ([8a](app-builds-for-ios-and-ipados.md)–[8c](iphone-and-ipad-distribution.md));
- the iPhone and the foldable ([9a](../ios/v0.2.5/iphone-app.md), [9b](../ios/v0.2.5/foldable-iphone.md)).

This item adds no capability. It is the release's proof: it shows every capability on every Apple platform, it checks the anti-goal, and it ships the release with the docs that say how to pair.

It closes **the release goal**: every capability on every Apple platform, with the Mac and the CLI still complete alone. Five things stand between the pieces and that release (Evidence).

**What this item delivers:**
- **Every command classified per platform.** The parity table gives each command a place on the Mac with no server, on the Mac against a server, on the iPad and on the iPhone. The suite fails the day a row has no place on a platform, or a reasoned carve-out loses its reason.
- **An end-to-end hermetic pass per platform.** One script of acts runs through the composed app against a scripted server: pair, list, open, a turn with its question and permission cards, a read in every place, a job, a task followed across a dropped connection, and on the Mac a device-tool call. It runs on macOS, on an iPad simulator, on an iPhone simulator, and in the foldable's folded and unfolded traits.
- **The anti-goal checked.** A Mac with no server configured goes through every place and a whole local conversation over its child process. A tripwire fails the test if any remote transport is built.
- **The v0.2.5 release.** It carries the macOS app as today. The iOS and iPadOS build reaches the person's devices by 8c's path. The release notes and the docs say how to pair.

### Evidence

Measured at HEAD (`4f92316d`) on 2026-11-07. Five things stand between the pieces and the release:

1. **The parity table knows one platform.** `ParityTable.places` (`lib/src/darwin/source/presentation/shell/ViewModels/Parity.swift:52-90`, 32 rows) gives each command one place, for a local binary on a Mac. 7c adds a remote meaning to each row (`onServer`, `onServer(here:)`, `notRemote(reason:)`), but nothing says what a row means on an iPad or a phone. A place awkward on a phone could quietly have no way in, and no test would fail. The table is also behind the binary today: `apogee __complete ""` on the branch binary (a scratch `APOGEE_HOME`) answers 30 top-level words, while the recorded `tests/presentation/shell/Fixtures/command-words.txt` holds 29 (`acp` is 5a's).
2. **No test drives the app whole.** The suite is per module. `ScriptedRunner` is defined in 14 test files and `ScriptedJobs` in 11, one per module's bundle. The session's tests replay the recorded `tests/data/protocol/Fixtures/mock-actor.jsonl` through a scripted child (`tests/presentation/session/Scripted.swift`). `tests/presentation/app/` holds `AppViewModelTests`, `CapabilitiesFromLaunchTests`, `ChildrenSeenTests` and `TerminationTests`, and none of them carries one conversation across the composed places. Nothing scripted plays a server. `tests/business/session/RealBinaryTests.swift` drives the installed binary, which is local and not hermetic.
3. **The anti-goal is held on the CLI's side only.** The CLI pins never-listens by symbol scan and by process: `cli.no_listen_symbols` (`lib/src/cli/tests/CMakeLists.txt:561-644`) and `cli.complete_opens_no_listening_socket` (`:882-892`). The app has no network code today, so nothing can reach a server. Once 7a adds a transport, nothing in the app's suite fails if a Mac with no server configured opens a connection, or if a local place starts depending on one.
4. **A release carries the Mac app and nothing else.** `release-from-pr.sh` attaches `apogee-gui-darwin.zip` (`:17`, `:84`, `:421-488`, GUI M17), and `build.sh` builds one target (`TARGETS=(macos-arm64)`, `:49`). Getting the iOS app onto a device is 8c's path, and 8c's default is that a release asset carries no iOS build.
5. **No document says how to pair, and the public ones lag.** A search for `pair` in the root `README.md` and the GUI `DEVELOPER.md` finds `check --fix`'s "repairs" (`README.md:37`) and the Models place's drafter pairing and `repair` (`DEVELOPER.md:53`), and nothing about pairing a device. `README.md:80` still says the macOS app "is not yet part of a release", which has been stale since M17. The app's zip README (`build.sh:163-177`) calls it "the desktop app, in development" and names only the local CLI. `apogee serve --help` names no pairing or device flag (a search for `pair|device` returns 0). The release-notes skill lists "TLS on `serve`" among the known limitations a release must re-check (`.claude/skills/apogee-maintenance-summarize-release/SKILL.md:74`); 59a makes every non-loopback bind speak TLS, with `--tls off` for an operator's own proxy, so v0.2.5's notes must say so.

## Acceptance criteria

- [ ] **Given** the release's binary, **when** its commands are classified, **then** every command it answers has a classified place on the Mac with no server, the Mac against a server, the iPad and the iPhone, and none is a queued gap.
- [ ] The whole pass is green on macOS, on an iPad simulator, on an iPhone simulator and in the foldable's folded and unfolded traits, in CI's `build darwin` (8c's iOS steps) with no secret.
- [ ] **Given** the Mac app with no server configured, **when** the standalone pass runs every place and a whole conversation over its local CLI, **then** it is green and the app builds no remote transport.
- [ ] **Given** the v0.2.5 release, **when** the person installs it, **then** it carries `apogee-gui-darwin.zip` with an ad-hoc-signed `Apogee.app` that says `0.2.5` and opens on the local CLI with nothing paired, and the iOS and iPadOS build at `0.2.5` reaches the person's iPhone and iPad by 8c's path.
- [ ] **Given** the root `README.md` and the release notes, **when** the person reads them, **then** they say, in a few steps, how to pair an iPhone or iPad with the person's own server, call it optional, and contain no credential.
- [ ] **Given** dev builds on the Mac, iPad and iPhone paired with a sandboxed `apogee-dev serve` and the mock backend (a live pass), **when** a conversation started on one device is opened on the others, **then** it continues there, and the Mac against the server edits a project's file on the Mac (the user's own pass on their devices and model is optional and said).
- [ ] The full suites, the CLI's and the app's, are green.

## Constraints

- **Anti-goal: nothing here may make standalone use need a server** (GUI SPEC → [No server requirement](../../../assistant/gui/SPEC.md#non-goals); the user's call, 2026-11-07). The CLI alone, and the Mac app over pipes to its local CLI, stay complete and the default. This item is where that is proven for the release. The standalone pass runs every place of the Mac with no server configured and no network. The macOS zip still opens on the local CLI with nothing paired. The release notes call pairing optional, and the Mac's install steps need no account, no server and no Apple membership.
- **Never-listens; serving = server** (the CLI's [never-listens rule](../../../assistant/cli/CLAUDE.md); GUI SPEC → *No HTTP path for local work*). No app opens a listening socket. The iPad and iPhone connections are outbound, and on a Mac with Apogee installed the app never connects to a server on its own machine. The CLI's `cli.no_listen_symbols` and `cli.complete_opens_no_listening_socket` stay green.
- **The machine-mode client rule, as amended 2026-11-07** ([GUI CLAUDE.md](../../../assistant/darwin/CLAUDE.md)). One protocol over two transports, stdio locally and the authenticated connection remotely, decoded with the same tolerance. The scripted server speaks exactly what 59b's transport carries, recorded from the real `apogee-dev serve`, never a dialect of the app's own.
- **Secrets are unleakable** (the CLI's invariant). No pairing credential, admin token or provider key appears in a recorded fixture, a preview, a log line, the release notes or the docs. The pairing steps say where a credential is made and kept, never a value: `apogee devices pair` on the server prints a one-time code, the device trades it once for its own token (59a), and the device keeps that token and the server's pin in its Keychain (7a).
- **Additive protocol** (the CLI's [ADR 0006](../../../adrs/cli/backwards-compatibility.md)). A v0.2.5 app meets an older server, and a v0.2.5 server meets an older app (a TestFlight build stays installed for up to 90 days, per 8c). The whole pass includes an older server's recorded capabilities, and every place shows the "not on this server" state (7c) instead of failing.
- **Parity** (the CLI's [ADR 0002](../../../adrs/cli/mode-parity.md); the GUI's [ADR 0003](../../../adrs/gui/parity.md)). There is one table for every platform, and a platform drops a command only with a stated reason. At the release no row is a gap that names a queued item on any Apple platform.
- **The layers and MVVM** ([ADR 0001](../../../adrs/gui/layer-enforcement.md), [ADR 0002](../../../adrs/darwin/presentation-mvvm.md), [ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md)). The platform column is a value in the shell's view-model (SwiftUI-free). The whole pass drives view-models through the composition root, not views. The layering check stays green on the macOS and iOS targets.
- **The dev channel** (the CLI docs' [Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel); GUI M18). Fixtures are captured from `apogee-dev serve` over a temp `APOGEE_HOME` with the mock backend. A live pass pairs dev builds with `apogee-dev serve`, never the person's production server, the release `apogee` or `~/.apogee`. The release's own app and server change only by what the release ships.

## Technical approach

Planned, under `lib/src/darwin/` unless named otherwise. The test files and fixtures this item adds are under Testing.

| File | Change |
|---|---|
| `source/presentation/shell/ViewModels/Parity.swift` | `CommandPlace` gains its place per platform. `ApplePlatform` has four cases: `macLocal`, `macServer`, `iPad` and `iPhone`. A row's place on each is derived from what it already states: its local place on `macLocal`, and its 7c remote meaning on the other three. An explicit override, with its reason, is allowed only where a platform differs (Open questions). The console's refusals are read for the platform the app runs on. |
| `scripts/build.sh` | `package`'s zip README (`:163-177`) says what v0.2.5's Mac app is: it works alone with the local CLI, pointing it at your own server is optional, and it names where pairing is described. The signature, its check and the asset's name are unchanged. |
| `lib/release/VERSION` | `0.2.5`, the release's own bump as for every release (the CLI docs' versioning). |
| The root `README.md` (doc) | The app line (`:80`) says what the release carries, and a short "Your iPhone and iPad" section describes pairing in steps, pointing to 59a's reference. |
| GUI `DEVELOPER.md` (doc) | The whole pass, the standalone pass and the per-platform parity law. |
| ADR 0003's addendum (doc) | The platform column. |
| GUI `SPEC.md` → Success criteria (doc) | One line for the three platforms. |
| The v0.2.5 release notes (doc) | Drafted with `apogee-maintenance-summarize-release`. Their Install section covers the Mac zip as today, the iOS and iPadOS path as 8c chose it, and pairing as optional. Their Known limitations include the table's `notRemote` reasons, and "TLS on `serve`" is re-checked: retired by 59a's TLS on every remote bind, with `--tls off` (no harness plane) said as it stands at the release. |

**Reference.**
- Siblings: [9a](../ios/v0.2.5/iphone-app.md), the iPhone app, and [9b](../ios/v0.2.5/foldable-iphone.md), the foldable, which is this item's gate. [8c](iphone-and-ipad-distribution.md), distribution, also this item's gate, and the owner of the [user] call on the Apple Developer Program. [8b](../ipados/v0.2.5/ipad-app.md), the iPad app and pairing. [8a](app-builds-for-ios-and-ipados.md), the iOS build. [7a](../v0.2.4/app-connection-local-or-server.md), the connection the scripted server fakes. [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md), the Mac's device tools. [7c](../v0.2.4/every-place-against-a-server.md), every place against a server and the remote column this item extends per platform.
- On the CLI side: [59a](../../cli/v0.2.3/paired-devices.md) (pairing, whose reference the docs point to), [59b](../../cli/v0.2.3/machine-mode-over-the-network.md) (the transport), [59c](../../cli/v0.2.3/many-sessions-at-once.md) (many sessions), [59d](../../cli/v0.2.3/reads-and-commands-over-the-network.md) (reads and commands), [59e](../../cli/v0.2.3/tasks-outlive-the-connection.md) (tasks that outlive a connection), [60a](../../cli/v0.2.4/one-tool-definition.md) and [60b](../../cli/v0.2.4/device-tools.md) (one tool definition, and device tools). After v0.2.5 comes [61a](../../cli/v0.2.6/sessions-as-training-data.md) and [61b](../../cli/v0.2.6/apogee-tuning-regime.md), Apogee-tuned training.
- Adjacent, not re-owned: [M18](../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel), the app's dev channel, and CLI v0.2.2's track 58 ([`mcp-serve-introduces-itself.md`](../../cli/v0.2.2/mcp-serve-introduces-itself.md) and its siblings).
- Precedents:
  - ADR 0003's gap rows, held to the backlog index by `ParityTests.gapsAreQueued` (`ParityTests.swift:94-95`), the model for "no gap at the release".
  - The recorded protocol fixture and its deliberate capture (`scripts/capture-fixture.sh`, 1b), the model for the server fixtures.
  - The CLI's never-listens tests, the model for the app's tripwire.
  - M17, the release carrying the Mac app.
  - The house release notes (`apogee-maintenance-summarize-release`).
- The server's references: [`http-api.md`](../../../reference/cli/http-api.md) and [`machine-mode.md`](../../../reference/cli/machine-mode.md).

## Testing

All under `lib/src/darwin/`. The app's suite runs no binary and opens no network, per GUI CLAUDE.md; the server fixtures are captured deliberately, never from a test.

- **`tests/presentation/shell/ParityTests.swift`** — the per-platform law. Every row has a place on each of the four platforms. Every override and every `notRemote` has a reason. No row is a gap naming a queued item at the release. Every screen place can be reached on each platform: through `CompactNavigation` (9a) on the iPhone and through the regular shell (8b) on the iPad. The fixture of command words must match the release's binary: `Fixtures/command-words.txt` is refreshed by `scripts/refresh-parity-fixture.sh` against the release's binary.
- **`tests/presentation/app/WholePass/ScriptedServer.swift` (new)** — a scripted fake of 7a's remote connection. It replays recorded server documents and streams for each request, answers 59a's pairing with a scripted acceptance and a scripted refusal, drops and restores the connection on cue, and fails by name on any request it was not scripted for, so a pass cannot drift off its script silently. It holds no credential: its pairing answer is a planted placeholder that the leak check looks for.
- **`tests/presentation/app/WholePass/WholePassTests.swift` (new)** — pins the script on each platform's trait: one script of acts through `AppViewModel`. Pair. List the server's chats. Open one and draw its earlier turns. Take a turn that streams its thinking and tool narration, answer its question card and its permission card, and stop a second turn. Read every place, run one command and one supervised job. Start a task, drop the connection, and follow the task again (59e). On the Mac only, answer a device-tool call on the Mac's own files (7b, 60b). Then meet an older server's capabilities. The same script runs for each platform's trait: macOS against a server, iPad regular, iPhone compact, and the foldable's folded and unfolded traits (9b).
- **`tests/presentation/app/StandaloneTests.swift` (new)** — the anti-goal. The composed Mac app with no server configured, its connection factory a tripwire that fails the test if a remote transport is built or asked for. No remote transport is built without a configured server. Every place appears and reads locally over `ScriptedRunner`/`ScriptedJobs`. The `mock-actor.jsonl` conversation runs whole over the scripted child. Settings shows no server control beyond 7a's opt-in.
- **`tests/presentation/app/Fixtures/server/` (new)** — the server's documents and streams for the whole pass. They are captured by 7c's planned `scripts/capture-server-fixture.sh`, which this item extends with the whole-pass conversation (the mock-actor turn, a task, a device-tool call). It runs deliberately against `apogee-dev serve` over a temp `APOGEE_HOME` and the mock backend, and never from a test.
- **Leak check** — over every fixture under `tests/presentation/app/Fixtures/server/`, every preview and the release notes' draft: the planted pairing placeholder, any admin token shape and any key shape never appear.
- **`build.sh package`** — its check of the unpacked zip and its `codesign --verify` still pass. The asset's name and signature are unchanged.
- **The CLI's side** — no new tests. The CLI's half of the anti-goal is its existing hermetic suite and the two never-listens tests: `cli.no_listen_symbols` and `cli.complete_opens_no_listening_socket` stay green.
- **Layering check** — stays green on both platforms' targets.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What are the platform column's cases, and how is each row's place on them set? | `[default]` | The platform column has four cases: `macLocal`, `macServer`, `iPad` and `iPhone`. It is derived: `macLocal` is the row's place today, and the other three are its 7c remote meaning. An override is allowed only where a platform truly differs, and it carries its reason. Example: `graph` is `onServer(here:)` on all three remote platforms, and the "here" is a save panel on the Mac and the Files sheet on the iPad and the iPhone. Example: `acp` is 5a's Settings screen on `macLocal` and `notRemote` on the other three (a host on the Mac spawns it over stdio). |
| How does the foldable map onto the shell? | `(consumed decision)` | 9b owns it. |
| Is the foldable a column of its own in the parity table? | `[default]` | The foldable is not a fifth column. Folded reads the `iPhone` column and unfolded the `iPad` one, as 9b switches between 9a's compact shell and 8b's regular one. |
| What does "fully working" mean at the release? | `[default]` | Three things. No row is a gap naming a queued item on any of the four platforms. Every `notRemote` and every override has a reason the person sees in the app. The whole pass is green on every platform. |
| Is the whole pass one script or one per platform? | `[default]` | One script with platform branches only where the platform differs: device tools on the Mac alone (iOS starts no shell, per 9a), and navigation through the compact map on the iPhone and the folded foldable. A branch that skips an act names why. |
| Is the scripted server a real server or a fake? | `[default]` | A fake of 7a's connection over recorded documents, not a real server. The app's suite runs no binary and opens no network, per GUI CLAUDE.md. The bridge to the real server is the deliberate capture from `apogee-dev serve`, plus the CLI's own tests of 59a–59e. |
| What is the standalone pass's tripwire, and what must a never-paired Mac show? | `[default]` | The tripwire is the composition's connection factory, which fails if any remote transport is built while no server is configured. A Mac that has never been paired draws no server state in any place, and the Mac's existing suite passes unedited beyond 7c's connection hand-off. |
| The Apple Developer Program or personal sideloading, and whether a release asset carries an iOS build? | `(consumed decision)` | 8c owns the [user] call, and the asset question, whose default there is no. 8c's [user] call must be answered before this item can be accepted. |
| What does the v0.2.5 release carry for the Apple apps? | `[default]` | `apogee-gui-darwin.zip` as today. The iOS and iPadOS build is the one 8c's path produces from the released tree, stamped `0.2.5`, and the release notes' Install section names that path in a few steps. If 8c's call adds an asset, this item takes it as 8c shapes it. |
| Where is how to pair written for the person? | `[default]` | In three places. The root `README.md` gets a short section. The release notes' Install section gets a few lines. 59a's CLI references hold the full contract: [`http-api.md`](../../../reference/cli/http-api.md) for TLS, the pin, pairing and the harness plane's gate, and the commands reference for `apogee devices pair\|list\|revoke`. The GUI's `reference/` stays empty, since the GUI exposes no contract of its own. |
| Who fixes the stale `README.md:80` line ("not yet part of a release")? | `[default]` | This item, if no earlier change has fixed it. |
| What is the release-candidate live pass? | `[default]` | A dev build of the app on the Mac, in the iPad and iPhone Simulators, and on the person's devices by 8c's path, paired with `apogee-dev serve` over a temp `APOGEE_HOME` and the mock backend. The person's live pass on their own devices and their own large model is optional and said. |
| What do the release notes' Known limitations name? | `[default]` | What is not remote (the table's `notRemote` reasons, such as adding a key at the server), the 90-day TestFlight expiry or the 7-day personal-team expiry as 8c's path implies, and TLS on `serve` as 59a leaves it (on for every remote bind, `--tls off` mounting no harness plane). |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item: **The vision.** Apogee is a private, self-hosted harness that feels like Claude on every device the person owns. A strong machine of theirs serves their desktops and phones, and Apogee mimics frontier harnesses (Claude Code, Codex, Gemini CLI, Ollama, vLLM) with local models. **No accounts or billing.** Paid features are a someday, not now: no customer accounts, billing or metering. The server admits the person and their devices only, so the release reaches the person's own devices, not the public. **The server is an add-on, never the only way the application runs.** The CLI alone, and the Mac app over pipes to a local CLI, stay complete and the default. Anything that makes the CLI or the Mac app need a server, a network or an account to do what they do today is a failure. This item checks that for the release. **The SPEC amendment is approved and written.** The GUI reaches the CLI over stdio locally, or over an authenticated network connection to the person's own `apogee serve`. **iPhone and iPad are clients of the person's Apogee server only**, with no on-device core. The Mac and the CLI stay standalone. **Where the work happens is C.** Server tools by default, and a connected desktop offers its own files and shell. When the Mac points at a server and is asked to change a project on the Mac, the Mac's files are the ones touched. The whole pass checks this on the Mac (7b, 60b). **"iPhone duo" is the foldable iPhone.** The app adapts to its folded phone screen and its unfolded near-iPad screen, and the whole pass runs both traits. **The placement.** v0.2.5 is the target where macOS, iPadOS and iOS (the iPhone and the foldable) fully work. The common tool definition (60a) lands before it, and Apogee-tuned training (61a, 61b) after it. **Windows and Android follow v0.2.5** (a recorded default). | the user |
| 2026-11-07 | Placed last in v0.2.5 (track 9), gated on 9b and 8c. It proves the whole, so it follows the last layout (9b, and through it 9a, 8b, 8a and 7a–7c) and the path onto a device (8c), without which "working on iPhone and iPad" would mean only the Simulator. It adds no capability, so it needs nothing the gates do not already bring. | the spec |
| 2026-11-18 | Moved here from v0.3.0, when the releases were merged; the id is unchanged. | the user |

## Out of scope

- Any new capability, screen or protocol field (each is its own item).
- Linux, Windows and Android (after v0.2.5, a recorded default).
- Apogee-tuned training (CLI 61a and 61b, v0.2.6).
- App Store release to the public, customer accounts, billing and metering.
- The Mac app's Developer ID and notarization (a separate call).
- Push notifications (9a's default).
- An on-device model or core on iPhone or iPad.

## Links

- **Blocked by:** [9b](../ios/v0.2.5/foldable-iphone.md) and [8c](iphone-and-ipad-distribution.md), and through them every earlier item of tracks 7–9 and CLI 59a–59e, 60a and 60b; 8c's [user] call must be answered before this item can be accepted · **Blocks:** —
- **Relates to:** [9a](../ios/v0.2.5/iphone-app.md), [8b](../ipados/v0.2.5/ipad-app.md), [8a](app-builds-for-ios-and-ipados.md), [7a](../v0.2.4/app-connection-local-or-server.md), [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md), [7c](../v0.2.4/every-place-against-a-server.md); CLI [59a](../../cli/v0.2.3/paired-devices.md)–[59e](../../cli/v0.2.3/tasks-outlive-the-connection.md), [60a](../../cli/v0.2.4/one-tool-definition.md), [60b](../../cli/v0.2.4/device-tools.md); [M18](../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel) and CLI track 58 (adjacent, not re-owned); M17 and 1b (precedents); the GUI SPEC's [One-liner](../../../assistant/gui/SPEC.md#one-liner) and [Non-goals](../../../assistant/gui/SPEC.md#non-goals); [ADR 0001](../../../adrs/gui/layer-enforcement.md), [ADR 0002](../../../adrs/darwin/presentation-mvvm.md), [ADR 0003](../../../adrs/gui/parity.md), [ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md), CLI [ADR 0002](../../../adrs/cli/mode-parity.md) and [ADR 0006](../../../adrs/cli/backwards-compatibility.md).
- **Follow-on:** CLI [61a](../../cli/v0.2.6/sessions-as-training-data.md) and [61b](../../cli/v0.2.6/apogee-tuning-regime.md), Apogee-tuned training, after v0.2.5.
