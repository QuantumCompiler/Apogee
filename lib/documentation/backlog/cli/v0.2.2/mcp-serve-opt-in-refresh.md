# CLI-58d · Claude can refresh the graphs and collections you list

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.2 |
| **Component** | `apogee mcp serve` (served calls) · `graph` and `embed` config verbs |
| **Labels** | `mcp-serve` `consent-as-config` `knowledge-refresh` |
| **Epic** | Track 58 · Claude aware of Apogee |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The MCP-awareness spike (2026-11-07) |
| **Created** | 2026-11-07 |

---

## Summary

`apogee mcp serve` is read-only apart from 54b's opt-in `complete`, so Claude can read Apogee's knowledge but not keep it current. This item serves `graph_update` (model-free) and `collection_refresh` (local embedder only) where a graph lists `update` or a collection lists `ingest` under `mcp_serve`, refreshing only the sources the config already records.

## Description

### User story

**As** a person using Apogee with Claude working in my project,
**I want** Claude to be able to refresh the code graphs and collections I have listed for it,
**so that** the assistant can fix a stale graph or collection itself, without me going to the command line, and never reads anything I did not already point Apogee at.

### Background

The MCP-awareness spike (2026-11-07) found `apogee mcp serve` read-only, apart from 54b's opt-in `complete`. This is the spike's fifth wall, W5, and track 58's item 58d.

The user's call, 2026-11-07: **opt-in per graph and collection**, as 54b's `complete` is opt-in per backend. Claude may refresh only what the config lists for it.

### Evidence

1. **W5 — Claude can read Apogee's knowledge but not keep it current.** A code graph goes stale as the project changes. Refreshing it is `apogee graph update <name>`: re-parse the changed files of its source trees, with no model call. A collection goes stale the same way, and refreshing it is a re-ingest of its sources, which embeds the changed chunks. Both need the person at the command line today. An assistant working in the very project the graph describes can see it is stale (its answers point at code that moved) and cannot fix it.

## Acceptance criteria

- [ ] **Given** the graph `shop` lists `update`, **when** a host calls `graph_update shop`, **then** it re-parses its sources and a following `graph_query` sees an edited function.
- [ ] **Given** nothing lists `update`, **when** a host lists the tools, **then** `graph_update` is absent.
- [ ] **Given** a collection with a local embedder lists `ingest`, **when** a host calls `collection_refresh` on it, **then** it refreshes its sources.
- [ ] **Given** a collection with a billed embedder, **when** `ingest` is written to its listing, **then** the write is refused.
- [ ] No call reads a path the config does not record.
- [ ] The full suite is green.

## Constraints

- **Consent is config, through the one editor** (`contracts/config_edit.h`; 54b's `mcp_serve:` shape, 47a's consent-as-config). A named graph lists `"mcp_serve": ["update"]`, a collection lists `"mcp_serve": ["ingest"]`. Empty by default. Written by a verb, never by hand only, and read again at each call (54b): a listing taken back refuses the next call.
- **Refresh, never widen.** `update` re-parses the graph's recorded `sources` (`contracts/config.h` `NamedGraphConfig::sources`); `ingest` re-ingests the collection's recorded sources. Neither takes a path from the host: no new folder is ever read on a host's initiative.
- **Model initiative never spends** (27f, 27t, 54b). `update` calls no model, so any listed graph may serve it. `ingest` embeds, so it is served only where the collection's embedder is local and unmetered by its own provider's word, asked at write, at the server's start and at the call (54b's three doors). A graph build that extracts with a model (`graph build` on a collection) is never served.
- **Calls, not reads.** Listed with `readOnlyHint: false`, after the reads, before or beside `complete` (`mcp::with_calls`); narrated on stderr, the stream a host captures (`update on 'shop': 2 files re-parsed`).
- **One implementation.** Each call runs the command's own core (`graph update`, the ingest path), never a second copy (ADR 0002).
- **Bounded.** One call refreshes one graph or one collection; a refresh already running for it is refused, not queued.

## Technical approach

| File | Change |
|---|---|
| `data/contracts/mcp_serve.h/.cpp` | The served-call table grows: `update` (graphs) and `ingest` (collections), the loader's refusal for any other name, the rule's wording |
| `data/contracts/config.h` + the loader | `mcp_serve` on `NamedGraphConfig` and on a collection's entry, read once per name in YAML and JSONC |
| `presentation/cli/` | The verb that writes it: `graph` gains `--mcp-serve update` on the verb that writes a graph's entry (its `--source` path); `embed` the same for a collection; `""` takes it back; completion from the table |
| `presentation/cli/mcp_serve.cpp` | `graph_update` (taking `graph`) and `collection_refresh` (taking `collection`), each listed only where something lists it, its `enum` the listed names |
| `lib/documentation/reference/cli/tools.md` | The serve chapter's *Refreshing what you list* |

**Reference.** 54b (`mcp_serve:`, `with_calls`, eligibility at three doors, narration); 47a (`server_tools:`, consent as config); 27k (`graph update`, model-free).

## Testing

- **`contracts/mcp_serve_test`** — the table, the loader, the editor in both formats.
- **`cli/mcp_serve_test`** — nothing listed is the read set; a listed graph's update re-parses only its sources; a collection with a billed embedder refused at write, left out at start and said; consent read again at the call; a refresh already running refused.
- **`cli.mcp_lifecycle`** — on the real binary over a code graph.
- **Guardrails pinned.** The consent matrix (unlisted, listed, taken back mid-session, billed embedder); the refresh-only rule (a host-supplied path argument is not in the schema, and an unknown argument is refused); the narration.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What are the served names and the config words? | `[default]` | The served names are `graph_update` and `collection_refresh`; the config words are `update` and `ingest`. |
| Is `ingest` on a collection whose embedder bills refused, or allowed with a warning? | `[default]` | Refused (the local-and-unmetered rule), not allowed with a warning. |
| Can a refresh call be cancelled over MCP, and how is its size bounded? | `[default]` | A call runs to its end with no MCP cancellation; a refresh's size is the sources' own, said in the description. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Opt-in per graph and collection, as `complete` is per backend: Claude may refresh only what the config lists for it | the user |
| 2026-11-07 | Specced from the MCP-awareness spike. Track 58, v0.2.2; ungated (it adds calls beside 54b's, needing nothing from 58a–58c) | the spec |

## Out of scope

- Model-extracted graph builds.
- Adding new sources from a host.
- Deleting anything.

## Links

- **Blocked by:** — · **Blocks:** —
- **Follows from:** the MCP-awareness spike, 2026-11-07 (W5)
- **Relates to:** 54b (`mcp_serve:`, `with_calls`, opt-in `complete`), 47a (`server_tools:`, consent as config), 27k (`graph update`), 27f and 27t (model initiative never spends), ADR 0002 (one implementation), 58a–58c (the rest of track 58, not needed by this item)
