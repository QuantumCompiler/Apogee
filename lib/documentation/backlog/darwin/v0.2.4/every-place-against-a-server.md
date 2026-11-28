# APP-7c · Every place works against a server

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Apple apps |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | Every place's view-model, the composition (`AppViewModel`), the reads and jobs seam, the media records, the parity table |
| **Labels** | `remote` `parity` `media` `honest-refusals` |
| **Epic** | Track 7 · the Mac as a client |
| **Blocked by** | [7a](app-connection-local-or-server.md), [CLI 59e](../../cli/v0.2.3/tasks-outlive-the-connection.md) |
| **Blocks** | — |
| **Reporter** | The Apple-platforms spike and brainstorm (2026-11-07), on the user's vision |
| **Created** | 2026-11-07 |

---

## Summary

Every place in the app is tied to the local binary and the local disk. This item makes every place run its reads, commands and jobs through the **active connection** (7a's), drawn the same on both connections: the media places show the server's records and play them on this device, the parity table classifies each command's remote meaning, and a place, sheet or button the server cannot serve says so in words, not by failing.

## Description

### User story

**As** a person using Apogee with their own `apogee serve` on a strong machine,
**I want** every place in the app to read and act through the connection I picked, local or my server,
**so that** the app shows and runs the server's work as fully as it runs the local CLI's, says why when the server cannot do something, and the iPad and iPhone, which have no local CLI, have every place too.

### Background

The user's vision, 2026-11-07: Apogee is a private, self-hosted harness that feels like Claude on every device the person owns, with one strong machine serving their desktops and phones. 7a gives the app a second way to reach the CLI, the person's own `apogee serve`. This item closes **W4 on the app's side**: every place reads and acts through the **active connection**. Sessions, Models, Suites, Tasks, Training, Knowledge, the Code graph, Video, Voice, Library, Review, Settings, the monitor and the console each run their reads and commands over that connection. A place whose data lives on one machine shows the server's records when it is remote, and a place the server cannot offer says why. iPhone and iPad have no local CLI (8a, 9a), so whatever does not work here does not work on them at all.

It delivers:
- every place runs its reads, commands and jobs through the active connection, drawn the same on both connections;
- the media places show the server's records and play them on this device;
- each command's remote meaning classified in the parity table;
- a place, sheet or button the server cannot serve says so in words, not by failing.

### Evidence

Measured 2026-11-07 at `4f92316d`. Each finding is what keeps a place tied to the local binary and the local disk (the open questions refer to them by number):

1. **Every read and command is "run this binary".** `CommandRunning` (`lib/src/darwin/source/business/reads/CommandRunning.swift:7-22`) takes `binary: URL` in all five of its calls, and `JobStarting` (`business/jobs/SupervisedJob.swift:169-172`) does the same for every long run. 138 single-line call sites in 27 files under `source/` pass a binary (`grep -rnE '(read|command|report|readKeepingStderr|startJob)\(binary:'`, declarations excluded; calls split over lines come on top). The biggest are `SessionLibraryViewModel` and `TrainingViewModel` (14 each), `LibraryViewModels` (12), `SettingsViewModel` (11) and `ModelsViewModel` (10).
2. **Places get a binary, not a connection.** The composition hands each place `sessions.binaryURL` through 18 single-line `connect(binary:)`/`use(binary:)` calls (`presentation/app/ViewModels/AppViewModel.swift:296-313`) and Settings' `connect(binary:version:skippedOverride:)` (`:314-315`). One `JobStarter` is built at `:80` and shared by every place. Each place gets its own `runner ?? ReadRunner()` (20 of them, `:84-176`).
3. **The media places read this Mac's disk, not the CLI.** `MediaLibrary.scan` (`business/media/MediaRecords.swift:175-206`) walks `~/Movies/Apogee`, `~/Music/Apogee` and added folders with `FileManager`. Video (`VideoViewModel.swift:122`), Voice (`VoiceViewModel.swift:90`) and Compose (`ComposeViewModel.swift:120`) draw their libraries from it. A composition's progress is its project file read off disk (`Projects.swift:95-100`). An output name is checked free against the local disk (`MediaRecords.swift:218-241`). Playback is `AVPlayer(url:)` on a local file (`styles/Components/MediaPlayer.swift:21`), and a speech's is `AVAudioPlayer(contentsOf:)` on one (`voice/Views/VoiceScreen.swift:201`). Compose offers a finished composition only once its file exists here (`compose/Views/ComposeView.swift:161`). The CLI has no read for these records. Probe, temp `APOGEE_HOME`: `apogee __complete video ""` answers `generate compose` and `apogee __complete voice ""` answers `say`, with no list verb. The admin plane's routes include none for media either (`reference/cli/http-api.md`, `## Routes`).
4. **Paths come from this device.** 13 `.fileImporter` pickers hand the CLI a path on this Mac: the execute door's folder, a task's folder, an attachment, a graph's folder, an ingest source, a dataset to prepare, an adapter, Video's seed image and added folder, Compose's assets and music, Voice's added folder, and Settings' binary. The other local-path affordances are 5 `NSWorkspace` "Show in Finder" buttons (Video, Compose, Voice, Store, Models), 3 `NSSavePanel`s (graph export and report, knowledge export), `@` completion from the local disk (`business/session/Mentions.swift:16`) and Suites' adapter footprint summed from local files (`SuitesViewModel.swift:167-186`). Library's editor opens and saves the person's own agent and symphony files straight off this Mac's disk (`library/ViewModels/LibraryViewModels.swift:169`, `:185`, `:339`, `:352`), not through the CLI. Probe, temp `APOGEE_HOME`: `apogee __complete agents ""` answers `create list edit delete` and `symphonies` answers `list show create edit delete play`, with no verb that hands a definition's text back or takes it in.
5. **The monitor is this Mac's.** It reads `system` (`monitor/ViewModels/MonitorViewModel.swift:73`) and the app's live children the composition lists (`:81`), with their resident memory from `ProcessMemory` (`:32`).
6. **The parity table has no remote column.** `ParityTable.places` (`presentation/shell/ViewModels/Parity.swift:52-90`, 32 rows: 24 screens, 2 console, 3 carve-outs, 3 plumbing) classifies each command once, for a local binary. Nothing says what `auth`, `bootstrap`, `reset` or `video` mean on a server. Probe, temp `APOGEE_HOME`: the binary at HEAD answers 30 top-level words to `apogee __complete ""`, `acp` among them (5a classifies it). The recorded fixture `tests/presentation/shell/Fixtures/command-words.txt` holds 29, without `acp`.
7. **The server has its own rules, and the app must state them.** Vendor-CLI backends are "never served" (`reference/cli/http-api.md:26`). A key is stored from "loopback peers only" (`:1449`). Nobody answers a permission prompt on a served request today (`:27`), which the CLI's 59b changes for paired devices.

## Acceptance criteria

- [ ] **Given** the app is pointed at a server, **when** the person uses any place (Sessions, Models, Suites, Tasks, Training, Knowledge, Code graph, Video, Voice, Library, Review, Settings, the monitor, the console), **then** it shows the server's data and runs its acts there, each act's result read back from the server.
- [ ] **Given** a server connection, **when** the person opens Video or Voice, **then** they list the server's clips, compositions and speeches and play them on this device; **and given** the server has no media read, **then** they say why and still generate.
- [ ] **Given** a task started remotely, **when** the connection drops and returns, **then** the task is followed again (59e).
- [ ] **Given** a server connection, **when** the person reaches an act the server cannot do (adding a key, Show in Finder, a remote task in a folder on this Mac, editing a definition before the server carries it, an older server's missing verb), **then** it says why in words, and none of them fails silently.
- [ ] **Given** a server connection, **when** the person runs a line in the console, **then** it runs and completes there, refusing by the table's remote column.
- [ ] **Given** no server is configured, **when** the Mac app runs, **then** it behaves exactly as before: the full suite is green with every local test unedited beyond the connection hand-off.

## Constraints

- **The anti-goal: nothing here may make standalone use need a server** (GUI SPEC → Non-goals, *No server requirement*; the user's call, 2026-11-07). On the local connection every place runs **the argv it runs today, byte for byte**, against the local binary, and still reads the local disk where it reads it today. The existing suite stays green with no test edited except where it is handed the connection. A Mac with no server configured never shows a remote-only control.
- **Never-listens; serving is the server's** (CLI SPEC → *Interactive = pipes, serving = server*, clarified 2026-11-07). The app opens no socket of its own, and on a Mac with Apogee installed it never connects to a server on its own machine (GUI SPEC → *No HTTP path for local work*).
- **The GUI is a machine-mode client, amended 2026-11-07** (GUI `CLAUDE.md` → *The GUI is a machine-mode client*). A remote place speaks the same protocol over 7a's transport, decodes it as tolerantly and keeps the same honesty. No place gets a second path to a provider, and none reimplements on the device a read the server does not offer.
- **Secrets stay unleakable.** No key travels in a URL, a log or a document. A key is added at the server, as the admin plane already requires (`http-api.md:1449`). The connection's own credential is 7a's and never reaches a view-model.
- **Additive protocol** (the CLI's [ADR 0006](../../../adrs/cli/backwards-compatibility.md)). An older server lacking a read or verb is a stated state of the place, never a failure. The app decides it from what the server says it speaks at connect (its schema, as 4k reads the local binary's), never from a call's failure alone.
- **Parity** (the CLI's [ADR 0002](../../../adrs/cli/mode-parity.md); the GUI's [ADR 0003](../../../adrs/gui/parity.md)). Every command keeps one classified place, and now also a classified remote meaning, held by test. A place drops nothing because it is remote unless the table names why.
- **The layers** ([ADR 0001](../../../adrs/gui/layer-enforcement.md), [ADR 0002](../../../adrs/darwin/presentation-mvvm.md)). The remote-or-local decision for a place lives in its view-model (SwiftUI-free), so the iPad and iPhone apps (8b, 9a) reuse it unchanged. Where the media records come from is Business (`media`). Nothing below Presentation imports AppKit, so 8a's iOS build is not made harder.
- **The dev channel** (GUI `CLAUDE.md` → *Development runs on the dev channel*; M18). A live pass runs a development app against `apogee-dev serve` over a temp `APOGEE_HOME` with a mock backend, never the release `apogee` or `~/.apogee`. M18 shipped 2026-11-21, so that development app is "Apogee Dev", driving `apogee-dev` alone.

## Technical approach

Planned. The connection type and its transport are 7a's; this item makes every place use them. Paths under `lib/src/darwin/`.

| File | Change |
|---|---|
| `source/business/reads/CommandRunning.swift`, `source/business/jobs/SupervisedJob.swift` | Reads, commands and jobs addressed to the active connection, not a `binary: URL`. On the local connection this is today's `ReadRunner` and `JobStarter`, unchanged. On a remote one it is 7a's runner over the server's reads and commands (CLI 59d). |
| `source/presentation/app/ViewModels/AppViewModel.swift` | `connect(binary:)` becomes `connect(_ connection:)` at `:296-315`. A change of connection re-reads every place, as a changed binary is re-asked today (GUI 4k, `:215-217`). |
| Every place's view-model under `source/presentation/<place>/ViewModels/` | Sessions, session, models (Models, Store, Downloads, DeletePlan), suites, tasks, training, knowledge (Records, Collections), graph, library, review, video, compose, voice, settings, monitor, console. Each gains its remote state: which acts it offers there and the reason for any it does not. |
| `source/business/media/MediaRecords.swift`, `Projects.swift` | `MediaLibrary`'s records from one of two sources. Locally, the folders, as today. Remotely, the server's read (open question below). Playback and Show in Finder take what the record's source can give. |
| `source/presentation/library/ViewModels/LibraryViewModels.swift` | The editor reads and writes a definition's file only on the local connection. Remotely it goes through the server's read and write, or is not offered (open question below). |
| `source/presentation/shell/ViewModels/Parity.swift` | `CommandPlace` gains its remote meaning (open question below). The console's refusals are drawn from it for a remote connection. |
| `source/presentation/styles/Components/` | One shared "not on this server" state (the reason, and the server's version where that is the reason), promoted on its second use (ADR 0002). |
| `scripts/capture-server-fixture.sh` (planned) | Captures the recorded server documents, modelled on `scripts/capture-fixture.sh`: `apogee-dev serve` over a temp `APOGEE_HOME` and the mock backend, run deliberately, never by a test. |

**Reference.** Siblings: [`app-connection-local-or-server.md`](app-connection-local-or-server.md) (7a, the connection this consumes) and [`mac-offers-its-files-and-shell.md`](../macos/v0.2.4/mac-offers-its-files-and-shell.md) (7b, the Mac's files and shell to a server session: `@`, attachments and a session's folder on the Mac). The CLI side: [`tasks-outlive-the-connection.md`](../../cli/v0.2.3/tasks-outlive-the-connection.md) (59e, why Tasks can follow a run after reconnecting) and [`reads-and-commands-over-the-network.md`](../../cli/v0.2.3/reads-and-commands-over-the-network.md) (59d, the reads and commands every place runs). After it: [`app-builds-for-ios-and-ipados.md`](../v0.2.5/app-builds-for-ios-and-ipados.md) (8a), [`ipad-app.md`](../ipados/v0.2.5/ipad-app.md) (8b), [`iphone-app.md`](../ios/v0.2.5/iphone-app.md) (9a). Adjacent, not re-owned: 6g (`tasks-screen-truthful`, the Tasks screen's truth locally, shipped 2026-11-08 — [Milestone G](../../../assistant/darwin/MILESTONES.md#milestone-g--the-second-sweeps-gui-findings)), [Milestone A](../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel) (M18). Precedents: the GUI's binary-change re-ask (4k), which re-reads before a place acts, is the model for a connection change. ADR 0003's gap rows, held to the backlog index by test, are the model for holding the remote column to the server's command list. The admin plane's loopback-only key route (`http-api.md:1444-1455`) is the model for keys. The server's reference is [`http-api.md`](../../../reference/cli/http-api.md); the client protocol is [`machine-mode.md`](../../../reference/cli/machine-mode.md) (→ *Everything else is a CLI command*, `:986`).

**Docs when it ships:** GUI `DEVELOPER.md` (each place's remote behavior), the `CLAUDE.md` Codebase Map, and ADR 0003's addendum for the remote column.

## Testing

All hermetic: scripted connection fakes and recorded documents; no binary, no server, no network.

- **Each place's suite under `tests/presentation/<place>/`** gains remote cases over the existing `ScriptedRunner`/`ScriptedJobs` fakes keyed by connection: its reads, one command, one job, a `notRemote` act's reason, and an older server's state, over a scripted connection and recorded server documents. The recorded server documents go in each place's `Fixtures/`, captured by the planned `scripts/capture-server-fixture.sh` (Technical approach), never by a test.
- **The local connection is byte-identical.** Each place's existing recorded argv and documents pass unchanged, held by the current suites run on the local connection.
- **`tests/business/media/`** gains source cases: `MediaLibrary`'s two sources are pinned, folders locally, the server's documents remotely. A remote record never calls `FileManager`.
- **`tests/presentation/shell/ParityTests.swift`** gains the remote column's law: every row classified, every `notRemote` with a reason, and the column checked against a recorded server command list.
- **No local-disk reach from a remote view-model.** No remote view-model touches `FileManager`, `NSWorkspace` or an open panel, Library's editor and Suites' footprint included. The layering check stays green, and Business and Data stay free of AppKit.
- **No key leaks.** No key field appears in any remote document, log or URL in the tests.

## Open questions

| Question | Status | Answer |
|---|---|---|
| The connection's type, its transport, pairing, how the person picks local or a server, and the runner and job starter over it | `(consumed decision)` | 7a. This item takes them as 7a defines them. |
| Which reads and commands the server carries and refuses, and how a server job is listed | `(consumed decision)` | CLI 59d. This item draws the server's word, never its own guess. |
| On the Mac, can a remote session's `@`, attachments and folder be the Mac's own? | `(consumed decision)` | 7b: on the Mac, a remote session's `@`, attachments and folder can be the Mac's own, through device tools. |
| What remote meaning does each command get (the parity table's remote column)? | `[default]` | Each `CommandPlace` gains one remote meaning: `onServer` (it runs on the server, against the server's data); `onServer(here:)` (it runs on the server, and a named part happens on this device: an export saved here, a clip played here); `notRemote(reason:)` (not offered on a server, said with the reason). The carve-outs stay carve-outs, their reasons naming the server. Proposed classification: chat, chats, execute, models, config, check, providers, system, task, bootstrap (said as installing on the server), knowledge, embed, train, datasets, agents, mcp, symphonies, review, analyze and complete are `onServer`. graph, video and voice are `onServer(here:)`. version is both (the app's, a local binary's if any, and the server's). auth is `onServer` for its list and clear, while adding a key is `notRemote` ("a key is added at the server itself", as `PUT /v1/admin/auth` already requires). acp is `notRemote` (a host on this machine spawns it over stdio). tui is the window on either connection (`onServer`). serve, reset and uninstall are `notRemote`, their reasons naming the server ("run it at the server's own prompt"). Of the plumbing, `__machine-schema` and `__complete` are `onServer` (the server's schema read at connect, its completion asked by the console's Tab and the path fields), and `__mcp-tools` is `notRemote` (a stdio server a client spawns on its own machine). Held by test: every row has a remote meaning, every `notRemote` has a reason, and the column agrees with the server's command list from a recorded fixture. |
| Where do the media records come from when remote? | `[default]` | From the server through a read of its records: `video.generation`, `voice.speech` and `video.project` documents, the same objects `MediaLibrary.scan` reads today. That needs a CLI read, which does not exist (Evidence 3). If 59d does not carry it when this item is taken, a CLI sibling is queued, gated both ways. Until it lands, the remote Video and Voice places say "this server doesn't list its media yet" and still run generate, compose and say as jobs on the server. Added folders are this device's and are not offered remotely. |
| How does playback work for a server's record? | `[default]` | Remote playback fetches the output through the connection into the app's own cache, plays it there, and clears the cache when the connection changes. Show in Finder is not offered for a server's record; Save to this device… is. If the transport carries no file fetch, the record shows its details and says playback needs the file here. |
| Whose paths do the sheets and pickers take when remote? | `[default]` | A sheet that names a path for a command (a graph's folder, an ingest source, a dataset, an adapter, Compose's assets and music, a seed image) takes a path **on the server**: typed, with path completion through the server's `__complete` where it completes one, not this device's open panel. The three save panels save, on this device, the document the server returned. Settings' binary picker is the local connection's alone. The execute door's folder on this Mac is 7b's (a server session opened in a granted folder). A remote task's folder is a path on the server. A folder on this Mac is not offered to a remote task, since a task outlives the connection and the Mac's files are reached only while a session holds 7b's grant; the sheet says so and names the two ways it can be done: a server session opened in that folder (7b), or the task on the local connection. |
| What does Library's editor do when remote? | `[default]` | It reads and writes a definition's text through the server, where the server carries such a read and write (59d, or a CLI sibling queued beside the media read). Until then, editing is not offered for a server's agents and symphonies, said in one line; list, create, delete, show and play still run on the server. It never opens a server path on this device's disk. |
| Whose settings does Settings show when remote? | `[default]` | Settings splits by owner. The app's own settings (appearance, text size, the connection, the binary override) are always this device's. Every pane over `config`, `check`, `providers`, `auth` and `bootstrap` reads and writes the server's when remote, its header naming the server. |
| What does the monitor show when remote? | `[default]` | It reads the server's `system`. It lists the jobs this device started over the connection, and the server's running jobs where 59d lists them. Resident memory per child is shown only for local children. |
| What happens on a connection change? | `[default]` | It re-reads every place before it acts (4k's rule) and stops nothing on the server. A remote job or task keeps running and is found again on return. A local child is still ended only by the person's Stop or by quit, as today. |
| What are Sessions' backends when remote? | `[default]` | The server's `models list`. An entry the server will not serve (a vendor CLI, `http-api.md:26`) is drawn in the server's words and not offered to chat. |
| What does an older server missing a read or verb show? | `[default]` | The shared "not on this server" state, naming the server's version, decided from what it said it speaks at connect. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm. | — |
| 2026-11-07 | The vision: a private, self-hosted harness that feels like Claude on every device the person owns, served by their own strong machine (e.g. 512 GB of RAM and a finely tuned large model). Apogee mimics frontier harnesses (Claude Code, Codex, Gemini CLI, Ollama, vLLM) with local models. Paid features are a someday, not now. No customer accounts, billing or metering; the server admits the person and their devices only. | the user |
| 2026-11-07 | Where the work happens is C: server tools by default, and a connected desktop offers its own files and shell, like Claude Code. When the Mac points at a server and is asked to change a project on the Mac, the Mac's files are the ones touched. That is 7b's, through a session's device tools on the Mac. This item's place commands act on the server's data, and no place sends a path on this Mac to the server as if it were the server's. | the user |
| 2026-11-07 | Remote is an add-on, never the only way the app runs. The CLI alone, and the Mac app over pipes to a local CLI, stay complete and the default. | the user |
| 2026-11-07 | The SPEC amendment is approved and written: the GUI reaches the CLI over stdio, or over an authenticated connection to the person's own `apogee serve`. | the user |
| 2026-11-07 | iPhone and iPad are clients of the server only, with no on-device core. The Mac and the CLI stay standalone. | the user |
| 2026-11-07 | v0.2.5 is the target for macOS, iPadOS and iOS (the iPhone, and the foldable iPhone in both its folded and unfolded screens) working fully, reached in successive versions. | the user |
| 2026-11-07 | Windows and Android follow v0.2.5 (a recorded default, among the user's calls of that day). | default |
| 2026-11-07 | In v0.2.4, track 7, beside 7a and 7b. It needs 7a's connection, and 7a needs the CLI's network reads and sessions (59b, 59d, v0.2.3). It must land before 8a/8b (v0.2.5): the iPad and iPhone apps are remote-only, so a place that does not work remotely is a place they do not have. | the spec |
| 2026-11-07 | Gated on CLI 59e as well as 7a. Tasks is the place a connection's loss hurts most: a remote run must be found and followed again after reconnecting or from another device, and only 59e makes a server task outlive its connection. | the spec |

## Out of scope

- The connection itself, its transport and pairing (7a; CLI 59a, 59b).
- The server's reads and commands (CLI 59d).
- The Mac's files and shell offered to a server session (7b).
- The iPad and iPhone layouts and the foldable (8b, 9a, 9b).
- Uploading a device's files to the server, and a code graph of the Mac's own folder built by the server (each a later item if wanted).
- Linux, Windows and Android, which follow v0.2.5.

## Links

- **Blocked by:** [7a](app-connection-local-or-server.md), [CLI 59e](../../cli/v0.2.3/tasks-outlive-the-connection.md) · **Blocks:** —
- **Relates to:** [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md) (the Mac's files and shell to a server session), [CLI 59d](../../cli/v0.2.3/reads-and-commands-over-the-network.md) (the reads and commands every place runs), CLI 59a and 59b (the connection's server side), 4k (the binary-change re-ask this follows), 5a (classified `acp`), 6g (`tasks-screen-truthful`, [Milestone G](../../../assistant/darwin/MILESTONES.md#milestone-g--the-second-sweeps-gui-findings)), [M18](../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel) (the dev channel), [`http-api.md`](../../../reference/cli/http-api.md), [`machine-mode.md`](../../../reference/cli/machine-mode.md), the GUI's [ADR 0003](../../../adrs/gui/parity.md); [M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed) and [M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name) (every name after `--`: best landed before this item records its argv baseline), and [M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person) (a finished job's notes, which a remote job fills too); [CLI-M59](../../../assistant/cli/MILESTONES.md#2026-11-27--the-http-graph-jobs-say-what-the-cli-says-maintenance-item-m59-the-http-graph-jobs-say-what-the-cli-says-and-build-a-mixed-graphs-code-as-the-cli-does) (shipped 2026-11-27: the HTTP graph jobs' `warnings`, a remote graph job's notes).
- **Follow-on:** [8a](../v0.2.5/app-builds-for-ios-and-ipados.md), [8b](../ipados/v0.2.5/ipad-app.md), [9a](../ios/v0.2.5/iphone-app.md) (remote-only, so they have only the places that work here); a CLI sibling for the media read and the definition read and write, queued if 59d does not carry them.
