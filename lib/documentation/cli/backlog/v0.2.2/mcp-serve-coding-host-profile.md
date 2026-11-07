# A lighter tool set for coding hosts

**What / why.** The MCP-awareness spike (2026-11-07; the branch binary `945784f5`, a scratch home, a raw JSON-RPC client) listed `apogee mcp serve`'s 22 tools.

**W4 — tool clutter.** 8 of the 22 duplicate what a coding host already has: `read_file`, `list_directory`, `search_files`, `grep_files` and `git_status`, `git_log`, `git_diff`, `git_show`. The server registers every native toolset (`presentation/cli/mcp_serve.cpp:477`, `tools::register_native_toolsets`), and the file tools are sandboxed to the server's launch folder. Claude Code's own file and git tools cover the same ground with the host's permissions. Two sets of the same tools cost the host's model context on every turn and invite it to read the project through the wrong door.

This item adds a lighter profile: Apogee's knowledge, models and notes without the duplicated file and git tools, for a coding assistant to register.

**Core constraint(s).**
- **The full set stays the default**: an existing registration (`apogee mcp serve`, or the hidden `__mcp-tools`) answers exactly as today (ADR 0006).
- **A profile only removes whole toolsets.** It never renames a tool, and a tool it keeps answers as it does in the full set.
- **One table**: the profiles and the toolsets each keeps are data in one place, with a test holding them to the registered toolsets.
- **The presets say it**: `mcp presets`' `self` prints both host entries, the full one and the coding-host one.

**Seam + files.**
- `presentation/cli/mcp_serve.h/.cpp` — `--profile all|knowledge` (default `all`); `knowledge` keeps retrieval, graph, notes, the status reads, 58b's reads and `complete` where listed, and drops the file and git toolsets; help and completion.
- `business/tools/toolsets.h/.cpp` — the toolset names the profile table refers to (no new tools).
- `presentation/operations/mcp_presets.cpp` — `self` gains the coding-host entry (`["mcp","serve","--profile","knowledge"]`).
- `lib/documentation/cli/reference/tools.md` — the serve chapter's profiles.
- Tests: `tests/presentation/cli/mcp_serve_test.cpp` (each profile's list; `all` byte-identical to today's; a kept tool's answer identical across profiles); `mcp_presets_test` (both entries); `cli.mcp_lifecycle` (the external client over `--profile knowledge`).

**Reference.** 54a (the served set and its order); the `self` preset; 58b (the reads `knowledge` keeps).

**Decisions made** (dated):
- 2026-11-07: Specced from the MCP-awareness spike (the user's idea; the user's calls: both hosts, opt-in writes, the next release). Track 58, v0.2.2.

**Open calls:**
- [default: the flag is `--profile` with two values, `all` and `knowledge`; a config-file profile is not added.]
- [default: notes stay in `knowledge`: they are Apogee's own store, not a duplicate of the host's.]

**Guardrail(s).** `all` held to today's list byte for byte; the profile table held to the registered toolsets (a toolset added later fails the test until it is placed in a profile).

**Acceptance criteria:**
- [ ] `apogee mcp serve --profile knowledge` lists 14 tools over today's set (22 less the 8 file and git tools), plus 58b's reads and `complete` where listed.
- [ ] `apogee mcp serve` and `__mcp-tools` are unchanged; `mcp presets` prints both entries; the full suite is green.

**Scope note.** Earmarked for v0.2.2 (track 58); ungated. The Claude Code plugin (mod [2a](../../../mod/backlog/v0.2.2/claude-code-plugin.md)) registers this profile.
