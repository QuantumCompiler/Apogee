# Backlog

The work queue, seen one release at a time. This page is the **overview**: what each release plans across every product, with every item linked to its document. Each product's own index (its `README.md`, linked below) stays the **authority** on its rows, ids, gates and format. This page mirrors it and is updated in the same pass. The [documentation router](../CLAUDE.md) says which product a change belongs to.

## v0.2.2, the release in progress

The branch is `v0.2.2`; the last published release is [v0.2.1](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.2.1), released 2026-11-26.

**Still to build: 5 items.**
- **The CLI:** 3. Track 58, Claude aware of Apogee: the MCP server offers a lighter set to coding hosts and refreshes what you list; it serves graphs and knowledge records since 58b shipped, and introduces itself since 58a shipped, both 2026-11-28. Track 64, self-update: `apogee update`.
- **The mod:** 2. Track 2, Claude aware of Apogee: Apogee as a Claude Code plugin, and graphs and knowledge records in the desktop mod.
- **The Apple apps:** none in this release; their next is v0.2.4.
- **The design:** none planned yet.

Outside the release, no maintenance item is waiting: the last, the CLI's M62, shipped 2026-11-28 (2026-11-26, M48 split into M48 and M61 on 2026-11-27, M48, M49, M50, M51, M52, M53, M54, M55, M56, M57, M58 and M59 shipped 2026-11-27, M62 specced 2026-11-27, M60, M61 and M62 shipped 2026-11-28); the Apple apps have none.

**Can start today** (nothing gates them): [CLI 58c](cli/v0.2.2/mcp-serve-coding-host-profile.md), [CLI 58d](cli/v0.2.2/mcp-serve-opt-in-refresh.md), [CLI 64a](cli/v0.2.2/apogee-update.md) and [mod 2b](mod/v0.2.2/mod-graphs-and-knowledge.md) (2b since CLI 58b shipped, 2026-11-28); no maintenance item is left.

### The CLI ([queue](cli/README.md#v022))

**Track 58, Claude aware of Apogee:** `apogee mcp serve` answers with 28 read-only tools since 58b added the graph and knowledge reads ([shipped 2026-11-28](../assistant/cli/MILESTONES.md#2026-11-28--mcp-serve-graphs-and-knowledge-backlog-item-58b-the-server-serves-graphs-and-knowledge-records)) and says what it is and what your setup holds since 58a ([shipped 2026-11-28](../assistant/cli/MILESTONES.md#2026-11-28--mcp-serve-introduces-itself-backlog-item-58a-the-server-introduces-itself)), so a host such as Claude Code knows when to use it. Still to come: a lighter set for coding hosts, and calls that refresh what you list.

| # | Item | Waits on |
|---|---|---|
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
| 2a | [Apogee as a Claude Code plugin](mod/v0.2.2/claude-code-plugin.md) | CLI 58c |
| 2b | [The desktop mod shows graphs and knowledge records](mod/v0.2.2/mod-graphs-and-knowledge.md) | — |

**One step is yours, still open from v0.2.1.** The merge ruleset that makes CI gate merges into `stable` is not in force yet (`required-checks.py --verify`, 2026-11-26: "there is no 'Stable: CI must pass' ruleset"). `lib/scripts/required-checks.py --pr 12 --apply` creates it from v0.2.1's pull request, the mod's two jobs, `what changed (mod)` and `build mod`, among its required checks ([the ROADMAP line](../assistant/cli/ROADMAP.md#v021--the-mods-cli-siblings-track-54-apogee-in-xcode-track-56-and-the-second-bug-sweep-track-57-released-2026-11-26--release-notes)).

### Ties to later releases

None now.

## Any time: maintenance

Maintenance items aren't tied to a release; any one can be picked up by name.

| Product | # | Item | Waits on |
|---|---|---|---|

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
