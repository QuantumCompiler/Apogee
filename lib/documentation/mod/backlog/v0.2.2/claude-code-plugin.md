# Apogee as a Claude Code plugin

**What / why.** The user's idea, 2026-11-07: a Claude mod or MCP server that makes Claude aware of Apogee's local models, retrieval and knowledge graph. The user's call the same day: **both hosts** — the Claude desktop app (the mod, track 1) and **Claude Code**. The spike found the capability shipped (`apogee mcp serve`, CLI 54a/54b, 2026-11-01: 22 read-only tools over the branch binary `945784f5`, plus an opt-in local-model `complete`), and the packaging missing for Claude Code. Today a Claude Code user registers the server by hand from the entry `apogee mcp presets` prints, and nothing tells Claude when Apogee's tools are the right ones.

This item packages Apogee for Claude Code as a plugin: the server's registration and a skill saying when to use it, installable from this repository. It is the Claude Code half of the "both hosts" answer; the mod's track 1 is the desktop half.

**Core constraint(s).**
- **⚠ The mod is a client of the binary** ([CLAUDE.md](../../assistant/CLAUDE.md)): the plugin spawns `apogee mcp serve` and reimplements nothing. No port; the binary on the person's PATH.
- **The server says what exists; the skill says when.** The inventory (collections, graphs, local models) is CLI 58a's instructions, read live at each connection. The skill names tools and situations only, never a list that would go stale.
- **The coding-host profile**: the plugin registers `apogee mcp serve --profile knowledge` (CLI 58c), so Claude Code's own file and git tools are not duplicated.
- **Development runs on the dev channel** (the CLI docs' law, 2026-10-31): the plugin as released names `apogee`; a development install of it names `apogee-dev`, and a live pass uses that one.
- **Static files only**: a manifest, an MCP registration and a skill. No build step, no TypeScript, so the mod's four-layer law has nothing to hold here (ADR 0001 governs the mod's code).

**Seam + files.**
- `lib/src/mod/claude-code/.claude-plugin/plugin.json` — the plugin's name (`apogee`), description and version (from `lib/release/VERSION`).
- `lib/src/mod/claude-code/.mcp.json` — the server entry: `apogee`, `["mcp","serve","--profile","knowledge"]`.
- `lib/src/mod/claude-code/skills/apogee/SKILL.md` — when to reach for Apogee: questions about the person's indexed documents and decisions (`search_documents`, `knowledge_query`), how code in an indexed project connects (`graph_query`, `graph_path`, `graph_explain`), what local models exist (`models_status`), a bounded side task for a local model (`complete`, only where offered), and refreshing a listed graph (`graph_update`, CLI 58d, only where offered).
- `.claude-plugin/marketplace.json` at the repository root — so `/plugin marketplace add QuantumCompiler/Apogee` then `/plugin install apogee@apogee` installs it.
- A dev-channel variant for development (`.mcp.json` naming `apogee-dev`), kept out of the marketplace.
- Tests (hermetic): a check script that the manifest and marketplace parse, the registration's argv is a valid `apogee mcp serve` invocation (held to a recorded `tools/list` fixture from the binary), and every tool the skill names exists in that fixture; `claude plugin validate` where the CLI is present.
- Docs: the mod's `DEVELOPER.md` (the plugin's folder and how to try it), the root `README.md` (installing it).

**Reference.** CLI 54a/54b (the server, `mcp presets`' `self` entry); CLI 58a (instructions), 58c (the profile), 58d (refresh calls); the mod's track 1 (the desktop half). Claude Code's plugin format (a `.claude-plugin/plugin.json` manifest, `.mcp.json`, `skills/`, and a marketplace manifest), checked against Claude Code's plugin documentation at build.

**Decisions made** (dated):
- 2026-11-07: The user's call: both hosts — this item is Claude Code's.
- 2026-11-07: In the mod's home: the mod is Apogee's Claude plugin, and this is its Claude Code form. Track 2, v0.2.2 (the user's call: the next release).
- 2026-11-07: Gated on CLI 58a and 58c: without the instructions the skill would have to carry a stale inventory, and without the profile the plugin would duplicate Claude Code's tools.

**Open calls:**
- [default: the plugin lives in `lib/src/mod/claude-code/`, sharing no code with the desktop mod; if mod 1a has not yet made `lib/src/mod/`, this item makes the folder.]
- [default: the marketplace serves from `stable`, so a release's merge is the plugin's release; no separate artifact.]
- [default: the plugin carries no slash commands; the desktop mod's `/apogee` set is the desktop app's, and Claude Code's skill plus the server cover the same ground.]

**Guardrail(s).** The fixture check pins the registration to a real invocation and the skill to real tool names, so a renamed tool fails the plugin's test (ADR 0006 forbids the rename anyway); the manifest's version held to `lib/release/VERSION`.

**Acceptance criteria:**
- [ ] `/plugin marketplace add` then `/plugin install` installs the plugin, and Claude Code lists the `apogee` server with its tools and its instructions (the user's live pass, against `apogee-dev`).
- [ ] The skill names only tools the server serves; the check script passes.
- [ ] The registration uses the coding-host profile; the docs say how to install and try it.

**Scope note.** Earmarked for v0.2.2 (mod track 2); 🔒 CLI 58a, CLI 58c. Out of scope: hooks, a status line, and anything that needs the desktop mod's runtime.
