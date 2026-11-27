# Backlog

The work queue, seen one release at a time. This page is the **overview**: what each release plans across every product, with every item linked to its document. Each product's own index (its `README.md`, linked below) stays the **authority** on its rows, ids, gates and format. This page mirrors it and is updated in the same pass. The [documentation router](../CLAUDE.md) says which product a change belongs to.

## v0.2.2, the release in progress

The branch is `v0.2.2`; the last published release is [v0.2.1](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.2.1), released 2026-11-26.

**Still to build: 7 items.**
- **The CLI:** 5. Track 58, Claude aware of Apogee: the MCP server introduces itself, serves graphs and knowledge records, offers a lighter set to coding hosts, and refreshes what you list. Track 64, self-update: `apogee update`.
- **The mod:** 2. Track 2, Claude aware of Apogee: Apogee as a Claude Code plugin, and graphs and knowledge records in the desktop mod.
- **The Apple apps:** none in this release; their next is v0.2.4.
- **The design:** none planned yet.

Outside the release, 18 maintenance items can be picked up any time: the CLI's M43–M47 (2026-11-22) and M48–M60 (2026-11-26); the Apple apps have none. Six wait on another maintenance item: the CLI's M44 on M43, M46 on M45, M49 and M50 on M48, M56 on M55, and M60 on M58. None of the 18 waits on a decision from you.

