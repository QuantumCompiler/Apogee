# Backlog

The work queue, seen one release at a time. This page is the **overview**: what each release plans across every product, with every item linked to its document. Each product's own index (its `README.md`, linked below) stays the **authority** on its rows, ids, gates and format. This page mirrors it and is updated in the same pass. The [documentation router](../CLAUDE.md) says which product a change belongs to.

## v0.2.1, the release in progress

The branch is `v0.2.1`; the last published release is v0.2.0.

**Still to build: 26 items.**
- **The design:** 20. The prototype is being rebuilt in React.
- **The mod:** 4. The Claude plugin is born.
- **The Apple apps:** 1, the app's icon.
- **The CLI:** 1, closing a gap in the git tools' confinement.

On top of that, 4 maintenance items can be picked up any time. None of the 26 waits on a decision from you.

**Can start today** (nothing gates them): [design 1a](design/v0.2.1/design-app-architecture-and-law.md), [mod 1a](mod/v0.2.1/mod-scaffold.md), [Apple apps 11a](darwin/v0.2.1/apogee-logo-as-the-app-icon.md) and [CLI 57y](cli/v0.2.1/git-folder-links-stay-in-bounds.md).

### The design ([queue](design/README.md#v021))

**Track 1:** the design prototype is rebuilt in React and TypeScript, using the same layers and screen modules as the Mac app. It reads the CLI's own recorded output, and it builds into one `lib/design/main.html` that opens with a double-click. The old one-file prototype is frozen once 1c takes its baseline. From 1e on, new screens are drawn in the React app.

| # | Item | Waits on |
|---|---|---|
| **Foundations** | | |
| 1a | [The design app's architecture and law](design/v0.2.1/design-app-architecture-and-law.md) | — |
| 1b | [Scaffold the React app and its layering check](design/v0.2.1/scaffold-and-layering-check.md) | 1a |
| 1c | [The old prototype's baseline](design/v0.2.1/old-prototype-baseline.md) | 1b |
| 1d | [The CLI stand-in](design/v0.2.1/cli-stand-in.md) | 1b |
| 1e | [Styles, the shell and the first screen](design/v0.2.1/styles-shell-and-the-first-screen.md) | 1d |
| **Screens** | | |
| 1f | [The session screen](design/v0.2.1/session-screen.md) (split in two before building) | 1e |
| 1g | [Sessions: the list, the inspector, the execute door and the console](design/v0.2.1/sessions-inspector-execute-and-console.md) | 1f |
| 1h | [Models](design/v0.2.1/models-screens.md) | 1e |
| 1i | [Settings and Appearance](design/v0.2.1/settings-and-appearance.md) | 1e |
| 1j | [Training](design/v0.2.1/training-screens.md) | 1e |
| 1k | [Video and the runtime consent](design/v0.2.1/video-and-runtime-consent.md) | 1e |
| 1l | [Compose](design/v0.2.1/compose-screens.md) | 1k |
| 1m | [Voice](design/v0.2.1/voice-screen.md) | 1k |
| 1n | [Library and Review](design/v0.2.1/library-and-review.md) | 1e |
| 1o | [Knowledge and Code graph](design/v0.2.1/knowledge-and-graph.md) | 1e |
| 1p | [Suites and Tasks](design/v0.2.1/suites-and-tasks.md) | 1e |
| **Review pages** | | |
| 1q | [The gallery and the foundations](design/v0.2.1/gallery-and-foundations.md) (replaces the claude.ai canvas) | 1e |
| 1r | [The build boards as pages](design/v0.2.1/build-boards-as-pages.md) | 1e |
| **Finish** | | |
| 1s | [The parity proof](design/v0.2.1/parity-proof.md): you see the side-by-side report before anything is deleted | 1c, every screen item (1g–1p) and both review pages (1q, 1r) |
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

**macOS.** Apogee in Xcode (track 5) and the second bug sweep's app findings (track 6) have shipped ([Milestone F](../assistant/darwin/MILESTONES.md#milestone-f--apogee-in-xcode-the-apps-side), [Milestone G](../assistant/darwin/MILESTONES.md#milestone-g--the-second-sweeps-gui-findings)). One item is left:

| # | Item | Waits on |
|---|---|---|
| 11a | [The Apogee logo is the app's icon and the official logo](darwin/v0.2.1/apogee-logo-as-the-app-icon.md) | — |

### The CLI ([queue](cli/README.md#v021))

One item is left, added to the second bug sweep on 2026-11-15:

| # | Item | Waits on |
|---|---|---|
| 57y | [Links in a git folder stay in bounds](cli/v0.2.1/git-folder-links-stay-in-bounds.md): the git tools judge the whole `.git` folder | — |

The rest of this release's three CLI tracks has shipped:
- the mod's CLI siblings, `apogee mcp serve` (track 54, [Milestone W](../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client));
- Apogee in Xcode (track 56, [Milestone AS](../assistant/cli/MILESTONES.md#milestone-as--apogee-in-xcode));
- the second bug sweep's CLI findings (track 57, [Milestone AT](../assistant/cli/MILESTONES.md#milestone-at--the-second-sweeps-cli-findings)), all but 57y.

**One step is yours.** After the release's build, you turn on the merge ruleset so CI gates merges into `stable` (57m, [the ROADMAP line](../assistant/cli/ROADMAP.md#v021--the-mods-cli-siblings-track-54-apogee-in-xcode-track-56-and-the-second-bug-sweep-track-57)).

### Ties to later releases

- **[Apple apps 10a](darwin/macos/v0.2.6/design-the-server-window.md)** (v0.2.6, designing the server window) waits on design 1e, so it is drawn in the React app.
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

One line per track. Follow the link to see the items and their gates in that product's queue.

| Release | CLI | Apple apps | Mod |
|---|---|---|---|
| v0.2.2 | [Track 58, Claude aware of Apogee](cli/README.md#v022) (58a–58d) | — | [Track 2, Claude aware of Apogee](mod/README.md#v022) (2a–2b) |
| v0.2.3 | [Track 59, the harness server](cli/README.md#v023) (59a–59h) | — | — |
| v0.2.4 | [Track 60, one tool definition and device tools](cli/README.md#v024) (60a–60e) | [Track 7, the Mac as a client](darwin/README.md#v024) (7a–7e) | — |
| v0.2.5 | — | [Track 8, iPad](darwin/README.md#v025) (8a–8c) | — |
| v0.2.6 | [Track 62, the server console](cli/README.md#v026) (62a–62h) | [Track 10, the server window](darwin/README.md#v026) (10a–10b, macOS) | — |
| v0.2.7 | [Track 63, people and access](cli/README.md#v027) (63a–63e) | [Track 10, people in the server window](darwin/README.md#v027) (10c, macOS) | — |
| v0.3.0 | — | [Track 9, iPhone, the foldable and the three platforms whole](darwin/README.md#v030) (9a–9c) | — |
| v0.3.1 | [Track 61, Apogee-tuned training](cli/README.md#v031) (61a–61b) | — | — |

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
