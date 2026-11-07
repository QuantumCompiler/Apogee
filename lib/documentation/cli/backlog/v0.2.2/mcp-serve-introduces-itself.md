# The server introduces itself

**What / why.** The user's idea, 2026-11-07: an MCP server that makes Claude *aware* of Apogee's local models, retrieval and knowledge graph. The spike the same day (the branch binary `945784f5`, a scratch home, a raw JSON-RPC client playing the host) found the tools there and the awareness missing.

**W1 — the server never says what it is.** The `initialize` result carries `protocolVersion`, `capabilities` and `serverInfo` (`apogee-tools`, `0.2.0`) and nothing else (`business/mcp/serve_stdio.cpp:39-46`). The probe read `"instructions": null`, and `resources/list` and `prompts/list` both answered `method not found`. MCP's `instructions` field is how a host learns what a server is for. Claude Code puts a server's instructions into the session's system prompt, under "MCP Server Instructions" (seen in the spike's own session, for another server). So a host's model gets 22 tool names with one-line descriptions and no picture: which collections exist, which graphs, whether a local model is offered, when Apogee's search beats its own.

This item makes the server introduce itself, from the person's own setup, the moment a host connects. It is the single change that most makes Claude "aware".

**Core constraint(s).**
- **Facts, not prose.** The instructions are composed at the server's start from what the config and stores hold, the same state the reads answer from, and say only what is true there: the collections (name, chunk count, embedder), the graphs (from 58b's `graph list`: name, kind, size), the backends offered for `complete` with each one's bound (54b's `complete_backends`), the record count, and which tools answer what.
- **Bounded.** The text stays under a fixed size (default below); a long list is cut with its count and the tool that lists it whole (`… and 14 more: list_graphs`).
- **Secrets are unleakable** (CLI CLAUDE.md): no key, no token, no credential path, nothing from `secrets/`. Absolute source paths are left out too (a graph is named, not located).
- **Fixed at start, like the tool list** (54b's call: hosts list once). A collection added later is reachable through the reads (read at call, 54a) and named at the host's next start.
- **UTF-8 where it enters** (CLI CLAUDE.md): names from the stores mended before they are sent.
- **Additive** (ADR 0006): a new field in an existing reply; an older host ignores it.

**Seam + files.**
- `business/mcp/serve_stdio.h/.cpp` — `serve_stdio` takes the instructions text and adds `instructions` to the `initialize` result when it is non-empty.
- `presentation/cli/mcp_serve.cpp` — `server_instructions(config, …)`: one paragraph on what Apogee is and the two kinds of tool, then the inventory, then the when-to-use lines (the person's indexed documents and decisions: `search_documents`, `knowledge_query`; how code connects: `graph_query`, `graph_path`; what models exist and where they stand: `models_status`; a local model for a bounded side task: `complete`, only where listed).
- `lib/documentation/cli/reference/tools.md` — the serve chapter documents the instructions: what they hold, the bound, fixed at start.
- Tests: `tests/business/mcp/serve_stdio_test.cpp` (the field present when given, absent when empty); `tests/presentation/cli/mcp_serve_test.cpp` (a golden over a planted home with collections, graphs, records and a listed `complete`; an empty home; the cut at the bound; no secret or path reaches the text, a planted key and source path never appearing); `cli.mcp_lifecycle` (the external client reads the field).

**Reference.** 54a/54b (the server, read at call, the fixed tool list, `complete_backends`); 58b (`graph list`, the graph and knowledge reads the text names).

**Decisions made** (dated):
- 2026-11-07: Specced from the MCP-awareness spike (the user's idea; the user's calls: both hosts, opt-in writes, the next release). Track 58, v0.2.2.
- 2026-11-07: Gated on 58b, so the introduction names the graph and knowledge reads it adds. Useful alone, but the inventory of graphs needs 58b's `graph list`.

**Open calls:**
- [default: the bound is 2,000 characters; each inventory list shows at most 8 names before the cut.]
- [default: the instructions name what exists (inventory) and when to use each tool; they never tell the host's model to prefer Apogee over its own tools in general, only for the person's indexed material and graphs.]
- [default: no MCP `resources` or `prompts` are added: the instructions and the reads carry the same facts, and a second channel would be a second place to keep current.]
- [default: with no config, the instructions say Apogee is not set up and name `apogee check`; the reads still answer their own reasons.]

**Guardrail(s).** The golden pins the shape; the planted-secret case pins that nothing from `secrets/` or a source path appears; the bound case pins the cut; the field's absence on an empty text pins additive behavior.

**Acceptance criteria:**
- [ ] Over the spike's sandbox, `initialize` carries instructions naming the `notes` collection and the `shop` graph and saying which tool answers what.
- [ ] A listed `complete` backend appears with its bound; an unlisted one never does.
- [ ] No key, token or absolute path appears; the text never passes the bound; the full suite is green.
- [ ] In Claude Code, a registered `apogee-dev mcp serve` shows the instructions in its server details (the user's live pass, optional and said).

**Scope note.** Earmarked for v0.2.2 (track 58); 🔒 58b. The Claude Code plugin (mod [2a](../../../mod/backlog/v0.2.2/claude-code-plugin.md)) relies on it for the inventory.
