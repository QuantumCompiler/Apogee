# The knowledge views: Knowledge, Collections, Graph

**What / why.** The knowledge layer is the shell's largest uncovered surface (measured 2026-10-10, the parity spike): `knowledge` (9 verbs), `embed` (5) and `graph` (13) have no view, and their reads carry **no machine documents at all for knowledge and embed (0 of 9, 0 of 5)** — while graph's navigation verbs (`path`, `explain`, `neighbors`, `query`, `report` — 5 of 13) already print the JSON documents 27l/27m shipped. Three views over `tui/list_view`: **Knowledge** — the records listed (id, concern, when), Enter the info card, a query row searching through the one retriever with the hit's retriever and score said as the command says them, `x` deleting after an ask; **Collections** — `embed list`'s collections (name, chunks, model binding), Enter the info, a query row, `x` delete; **Graph** — the graphs and their stats, Enter the explain card of a picked node, the navigation reads drawn from their existing documents. Reads and queries only: `ingest`, `build`, `update`, `reindex`, `dedupe` are long-running and stay commands (Open calls below). Closes the knowledge slice of walls W1 and W2.

**Core constraint(s).** The reads that lack documents are carved **as** `--output-format json` documents in the same change (the 28h idiom — [ADR 0002](../../adrs/cli/mode-parity.md)'s "reads have twins" means the view's row function and the machine document are one function, and `machine-mode.md` gains their rows). The cores exist and are not re-derived: `operations/knowledge_core.h`, `operations/collections.h`, `operations/graph_members.h`, `graph/navigate` — the views draw them through carved `cli/` reads (32d's rule: the shared cores are `cli/` functions where a command and a view meet). A query honors **one retriever per turn, reported honestly**: the view's result line names the retriever that actually ran and its score on that retriever's scale (26s), in the command's own words. Graph navigation returns the CLI's documents **byte for byte** (27l's rule for every surface). Deletes ask (32d: a single key is not a typed command).

**Seam + files.**
- `lib/src/cli/source/presentation/cli/tui_knowledge.h/.cpp` — the three views, registered in `cli/tui_cmd.cpp`.
- `cli/knowledge.h/.cpp` — carve `read_knowledge_rows` / the info card, ship `knowledge list|info|status --output-format json`; the query path through the existing core.
- `cli/embed.h/.cpp` — carve `read_collection_rows` / info, ship `embed list|info --output-format json`; query through the one retriever resolver (already the command's path).
- `cli/graph.h/.cpp`, `cli/graph_navigate.h` — the graphs/stats rows carved (ship `graph stats|show --output-format json`); the explain/neighbors views drawing 27l's documents as returned.
- `lib/documentation/reference/machine-mode.md` — the new documents' rows; `tests/presentation/cli/tui_knowledge_test.cpp` (new); leak-test rows; classification flips in 37a's table (`knowledge`, `embed`, `graph`).

**Reference.** 27l/27m — the navigation and report documents (drawn, never re-derived) and the "byte for byte on every surface" rule; 28h — reads as JSON documents; 26s — per-retriever score honesty; 32d — the carved-read and ask-first idioms.

**Decisions made** (dated):
- 2026-10-10 — Query interaction is an input row above the results list in the view (the Session view's input idiom), the result lines the command's own — no new result renderer (the user's placement of the set in v0.1.6 on the spike report; the design the spike proposed, accepted).

**Open calls:**
- [default: `ingest`, `build`, `update`, `reindex` and `dedupe` stay commands in the shell's first cut — they are long-running and belong to the progress seam's consumers; whether a later item takes them over [37e](tui-progress-seam.md)'s widget is a call made then, recorded on 37a's classification as the reason.]
- [default: `knowledge capture`, `link` and `export`, and `graph export`, stay commands — capture is a model call with a source conversation, export writes files; neither is a view's keystroke.]

**Guardrail(s).** Each view's rows golden against its own new JSON document; the query line's retriever/score wording against the command's for the same store (a fixture collection with and without vectors — the demotion said identically); graph cards byte-equal to `graph explain`'s document; deletes ask, a `no` writes nothing; the leak test's planted key searched in all three views.

**Acceptance criteria:**
- [ ] Knowledge, Collections and Graph are views on the shell; every list re-reads on show.
- [ ] `knowledge list|info|status`, `embed list|info`, `graph stats|show` print `--output-format json` documents, documented in machine-mode.md, and the views draw exactly those rows.
- [ ] A query from the view runs the one retriever path and reports retriever and score as the command does, including the lexical demotion case.
- [ ] Graph navigation cards match the existing documents byte for byte.
- [ ] 37a's classification rows flip; the full suite is green.

**Scope note.** Earmarked for v0.1.6; gated on [37a](tui-parity-law.md). Out of scope: ingest/build surfaces (see Open calls), any retrieval behavior change, any graph write.
