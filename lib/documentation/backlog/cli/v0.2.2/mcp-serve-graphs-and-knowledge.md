# CLI-58b · The server serves graphs and knowledge records

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.2 |
| **Component** | `apogee mcp serve` reads · `apogee graph` |
| **Labels** | `mcp-server` `graphs` `knowledge-records` |
| **Epic** | Track 58 · Claude aware of Apogee |
| **Blocked by** | — |
| **Blocks** | [58a](mcp-serve-introduces-itself.md), [mod 2b](../../mod/v0.2.2/mod-graphs-and-knowledge.md) |
| **Reporter** | The MCP-awareness spike (2026-11-07), from the user's idea |
| **Created** | 2026-11-07 |

---

## Summary

Claude, through `apogee mcp serve`, cannot find the named graphs (`list_collections` lists embedding collections only, and no served tool lists graphs) and cannot read the knowledge records at all. This item adds `apogee graph list` and serves six new reads (`list_graphs`, `graph_stats`, `graph_report`, `knowledge_list`, `knowledge_info`, `knowledge_query`), each its command's own document, removing walls W2 and W3.

## Description

### User story

**As** a person using Apogee with Claude as an MCP host,
**I want** the server to list my named graphs, serve their stats and reports, and serve my knowledge records,
**so that** Claude can orient itself in my project's graphs and read the project's "why" without me pasting it in.

### Background

The user's idea, 2026-11-07: a Claude mod or MCP server that makes Claude aware of Apogee's local models, retrieval and knowledge graph. The spike the same day found the server (`apogee mcp serve`, 54a/54b, shipped 2026-11-01) already serving most of it. This item removes two of the walls it found (W2 and W3); both are "serve the reads the CLI already has", one seam (`status_reads`), one test pattern.

### Evidence

Measured by the spike, 2026-11-07, against the branch binary (`945784f5`) in a scratch home, with a raw JSON-RPC client playing the host and a model-free code graph `shop` built over a two-file Python project.

1. **What already works.** 22 read-only tools listed: 4 file, 4 git, 2 notes, 3 retrieval (`list_collections`, `collection_info`, `search_documents`), 4 graph (`graph_query`, `graph_explain`, `graph_neighbors`, `graph_path`) and 5 status reads. `complete` is added where a local backend lists it.
2. **What already works.** `graph_query`, `graph_explain` and `graph_path` answered over `shop` with file:line entities and no model (`graph_path` asked which `charge` was meant: `name:.charge, function:store.Store.charge (store.py:2)`).
3. **W2 — Claude can't find named graphs.** `list_collections` (`business/tools/rag_query.cpp:188-200`) lists the embedding collections only: it answered `notes  1 chunks`, never `shop`. No served tool lists graphs. `graph_query` without `graph` found `shop` only because it was the home's one graph. The CLI has no listing either: `apogee graph list` answered `A subcommand is required`. The model-free reads Claude would use to orient (`graph stats`, `graph report`, both printing `--output-format json`) are not served.
4. **W3 — knowledge records are not served.** `apogee knowledge query|list|info` (the decision records, `business/knowledge/`) has no MCP tool. The records are the project's "why", the thing an assistant most lacks.

## Acceptance criteria

- [ ] **Given** a home with named graphs and collection graphs, **when** I run `apogee graph list`, **then** it lists every named graph and collection graph, in text and in JSON.
- [ ] **Given** the spike's sandbox (`shop` and `notes`), **when** a host calls `list_graphs`, **then** it names `shop`; **and when** it calls `graph_stats shop` and `graph_report shop`, **then** they answer as their commands do.
- [ ] **Given** a home with knowledge records, **when** a host calls `knowledge_list`, `knowledge_info` and `knowledge_query`, **then** they answer as `apogee knowledge list|info|query --output-format json` do.
- [ ] The tool list grows only by appending; `complete` stays last; the full suite is green.

## Constraints

