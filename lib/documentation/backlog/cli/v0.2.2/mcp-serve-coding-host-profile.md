# CLI-58c · A lighter tool set for coding hosts

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.2 |
| **Component** | `apogee mcp serve` (the MCP server) and `mcp presets` |
| **Labels** | `mcp-serve` `tool-profile` `coding-host` |
| **Epic** | Track 58 · Claude aware of Apogee |
| **Blocked by** | — |
| **Blocks** | [mod 2a](../../mod/v0.2.2/claude-code-plugin.md) |
| **Reporter** | The MCP-awareness spike (2026-11-07), on the user's idea |
| **Created** | 2026-11-07 |

---

## Summary

`apogee mcp serve` hands a coding host all 22 of its tools, and 8 of them duplicate the host's own file and git tools. This item adds a lighter profile, `--profile knowledge`: Apogee's knowledge, models and notes without the duplicated file and git tools, for a coding assistant to register. The full set stays the default.

## Description

### User story

**As** a person using Apogee from a coding assistant such as Claude Code,
**I want** to register Apogee's server with only the tools my coding host does not already have,
**so that** the host's model context is not spent on two sets of the same tools every turn, and it reads the project through the host's own tools and permissions.

### Background

The MCP-awareness spike (2026-11-07; the branch binary `945784f5`, a scratch home, a raw JSON-RPC client) listed `apogee mcp serve`'s 22 tools. This item takes its wall W4. The lighter profile keeps Apogee's knowledge, models and notes and drops the duplicated file and git tools; the Claude Code plugin (mod [2a](../../mod/v0.2.2/claude-code-plugin.md)) registers this profile.

### Evidence

1. **W4 — tool clutter.** 8 of the 22 tools duplicate what a coding host already has: `read_file`, `list_directory`, `search_files`, `grep_files` and `git_status`, `git_log`, `git_diff`, `git_show` (the spike, 2026-11-07, `945784f5`, a raw JSON-RPC client in a scratch home).
2. **Every native toolset is registered.** The server registers every native toolset (`presentation/cli/mcp_serve.cpp:477`, `tools::register_native_toolsets`), and the file tools are sandboxed to the server's launch folder.
3. **The host already covers the same ground.** Claude Code's own file and git tools cover the same ground with the host's permissions.
4. **The cost.** Two sets of the same tools cost the host's model context on every turn and invite it to read the project through the wrong door.

## Acceptance criteria

- [ ] **Given** today's tool set, **when** a client lists the tools of `apogee mcp serve --profile knowledge`, **then** it gets 14 tools over today's set (22 less the 8 file and git tools), plus 58b's reads and `complete` where listed.
- [ ] **Given** an existing registration, **when** a client uses `apogee mcp serve` or `__mcp-tools`, **then** both are unchanged: they list and answer exactly as today.
- [ ] **Given** the presets, **when** I run `mcp presets`, **then** it prints both entries, the full one and the coding-host one.
- [ ] The full suite is green.

## Constraints

- **The full set stays the default.** An existing registration (`apogee mcp serve`, or the hidden `__mcp-tools`) answers exactly as today (ADR 0006).
- **A profile only removes whole toolsets.** It never renames a tool, and a tool it keeps answers as it does in the full set.
- **One table.** The profiles and the toolsets each keeps are data in one place, with a test holding them to the registered toolsets.
- **The presets say it.** `mcp presets`' `self` prints both host entries, the full one and the coding-host one.

## Technical approach

| File | Change |
|---|---|
| `presentation/cli/mcp_serve.h/.cpp` | `--profile all\|knowledge` (default `all`); `knowledge` keeps retrieval, graph, notes, the status reads, 58b's reads and `complete` where listed, and drops the file and git toolsets; help and completion |
| `business/tools/toolsets.h/.cpp` | the toolset names the profile table refers to (no new tools) |
| `presentation/operations/mcp_presets.cpp` | `self` gains the coding-host entry (`["mcp","serve","--profile","knowledge"]`) |
| `lib/documentation/reference/cli/tools.md` | the serve chapter's profiles |

**Reference.** 54a (the served set and its order); the `self` preset; 58b (the reads `knowledge` keeps).

## Testing

- **`tests/presentation/cli/mcp_serve_test.cpp`** — each profile's list; `all` byte-identical to today's (held to today's list byte for byte); a kept tool's answer identical across profiles.
- **The profile table** — held to the registered toolsets: a toolset added later fails the test until it is placed in a profile.
- **`mcp_presets_test`** — both entries.
- **`cli.mcp_lifecycle`** — the external client over `--profile knowledge`.

## Open questions

| Question | Status | Answer |
|---|---|---|
| How is the profile chosen, and is there a config-file profile? | `[default]` | the flag is `--profile` with two values, `all` and `knowledge`; a config-file profile is not added |
| Do notes stay in `knowledge`? | `[default]` | notes stay in `knowledge`: they are Apogee's own store, not a duplicate of the host's |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the MCP-awareness spike (the user's idea; the user's calls: both hosts, opt-in writes, the next release). Track 58, v0.2.2; earmarked for v0.2.2 (track 58), ungated. | the user |

## Out of scope

- None stated beyond the constraints above.

## Links

- **Blocked by:** — · **Blocks:** [mod 2a](../../mod/v0.2.2/claude-code-plugin.md) (the Claude Code plugin registers this profile)
- **Relates to:** 54a (the served set and its order); 58b (the reads `knowledge` keeps); the `self` preset; ADR 0006
