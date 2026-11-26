# MOD-2a · Apogee as a Claude Code plugin

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Claude plugin (mod) |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.2 |
| **Component** | The Claude Code plugin (`lib/src/mod/claude-code/`, the repository's marketplace) |
| **Labels** | `claude-code` `plugin` `mcp-server` `skill` |
| **Epic** | Track 2 · Claude aware of Apogee |
| **Blocked by** | [CLI 58a](../../cli/v0.2.2/mcp-serve-introduces-itself.md), [CLI 58c](../../cli/v0.2.2/mcp-serve-coding-host-profile.md) |
| **Blocks** | — |
| **Reporter** | The user (the idea and the "both hosts" call, 2026-11-07), shaped by that day's spike |
| **Created** | 2026-11-07 |

---

## Summary

Apogee's MCP server already ships, but a Claude Code user has to register it by hand from `apogee mcp presets`, and nothing tells Claude when Apogee's tools are the right ones. This item packages Apogee for Claude Code as a plugin: the server's registration (with the coding-host profile) and a skill saying when to use it, installable from this repository's marketplace.

## Description

### User story

**As** a person using Apogee who works in Claude Code,
**I want** to install Apogee as a Claude Code plugin from this repository's marketplace, with its server registered and a skill that says when to reach for it,
**so that** Claude uses my local models, indexed documents and knowledge graphs at the right moments without my registering the server by hand.

### Background

The user's idea, 2026-11-07: a Claude mod or MCP server that makes Claude aware of Apogee's local models, retrieval and knowledge graph. The user's call the same day: **both hosts** — the Claude desktop app (the mod, track 1) and **Claude Code**.

This item packages Apogee for Claude Code as a plugin: the server's registration and a skill saying when to use it, installable from this repository. It is the Claude Code half of the "both hosts" answer; the mod's track 1 is the desktop half.

### Evidence

From the spike (2026-11-07, the day the track opened).

1. **The capability shipped.** `apogee mcp serve` (CLI 54a/54b, 2026-11-01): 22 read-only tools over the branch binary `945784f5`, plus an opt-in local-model `complete`.
2. **The packaging for Claude Code is missing.** Today a Claude Code user registers the server by hand from the entry `apogee mcp presets` prints.
3. **Nothing tells Claude when to use it.** Nothing tells Claude when Apogee's tools are the right ones.

## Acceptance criteria

- [ ] **Given** the user's live pass against `apogee-dev`, **when** they run `/plugin marketplace add` then `/plugin install`, **then** the plugin installs, and Claude Code lists the `apogee` server with its tools and its instructions.
- [ ] The skill names only tools the server serves; the check script passes.
- [ ] The registration uses the coding-host profile; the docs say how to install and try it.

## Constraints

- **⚠ The mod is a client of the binary** ([CLAUDE.md](../../../assistant/mod/CLAUDE.md)): the plugin spawns `apogee mcp serve` and reimplements nothing. No port; the binary on the person's PATH.
- **The server says what exists; the skill says when.** The inventory (collections, graphs, local models) is CLI 58a's instructions, read live at each connection. The skill names tools and situations only, never a list that would go stale.
- **The coding-host profile.** The plugin registers `apogee mcp serve --profile knowledge` (CLI 58c), so Claude Code's own file and git tools are not duplicated.
- **Development runs on the dev channel** (the CLI docs' law, 2026-10-31): the plugin as released names `apogee`; a development install of it names `apogee-dev`, and a live pass uses that one.
- **Static files only.** A manifest, an MCP registration and a skill. No build step, no TypeScript, so the mod's four-layer law has nothing to hold here (ADR 0001 governs the mod's code).

## Technical approach

Paths under `lib/src/mod/claude-code/` except the marketplace manifest, which sits at the repository root.

| File | Change |
|---|---|
| `.claude-plugin/plugin.json` | the plugin's name (`apogee`), description and version (from `lib/release/VERSION`) |
| `.mcp.json` | the server entry: `apogee`, `["mcp","serve","--profile","knowledge"]` |
| `skills/apogee/SKILL.md` | when to reach for Apogee (the situations and tools listed below) |
| `.claude-plugin/marketplace.json` (repository root) | so `/plugin marketplace add QuantumCompiler/Apogee` then `/plugin install apogee@apogee` installs it |
| `.mcp.json` dev-channel variant | for development, naming `apogee-dev`; kept out of the marketplace |

The skill says when to reach for Apogee:

- questions about the person's indexed documents and decisions (`search_documents`, `knowledge_query`);
- how code in an indexed project connects (`graph_query`, `graph_path`, `graph_explain`);
- what local models exist (`models_status`);
- a bounded side task for a local model (`complete`, only where offered);
- refreshing a listed graph (`graph_update`, CLI 58d, only where offered).

**Reference.** CLI 54a/54b (the server, `mcp presets`' `self` entry); CLI 58a (instructions), 58c (the profile), 58d (refresh calls); the mod's track 1 (the desktop half). Claude Code's plugin format (a `.claude-plugin/plugin.json` manifest, `.mcp.json`, `skills/`, and a marketplace manifest), checked against Claude Code's plugin documentation at build.

**Docs when it ships:** the mod's `DEVELOPER.md` (the plugin's folder and how to try it), the root `README.md` (installing it).

## Testing

All hermetic.

- **The check script.** The manifest and marketplace parse; the registration's argv is a valid `apogee mcp serve` invocation, held to a recorded `tools/list` fixture from the binary; every tool the skill names exists in that fixture.
- **`claude plugin validate`** where the CLI is present.
- **Guardrail.** The fixture check pins the registration to a real invocation and the skill to real tool names, so a renamed tool fails the plugin's test (ADR 0006 forbids the rename anyway); the manifest's version is held to `lib/release/VERSION`.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Where does the plugin live? | `[default]` | The plugin lives in `lib/src/mod/claude-code/`, sharing no code with the desktop mod (mod 1a made `lib/src/mod/`, 2026-11-21). |
| How is the plugin released? | `[default]` | The marketplace serves from `stable`, so a release's merge is the plugin's release; no separate artifact. |
| Does the plugin carry slash commands? | `[default]` | No slash commands; the desktop mod's `/apogee` set is the desktop app's, and Claude Code's skill plus the server cover the same ground. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Both hosts — this item is Claude Code's. | the user |
| 2026-11-07 | In the mod's home: the mod is Apogee's Claude plugin, and this is its Claude Code form. Track 2, v0.2.2 (the user's call: the next release). | the user |
| 2026-11-07 | Gated on CLI 58a and 58c: without the instructions the skill would have to carry a stale inventory, and without the profile the plugin would duplicate Claude Code's tools. | the spec |

## Out of scope

- Hooks
- A status line
- Anything that needs the desktop mod's runtime

## Links

- **Blocked by:** [CLI 58a](../../cli/v0.2.2/mcp-serve-introduces-itself.md) (the server's instructions), [CLI 58c](../../cli/v0.2.2/mcp-serve-coding-host-profile.md) (the coding-host profile) · **Blocks:** —
- **Relates to:** CLI 54a/54b (the server, `mcp presets`), CLI 58d (refresh calls), the mod's track 1 (the desktop half, shipped 2026-11-21; mod 1a made `lib/src/mod/`), [CLAUDE.md](../../../assistant/mod/CLAUDE.md), ADR 0001, ADR 0006
- **Follows from:** the user's idea and "both hosts" call, and the spike, 2026-11-07
