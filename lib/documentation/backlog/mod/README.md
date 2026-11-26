# Apogee Mod — Backlog

**The work queue: one document per work item** — the layer between [ROADMAP.md](../../assistant/mod/ROADMAP.md)'s board and [MILESTONES.md](../../assistant/mod/MILESTONES.md)'s record. An agent takes an item from the index and **implements it straight from its document**. (The CLI's queue is [`backlog/cli/`](../cli/README.md), the GUI's [`backlog/darwin/`](../darwin/README.md); an item needing a change in another application gets a sibling there, cross-gated by path.)

**Lifecycle, tags, gates and format** are the house's, exactly as the [GUI backlog](../darwin/README.md) states them: discussed → specced here (kebab-case, the ticket format: key `MOD`, `MOD-1a`, Project `Claude plugin (mod)`, its header restating the index row, the index staying the authority) → taken (🚧) → shipped into MILESTONES and **deleted with its row**; each step that adds, ships, moves or re-gates a row changes the [backlog overview](../README.md#keeping-this-page-true) in the same pass. An open question's Status cell is `[user]` (blocks the build), `[default]` (the recorded default, taken and logged in the Decision log unless vetoed) or `(consumed decision)` (owned elsewhere); Status is 🟢 / 🔒 *items* / 🚧 / ❌. Ids are ever-assigned: lettered tracks (`1a`), a new concern opening the next track number; a maintenance table is born with its first item, the CLI's conventions mirrored. Items live at the backlog root while the queue keeps one table; new tables get subdirectories with their first item.

## Index

Priority-ordered: the topmost item whose gate is satisfied is the next one to build.

**Track 1, the mod is born** (1a–1d) has shipped, complete in **v0.2.1** — in development on its branch, released when it merges into `stable`. Opened 2026-10-21 from that day's mod spike (the user's call), it made the Claude plugin at `lib/src/mod/`: its scaffold and four layers under the layering check (1a), Apogee's served tools, the `/apogee` set and the status line (1b), the pane (1c) and its own workflow (1d). Its story is [ROADMAP.md](../../assistant/mod/ROADMAP.md), and each item's record is in [Milestone A](../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born). The earliest table still open is **v0.2.2**'s, Claude aware of Apogee (track 2).

### v0.2.2

**Track 2, Claude aware of Apogee**, opened 2026-11-07 from that day's spike (the user's idea and calls; the walls, the measurements and the CLI's half are the CLI's [track 58 preamble](../cli/README.md#v022)). The user's call **both hosts** puts Claude Code beside the desktop mod: **2a** packages the server and a when-to-use skill as a Claude Code plugin, waiting on CLI 58a (the server's own inventory) and 58c (the coding-host profile); **2b** carries CLI 58b's graph and knowledge reads into the desktop mod's commands and pane, waiting on CLI 58b (its 1c gate satisfied 2026-11-21, when 1c shipped). Every call is a recorded default.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 2a | Apogee as a Claude Code plugin — the server registered with the coding-host profile and a skill saying when to use Apogee, installable from this repository's marketplace | v0.2.2 | [`claude-code-plugin.md`](v0.2.2/claude-code-plugin.md) | 🔒 CLI 58a, CLI 58c |
| 2b | The desktop mod shows graphs and knowledge records — `/apogee graphs`, `/apogee knowledge <question>` and the pane's Knowledge section over CLI 58b's reads | v0.2.2 | [`mod-graphs-and-knowledge.md`](v0.2.2/mod-graphs-and-knowledge.md) | 🔒 CLI 58b |
