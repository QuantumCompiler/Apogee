# Graph artifacts: the report, the visualization, the exports

**What / why.** A graph's highest-leverage output is often not a query answer but an *artifact*: the one-page architecture read a newcomer starts from, the picture a team argues over, the file another tool imports. Graphify ships three and people use all of them; this item is their Apogee equivalents over the existing store. **`graph report`** — a Markdown architecture summary through the in-process renderer: the communities with their existing summaries, the hubs ("god nodes": the highest-degree entities everything flows through), the extracted/inferred mix, cross-collection links in a named graph, the decision nodes with what they concern, and the orphans worth asking about. **`graph export html`** — one **self-contained** interactive file: force-directed layout, community colors, click-to-inspect (the `explain` card), full-text search; inline JS and CSS, no CDN, no network, no server — a file you open, mail, or commit, never a port. **`graph export graphml`** and **`graph export mermaid`** — the interop pair: GraphML for real graph tooling, a Mermaid call-flow diagram for a code graph's docs-friendly view.

**Core constraint(s).**
- **A file, never a server.** The HTML artifact must work from `file://` offline forever: assets inline, data embedded, zero fetches (asserted mechanically — no `http` reference in the output). The never-listens invariant is why this is an export and not a `graph serve`.
- **The renderer never calls a model.** `graph report` is assembled from the store — community summaries were already generated and paid for at `graph communities` time; the report reads them. A report over a graph with no summaries says so per community rather than generating.
- **One source of truth:** the report's facts and the HTML's node cards come from the same queries [27l](graph-navigation.md) builds (`node_card`, degree, neighborhoods) — no third computation of "what is a hub".
- **Bounded size, stated honestly:** a huge graph's HTML caps nodes by degree rank with the cap printed in the artifact itself; the report lists top-N with counts, never the full enumeration. GraphML is the uncapped escape hatch.
- **Privacy follows the store:** artifacts carry entity names, relations, summaries and `file:line` mentions — never chunk text beyond what mentions already surface, and never anything from the private layout rows. An artifact is made to leave the machine; it must contain only what the user would knowingly send.

**Seam + files.**
- `graph/report.h/.cpp` (new, guarded): the report model — hubs by degree, origin mix, community roll-up, decision list, orphan sample — pure over the store; rendered through `render/` (the `human_summary`-last discipline).
- `graph/export_html.h/.cpp`, `graph/export_graphml.h/.cpp`, `graph/export_mermaid.h/.cpp`: serializers over the same payloads; the HTML template an `assets/` file compiled in like the clerks' prompts, data embedded as one JSON block.
- `cli/graph.cpp`: `graph report [--out <file>]`, `graph export html|graphml|mermaid --out <file>`, both honoring `--graph`/`--collection`.
- Tests: `tests/business/graph/report_test.cpp` (golden report over the fixture graph; the no-summaries case; cap behavior), `export_test.cpp` (GraphML round-trips through a reference parser; the HTML contains its data block, its cap note, and no external reference; Mermaid output parses).

**Reference (Ommi).** No analog — Ommi's knowledge layer exported records (`knowledge export`), never graph artifacts. External prior art: Graphify's `graph.html`, `GRAPH_REPORT.md` (god nodes, surprising connections, suggested questions) and its Mermaid/GraphML/Neo4j exports. Divergences: no wiki/Obsidian publishing in the first cut; no Neo4j-specific format (GraphML covers the import path); the report reuses paid-for community summaries instead of generating fresh prose.

**Decisions made** (dated):
- 2026-09-30 — Split from the v0.1.7 code-graph work, third in build order: the artifacts are designed against [27l](graph-navigation.md)'s payloads and shine brightest once [27k](code-graph-extraction.md)'s code nodes and origin tags exist.
- 2026-09-30 — Self-contained HTML as a hard rule, not a preference: an artifact that phones a CDN is a privacy leak and a future 404; one that needs a server contradicts the invariant that only `serve` owns a port.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the layout is hand-rolled or a pinned single-file MIT library — whichever stays under ~200KB inline; never a CDN.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): hub rank is plain degree in the first cut; the report labels its metric.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): HTML caps at 2,000 nodes by degree rank, the cap printed; report shows top-10 hubs and sampled top-10 orphans.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): artifacts land in the working directory under the default names; nothing writes into the data directory.

**Guardrail(s).**
- The HTML artifact: zero external references (mechanically grepped), renders its embedded data block, and carries its cap note — asserted on the fixture graph and on an over-cap synthetic graph.
- The golden report: byte-stable over the fixture store, including the origin-mix line and the no-summaries fallback.
- GraphML validated by round-trip through an independent parser in the test; Mermaid by its own grammar check.
- The privacy sweep: a distinctive string planted in private-row content never appears in any artifact.
- No model calls: all three commands succeed with zero backends configured.

**Acceptance criteria:**
- [ ] `apogee graph report` on a built graph prints the architecture summary — communities, hubs, origin mix, decisions, orphans — with zero model calls, and `--out` writes the same bytes to a file.
- [ ] `graph export html` produces one file that opens from `file://` with no network, supports search, community colors, and click-to-explain, and states its node cap when capped.
- [ ] `graph export graphml` imports cleanly into a stock GraphML reader; `graph export mermaid` of a code graph renders a call-flow diagram.
- [ ] All three honor `--graph`/`--collection`, and nothing in any artifact references the private layout or chunk text beyond mentions.

**Scope note.** Item **27m** (30c under the then-v0.1.7, 31m under the then-v0.1.8, until 2026-10-03's merge and migration — the user's calls), earmarked for **v0.1.4**; build after [27k](code-graph-extraction.md) and [27l](graph-navigation.md) (it renders their payloads; the origin mix and code hubs need 27k, the cards need 27l). Out of scope: wiki/Obsidian publishing; Neo4j/FalkorDB-specific exports; serving the visualization; PR dashboards and impact analysis (Graphify's forge-coupled features ride Apogee's parked forge-integration idea, not this track).
