# CLI-58a · The server introduces itself

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.2 |
| **Component** | `mcp serve` (the MCP server's `initialize`) |
| **Labels** | `mcp` `claude-awareness` `additive` |
| **Epic** | Track 58 · Claude aware of Apogee |
| **Blocked by** | [58b](mcp-serve-graphs-and-knowledge.md) |
| **Blocks** | [mod 2a](../../mod/v0.2.2/claude-code-plugin.md) |
| **Reporter** | The user's idea; the MCP-awareness spike (2026-11-07) |
| **Created** | 2026-11-07 |

---

## Summary

`apogee mcp serve` never says what it is: its `initialize` result has no `instructions`, so a host's model sees 22 tool names and no picture of the person's setup. This item makes the server introduce itself, from the person's own setup, the moment a host connects: what Apogee is, its collections, graphs and offered local models, and which tool answers what; bounded, with no secret or path.

## Description

### User story

**As** a person using Apogee through an MCP host such as Claude Code,
**I want** the host's model to be told, when it connects, what Apogee is, which collections, graphs and local models I have, and which tool answers what,
**so that** it reaches for Apogee's search and graphs for my indexed material without my having to explain them.

### Background

The user's idea, 2026-11-07: an MCP server that makes Claude *aware* of Apogee's local models, retrieval and knowledge graph. The spike the same day (the branch binary `945784f5`, a scratch home, a raw JSON-RPC client playing the host) found the tools there and the awareness missing.

This item makes the server introduce itself, from the person's own setup, the moment a host connects. It is the single change that most makes Claude "aware".

### Evidence

1. **W1 — the server never says what it is.** The `initialize` result carries `protocolVersion`, `capabilities` and `serverInfo` (`apogee-tools`, `0.2.0`) and nothing else (`business/mcp/serve_stdio.cpp:39-46`).
2. The spike's probe read `"instructions": null`, and `resources/list` and `prompts/list` both answered `method not found`.
3. MCP's `instructions` field is how a host learns what a server is for. Claude Code puts a server's instructions into the session's system prompt, under "MCP Server Instructions" (seen in the spike's own session, for another server).
4. So a host's model gets 22 tool names with one-line descriptions and no picture: which collections exist, which graphs, whether a local model is offered, when Apogee's search beats its own.

## Acceptance criteria

- [ ] **Given** the spike's sandbox, **when** a client sends `initialize`, **then** the result carries instructions naming the `notes` collection and the `shop` graph and saying which tool answers what.
- [ ] **Given** a `complete` backend that is listed and one that is not, **when** the instructions are composed, **then** the listed one appears with its bound and the unlisted one never does.
- [ ] **Given** any setup, **when** the instructions are composed, **then** no key, token or absolute path appears, and the text never passes the bound.
- [ ] The full suite is green.
- [ ] **Given** a registered `apogee-dev mcp serve` in Claude Code, **when** the user opens its server details, **then** the instructions show there (the user's live pass, optional and said).

## Constraints

- **Facts, not prose.** The instructions are composed at the server's start from what the config and stores hold, the same state the reads answer from, and say only what is true there: the collections (name, chunk count, embedder), the graphs (from 58b's `graph list`: name, kind, size), the backends offered for `complete` with each one's bound (54b's `complete_backends`), the record count, and which tools answer what.
- **Bounded.** The text stays under a fixed size (default below); a long list is cut with its count and the tool that lists it whole (`… and 14 more: list_graphs`).
- **Secrets are unleakable** (CLI CLAUDE.md): no key, no token, no credential path, nothing from `secrets/`. Absolute source paths are left out too (a graph is named, not located).
- **Fixed at start, like the tool list** (54b's call: hosts list once). A collection added later is reachable through the reads (read at call, 54a) and named at the host's next start.
- **UTF-8 where it enters** (CLI CLAUDE.md): names from the stores mended before they are sent.
- **Additive** (ADR 0006): a new field in an existing reply; an older host ignores it.

## Technical approach

| File | Change |
|---|---|
| `business/mcp/serve_stdio.h/.cpp` | `serve_stdio` takes the instructions text and adds `instructions` to the `initialize` result when it is non-empty |
| `presentation/cli/mcp_serve.cpp` | `server_instructions(config, …)`: one paragraph on what Apogee is and the two kinds of tool, then the inventory, then the when-to-use lines (see below) |
| `lib/documentation/reference/cli/tools.md` | the serve chapter documents the instructions: what they hold, the bound, fixed at start |

The when-to-use lines in `server_instructions`:

- the person's indexed documents and decisions: `search_documents`, `knowledge_query`;
- how code connects: `graph_query`, `graph_path`;
- what models exist and where they stand: `models_status`;
- a local model for a bounded side task: `complete`, only where listed.

**Reference.** 54a/54b (the server, read at call, the fixed tool list, `complete_backends`); 58b (`graph list`, the graph and knowledge reads the text names).

## Testing

- **`tests/business/mcp/serve_stdio_test.cpp`** — the field present when given, absent when empty (the field's absence on an empty text pins additive behavior).
- **`tests/presentation/cli/mcp_serve_test.cpp`** — a golden over a planted home with collections, graphs, records and a listed `complete` (the golden pins the shape); an empty home; the cut at the bound (the bound case pins the cut); no secret or path reaches the text, a planted key and source path never appearing (the planted-secret case pins that nothing from `secrets/` or a source path appears).
- **`cli.mcp_lifecycle`** — the external client reads the field.

## Open questions

| Question | Status | Answer |
|---|---|---|
| How large may the instructions be, and how many names does each inventory list show? | `[default]` | The bound is 2,000 characters; each inventory list shows at most 8 names before the cut. |
| What do the instructions tell the host's model to do? | `[default]` | The instructions name what exists (inventory) and when to use each tool; they never tell the host's model to prefer Apogee over its own tools in general, only for the person's indexed material and graphs. |
| Are MCP `resources` or `prompts` added beside the instructions? | `[default]` | No MCP `resources` or `prompts` are added: the instructions and the reads carry the same facts, and a second channel would be a second place to keep current. |
| What do the instructions say when there is no config? | `[default]` | With no config, the instructions say Apogee is not set up and name `apogee check`; the reads still answer their own reasons. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the MCP-awareness spike (the user's idea; the user's calls: both hosts, opt-in writes, the next release). Track 58, v0.2.2. | the user |
| 2026-11-07 | Gated on 58b, so the introduction names the graph and knowledge reads it adds. Useful alone, but the inventory of graphs needs 58b's `graph list`. | the spec |

## Out of scope

- MCP `resources` and `prompts` (the recorded default above).
- Telling the host's model to prefer Apogee over its own tools in general (the recorded default above).

## Links

- **Blocked by:** [58b](mcp-serve-graphs-and-knowledge.md) · **Blocks:** the Claude Code plugin (mod [2a](../../mod/v0.2.2/claude-code-plugin.md)), which relies on it for the inventory
- **Relates to:** 54a/54b (the server, the fixed tool list, `complete_backends`); ADR 0006 (additive changes); CLI CLAUDE.md (secrets are unleakable; UTF-8 where it enters)
- **Follows from:** the MCP-awareness spike, 2026-11-07 (track 58, W1)
