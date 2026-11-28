# MOD-2b · The desktop mod shows graphs and knowledge records

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Claude plugin (mod) |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.2 |
| **Component** | The `/apogee` commands and the pane's Knowledge section |
| **Labels** | `graphs` `knowledge` `slash-commands` `pane` |
| **Epic** | Track 2 · Claude aware of Apogee |
| **Blocked by** | — (1c shipped 2026-11-21, [Milestone A](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born); CLI 58b [shipped 2026-11-28](../../../assistant/cli/MILESTONES.md#2026-11-28--mcp-serve-graphs-and-knowledge-backlog-item-58b-the-server-serves-graphs-and-knowledge-records)) |
| **Blocks** | — |
| **Reporter** | The MCP-awareness spike (2026-11-07) |
| **Created** | 2026-11-07 |

---

## Summary

Claude cannot reach named graphs or knowledge records through Apogee's server; CLI 58b serves them, and the desktop mod's own surfaces, specced before those reads existed, do not show them. This item carries 58b's reads into the mod: the tools arrive through 1b's registration with no change, and the person gains `/apogee graphs`, `/apogee knowledge <question>` and a Knowledge section in the pane.

## Description

### User story

**As** a person using Apogee from the Claude desktop app,
**I want** `/apogee graphs`, `/apogee knowledge <question>` and a Knowledge section in the pane,
**so that** I can see my named graphs and the knowledge records that answer a question without leaving the chat.

### Background

The mod's track 1 registers the server's tools into a Claude session (1b), gives the person `/apogee` commands (1b) and a pane (1c), all specced before the graph and knowledge reads existed. CLI 58b serves them: `list_graphs`, `graph_stats`, `graph_report`, `knowledge_list`, `knowledge_info` and `knowledge_query`.

This item carries 58b's reads into the desktop mod's own surfaces: the tools arrive through 1b's registration with no change, and the person gains `/apogee graphs` and `/apogee knowledge <question>` and a Knowledge section in the pane.

### Evidence

From the MCP-awareness spike (2026-11-07), two capabilities Claude cannot reach through Apogee's server:

1. **W2 — named graphs.** `list_collections` never lists them.
2. **W3 — knowledge records.** There is no tool for them at all.

## Acceptance criteria

- [ ] **Given** a binary with 58b, **when** the person runs `/apogee graphs` or `/apogee knowledge <question>`, **then** each renders the CLI's documents; and the pane's Knowledge section shows graphs and records.
- [ ] **Given** a binary without 58b, **when** the person runs either command or opens the pane, **then** both say the binary needs updating and the rest of the mod works.
- [ ] The mod's suite is green.

## Constraints

- **⚠ The mod is a client of the binary** ([CLAUDE.md](../../../assistant/mod/CLAUDE.md)): documents in, commands out, tolerant reads. Each command runs the CLI's own read (`apogee graph list`, `graph stats`, `knowledge list|query`, each `--output-format json`) and renders its document; nothing is computed here.
- **The four layers** (ADR 0001): document types in Data, the client calls in Business, registration and rendering in Presentation.
- **Tolerant of an older binary.** On a CLI without 58b, the commands and the section say the binary is too old to list graphs or records, naming the update, and the rest of the mod works.

## Technical approach

| File | Change |
|---|---|
| `src/data/` | the `graph list`, `graph stats` and `knowledge` document types |
| `src/business/` | the reads (`apogee graph list --output-format json`, …) |
| `src/presentation/` | `/apogee graphs` (each graph with its kind and size), `/apogee knowledge <question>` (the records that answer it); the pane's Knowledge section (graphs and the newest records) |

**Reference.** Mod 1b ([shipped 2026-11-21](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)) and 1c ([shipped 2026-11-21](../../../assistant/mod/MILESTONES.md#2026-11-21--mod-pane-backlog-item-1c-the-pane-beside-the-chat): the pane, `src/presentation/pane/`), whose surfaces this extends; CLI 58b ([MILESTONES](../../../assistant/cli/MILESTONES.md#2026-11-28--mcp-serve-graphs-and-knowledge-backlog-item-58b-the-server-serves-graphs-and-knowledge-records), shipped 2026-11-28).

## Testing

- **Handler matrices** over recorded documents from the binary: each command's document rendered from a recorded fixture.
- **The older-binary refusal**: the older-binary path pinned.
- **Argv pins per command**: each command's argv pinned.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What do `/apogee knowledge` and `/apogee graphs` show? | `[default]` | `/apogee knowledge` shows the top 5 records with their branch and date; `/apogee graphs` every graph. |
| Does the pane get a refresh button? | `[default]` | No refresh button in the pane: refreshing is CLI 58d's served call, offered to Claude where the person listed it; the person refreshes from the CLI. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the MCP-awareness spike (the user's calls: both hosts, opt-in writes, the next release). Mod track 2, v0.2.2. | the user |
| 2026-11-07 | A new item, not an extension of 1b: 1b is a v0.2.1 item and cannot wait on v0.2.2's 58b. | the spec |
| 2026-11-28 | **Released: CLI 58b shipped** ([MILESTONES](../../../assistant/cli/MILESTONES.md#2026-11-28--mcp-serve-graphs-and-knowledge-backlog-item-58b-the-server-serves-graphs-and-knowledge-records)): `mcp serve` lists `list_graphs`, `graph_stats`, `graph_report`, `knowledge_list`, `knowledge_info` and `knowledge_query` after the five status reads, each its command's document byte for byte -- `graph list`, `graph stats`, `graph report`'s Markdown as text, `knowledge list`, `knowledge info` and the new `knowledge query --output-format json` (with `--rerank off`) -- so `/apogee graphs` and `/apogee knowledge` can read the CLI's documents or the served tools; `knowledge_list` takes `status` and `discipline`, `knowledge_query` `question` and `top_k`. The mod's recorded tool list (`tests/support/recorded.ts`) predates them. | CLI 58b's build |

## Out of scope

- A refresh button in the pane (the recorded default above): refreshing is CLI 58d's served call; the person refreshes from the CLI.

## Links

- **Blocked by:** — (1c shipped 2026-11-21, [Milestone A](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born); CLI 58b [shipped 2026-11-28](../../../assistant/cli/MILESTONES.md#2026-11-28--mcp-serve-graphs-and-knowledge-backlog-item-58b-the-server-serves-graphs-and-knowledge-records), the reads this item carries) · **Blocks:** —
- **Relates to:** Mod 1b ([shipped 2026-11-21](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)), whose registration carries the tools and whose `/apogee` commands this extends; CLI 58d (the served refresh call); ADR 0001 (the four layers).
- **Follows from:** the MCP-awareness spike (2026-11-07), walls W2 and W3.
