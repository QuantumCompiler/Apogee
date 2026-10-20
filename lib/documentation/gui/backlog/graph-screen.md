# The code graph screen

**What / why.** The design's Graph screen (`ScreenGraph`): symbols with their callers and callees, files, explain cards, and rebuild — all **ready** per the contract board and verified by the spike (the verbs exist: `graph stats | show | neighbors | explain | path | query`, each with its JSON document), with **the layout the app's own job** (the board's app-side row). This item renders the graph reads natively: the node list and search over `query`/`show`, the explain card (`explain`'s document: degree, neighbours by relation, provenance), neighbour navigation, and rebuild (`graph build`/`graph update`) as a supervised job with read-back. The layout is computed client-side from the documents' nodes and edges — the CLI's HTML export has its own force layout as prior art, but the screen draws from the JSON documents, never embeds the export.

**Core constraint(s).** ADR 0001/0002; documents in, views out — the reads are the one source, refreshed on focus and after a rebuild; the layout is presentation arithmetic (a `ViewModels/`-side pure computation over the document's nodes, SwiftUI-free, testable on fixtures); caps honored client-side (the documents' own counted lists, never re-queried into unbounded walks).

**Seam + files.** `source/presentation/graph/` (the screen, the explain card, the layout computation in its view-model), `source/business/reads/` (the graph verbs), the jobs core for rebuild. Tests: the layout over fixture documents (deterministic positions for a seeded layout), the explain card's fields, navigation over scripted documents, rebuild's read-back; hermetic.

**Reference.** `ScreenGraph.dc.html`; the contract board's CODE GRAPH rows (all ready/app); the CLI's 27l navigation documents — the shapes consumed; 27m's `export_html` force layout — prior art for the client-side layout, not embedded.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call); layout is client-side over the JSON documents, the HTML export untouched.

**Open calls:**
- [default: the layout algorithm (a seeded force pass over the capped node set) and its caps at build, deterministic under test.]

**Guardrail(s).** Deterministic layout fixtures; document-shape matrices; rebuild read-back; hermetic.

**Acceptance criteria:**
- [ ] The screen searches, explains and navigates a fixture graph from the documents alone; rebuild runs supervised and the view refreshes.
- [ ] Layout is deterministic under test and computed in the view-model; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on [`app-shell.md`](app-shell.md) (1e). Out of scope: prose-graph/communities views (a later item), any CLI change, embedding the HTML export.
