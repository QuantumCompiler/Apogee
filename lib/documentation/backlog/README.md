# Backlog

The work queue, seen one release at a time. This page is the **overview**: what each release plans across every product, with every item linked to its document. Each product's own index (its `README.md`, linked below) stays the **authority** on its rows, ids, gates and format. This page mirrors it and is updated in the same pass. The [documentation router](../CLAUDE.md) says which product a change belongs to.

## v0.2.1, the release in progress

The branch is `v0.2.1`; the last published release is v0.2.0.

**Still to build: 8 items.**
- **The design:** 4. The prototype is being rebuilt in React; its law (1a), its scaffold (1b), the old prototype's baseline (1c), the CLI stand-in (1d), its first screen (1e), the session screen's two halves (1u, 1v), the Sessions place around it with the console (1g), the Models place (1h), Settings (1i), the Training place (1j), the Video place with the runtime consent (1k), the compositions (1l), the Voice place (1m), the Library and Review places (1n), the Knowledge and Code graph places (1o) and the Suites and Tasks places (1p) have shipped: every place is ported.
- **The mod:** 4. The Claude plugin is born.
- **The Apple apps:** none; their three tracks for this release have shipped.
- **The CLI:** none; its three tracks for this release have shipped.

On top of that, 4 maintenance items can be picked up any time. None of the 8 waits on a decision from you.

**Can start today** (nothing gates them): [design 1q](design/v0.2.1/gallery-and-foundations.md), [design 1r](design/v0.2.1/build-boards-as-pages.md) and [mod 1a](mod/v0.2.1/mod-scaffold.md).

### The design ([queue](design/README.md#v021))

**Track 1:** the design prototype is rebuilt in React and TypeScript, using the same layers and screen modules as the Mac app. It reads the CLI's own recorded output, and it builds into one `lib/design/main.html` that opens with a double-click. The old one-file prototype is frozen at `b375fac9`, 1c's baseline. New screens are drawn in the React app now. The law, 1a, has shipped: the design's [ADR 0001](../adrs/design/react-design-prototype.md). So has the scaffold, 1b: the empty app, its module map and layering check, and one `npm run check` ([DEVELOPER.md](../assistant/design/DEVELOPER.md)). So has the old prototype's baseline, 1c: its still frames at that revision, re-made by `npm run baseline` ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-old-prototypes-baseline)). So has the CLI stand-in, 1d: the app's three lower layers, answering every read, command, job and chat from the CLI's own recordings ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-cli-stand-in)). So has the first screen, 1e: the tokens generated from tokens.json, the shell with its 11 places, the composition root, and the system monitor on the CLI's recorded `system` document, so the built file opens at the monitor ([DEVELOPER.md](../assistant/design/DEVELOPER.md#styles-the-shell-and-the-first-screen)). The session screen, 1f, was split on 2026-11-19 into 1u and 1v, and 1f stays spent. Its first half, 1u, has shipped too: the session's turn grown only from the CLI's recorded streams, its header and composer, the answer's markdown and code, and the presets `chat:new`, `chat:private`, `chat:play` and `chat:suite`. So has its second, 1v: the five scenarios and the session's states replayed from captured or hand-made streams, the nine presets from `chat` to `chat:compacting`, and `chat` the default start again ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-session)). So has 1g: the sidebar's list of the CLI's saved chats on every screen, the inspector beside the session, the execute door and the console, with the presets `execute` and `console` ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-sessions-place-and-the-console)). So has 1h: the Models place -- its four tabs, its sheets and five confirms -- on the CLI's recordings, a pull and a conversion landing in Stored weights, and its nine presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-models-place)). So has 1i: Settings -- its ten panes over the CLI's reads and verbs, Appearance restyling the whole app, Keyboard listing the shell's keys -- and its thirteen presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#settings)). So has 1j: the Training place -- its five tabs, the promote and pull sheets, a pipeline, a cycle pass, a promote and a pull replayed from the CLI's streams -- and its ten presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-training-place)). So has 1k: the Video place's clips over the records the CLI wrote, a clip's page with its player, the generate form, a run replayed from its stream, and the one-time install consent and card, with the player, the phase row, the install card and the consent sheet in the shared styles module, and its four presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-video-place-and-the-runtime-consent)). So has 1l: the compositions in Video's second tab -- a brief planned on paper and rendered scene by scene from the project the composer saves, Stop and Resume, an edit previewed and rendered as a new video -- the chosen voice shared through `styles`, and its five presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-compositions)). So has 1m: the Voice place -- the speeches over the records the CLI wrote beside their WAVs, the speak card with the voice model's own voices, the selected speech with its waveform, player and sentence timings, a speech spoken from a hand-made stream and read back from the folder -- and its preset `voice` ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-voice-place)). So has 1n: the Library and Review places -- the agents, symphonies and MCP servers, the person's own definition files edited in place and closed asked first, Review's dry run, its targets, the last sweep and its skills -- and their ten presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#library-and-review)). So has 1o: the Knowledge and Code graph places -- the decision records and the collections on the CLI's recordings, the code graph on the recorded `demo` graph with its Functions and Files views, the notes prose graph, the More menu's export, dedupe and delete, and Ask about this landing in a new session -- and their six presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#knowledge-and-the-code-graph)). So has 1p: the Suites and Tasks places -- the recorded suites and the CLI's verdict on each, a chat opened under one and the session switched to it, the ledger's tasks, a run followed live from its recorded stream with halt, cancel, resume and Run again, a refusal's alert and the New task sheet -- and their nine presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#suites-and-tasks)); every place is ported. All seventeen are in [Milestone A](../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react).