- **Reads have twins** (ADR 0002): every served read is a CLI read's document, byte for byte less the final newline (54a's rule). `apogee graph list --output-format json` comes first, as the CLI read `list_graphs` serves.
- **Additive** (ADR 0006, restated for the server in tools.md): tools are added, never renamed or removed. The new reads go after the five status reads and before `complete`, which stays last (54b).
- **Nothing writes, nothing spends but a query's embedding.** `knowledge_query` embeds its question as `search_documents` does: one small call through the records' embedder, under the 2026-09-13 spend rule 54b kept. A lexical fallback spends nothing. Every other new read is model-free.
- **Reads run at their call** (54a): each loads the config and stores when asked, so a host sees a graph built after the server started.
- **The server never ends on a failed read**: an error result in the command's own words (`mcp::with_reads`).

## Technical approach

| File | Change |
|---|---|
| `presentation/cli/graph.cpp` | `graph list` (named graphs from `graphs:` with their sources and collections, plus each collection with a graph built: name, nodes, edges, last build), text and `--output-format json`; its completion, its parity row in `cli/tui_parity.cpp` (ADR 0010) and its HTTP row |
| `presentation/cli/mcp_serve.cpp` | `status_reads` grows: `list_graphs`, `graph_stats` (taking `graph`), `graph_report` (taking `graph`), `knowledge_list` (taking `branch` and `limit`, the verb's read-only filters), `knowledge_info` (taking `id`), `knowledge_query` (taking `question`, `top_k`). Each composes the functions its command prints from, as 54a's do; seams carved where a command renders inline |
| `lib/documentation/reference/cli/tools.md` | *Apogee as an MCP server*'s reads table grows; the spend line names `knowledge_query` |
| `machine-mode.md` | names the reads that are also MCP tools |

**Reference.** 54a's status reads (CLI MILESTONES, 2026-11-01: `ServedRead`, `with_reads`, byte for byte, read at call); 54b's `search_documents` growth and the spend rule; the graph navigation tools (27l); the knowledge records (Milestone Y).

## Testing

- **`tests/presentation/cli/mcp_serve_test.cpp`** — each new read byte for byte against its command over a planted graph and records; a graph built after the server started seen by the next call; the names in order; `complete` still last.
- **`tests/presentation/cli/graph_test.cpp`** — `graph list`.
- **`cli.mcp_lifecycle`** on the real binary — the external client holding the grown list.
- **Guardrails.** Each new read held byte for byte to its command; the list order pinned (new reads after the status reads, `complete` last); `knowledge_query`'s lexical path seen spending nothing (no provider built); a malformed graph or records store answered as an error result with the server going on.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What are the new reads named? | `[default]` | The server's style, the command's words joined: `list_graphs` (matching `list_collections`), `graph_stats`, `graph_report`, `knowledge_list`, `knowledge_info`, `knowledge_query`. |
| What does `graph list` list, and in what shape? | `[default]` | Named graphs and collection graphs in one document, each with its kind (`named` or `collection`), so `graph_query`'s `graph`/`collection` arguments can be filled from it. |
| Is `graph_report`'s Markdown served as text or JSON-wrapped? | `[default]` | Served as text, not JSON-wrapped: it is the command's own Markdown, the document a host's model reads best. |
| Is chat recall (`chats recall`) served? | `[default]` | Chat recall stays unserved: it is a person's past conversations, not project knowledge; a later listing if asked. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the MCP-awareness spike (the user's idea; the user's calls the same day: both hosts, opt-in writes per graph and collection, the next release). Placed in a new v0.2.2 table, track 58. | the user |
| 2026-11-07 | W2 and W3 are one item: both are "serve the reads the CLI already has", one seam (`status_reads`), one test pattern. | the spec |

## Out of scope

- Chat recall (`chats recall`): a person's past conversations, not project knowledge; a later listing if asked.

## Links

- **Blocked by:** — (ungated) · **Blocks:** [58a](mcp-serve-introduces-itself.md) (its introduction names these reads, so it waits on this item), [mod 2b](../../mod/v0.2.2/mod-graphs-and-knowledge.md)
- **Follow-on:** the mod's use of these reads is mod [2b](../../mod/v0.2.2/mod-graphs-and-knowledge.md).
- **Relates to:** 54a and 54b (the server, shipped 2026-11-01), 27l (the graph navigation tools), Milestone Y (the knowledge records), ADR 0002, ADR 0006, ADR 0010.
