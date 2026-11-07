# The desktop mod shows graphs and knowledge records

**What / why.** The MCP-awareness spike (2026-11-07) found two capabilities Claude cannot reach through Apogee's server: named graphs (W2, which `list_collections` never lists) and knowledge records (W3, with no tool at all). CLI 58b serves them: `list_graphs`, `graph_stats`, `graph_report`, `knowledge_list`, `knowledge_info` and `knowledge_query`. The mod's track 1 registers the server's tools into a Claude session (1b), gives the person `/apogee` commands (1b) and a pane (1c), all specced before those reads existed.

This item carries 58b's reads into the desktop mod's own surfaces: the tools arrive through 1b's registration with no change, and the person gains `/apogee graphs` and `/apogee knowledge <question>` and a Knowledge section in the pane.

**Core constraint(s).**
- **⚠ The mod is a client of the binary** ([CLAUDE.md](../../assistant/CLAUDE.md)): documents in, commands out, tolerant reads. Each command runs the CLI's own read (`apogee graph list`, `graph stats`, `knowledge list|query`, each `--output-format json`) and renders its document; nothing is computed here.
- **The four layers** (ADR 0001): document types in Data, the client calls in Business, registration and rendering in Presentation.
- **Tolerant of an older binary**: on a CLI without 58b, the commands and the section say the binary is too old to list graphs or records, naming the update, and the rest of the mod works.

**Seam + files.**
- `src/data/` — the `graph list`, `graph stats` and `knowledge` document types.
- `src/business/` — the reads (`apogee graph list --output-format json`, …).
- `src/presentation/` — `/apogee graphs` (each graph with its kind and size), `/apogee knowledge <question>` (the records that answer it); the pane's Knowledge section (graphs and the newest records).
- Tests: handler matrices over recorded documents from the binary, the older-binary refusal, argv pins per command.

**Reference.** Mod 1b ([`mod-tools-and-commands.md`](../mod-tools-and-commands.md)) and 1c ([`mod-pane.md`](../mod-pane.md)), whose surfaces this extends; CLI 58b ([`mcp-serve-graphs-and-knowledge.md`](../../../cli/backlog/v0.2.2/mcp-serve-graphs-and-knowledge.md)).

**Decisions made** (dated):
- 2026-11-07: Specced from the MCP-awareness spike (the user's calls: both hosts, opt-in writes, the next release). Mod track 2, v0.2.2.
- 2026-11-07: A new item, not an extension of 1b: 1b is a v0.2.1 item and cannot wait on v0.2.2's 58b.

**Open calls:**
- [default: `/apogee knowledge` shows the top 5 records with their branch and date; `/apogee graphs` every graph.]
- [default: no refresh button in the pane: refreshing is CLI 58d's served call, offered to Claude where the person listed it; the person refreshes from the CLI.]

**Guardrail(s).** Each command's argv pinned and its document rendered from a recorded fixture; the older-binary path pinned.

**Acceptance criteria:**
- [ ] `/apogee graphs` and `/apogee knowledge <question>` render the CLI's documents; the pane's Knowledge section shows graphs and records.
- [ ] On a binary without 58b, both say the binary needs updating and the rest of the mod works; the mod's suite is green.

**Scope note.** Earmarked for v0.2.2 (mod track 2); 🔒 1c, CLI 58b (1c already waits on 1b).
