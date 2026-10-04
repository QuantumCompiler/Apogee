# Graph navigation: path, explain, and the graph toolset

**What / why.** A graph you can only build and expand is a map you cannot walk. Today `graph show` prints a node and retrieval-time expansion injects neighbors into a turn, but nobody — person or model — can *traverse*: no "how are A and B connected?", no "show me everything around X and why it's there". This item adds the navigation verbs Graphify proved people actually use, over Apogee's existing store: **`graph path <a> <b>`** (shortest path over `kg_edges`, every hop labelled with its relation and origin), **`graph explain <node>`** (the node's kinds, degree, neighbors grouped by relation, its mentions' provenance — `file:line` for code, chunk for prose — community membership, and any attached decision records), and **`graph query "<question>"`** scoped-subgraph search (entity match → bounded neighborhood, the expansion machinery reused at the command line). The same three arrive as a read-only **`graph` native toolset**, which makes them available to every tool-using model on every backend — and, because the in-binary `apogee __mcp-tools` serves exactly the read-only toolsets, to any external MCP client with nothing new built. That is Graphify's MCP story (`query_graph`, `get_neighbors`, `shortest_path`) falling out of plumbing Apogee already owns.

**Core constraint(s).**
- **Read-only, everywhere.** Navigation never writes: not a cache row, not a stat. That is what lets the toolset register ungated (`read-only` never prompts, Milestone X's rule) and be served by `__mcp-tools` (which serves only read-only tools, Milestone W's rule). A future write verb would be a different item and a gated tool.
- **One traversal core.** The CLI verbs, the toolset, and any admin read all call one implementation in `graph/`; the agentloop's expansion keeps its own budgeted path but shares the store queries — no second neighbor-walk growing its own bugs.
- **Honest resolution:** a name that matches nothing says so and suggests near matches; a name matching several nodes lists them and asks for the qualified one, never silently picks. Graphs-first precedence (a named graph over its member collections) applies exactly as it does for expansion.
- **Bounded output by construction:** `path` caps hops, `explain` caps neighbors per relation, `query` keeps the expansion budget — a model-facing tool that can dump an unbounded subgraph is a context bomb.
- **Parity:** the verbs ride `--graph`/`--collection` selection like every graph command; machine-readable output follows [29e](../v0.1.6/machine-readable-reads.md)'s `--output-format json` conventions (adopted whether or not 29e has shipped first); served reads follow the existing admin graph routes' pattern.

**Seam + files.**
- `graph/navigate.h/.cpp` (new, guarded like the rest of `graph/`): `shortest_path`, `neighborhood`, `node_card` (the explain payload) over `embedstore/graph` queries; pure over an injected store handle.
- `embedstore/graph.h/.cpp`: the two or three indexed queries traversal needs (edges by node and relation, bidirectional), added beside the existing ones.
- `commands/graph.cpp`: the three verbs, human rendering (hops as `a -[calls·extracted]-> b`), JSON per 29e.
- `tools/` (the `graph` toolset): `graph_query`, `graph_path`, `graph_explain`, `graph_neighbors` — thin wrappers over the one core, registered read-only; served by `__mcp-tools` with zero additional code (asserted, not assumed).
- `httpserver/`: read twins only where the existing graph admin surface already has the pattern; nothing mutating.
- Tests: `tests/graph/navigate_test.cpp` (path/neighborhood over a fixture graph, table-tested: no path, self, caps, ambiguity); `tests/tools/` for the toolset's bounds; an `mcp_e2e`-style check that an external MCP client reaches `graph_path`.

**Reference (Ommi).** No analog — Ommi's graph layer (the shape Milestone Y ported) had build and expansion, not user-facing traversal. External prior art: Graphify's `query` / `path` / `explain` verbs and its MCP tool set, adopted in Apogee idiom; divergence: no "strict mode" forcing models through the graph — the toolset is offered, selection stays the model's (and [26g](../../assistant/MILESTONES.md#milestone-f--the-shared-agent-loop)'s, shipped 2026-10-03).

**Decisions made** (dated):
- 2026-09-30 — Split from the v0.1.7 code-graph work: navigation is useful over the *existing* prose graphs on its own, so it does not gate on [27k](code-graph-extraction.md) — it only gets better when code nodes arrive.
- 2026-09-30 — The toolset route to MCP rather than new server code: `__mcp-tools` already serves read-only toolsets to any client; building a second graph-specific server would duplicate a shipped mechanism.

**Open calls:**
- [default: `path` is undirected with direction shown per hop (callers and callees both connect things); `--directed` for the strict case] Path semantics.
- [default: caps — 8 hops, 12 neighbors per relation in `explain`, the existing expansion budget for `query`; each overridable by flag, never unbounded] The bounds.
- [default: node naming accepts `name`, `kind:name`, and for code nodes `path:line` — the mention table already holds what's needed to resolve all three] Addressing.
- [default: `query`'s entity matching resolves lexically (exact, then FTS over names) when no embedder is configured, so the track's model-free guarantee ([27k](code-graph-extraction.md)) holds through navigation; an embedder, when present, only improves recall] The no-model path through `query`.

**Guardrail(s).**
- The traversal table tests: disconnected nodes, self-paths, cap enforcement, ambiguous names listing candidates — all against a committed fixture graph, no model, no network.
- One-core assertion: the CLI verbs and the tools produce identical payloads for identical inputs (golden-compared).
- The MCP check: a stdio client lists and calls `graph_path` against the real binary; the tool is marked read-only and never prompts.
- Output bounds mutation-tested where the repo's convention applies.

**Acceptance criteria:**
- [ ] `apogee graph path <a> <b>` prints each hop with relation and origin; a nonexistent connection says "no path within 8 hops" rather than nothing.
- [ ] `graph explain <node>` shows kinds, degree, grouped neighbors, provenance mentions, community, and attached decisions — for a prose node today, and for a code node once 27k lands, with no code change here.
- [ ] A tool-using chat on any backend can answer "how does X reach Y?" by calling `graph_path`, unprompted by the permission gate.
- [ ] An external MCP client sees `graph_query`/`graph_path`/`graph_explain`/`graph_neighbors` from `apogee __mcp-tools` and gets the same payloads the CLI prints as JSON.
- [ ] Every verb honors `--graph`/`--collection` and `--output-format json`.

**Scope note.** Item **27l** (30b under the then-v0.1.7, 31l under the then-v0.1.8, until 2026-10-03's merge and migration — the user's calls), earmarked for **v0.1.4**; gated on nothing pending (works over existing graphs; enriched by [27k](code-graph-extraction.md)). Out of scope: write operations of any kind; visualization and reports ([27m](graph-artifacts.md)); forcing models to query the graph before reading files (deliberately not ported from Graphify).