**Can start today** (nothing gates them): [CLI 58b](cli/v0.2.2/mcp-serve-graphs-and-knowledge.md), [CLI 58c](cli/v0.2.2/mcp-serve-coding-host-profile.md), [CLI 58d](cli/v0.2.2/mcp-serve-opt-in-refresh.md) and [CLI 64a](cli/v0.2.2/apogee-update.md); and any maintenance item below that waits on nothing (the CLI's M43, M45, M47, M48, M51–M55 and M57–M59).

### The CLI ([queue](cli/README.md#v022))

**Track 58, Claude aware of Apogee:** `apogee mcp serve`, which already answers with 22 read-only tools, learns to say what it is and what it can reach, so a host such as Claude Code knows when to use it.

| # | Item | Waits on |
|---|---|---|
| 58a | [The server introduces itself](cli/v0.2.2/mcp-serve-introduces-itself.md) | 58b |
| 58b | [Graphs and knowledge records served](cli/v0.2.2/mcp-serve-graphs-and-knowledge.md) | — |
| 58c | [A lighter tool set for coding hosts](cli/v0.2.2/mcp-serve-coding-host-profile.md) | — |
| 58d | [Claude can refresh what you list](cli/v0.2.2/mcp-serve-opt-in-refresh.md) | — |

**Track 64, self-update:** `apogee update`, the third install path beside the two installers, with a once-a-day notice, on by default on a terminal (your call, 2026-11-26).

| # | Item | Waits on |
|---|---|---|
| 64a | [`apogee update` brings the installed binary to the latest release](cli/v0.2.2/apogee-update.md) | — |

### The mod ([queue](mod/README.md#v022))

**Track 2, Claude aware of Apogee:** the server reaches Claude through both hosts.

| # | Item | Waits on |
|---|---|---|
| 2a | [Apogee as a Claude Code plugin](mod/v0.2.2/claude-code-plugin.md) | CLI 58a, CLI 58c |
| 2b | [The desktop mod shows graphs and knowledge records](mod/v0.2.2/mod-graphs-and-knowledge.md) | CLI 58b |

**One step is yours, still open from v0.2.1.** The merge ruleset that makes CI gate merges into `stable` is not in force yet (`required-checks.py --verify`, 2026-11-26: "there is no 'Stable: CI must pass' ruleset"). `lib/scripts/required-checks.py --pr 12 --apply` creates it from v0.2.1's pull request, the mod's two jobs, `what changed (mod)` and `build mod`, among its required checks ([the ROADMAP line](../assistant/cli/ROADMAP.md#v021--the-mods-cli-siblings-track-54-apogee-in-xcode-track-56-and-the-second-bug-sweep-track-57-released-2026-11-26--release-notes)).

### Ties to later releases

None now.

## Any time: maintenance

Maintenance items aren't tied to a release; any one can be picked up by name.

| Product | # | Item | Waits on |
|---|---|---|---|
| CLI | M43 | [The gate's config edits land, or say they did not](cli/maintenance/gate-edits-land-or-say-they-did-not.md) | — |
| CLI | M44 | [The YAML editor refuses, by name, the layouts it does not write](cli/maintenance/yaml-editor-refuses-the-layouts-it-does-not-write.md) | M43 |
| CLI | M45 | [A collection is named, not pathed, at every reader](cli/maintenance/a-collection-is-named-not-pathed-at-every-reader.md) | — |
| CLI | M46 | [A collection the config can't hold is refused before it is written](cli/maintenance/a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md) | M45 |
| CLI | M47 | [Config migrate carries every line the loader reads, or names the one it can't](cli/maintenance/config-migrate-carries-every-line-or-names-it.md) | — |
| CLI | M48 | [A suggested command runs as printed, whatever the name or path in it](cli/maintenance/a-suggested-command-runs-as-printed.md) | — |
| CLI | M49 | [A printed command names the binary and root that printed it](cli/maintenance/a-printed-command-names-the-binary-that-printed-it.md) | M48 |
| CLI | M50 | [A backend name the editors refuse gets the rename door that reaches it](cli/maintenance/a-refused-backend-name-gets-the-rename-that-reaches-it.md) | M48 |
| CLI | M51 | [The TUI's row actions reach a row whose name starts with a dash](cli/maintenance/the-tuis-row-actions-reach-a-dash-led-row.md) | — |
| CLI | M52 | [A refused agent or MCP scaffold writes nothing](cli/maintenance/a-refused-scaffold-writes-nothing.md) | — |
| CLI | M53 | [A symphony the CLI writes is one it can delete](cli/maintenance/a-symphony-the-cli-writes-is-one-it-can-delete.md) | — |
| CLI | M54 | [Rename and HTTP GET find a config entry by its case-folded name](cli/maintenance/rename-and-get-find-an-entry-by-its-folded-name.md) | — |
| CLI | M55 | [A dataset is named, not pathed, at every reader](cli/maintenance/a-dataset-is-named-not-pathed-at-every-reader.md) | — |
| CLI | M56 | [A new dataset name is one every verb reads as a name](cli/maintenance/a-new-dataset-name-is-one-every-verb-reads.md) | M55 |
| CLI | M57 | [A saved chat names only its own files](cli/maintenance/a-saved-chat-names-only-its-own-files.md) | — |
| CLI | M58 | [A run's notes are said once, on stderr, as `apogee <verb>: …`](cli/maintenance/a-runs-notes-are-said-once-on-stderr.md) | — |
| CLI | M59 | [The HTTP graph jobs say what the CLI says, and build a mixed graph's code as the CLI does](cli/maintenance/the-http-graph-jobs-say-what-the-cli-says.md) | — |
| CLI | M60 | [The models verbs say their notes in the house voice on stderr, and as notices on an open stream](cli/maintenance/the-models-verbs-say-their-notes-on-stderr.md) | M58 |

## Later releases

One row per release, with each track linked. Follow a link to see the items and their gates in that product's queue.

| Release | CLI | Apple apps | Mod |
|---|---|---|---|
| v0.2.3 | [Track 59, the harness server](cli/README.md#v023) (59a–59h) | — | — |
| v0.2.4 | [Track 60, one tool definition and device tools](cli/README.md#v024) (60a–60e); [track 62, the server console](cli/README.md#v024) (62a–62h); [track 63, people and access](cli/README.md#v024) (63a–63e) | [Track 7, the Mac as a client](darwin/README.md#v024) (7a–7e); [track 10, the server window and its People place](darwin/README.md#v024) (10a–10c, macOS) | — |
| v0.2.5 | — | [Track 8, iPad](darwin/README.md#v025) (8a–8c); [track 9, iPhone, the foldable and the three platforms whole](darwin/README.md#v025) (9a–9c) | — |
| v0.2.6 | [Track 61, Apogee-tuned training](cli/README.md#v026) (61a–61b) | — | — |

On 2026-11-18 (the user's call), v0.2.4 took in v0.2.6 and v0.2.7, v0.2.5 took in v0.3.0, and v0.3.1 became v0.2.6. The items kept their ids.

The design has nothing planned in a later release yet.

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
