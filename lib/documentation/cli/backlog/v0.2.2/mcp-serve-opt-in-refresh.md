# Claude can refresh the graphs and collections you list

**What / why.** The MCP-awareness spike (2026-11-07) found `apogee mcp serve` read-only, apart from 54b's opt-in `complete`.

**W5 — Claude can read Apogee's knowledge but not keep it current.** A code graph goes stale as the project changes. Refreshing it is `apogee graph update <name>`: re-parse the changed files of its source trees, with no model call. A collection goes stale the same way, and refreshing it is a re-ingest of its sources, which embeds the changed chunks. Both need the person at the command line today. An assistant working in the very project the graph describes can see it is stale (its answers point at code that moved) and cannot fix it.

The user's call, 2026-11-07: **opt-in per graph and collection**, as 54b's `complete` is opt-in per backend. Claude may refresh only what the config lists for it.

**Core constraint(s).**
- **Consent is config, through the one editor** (`contracts/config_edit.h`; 54b's `mcp_serve:` shape, 47a's consent-as-config): a named graph lists `"mcp_serve": ["update"]`, a collection lists `"mcp_serve": ["ingest"]`. Empty by default. Written by a verb, never by hand only, and read again at each call (54b): a listing taken back refuses the next call.
- **Refresh, never widen.** `update` re-parses the graph's recorded `sources` (`contracts/config.h` `NamedGraphConfig::sources`); `ingest` re-ingests the collection's recorded sources. Neither takes a path from the host: no new folder is ever read on a host's initiative.
- **Model initiative never spends** (27f, 27t, 54b): `update` calls no model, so any listed graph may serve it. `ingest` embeds, so it is served only where the collection's embedder is local and unmetered by its own provider's word, asked at write, at the server's start and at the call (54b's three doors). A graph build that extracts with a model (`graph build` on a collection) is never served.
- **Calls, not reads**: listed with `readOnlyHint: false`, after the reads, before or beside `complete` (`mcp::with_calls`); narrated on stderr, the stream a host captures (`update on 'shop': 2 files re-parsed`).
- **One implementation**: each call runs the command's own core (`graph update`, the ingest path), never a second copy (ADR 0002).
- **Bounded**: one call refreshes one graph or one collection; a refresh already running for it is refused, not queued.

**Seam + files.**
- `data/contracts/mcp_serve.h/.cpp` — the served-call table grows: `update` (graphs) and `ingest` (collections), the loader's refusal for any other name, the rule's wording.
- `data/contracts/config.h` + the loader — `mcp_serve` on `NamedGraphConfig` and on a collection's entry, read once per name in YAML and JSONC.
- `presentation/cli/` — the verb that writes it: `graph` gains `--mcp-serve update` on the verb that writes a graph's entry (its `--source` path); `embed` the same for a collection; `""` takes it back; completion from the table.
- `presentation/cli/mcp_serve.cpp` — `graph_update` (taking `graph`) and `collection_refresh` (taking `collection`), each listed only where something lists it, its `enum` the listed names.
- `lib/documentation/cli/reference/tools.md` — the serve chapter's *Refreshing what you list*.
- Tests: `contracts/mcp_serve_test` (the table, the loader, the editor in both formats); `cli/mcp_serve_test` (nothing listed is the read set; a listed graph's update re-parses only its sources; a collection with a billed embedder refused at write, left out at start and said; consent read again at the call; a refresh already running refused); `cli.mcp_lifecycle` on the real binary over a code graph.

**Reference.** 54b (`mcp_serve:`, `with_calls`, eligibility at three doors, narration); 47a (`server_tools:`, consent as config); 27k (`graph update`, model-free).

**Decisions made** (dated):
- 2026-11-07: The user's call: opt-in per graph and collection, as `complete` is per backend.
- 2026-11-07: Specced from the MCP-awareness spike. Track 58, v0.2.2; ungated (it adds calls beside 54b's, needing nothing from 58a–58c).

**Open calls:**
- [default: the served names are `graph_update` and `collection_refresh`; the config words are `update` and `ingest`.]
- [default: `ingest` on a collection whose embedder bills is refused (the local-and-unmetered rule), not allowed with a warning.]
- [default: a call runs to its end with no MCP cancellation; a refresh's size is the sources' own, said in the description.]

**Guardrail(s).** The consent matrix pinned (unlisted, listed, taken back mid-session, billed embedder); the refresh-only rule pinned (a host-supplied path argument is not in the schema, and an unknown argument is refused); the narration pinned.

**Acceptance criteria:**
- [ ] With `shop` listing `update`, `graph_update shop` re-parses its sources and a following `graph_query` sees an edited function; with nothing listed, the tool is absent.
- [ ] A collection with a local embedder listing `ingest` refreshes its sources; one with a billed embedder is refused at write.
- [ ] No call reads a path the config does not record; the full suite is green.

**Scope note.** Earmarked for v0.2.2 (track 58); ungated. Out of scope: model-extracted graph builds, adding new sources from a host, deleting anything.