| # | Item | Waits on |
|---|---|---|
| **Review pages** | | |
| 1q | [The gallery and the foundations](design/v0.2.1/gallery-and-foundations.md) (replaces the claude.ai canvas) | — |
| 1r | [The build boards as pages](design/v0.2.1/build-boards-as-pages.md) | — |
| **Finish** | | |
| 1s | [The parity proof](design/v0.2.1/parity-proof.md): you see the side-by-side report before anything is deleted | both review pages (1q, 1r) |
| 1t | [Retire the old prototype and rewrite the law](design/v0.2.1/retire-the-old-prototype.md): the old files and the canvas go | 1s |

### The mod ([queue](mod/README.md#index))

**Track 1, the mod is born:** the Claude plugin, driving the same `apogee` binary through its MCP server and JSON documents.

| # | Item | Waits on |
|---|---|---|
| 1a | [The mod scaffold](mod/v0.2.1/mod-scaffold.md): the plugin skeleton, its layers and finding the binary | — |
| 1b | [Tools, slash commands and the status line](mod/v0.2.1/mod-tools-and-commands.md) | 1a |
| 1c | [The pane](mod/v0.2.1/mod-pane.md): backends, models, training and the ledger | 1b |
| 1d | [The mod's pipeline](mod/v0.2.1/mod-pipeline.md) | 1a |

### The Apple apps ([queue](darwin/README.md#v021))

**macOS.** Nothing is left to build. Apogee in Xcode (track 5), the second bug sweep's app findings (track 6) and the app's logo (track 11) have shipped ([Milestone F](../assistant/darwin/MILESTONES.md#milestone-f--apogee-in-xcode-the-apps-side), [Milestone G](../assistant/darwin/MILESTONES.md#milestone-g--the-second-sweeps-gui-findings), [Milestone H](../assistant/darwin/MILESTONES.md#milestone-h--the-apps-logo)).

### The CLI ([queue](cli/README.md#v021))

Nothing is left to build. This release's three CLI tracks have shipped:
- the mod's CLI siblings, `apogee mcp serve` (track 54, [Milestone W](../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client));
- Apogee in Xcode (track 56, [Milestone AS](../assistant/cli/MILESTONES.md#milestone-as--apogee-in-xcode));
- the second bug sweep's CLI findings (track 57, [Milestone AT](../assistant/cli/MILESTONES.md#milestone-at--the-second-sweeps-cli-findings)).

**One step is yours.** After the release's build, you turn on the merge ruleset so CI gates merges into `stable` (57m, [the ROADMAP line](../assistant/cli/ROADMAP.md#v021--the-mods-cli-siblings-track-54-apogee-in-xcode-track-56-and-the-second-bug-sweep-track-57)).

### Ties to later releases

- **[Mod 2b](mod/v0.2.2/mod-graphs-and-knowledge.md)** (v0.2.2) waits on mod 1c.

## Any time: maintenance

Maintenance items aren't tied to a release; any one can be picked up by name.

| Product | # | Item | Waits on |
|---|---|---|---|
| CLI | M34 | [A failed Claude CLI turn says why](cli/maintenance/claude-cli-says-why.md) | — |
| CLI | M35 | [The code server's write guard holds for deletes, folders and permissions](cli/maintenance/code-exec-write-guard-holds.md) | — |
| Apple apps (macOS) | M18 | [A development build of the app runs on the dev channel](darwin/macos/maintenance/app-dev-channel.md) | — |
| Apple apps (macOS) | M19 | [Copy in Settings › Xcode says it copied](darwin/macos/maintenance/xcode-copy-says-copied.md) | — |

## Later releases

One row per release, with each track linked. Follow a link to see the items and their gates in that product's queue.

| Release | CLI | Apple apps | Mod |
|---|---|---|---|
| v0.2.2 | [Track 58, Claude aware of Apogee](cli/README.md#v022) (58a–58d) | — | [Track 2, Claude aware of Apogee](mod/README.md#v022) (2a–2b) |
| v0.2.3 | [Track 59, the harness server](cli/README.md#v023) (59a–59h) | — | — |
| v0.2.4 | [Track 60, one tool definition and device tools](cli/README.md#v024) (60a–60e); [track 62, the server console](cli/README.md#v024) (62a–62h); [track 63, people and access](cli/README.md#v024) (63a–63e) | [Track 7, the Mac as a client](darwin/README.md#v024) (7a–7e); [track 10, the server window and its People place](darwin/README.md#v024) (10a–10c, macOS) | — |
| v0.2.5 | — | [Track 8, iPad](darwin/README.md#v025) (8a–8c); [track 9, iPhone, the foldable and the three platforms whole](darwin/README.md#v025) (9a–9c) | — |
| v0.2.6 | [Track 61, Apogee-tuned training](cli/README.md#v026) (61a–61b) | — | — |

On 2026-11-18 (the user's call), v0.2.4 took in v0.2.6 and v0.2.7, v0.2.5 took in v0.3.0, and v0.3.1 became v0.2.6. The items kept their ids.

The design has nothing planned after v0.2.1 yet.

## The queues

| Folder | Product |
|---|---|
| [`cli/`](cli/) | **The CLI**, the harness itself: commands, backends, the TUI, `serve`, machine mode |
| `gui/` | **Every GUI app**, what binds every graphical app alike, whatever its language |
| [`darwin/`](darwin/) | **The Apple apps**, macOS, iPadOS and iOS, one Swift codebase; an item or note for one OS sits in its subfolder |
| [`mod/`](mod/) | **The mod**, the Claude plugin, driving the same binary |
| [`design/`](design/) | **The design**, the prototype every GUI app is built from (`lib/design/`) |
| `unix/` | **The Linux app**, not built yet |
| `windows/` | **The Windows app**, not built yet |

There is no Android queue yet.

## Keeping this page true

A skill or session that changes a product's index changes this page in the same pass:
- **An item is added:** its row goes into its release's section here, or into Later releases as a track line.
- **An item ships:** its row leaves, and the counts and "can start today" are updated.
- **An item moves:** its row moves.
- **An item is split:** its sub-items replace its row.
- **A gate changes:** the row's "Waits on" changes.
- **A release ships:** the next release becomes "in progress".

The docs check catches a stale link here, but not a missing row. So the backlog skills' validation step also compares this page with the product indexes.
